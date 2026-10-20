// PWM-synchronised phase-current sampling on the ESP32-S3's internal SAR ADCs (see
// current_sampler.h).  Built only with -D SYNC_CURRENT_SENSE.
//
// Choice of scheme (measured 2026-10-20 with adc-timing-ota, motors idle):
//   - one conversion takes 7.2 us on ADC1 and 6.2 us on ADC2; ADC1 + ADC2 started together finish
//     in 8.4 us, and two such pairs in 16.7 us;
//   - two sequential conversions on one ADC take 14.4 us.
// Giving each motor its own ADC and interrupt (unit 0 -> ADC1, unit 1 -> ADC2) would need two
// interrupts per period that each busy-wait ~14.4 us for two sequential conversions: ~29 us of
// every 50 us period, with the two units 14.6 us out of phase so the waits cannot overlap.
// Aligning the two MCPWM units instead lets ONE interrupt read both motors in parallel: two
// ADC1+ADC2 pairs, ~16.7 us per period (~35% of core 0).  Both units are clocked from the same
// 160 MHz source with the same prescaler and period, so once their counters are aligned they
// stay aligned; samplerBegin() aligns them once at boot (outputs off) and measures the result.
//
// Timing within a period (P = 4000 ticks of 6.25 ns, the counter runs 0..P..0, peak = centre of
// the all-low-sides-on window):
//   1. A general-purpose timer alarm fires shortly before (peak - lead).  No MCPWM event is free
//      at that point: SimpleFOC writes both comparators of all three operators, and a timer-zero
//      interrupt would leave ~15 us to busy-wait.  The alarm is re-armed every period from the
//      MCPWM counter itself, so it cannot drift.
//   2. The interrupt spins on the MCPWM counter until exactly (peak - lead), so its own entry
//      latency (0.5-5.2 us measured) does not move the sample instants.
//   3. Pair 1: each motor's LOWEST-duty phase (widest low-side window), ADC1 for motor 1 and
//      ADC2 for motor 2 in parallel.  It ends ~8.4 us later, i.e. at about the peak.
//   4. Pair 2: each motor's MIDDLE-duty phase, at the peak (its window is the narrow one).
//   5. Every sample is timestamped from the counter and checked against its phase's low-side
//      window (from the compare value), less settling and hold margins; samples outside it are
//      flagged, not used.
// The interrupt does integer work only (no FPU in ISRs on this chip); conversion to amps is done
// by samplerRead() in task context.

#ifdef SYNC_CURRENT_SENSE

#include <Arduino.h>
#include <driver/mcpwm.h>
#include <driver/timer.h>
#include <soc/mcpwm_struct.h>
#include <soc/sens_reg.h>
#include <esp_intr_alloc.h>
#include <esp_ipc.h>
#include "current_sampler.h"
#include "config.h"
#include "pins.h"

// --- Raw SAR reads (RTC controller), as in adc_timing_test.cpp ---
// SimpleFOC 2.3.2's ESP32-S3 adcRead() returns ADC1's result for ADC2 pins, so it is not used.
// Attenuation, power and controller set-up are left as analogRead() left them at boot
// (MotorController::calibrateCurrentZero).  S3 pads: GPIO1-10 = ADC1 ch 0-9, GPIO11-20 = ADC2 ch 0-9.
static const uint32_t CONV_TIMEOUT_CYCLES = 240 * 30;   // 30 us at 240 MHz: the ADC is wedged

static inline uint32_t IRAM_ATTR ccount() {
    uint32_t c;
    asm volatile("rsr %0, ccount" : "=a"(c));
    return c;
}

// Start a conversion: two plain writes of the control register (pad select with start low, then
// start high).  The register's other bits are captured once at samplerBegin (nothing else writes
// it while the sampler runs); read-modify-writes of these RTC-domain registers cost ~0.6 us per
// access, which made each start ~1.9 us.
static uint32_t s_meas1_base = 0, s_meas2_base = 0;   // CTRL2 with start = 0 and no pad selected

// Select the pad (start low) well before the start: with the pad switched right before the
// start, ADC2's zero-current noise rose from ~1 to 4-6 LSB (the input had no time to settle).
static inline void IRAM_ATTR adcSelect(int ch1, int ch2) {
    WRITE_PERI_REG(SENS_SAR_MEAS1_CTRL2_REG, s_meas1_base | ((uint32_t)(1 << ch1) << SENS_SAR1_EN_PAD_S));
    WRITE_PERI_REG(SENS_SAR_MEAS2_CTRL2_REG, s_meas2_base | ((uint32_t)(1 << ch2) << SENS_SAR2_EN_PAD_S));
}

static volatile int32_t s_stagger_ticks = 0;   // ADC2 starts this long after ADC1 (MCPWM ticks)
static volatile bool    s_lo_twice      = true; // read the lowest-duty phase again after the peak
static volatile int32_t s_every         = SYNC_EVERY_N_PERIODS;   // sample every Nth PWM period

static inline void IRAM_ATTR adcStartPair(int ch1, int ch2) {
    WRITE_PERI_REG(SENS_SAR_MEAS1_CTRL2_REG,
                   s_meas1_base | ((uint32_t)(1 << ch1) << SENS_SAR1_EN_PAD_S) | SENS_MEAS1_START_SAR_M);
    if (s_stagger_ticks > 0) {
        // CPU cycles, not PWM position: a position difference never ends if the counter wraps
        // (that hung this interrupt into the watchdog once).  240 MHz / 160 MHz = 3/2.
        uint32_t c0 = ccount(), wait = (uint32_t)s_stagger_ticks * 3 / 2;
        while (ccount() - c0 < wait) {}
    }
    WRITE_PERI_REG(SENS_SAR_MEAS2_CTRL2_REG,
                   s_meas2_base | ((uint32_t)(1 << ch2) << SENS_SAR2_EN_PAD_S) | SENS_MEAS2_START_SAR_M);
}

static void captureAdcControl() {
    s_meas1_base = READ_PERI_REG(SENS_SAR_MEAS1_CTRL2_REG) &
                   ~(SENS_MEAS1_START_SAR_M | (SENS_SAR1_EN_PAD_V << SENS_SAR1_EN_PAD_S) |
                     (SENS_MEAS1_DATA_SAR_V << SENS_MEAS1_DATA_SAR_S) | SENS_MEAS1_DONE_SAR_M);
    s_meas2_base = READ_PERI_REG(SENS_SAR_MEAS2_CTRL2_REG) &
                   ~(SENS_MEAS2_START_SAR_M | (SENS_SAR2_EN_PAD_V << SENS_SAR2_EN_PAD_S) |
                     (SENS_MEAS2_DATA_SAR_V << SENS_MEAS2_DATA_SAR_S) | SENS_MEAS2_DONE_SAR_M);
}

static volatile uint32_t s_timeouts = 0;
static volatile uint32_t s_wedged   = 0;   // consecutive periods with a conversion timeout
static const uint32_t    WEDGED_STOP = 20; // then stop: each such period costs up to 4 x 30 us

// Wait for a conversion and return it, or 0xFFFF after CONV_TIMEOUT_CYCLES.
static inline uint16_t IRAM_ATTR adc1End() {
    uint32_t t0 = ccount();
    while (GET_PERI_REG_MASK(SENS_SAR_MEAS1_CTRL2_REG, SENS_MEAS1_DONE_SAR) == 0) {
        if (ccount() - t0 > CONV_TIMEOUT_CYCLES) { s_timeouts++; return 0xFFFF; }
    }
    return GET_PERI_REG_BITS2(SENS_SAR_MEAS1_CTRL2_REG, SENS_MEAS1_DATA_SAR, SENS_MEAS1_DATA_SAR_S);
}

static inline uint16_t IRAM_ATTR adc2End() {
    uint32_t t0 = ccount();
    while (GET_PERI_REG_MASK(SENS_SAR_MEAS2_CTRL2_REG, SENS_MEAS2_DONE_SAR) == 0) {
        if (ccount() - t0 > CONV_TIMEOUT_CYCLES) { s_timeouts++; return 0xFFFF; }
    }
    return GET_PERI_REG_BITS2(SENS_SAR_MEAS2_CTRL2_REG, SENS_MEAS2_DATA_SAR, SENS_MEAS2_DATA_SAR_S);
}

// Motor m's phase k: ADC channel (motor 0 on ADC1, motor 1 on ADC2).
// DRAM_ATTR: the interrupt runs while flash writes (NVS) have the cache off, so nothing it reads may
// live in flash.  In .rodata this table made every OTA request (which writes NVS) panic.
static DRAM_ATTR const int ADC_CH[2][3] = { { CURA - 1, CURB - 1, CURC - 1 },
                                  { CURA2 - 11, CURB2 - 11, CURC2 - 11 } };

// --- Timer used to schedule the interrupt ---
static const timer_group_t GPT_GROUP     = TIMER_GROUP_1;
static const timer_idx_t   GPT_IDX       = TIMER_0;
static const uint32_t      GPT_DIVIDER   = 2;     // 80 MHz APB / 2 = 40 MHz
static const int32_t       TICKS_PER_GPT = 4;     // MCPWM ticks (160 MHz) per GPT tick (40 MHz)

// --- Raw per-period record, written by the interrupt ---
struct RawSample {
    uint32_t seq;
    uint16_t cmp[3];
    uint16_t raw_lo, raw_mid, raw_lo2;
    int16_t  t_lo, t_mid, t_lo2; // conversion start, ticks relative to the peak
    int16_t  on_lo, off_lo;      // ticks since that phase's low side turned on / until it turns off,
    int16_t  on_mid, off_mid;    // at the conversion start
    int16_t  on_lo2, off_lo2;
    uint8_t  ph_lo, ph_mid, flags;
    uint8_t  lo_which;           // bit 0: first lowest-duty read valid, bit 1: second
};

static const uint32_t RING_SIZE = 256;   // 12.8 ms; a housekeeping pass takes ~2.5 ms + 2 ms wait
                                         // with the motors on, and has been seen 7 ms apart
struct Ring {
    RawSample         buf[RING_SIZE];
    volatile uint32_t head = 0;          // written by the interrupt
    volatile uint32_t tail = 0;          // written by samplerRead (same core, lower priority)
    volatile uint32_t dropped = 0;
};
static Ring s_ring[2];

// --- State shared with the interrupt (all integers) ---
static volatile bool     s_running      = false;
static intr_handle_t     s_isr_handle   = nullptr;
static int32_t           s_P            = 4000;   // timer period register (half the PWM period)
static int32_t           s_dt_ticks     = 81;     // dead time per edge (ticks)
static volatile int32_t  s_lead_ticks   = 0;
static volatile int32_t  s_mid_ticks    = 0;
static volatile int32_t  s_settle_ticks = 0;
static volatile int32_t  s_hold_ticks   = 0;
static const int32_t     PRE_TICKS      = 512;    // alarm this early (3.2 us) and spin the rest:
                                                  // alarm-to-entry is ~2.6 us, measured
static const int32_t     CONV_PAIR_TICKS = 1152;  // a pair is done 7.2 us after ADC1 starts (ADC2: 1 us
                                                  // later, 6.2 us long); measured
static const int32_t     SPIN_MAX_TICKS = 1600;   // never spin longer than 10 us
static const int32_t     LATE_TICKS     = 160;    // > 1 us after the target counts as late
static uint32_t          s_seq          = 0;
static uint16_t          s_prev_cmp[2][3];

// Zero-offset measurement: read every channel in rotation, no windows.
static volatile bool     s_zero_mode    = false;
static uint32_t          s_zero_sum[2][3], s_zero_cnt[2][3];
static uint64_t          s_zero_sumsq[2][3];

// Statistics (cycles at 240 MHz, ticks at 160 MHz)
static volatile uint32_t s_st_periods, s_st_late;
static volatile uint64_t s_st_isr_sum;   // cycles: a uint32 overflows after ~40 s
static volatile uint32_t s_st_isr_max;
static volatile int32_t  s_st_entry_min, s_st_entry_max;
static volatile int64_t  s_st_entry_sum;
static volatile int64_t  s_st_start_sum;
static volatile int32_t  s_st_start_max;
static volatile uint32_t s_st_lo_ok[2], s_st_mid_ok[2], s_st_full[2], s_st_off[2];
static volatile uint32_t s_alive[2];
static portMUX_TYPE      s_stats_mux = portMUX_INITIALIZER_UNLOCKED;   // statistics, across cores   // never reset: periods with a usable sample (or outputs off)

static float s_zero_v[2][3]     = { { CSA_VREF, CSA_VREF, CSA_VREF }, { CSA_VREF, CSA_VREF, CSA_VREF } };
static float s_zero_raw[2][3]   = { { 2048, 2048, 2048 }, { 2048, 2048, 2048 } };
static float s_zero_sd[2][3];
static float s_align_spread_us  = NAN;
static bool  s_aligned          = false;
static SamplerTiming s_timing   = { SYNC_LEAD_US, SYNC_MID_US, SYNC_SETTLE_US, SYNC_HOLD_US, SYNC_STAGGER_US, true,
                                     SYNC_EVERY_N_PERIODS };

static const float TICK_US = 1.0f / 160.0f;   // MCPWM tick: 160 MHz, no prescaler (SimpleFOC sets 0)

// Position of unit 0 timer 0 in the 0..2P triangle (0 = zero, P = peak).
static inline int32_t IRAM_ATTR pwmPosition() {
    uint32_t st  = MCPWM0.timer[0].timer_status.val;
    int32_t  val = (int32_t)(st & 0xFFFF);
    return (st & (1u << 16)) ? 2 * s_P - val : val;   // bit 16: counting down
}

// Where a sample started at t (ticks from the peak) sits in the low-side window of a phase with
// compare value c.  The low side conducts from (c + dead time) on the way up to c on the way
// down, i.e. from -(P - c) + DT to +(P - c) relative to the peak.  *on = ticks since the low side
// turned on, *off = ticks until it turns off.
static inline void IRAM_ATTR windowPosition(int32_t t, int32_t c, int16_t* on, int16_t* off) {
    int32_t half = s_P - c;
    *on  = (int16_t)constrain(t - (-half + s_dt_ticks), -32000, 32000);
    *off = (int16_t)constrain(half - t, -32000, 32000);
}

// Inside the window less the settle and hold margins.  `slack` widens both: a duty that changed
// since the previous period may not be the one in force yet.
static inline bool IRAM_ATTR inWindow(int32_t on, int32_t off, int32_t slack) {
    return on >= s_settle_ticks + slack && off >= s_hold_ticks + slack;
}

static inline void IRAM_ATTR pushSample(int m, const RawSample& r) {
    Ring&    q    = s_ring[m];
    uint32_t head = q.head;
    if (head - q.tail >= RING_SIZE) { q.dropped++; return; }
    q.buf[head % RING_SIZE] = r;
    asm volatile("memw" ::: "memory");   // the entry is written before the other core sees head move
    q.head = head + 1;
}

// Arm the alarm for when the PWM counter reaches `pos_target` (0..2P).  next_period: the target
// is in the next period (always in the future); otherwise it is later in this one, and an alarm
// that is already due fires at once.
//
// Reading the timer-group counter means latching it and waiting (~1 us), so it is read once per
// period (next_period = true) and the alarms within the period are placed relative to the last
// alarm: s_alarm_g fired when the PWM counter was at s_alarm_pos (both timers run from the same
// PLL).  If the target is less than 1 us away (this stage ran late) the counter is read anyway.
static uint64_t s_alarm_g   = 0;
static int32_t  s_alarm_pos = 0;

static inline void IRAM_ATTR armAt(int32_t pos_target, bool next_period) {
    const int32_t P   = s_P;
    int32_t       now = pwmPosition();
    if (!next_period && pos_target - now >= 160 && pos_target > s_alarm_pos) {
        s_alarm_g  += (uint64_t)((pos_target - s_alarm_pos) / TICKS_PER_GPT);
        s_alarm_pos = pos_target;
        timer_group_set_alarm_value_in_isr(GPT_GROUP, GPT_IDX, s_alarm_g);
        timer_group_enable_alarm_in_isr(GPT_GROUP, GPT_IDX);
        return;
    }
    uint64_t g     = timer_group_get_counter_value_in_isr(GPT_GROUP, GPT_IDX);
    now            = pwmPosition();
    int32_t  delta = pos_target - now;
    if (next_period) {
        while (delta <= 2 * PRE_TICKS) delta += 2 * P;
        delta += (s_every - 1) * 2 * P;           // skip periods between samples
    } else if (delta < 2 * TICKS_PER_GPT) {
        delta = 2 * TICKS_PER_GPT;
    }
    s_alarm_g   = g + (uint64_t)(delta / TICKS_PER_GPT);
    s_alarm_pos = pos_target % (2 * P);   // next period: the same position, one period on
    timer_group_set_alarm_value_in_isr(GPT_GROUP, GPT_IDX, s_alarm_g);
    timer_group_enable_alarm_in_isr(GPT_GROUP, GPT_IDX);
}

// State carried between the three stages of one period.
static int      s_stage = 0;
static uint16_t s_cmp[2][3];
static uint8_t  s_lo[2], s_mid[2], s_flags[2];
static int32_t  s_t_lo, s_t_mid, s_entry;
static uint16_t s_raw_lo[2], s_raw_mid[2];
static int32_t  s_t_lo2;
static uint32_t s_cycles;   // CPU cycles spent in this period's stages so far
volatile uint32_t g_sync_prof[8];   // profiling: cycle sums of selected steps (SYNCPROF)

// The sampling interrupt, in three short stages per PWM period so the CPU does not sit waiting
// for conversions (one stage that waited for both pairs took ~24 us of every 50 us of core 0,
// and the housekeeping task could not keep up):
//   stage 0, just before peak - lead: read the duties, spin to peak - lead, start pair 1;
//   stage 1, just before the peak:    spin to the peak, collect pair 1, start pair 2;
//   stage 2, ~8 us later:             collect pair 2, check windows, push the samples.
static void IRAM_ATTR samplerIsr(void*) {
    timer_group_clr_intr_status_in_isr(GPT_GROUP, GPT_IDX);
    if (!s_running) { s_stage = 0; return; }
    uint32_t c0 = ccount();
    const int32_t P      = s_P;
    const int32_t target = P - s_lead_ticks;   // first pair's start, on the rising slope

    if (s_stage == 0) {
        // Compare values written for this period.  They are the shadow registers: SimpleFOC's
        // writes take effect at the next timer zero, so a write since the last zero is not in
        // force yet.  That is handled as slack in the window check (duty moves a little per period).
        for (int m = 0; m < 2; m++) {
            volatile mcpwm_dev_t& dev = m ? MCPWM1 : MCPWM0;
            for (int k = 0; k < 3; k++) s_cmp[m][k] = (uint16_t)dev.operators[k].timestamp[0].gen;
            int a = s_cmp[m][0], b = s_cmp[m][1], c = s_cmp[m][2];
            // Sort: lowest compare = lowest duty = widest low-side window.
            int l = 0, h = 0;
            if (b < a) l = 1;
            if (c < (l == 0 ? a : b)) l = 2;
            if (b > a) h = 1;
            if (c > (h == 0 ? a : b)) h = 2;
            if (h == l) h = (l + 1) % 3;              // all equal
            s_lo[m]    = (uint8_t)l;
            s_mid[m]   = (uint8_t)(3 - l - h);
            s_flags[m] = (a == 0 && b == 0 && c == 0) ? SAMPLE_OUTPUTS_OFF : 0;
            for (int k = 0; k < 3; k++)
                if (s_cmp[m][k] != s_prev_cmp[m][k]) s_flags[m] |= SAMPLE_DUTY_MOVED;
        }
        if (s_zero_mode) {   // drivers asleep: read every channel in rotation
            s_lo[0] = s_lo[1] = (uint8_t)(s_seq % 3);
            s_mid[0] = s_mid[1] = (uint8_t)((s_seq + 1) % 3);
        }

        adcSelect(ADC_CH[0][s_lo[0]], ADC_CH[1][s_lo[1]]);   // settles during the spin
        uint32_t p0 = ccount();
        g_sync_prof[0] += p0 - c0;                  // duty reads + sort
        // Spin to the first pair's target and start it: lowest-duty phase of each motor.
        int32_t pos = pwmPosition();
        s_entry     = target - pos;                 // ticks early (+) or late (-) at entry
        while (target - pos > 0 && target - pos < SPIN_MAX_TICKS) pos = pwmPosition();
        s_t_lo = pos - P;
        uint32_t p1 = ccount();
        g_sync_prof[1] += p1 - p0;                  // spin
        adcStartPair(ADC_CH[0][s_lo[0]], ADC_CH[1][s_lo[1]]);
        uint32_t p2 = ccount();
        g_sync_prof[2] += p2 - p1;                  // two ADC starts

        s_stage  = 1;
        armAt(P + s_mid_ticks - PRE_TICKS, false);
        g_sync_prof[3] += ccount() - p2;            // armAt
        s_cycles = ccount() - c0;
        return;
    }

    if (s_stage == 1) {
        // Collect pair 1, select pair 2's pads, spin to the second pair's target, start pair 2:
        // the middle-duty phase.
        const int32_t target2 = P + s_mid_ticks;
        uint32_t q0 = ccount();
        s_raw_lo[0] = adc1End();
        s_raw_lo[1] = adc2End();
        adcSelect(ADC_CH[0][s_mid[0]], ADC_CH[1][s_mid[1]]);
        g_sync_prof[5] += ccount() - q0;            // two ADC collects + select
        int32_t pos = pwmPosition();
        while (target2 - pos > 0 && target2 - pos < SPIN_MAX_TICKS) pos = pwmPosition();
        g_sync_prof[4] += ccount() - q0;            // stage 1 up to the start
        s_t_mid = pos - P;
        adcStartPair(ADC_CH[0][s_mid[0]], ADC_CH[1][s_mid[1]]);

        s_stage   = 2;
        s_cycles += ccount() - c0;
        armAt(pos + CONV_PAIR_TICKS - PRE_TICKS, false);
        return;
    }

    if (s_stage == 2 && s_lo_twice) {
        // Collect pair 2, then read the lowest-duty phase again as far after the peak as the
        // first read was before it.  In the zero vector (all low sides on) the windings are
        // shorted and each phase current slopes at -(R i + e) / L, ~0.12 A/us at 12,000 RPM, so a
        // read 8.4 us early is up to ~1 A off its value at the peak; the mean of two symmetric
        // reads cancels the slope.
        s_raw_mid[0] = adc1End();
        s_raw_mid[1] = adc2End();
        adcSelect(ADC_CH[0][s_lo[0]], ADC_CH[1][s_lo[1]]);
        const int32_t target3 = P - s_t_lo;          // mirror of the first read about the peak
        int32_t pos = pwmPosition();
        while (target3 - pos > 0 && target3 - pos < SPIN_MAX_TICKS) pos = pwmPosition();
        s_t_lo2 = pos - P;
        adcStartPair(ADC_CH[0][s_lo[0]], ADC_CH[1][s_lo[1]]);

        s_stage   = 3;
        s_cycles += ccount() - c0;
        armAt(pos + CONV_PAIR_TICKS - PRE_TICKS, false);
        return;
    }

    // Stage 3 (or 2 without the second read): collect, check windows, push.
    uint16_t raw_lo2[2] = { 0xFFFF, 0xFFFF };
    bool     did_lo2    = (s_stage == 3);
    s_stage = 0;
    if (did_lo2) {
        raw_lo2[0] = adc1End();
        raw_lo2[1] = adc2End();
    } else {
        s_raw_mid[0] = adc1End();
        s_raw_mid[1] = adc2End();
        s_t_lo2      = 0;
    }
    const uint16_t* raw_lo  = s_raw_lo;
    const uint16_t* raw_mid = s_raw_mid;

    // A wedged ADC would make every period wait out its timeouts and starve core 0.  Stop instead;
    // the housekeeping task's stale check then stops the motors.
    if (raw_lo[0] == 0xFFFF || raw_lo[1] == 0xFFFF || raw_mid[0] == 0xFFFF || raw_mid[1] == 0xFFFF ||
        (did_lo2 && (raw_lo2[0] == 0xFFFF || raw_lo2[1] == 0xFFFF))) {
        if (++s_wedged >= WEDGED_STOP) { s_running = false; return; }
    } else {
        s_wedged = 0;
    }

    const int32_t  t_lo  = s_t_lo, t_mid = s_t_mid, t_lo2 = s_t_lo2;
    const uint32_t seq   = s_seq;
    s_seq += s_every;   // PWM periods, so a sample's seq difference is time
    const bool     late  = (t_lo - (target - P)) > LATE_TICKS;

    if (s_zero_mode) {
        for (int m = 0; m < 2; m++) {
            if (raw_lo[m] != 0xFFFF) {
                s_zero_sum[m][s_lo[m]] += raw_lo[m];
                s_zero_sumsq[m][s_lo[m]] += (uint64_t)raw_lo[m] * raw_lo[m];
                s_zero_cnt[m][s_lo[m]]++;
            }
            if (raw_lo2[m] != 0xFFFF) {
                s_zero_sum[m][s_lo[m]] += raw_lo2[m];
                s_zero_sumsq[m][s_lo[m]] += (uint64_t)raw_lo2[m] * raw_lo2[m];
                s_zero_cnt[m][s_lo[m]]++;
            }
            if (raw_mid[m] != 0xFFFF) {
                s_zero_sum[m][s_mid[m]] += raw_mid[m];
                s_zero_sumsq[m][s_mid[m]] += (uint64_t)raw_mid[m] * raw_mid[m];
                s_zero_cnt[m][s_mid[m]]++;
            }
        }
    } else {
        for (int m = 0; m < 2; m++) {
            RawSample r = {};
            r.seq     = seq;
            r.cmp[0]  = s_cmp[m][0]; r.cmp[1] = s_cmp[m][1]; r.cmp[2] = s_cmp[m][2];
            r.raw_lo  = raw_lo[m];
            r.raw_mid = raw_mid[m];
            r.raw_lo2 = raw_lo2[m];
            const int32_t st = m ? s_stagger_ticks : 0;   // ADC2 (motor 2) starts later
            r.t_lo    = (int16_t)(t_lo + st);
            r.t_mid   = (int16_t)(t_mid + st);
            r.t_lo2   = (int16_t)(t_lo2 + st);
            r.ph_lo   = s_lo[m];
            r.ph_mid  = s_mid[m];
            uint8_t f = s_flags[m] | (late ? SAMPLE_LATE : 0);
            int32_t slack_lo  = 0, slack_mid = 0;
            if (f & SAMPLE_DUTY_MOVED) {
                slack_lo  = abs((int32_t)s_cmp[m][s_lo[m]]  - (int32_t)s_prev_cmp[m][s_lo[m]]);
                slack_mid = abs((int32_t)s_cmp[m][s_mid[m]] - (int32_t)s_prev_cmp[m][s_mid[m]]);
            }
            windowPosition(r.t_lo, s_cmp[m][s_lo[m]], &r.on_lo, &r.off_lo);
            windowPosition(r.t_mid, s_cmp[m][s_mid[m]], &r.on_mid, &r.off_mid);
            windowPosition(r.t_lo2, s_cmp[m][s_lo[m]], &r.on_lo2, &r.off_lo2);
            if (!(f & SAMPLE_OUTPUTS_OFF)) {
                bool lo1 = raw_lo[m] != 0xFFFF && inWindow(r.on_lo, r.off_lo, slack_lo);
                bool lo2 = raw_lo2[m] != 0xFFFF && inWindow(r.on_lo2, r.off_lo2, slack_lo);
                if (lo1 || lo2) f |= SAMPLE_LO_OK;
                if (lo1 && lo2) f |= SAMPLE_LO_PAIR;
                r.lo_which = (uint8_t)((lo1 ? 1 : 0) | (lo2 ? 2 : 0));
                if (raw_mid[m] != 0xFFFF && inWindow(r.on_mid, r.off_mid, slack_mid)) f |= SAMPLE_MID_OK;
                if ((f & SAMPLE_LO_OK) && (f & SAMPLE_MID_OK)) f |= SAMPLE_FULL;
            }
            r.flags = f;
            pushSample(m, r);
            if (f & SAMPLE_LO_OK)       s_st_lo_ok[m]++;
            if (f & SAMPLE_MID_OK)      s_st_mid_ok[m]++;
            if (f & SAMPLE_FULL)        s_st_full[m]++;
            if (f & SAMPLE_OUTPUTS_OFF) s_st_off[m]++;
            if (f & (SAMPLE_LO_OK | SAMPLE_OUTPUTS_OFF)) s_alive[m]++;
        }
    }
    for (int m = 0; m < 2; m++)
        for (int k = 0; k < 3; k++) s_prev_cmp[m][k] = s_cmp[m][k];

    g_sync_prof[6] += ccount() - c0;               // stage 2 up to stats
    // Statistics (under the lock samplerResetStats() takes from the other core).
    portENTER_CRITICAL_ISR(&s_stats_mux);
    int32_t start_err = t_lo - (target - P);
    s_st_periods++;
    if (late) s_st_late++;
    s_st_entry_sum += s_entry;
    if (s_entry < s_st_entry_min) s_st_entry_min = s_entry;
    if (s_entry > s_st_entry_max) s_st_entry_max = s_entry;
    s_st_start_sum += start_err;
    if (start_err > s_st_start_max) s_st_start_max = start_err;
    portEXIT_CRITICAL_ISR(&s_stats_mux);

    // Next period: PRE_TICKS before the next first-pair target, computed from where the PWM
    // counter is now, so the schedule follows the MCPWM timer exactly.
    armAt(target - PRE_TICKS, true);

    uint32_t dur = s_cycles + (ccount() - c0);
    s_st_isr_sum += dur;
    if (dur > s_st_isr_max) s_st_isr_max = dur;
}

// --- Task-side helpers ---

static int32_t usToTicks(float us) { return (int32_t)lroundf(us * 160.0f); }

static void applyTiming() {
    s_lead_ticks   = constrain(usToTicks(s_timing.lead_us), 0, s_P - 200);
    s_mid_ticks    = constrain(usToTicks(s_timing.mid_us), -1600, 1600);
    s_settle_ticks = usToTicks(s_timing.settle_us);
    s_hold_ticks   = usToTicks(s_timing.hold_us);
    s_stagger_ticks = constrain(usToTicks(s_timing.stagger_us), 0, 800);
    s_lo_twice      = s_timing.lo_twice;
    s_every         = constrain(s_timing.every, 1, 4);
}

void samplerResetStats() {
    // The interrupt runs on the other core: take the same lock it takes around its statistics.
    // (Clearing s_running instead would stop the sampler: a skipped interrupt does not re-arm.)
    portENTER_CRITICAL(&s_stats_mux);
    s_st_periods = s_st_late = 0;
    s_st_isr_sum = s_st_isr_max = 0;
    s_st_entry_min = INT32_MAX; s_st_entry_max = INT32_MIN; s_st_entry_sum = 0;
    s_st_start_sum = 0; s_st_start_max = INT32_MIN;
    for (int m = 0; m < 2; m++) {
        s_st_lo_ok[m] = s_st_mid_ok[m] = s_st_full[m] = s_st_off[m] = 0;
        s_ring[m].dropped = 0;
    }
    portEXIT_CRITICAL(&s_stats_mux);
}

SamplerStats samplerStats() {
    SamplerStats s{};
    uint32_t n   = s_st_periods ? s_st_periods : 1;
    s.periods    = s_st_periods;
    s.late       = s_st_late;
    s.isr_us_avg = (float)s_st_isr_sum / n / 240.0f;
    s.isr_us_max = s_st_isr_max / 240.0f;
    // Entry: how far ahead of the first pair's target the interrupt began (PRE_TICKS nominal).
    s.entry_us_min = s_st_periods ? s_st_entry_min * TICK_US : NAN;
    s.entry_us_avg = (float)s_st_entry_sum / n * TICK_US;
    s.entry_us_max = s_st_periods ? s_st_entry_max * TICK_US : NAN;
    s.start_us_avg = (float)s_st_start_sum / n * TICK_US;
    s.start_us_max = s_st_periods ? s_st_start_max * TICK_US : NAN;
    for (int m = 0; m < 2; m++) {
        s.dropped[m] = s_ring[m].dropped;
        s.lo_ok[m]   = s_st_lo_ok[m];
        s.mid_ok[m]  = s_st_mid_ok[m];
        s.full[m]    = s_st_full[m];
        s.off[m]     = s_st_off[m];
    }
    return s;
}

SamplerTiming samplerTiming() { return s_timing; }

void samplerSetTiming(const SamplerTiming& t) {
    s_timing = t;
    applyTiming();
}

float samplerAlignSpreadUs() { return s_align_spread_us; }
float samplerPeriodUs()      { return 2.0f * s_P * TICK_US; }
float samplerDeadTimeUs()    { return s_dt_ticks * TICK_US; }
bool  samplerRunning()       { return s_running; }
uint32_t samplerAliveCount(int motor) { return s_alive[motor & 1]; }

float samplerZeroVolts(int motor, int phase)    { return s_zero_v[motor & 1][phase % 3]; }
float samplerZeroNoiseLsb(int motor, int phase) { return s_zero_sd[motor & 1][phase % 3]; }

static inline int32_t posOf(uint32_t st) {
    int32_t val = (int32_t)(st & 0xFFFF);
    return (st & (1u << 16)) ? 2 * s_P - val : val;
}

// Phase of each of the six timers relative to unit 0 timer 0, in ticks (+ = ahead), each read
// bracketed by two reads of unit 0 timer 0 so the read time cancels.  Average of 64 brackets.
static void measureOffsets(int32_t off[6]) {
    volatile mcpwm_dev_t* units[2] = { &MCPWM0, &MCPWM1 };
    for (int i = 0; i < 6; i++) {
        int64_t sum = 0;
        int     n   = 0;
        for (int k = 0; k < 200 && n < 64; k++) {
            portDISABLE_INTERRUPTS();
            uint32_t a = MCPWM0.timer[0].timer_status.val;
            uint32_t x = units[i / 3]->timer[i % 3].timer_status.val;
            uint32_t b = MCPWM0.timer[0].timer_status.val;
            portENABLE_INTERRUPTS();
            int32_t pa = posOf(a), px = posOf(x), pb = posOf(b);
            if (pb < pa) continue;                       // unit 0 wrapped between the reads
            int32_t d = px - (pa + pb) / 2;
            while (d > s_P) d -= 2 * s_P;
            while (d < -s_P) d += 2 * s_P;
            sum += d;
            n++;
        }
        off[i] = n ? (int32_t)(sum / n) : 0;
    }
}

static void sayOffsets(void (*say)(const char*), const char* what, const int32_t off[6]) {
    char line[110];
    snprintf(line, sizeof(line), "SYNC timer phase %s (us vs U0T0): U0 %+.2f %+.2f  U1 %+.2f %+.2f %+.2f", what,
             off[1] * TICK_US, off[2] * TICK_US, off[3] * TICK_US, off[4] * TICK_US, off[5] * TICK_US);
    say(line);
}

// Align all six MCPWM timers (both units) to count from zero together.  Both units run from the
// same clock with the same prescaler and period, so this holds from then on.  It makes the
// counters jump once, so only call it with every motor output off.
static void alignTimers(void (*say)(const char*)) {
    int32_t off[6];
    measureOffsets(off);
    sayOffsets(say, "before", off);

    // How SimpleFOC wires each unit: timer 0's sync output fires on every timer zero, and all
    // three timers (0 too) reload their sync phase from it.  That is what keeps a motor's three
    // phases locked, and it means every phase value must stay 0: anything else is reloaded every
    // period and shortens the PWM period.  So the only free variable between the units is WHEN
    // unit 1 is synced: a software sync on unit 1 timer 0 restarts unit 1 from zero.  Fire it as
    // unit 0 reaches zero, less the time the write takes to land (`d`, learned from the residual).
    int32_t d = 160;
    bool    fired = false;
    for (int iter = 0; iter < 8; iter++) {
        const int32_t target = (2 * s_P - d) % (2 * s_P);   // unit 0 position to fire at
        portDISABLE_INTERRUPTS();
        fired = false;
        for (int k = 0; k < 20000; k++) {       // bounded: the target comes round every 50 us
            int32_t q    = posOf(MCPWM0.timer[0].timer_status.val);
            int32_t past = (q - target + 2 * s_P) % (2 * s_P);
            if (past < 120) { fired = true; break; }
        }
        MCPWM1.timer[0].timer_sync.timer_sync_sw = ~MCPWM1.timer[0].timer_sync.timer_sync_sw;
        portENABLE_INTERRUPTS();
        delayMicroseconds(200);
        measureOffsets(off);
        int32_t err = (off[3] + off[4] + off[5]) / 3;   // = d - actual write delay
        if (abs(err) <= 4) break;
        d = (d - err) % (2 * s_P);
        if (d < 0) d += 2 * s_P;
    }
    sayOffsets(say, "after", off);
    char line[64];
    snprintf(line, sizeof(line), "SYNC unit 1 sync fired %.2f us before unit 0's zero%s", d * TICK_US,
             fired ? "" : " (TIMED OUT)");
    say(line);
    int32_t worst = 0;
    for (int i = 1; i < 6; i++) worst = max(worst, abs(off[i]));
    s_align_spread_us = worst * TICK_US;
    s_aligned         = true;
}

static bool startTimerAndIsr(void (*say)(const char*)) {
    timer_config_t cfg = {};
    cfg.alarm_en    = TIMER_ALARM_EN;
    cfg.counter_en  = TIMER_PAUSE;
    cfg.intr_type   = TIMER_INTR_LEVEL;
    cfg.counter_dir = TIMER_COUNT_UP;
    cfg.auto_reload = TIMER_AUTORELOAD_DIS;
    cfg.divider     = GPT_DIVIDER;
    esp_err_t err   = timer_init(GPT_GROUP, GPT_IDX, &cfg);
    if (err == ESP_OK) err = timer_set_counter_value(GPT_GROUP, GPT_IDX, 0);
    if (err == ESP_OK) err = timer_set_alarm_value(GPT_GROUP, GPT_IDX, 2000);   // first: 50 us
    if (err == ESP_OK) err = timer_enable_intr(GPT_GROUP, GPT_IDX);
    if (err == ESP_OK && !s_isr_handle) {
        // Allocate the interrupt on core 1 (an interrupt is allocated on the core that registers
        // it, so run the registration there through the IPC task).  On core 0 it took ~45-55% of
        // the core with its four stages and dispatch overhead, and the housekeeping task - over-
        // current check, load fit, observer - fell behind and dropped samples.  Core 1 runs only
        // the open-loop commutation, which at one pole pair (200 Hz electrical at 12,000 RPM)
        // needs far less than its loop rate; and a closed current loop would live there anyway.
        // This build runs the IPC task at the CALLER's priority (CONFIG_ESP_IPC_USES_CALLERS_PRIORITY),
        // and the FOC task on core 1 never blocks: called at the housekeeping priority the IPC
        // never ran and the task watchdog fired.  So call it from above the FOC task's priority.
        static esp_err_t reg_err;
        reg_err = ESP_FAIL;
        UBaseType_t prio = uxTaskPriorityGet(nullptr);
        vTaskPrioritySet(nullptr, configMAX_PRIORITIES - 2);
        esp_err_t ipc = esp_ipc_call_blocking(SYNC_ISR_CORE, [](void*) {
            reg_err = timer_isr_register(GPT_GROUP, GPT_IDX, samplerIsr, nullptr, ESP_INTR_FLAG_IRAM, &s_isr_handle);
        }, nullptr);
        vTaskPrioritySet(nullptr, prio);
        err = (ipc == ESP_OK) ? reg_err : ipc;
    }
    if (err == ESP_OK) err = timer_start(GPT_GROUP, GPT_IDX);
    if (err != ESP_OK) {
        char line[80];
        snprintf(line, sizeof(line), "SYNC FAILED: timer set-up error %d", (int)err);
        say(line);
        return false;
    }
    return true;
}

static bool measureZero(void (*say)(const char*)) {
    for (int m = 0; m < 2; m++)
        for (int k = 0; k < 3; k++) { s_zero_sum[m][k] = 0; s_zero_sumsq[m][k] = 0; s_zero_cnt[m][k] = 0; }
    s_zero_mode = true;
    uint32_t t0 = millis();
    const uint32_t N = 600;   // per channel; 3 channels share 2 reads per period -> ~45 ms
    for (;;) {
        bool done = true;
        for (int m = 0; m < 2; m++)
            for (int k = 0; k < 3; k++) if (s_zero_cnt[m][k] < N) done = false;
        if (done || millis() - t0 > 500) break;
        delay(5);
    }
    s_zero_mode = false;
    bool ok = true;
    for (int m = 0; m < 2; m++) {
        for (int k = 0; k < 3; k++) {
            uint32_t n = s_zero_cnt[m][k];
            if (n < N / 2) { ok = false; continue; }
            float mean = (float)s_zero_sum[m][k] / n;
            float var  = (float)((double)s_zero_sumsq[m][k] / n - (double)mean * mean);
            s_zero_raw[m][k] = mean;
            s_zero_v[m][k]   = mean / 4095.0f * 3.3f;
            s_zero_sd[m][k]  = sqrtf(var > 0 ? var : 0);
        }
    }
    char line[100];
    snprintf(line, sizeof(line), "SYNC zero LSB/sd M1 %.0f/%.1f %.0f/%.1f %.0f/%.1f  M2 %.0f/%.1f %.0f/%.1f %.0f/%.1f%s",
             s_zero_raw[0][0], s_zero_sd[0][0], s_zero_raw[0][1], s_zero_sd[0][1], s_zero_raw[0][2], s_zero_sd[0][2],
             s_zero_raw[1][0], s_zero_sd[1][0], s_zero_raw[1][1], s_zero_sd[1][1], s_zero_raw[1][2], s_zero_sd[1][2],
             ok ? "" : " INCOMPLETE");
    say(line);
    return ok;
}

extern bool driversAwake();

bool samplerBegin(void (*say)(const char*)) {
    if (s_running) return true;
    s_P = (int32_t)MCPWM0.timer[0].timer_cfg0.timer_period;
    if (s_P < 1000 || (int32_t)MCPWM1.timer[0].timer_cfg0.timer_period != s_P ||
        MCPWM0.timer[0].timer_cfg0.timer_prescale != 0 || MCPWM1.timer[0].timer_cfg0.timer_prescale != 0 ||
        MCPWM0.clk_cfg.clk_prescale != 0 || MCPWM1.clk_cfg.clk_prescale != 0) {
        say("SYNC FAILED: MCPWM units not set up as expected (period/prescalers differ)");
        return false;
    }
    captureAdcControl();
    int32_t fed = (int32_t)MCPWM0.operators[0].dt_fed_cfg.dt_fed;
    s_dt_ticks  = fed > 0 ? fed : 81;
    applyTiming();

    bool asleep = !driversAwake();
    if (!s_aligned) {
        if (!asleep) {
            say("SYNC FAILED: drivers awake at start - timers not aligned");
            return false;
        }
        alignTimers(say);
    }

    for (int m = 0; m < 2; m++) {
        s_ring[m].head = s_ring[m].tail = 0;
        for (int k = 0; k < 3; k++) s_prev_cmp[m][k] = 0;
    }
    samplerResetStats();
    s_wedged  = 0;
    s_stage   = 0;   // an earlier stop may have left it mid-period
    s_running = true;   // before the timer starts: an interrupt that finds it false does not re-arm
    if (!startTimerAndIsr(say)) {
        s_running = false;
        return false;
    }

    if (asleep) {
        if (!measureZero(say)) {
            samplerEnd();
            return false;
        }
    }
    samplerResetStats();
    return true;
}

static void sayUsb(const char* line) { if (Serial) Serial.println(line); }

bool samplerRezero() {
    if (!s_running || driversAwake()) return false;
    measureZero(sayUsb);
    return true;
}

void samplerEnd() {
    s_running = false;
    timer_pause(GPT_GROUP, GPT_IDX);
    timer_disable_intr(GPT_GROUP, GPT_IDX);
    // The interrupt runs on the other core and may be mid-stage: give it time to finish (a stage
    // waits at most for one conversion) before the caller touches the ADCs or WiFi.
    delayMicroseconds(200);
}

int samplerRead(int motor, CurrentSample* out, int max) {
    Ring&    q     = s_ring[motor & 1];
    uint32_t head  = q.head;
    asm volatile("memw" ::: "memory");
    int      n     = 0;
    const float to_v   = 3.3f / 4095.0f;
    const float inv_p  = 1.0f / (float)s_P;
    const float inv_g  = 1.0f / CSA_GAIN_V_PER_A;
    while (q.tail != head && n < max) {
        const RawSample& r = q.buf[q.tail % RING_SIZE];
        CurrentSample&   s = out[n++];
        s.seq    = r.seq;
        s.flags  = r.flags;
        s.ph_lo  = r.ph_lo;
        s.ph_mid = r.ph_mid;
        for (int k = 0; k < 3; k++) {
            s.duty[k] = (float)r.cmp[k] * inv_p;
            s.i[k]    = 0.0f;
        }
        // The lowest-duty phase: the mean of its two reads (symmetric about the peak) when both
        // were in their windows, else whichever was.
        float raw_lo;
        if (r.lo_which == 3)      raw_lo = 0.5f * ((float)r.raw_lo + (float)r.raw_lo2);
        else if (r.lo_which == 2) raw_lo = r.raw_lo2;
        else                      raw_lo = (r.raw_lo == 0xFFFF ? 0 : r.raw_lo);
        float i_lo  = (raw_lo * to_v - s_zero_v[motor][r.ph_lo]) * inv_g;
        s.i_lo_before = (r.lo_which & 1) ? ((float)r.raw_lo * to_v - s_zero_v[motor][r.ph_lo]) * inv_g : NAN;
        s.i_lo_after  = (r.lo_which & 2) ? ((float)r.raw_lo2 * to_v - s_zero_v[motor][r.ph_lo]) * inv_g : NAN;
        float i_mid = ((r.raw_mid == 0xFFFF ? 0 : r.raw_mid) * to_v - s_zero_v[motor][r.ph_mid]) * inv_g;
        s.i[r.ph_lo]  = i_lo;
        s.i[r.ph_mid] = i_mid;
        int hi = 3 - r.ph_lo - r.ph_mid;
        if (r.ph_lo != r.ph_mid) s.i[hi] = -(i_lo + i_mid);
        s.t_lo_us    = r.t_lo * TICK_US;
        s.t_mid_us   = r.t_mid * TICK_US;
        s.on_lo_us   = r.on_lo * TICK_US;
        s.on_mid_us  = r.on_mid * TICK_US;
        s.off_mid_us = r.off_mid * TICK_US;
        q.tail++;
    }
    return n;
}

#endif
