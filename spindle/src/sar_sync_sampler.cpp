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

static inline void IRAM_ATTR adc1Start(int ch) {
    CLEAR_PERI_REG_MASK(SENS_SAR_MEAS1_CTRL2_REG, SENS_MEAS1_START_SAR_M);
    SET_PERI_REG_BITS(SENS_SAR_MEAS1_CTRL2_REG, SENS_SAR1_EN_PAD, (1 << ch), SENS_SAR1_EN_PAD_S);
    SET_PERI_REG_MASK(SENS_SAR_MEAS1_CTRL2_REG, SENS_MEAS1_START_SAR_M);
}

static inline void IRAM_ATTR adc2Start(int ch) {
    CLEAR_PERI_REG_MASK(SENS_SAR_MEAS2_CTRL2_REG, SENS_MEAS2_START_SAR_M);
    SET_PERI_REG_BITS(SENS_SAR_MEAS2_CTRL2_REG, SENS_SAR2_EN_PAD, (1 << ch), SENS_SAR2_EN_PAD_S);
    SET_PERI_REG_MASK(SENS_SAR_MEAS2_CTRL2_REG, SENS_MEAS2_START_SAR_M);
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
static const int ADC_CH[2][3] = { { CURA - 1, CURB - 1, CURC - 1 },
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
    uint16_t raw_lo, raw_mid;
    int16_t  t_lo, t_mid;        // conversion start, ticks relative to the peak
    int16_t  on_lo, off_lo;      // ticks since that phase's low side turned on / until it turns off,
    int16_t  on_mid, off_mid;    // at the conversion start
    uint8_t  ph_lo, ph_mid, flags;
};

static const uint32_t RING_SIZE = 128;   // 6.4 ms of periods; the housekeeping pass is 2 ms
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
static const int32_t     PRE_TICKS      = 400;    // alarm this early (2.5 us) and spin the rest
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
static volatile uint32_t s_st_isr_sum, s_st_isr_max;
static volatile int32_t  s_st_entry_min, s_st_entry_max;
static volatile int64_t  s_st_entry_sum;
static volatile int64_t  s_st_start_sum;
static volatile int32_t  s_st_start_max;
static volatile uint32_t s_st_lo_ok[2], s_st_mid_ok[2], s_st_full[2], s_st_off[2];

static float s_zero_v[2][3]     = { { CSA_VREF, CSA_VREF, CSA_VREF }, { CSA_VREF, CSA_VREF, CSA_VREF } };
static float s_zero_raw[2][3]   = { { 2048, 2048, 2048 }, { 2048, 2048, 2048 } };
static float s_zero_sd[2][3];
static float s_align_spread_us  = NAN;
static bool  s_aligned          = false;
static SamplerTiming s_timing   = { SYNC_LEAD_US, SYNC_MID_US, SYNC_SETTLE_US, SYNC_HOLD_US };

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
    q.head = head + 1;
}

static void IRAM_ATTR samplerIsr(void*) {
    timer_group_clr_intr_status_in_isr(GPT_GROUP, GPT_IDX);
    if (!s_running) return;
    uint32_t c0 = ccount();

    const int32_t P      = s_P;
    const int32_t target = P - s_lead_ticks;   // first pair's start, on the rising slope

    // Compare values written for this period.  They are the shadow registers: SimpleFOC's writes
    // take effect at the next timer zero, so a write since the last zero is not in force yet.
    // That is handled as slack in the window check (the duty moves a little per period).
    uint16_t cmp[2][3];
    uint8_t  lo[2], mid[2], flags[2];
    for (int m = 0; m < 2; m++) {
        volatile mcpwm_dev_t& dev = m ? MCPWM1 : MCPWM0;
        for (int k = 0; k < 3; k++) cmp[m][k] = (uint16_t)dev.operators[k].timestamp[0].gen;
        int a = cmp[m][0], b = cmp[m][1], c = cmp[m][2];
        // Sort: lowest compare = lowest duty = widest low-side window.
        int l = 0, h = 0;
        if (b < a) l = 1;
        if (c < (l == 0 ? a : b)) l = 2;
        if (b > a) h = 1;
        if (c > (h == 0 ? a : b)) h = 2;
        if (h == l) h = (l + 1) % 3;              // all equal
        lo[m]    = (uint8_t)l;
        mid[m]   = (uint8_t)(3 - l - h);
        flags[m] = (a == 0 && b == 0 && c == 0) ? SAMPLE_OUTPUTS_OFF : 0;
        for (int k = 0; k < 3; k++)
            if (cmp[m][k] != s_prev_cmp[m][k]) flags[m] |= SAMPLE_DUTY_MOVED;
    }

    if (s_zero_mode) {   // drivers asleep: read every channel in rotation
        lo[0] = lo[1] = (uint8_t)(s_seq % 3);
        mid[0] = mid[1] = (uint8_t)((s_seq + 1) % 3);
    }

    // Spin to the first pair's target.
    int32_t pos   = pwmPosition();
    int32_t entry = target - pos;                 // ticks early (+) or late (-) at entry
    while (target - pos > 0 && target - pos < SPIN_MAX_TICKS) pos = pwmPosition();

    // Pair 1: lowest-duty phase of each motor.
    int32_t t_lo = pos - P;
    adc1Start(ADC_CH[0][lo[0]]);
    adc2Start(ADC_CH[1][lo[1]]);
    uint16_t r1_lo = adc1End();
    uint16_t r2_lo = adc2End();

    // Pair 2: middle-duty phase, at the peak (or later, if mid_ticks > 0).
    const int32_t target2 = P + s_mid_ticks;
    pos = pwmPosition();
    while (target2 - pos > 0 && target2 - pos < SPIN_MAX_TICKS) pos = pwmPosition();
    int32_t t_mid = pos - P;
    adc1Start(ADC_CH[0][mid[0]]);
    adc2Start(ADC_CH[1][mid[1]]);
    uint16_t r1_mid = adc1End();
    uint16_t r2_mid = adc2End();

    // A wedged ADC would make every period take ~120 us and starve core 0.  Stop instead; the
    // housekeeping task's stale check then stops the motors.
    if (r1_lo == 0xFFFF || r2_lo == 0xFFFF || r1_mid == 0xFFFF || r2_mid == 0xFFFF) {
        if (++s_wedged >= WEDGED_STOP) { s_running = false; return; }
    } else {
        s_wedged = 0;
    }

    const uint32_t seq   = s_seq++;
    const bool     late  = (t_lo - (target - P)) > LATE_TICKS;
    const uint16_t raw_lo[2]  = { r1_lo, r2_lo };
    const uint16_t raw_mid[2] = { r1_mid, r2_mid };

    if (s_zero_mode) {
        for (int m = 0; m < 2; m++) {
            if (raw_lo[m] != 0xFFFF) {
                s_zero_sum[m][lo[m]] += raw_lo[m];
                s_zero_sumsq[m][lo[m]] += (uint64_t)raw_lo[m] * raw_lo[m];
                s_zero_cnt[m][lo[m]]++;
            }
            if (raw_mid[m] != 0xFFFF) {
                s_zero_sum[m][mid[m]] += raw_mid[m];
                s_zero_sumsq[m][mid[m]] += (uint64_t)raw_mid[m] * raw_mid[m];
                s_zero_cnt[m][mid[m]]++;
            }
        }
    } else {
        for (int m = 0; m < 2; m++) {
            RawSample r = {};
            r.seq     = seq;
            r.cmp[0]  = cmp[m][0]; r.cmp[1] = cmp[m][1]; r.cmp[2] = cmp[m][2];
            r.raw_lo  = raw_lo[m];
            r.raw_mid = raw_mid[m];
            r.t_lo    = (int16_t)t_lo;
            r.t_mid   = (int16_t)t_mid;
            r.ph_lo   = lo[m];
            r.ph_mid  = mid[m];
            uint8_t f = flags[m] | (late ? SAMPLE_LATE : 0);
            int32_t slack_lo  = 0, slack_mid = 0;
            if (f & SAMPLE_DUTY_MOVED) {
                slack_lo  = abs((int32_t)cmp[m][lo[m]]  - (int32_t)s_prev_cmp[m][lo[m]]);
                slack_mid = abs((int32_t)cmp[m][mid[m]] - (int32_t)s_prev_cmp[m][mid[m]]);
            }
            windowPosition(t_lo, cmp[m][lo[m]], &r.on_lo, &r.off_lo);
            windowPosition(t_mid, cmp[m][mid[m]], &r.on_mid, &r.off_mid);
            if (!(f & SAMPLE_OUTPUTS_OFF)) {
                if (raw_lo[m] != 0xFFFF && inWindow(r.on_lo, r.off_lo, slack_lo)) f |= SAMPLE_LO_OK;
                if (raw_mid[m] != 0xFFFF && inWindow(r.on_mid, r.off_mid, slack_mid)) f |= SAMPLE_MID_OK;
                if ((f & SAMPLE_LO_OK) && (f & SAMPLE_MID_OK)) f |= SAMPLE_FULL;
            }
            r.flags = f;
            pushSample(m, r);
            if (f & SAMPLE_LO_OK)       s_st_lo_ok[m]++;
            if (f & SAMPLE_MID_OK)      s_st_mid_ok[m]++;
            if (f & SAMPLE_FULL)        s_st_full[m]++;
            if (f & SAMPLE_OUTPUTS_OFF) s_st_off[m]++;
        }
    }
    for (int m = 0; m < 2; m++)
        for (int k = 0; k < 3; k++) s_prev_cmp[m][k] = cmp[m][k];

    // Statistics.
    int32_t start_err = t_lo - (target - P);
    s_st_periods++;
    if (late) s_st_late++;
    s_st_entry_sum += entry;
    if (entry < s_st_entry_min) s_st_entry_min = entry;
    if (entry > s_st_entry_max) s_st_entry_max = entry;
    s_st_start_sum += start_err;
    if (start_err > s_st_start_max) s_st_start_max = start_err;

    // Re-arm for the next period: PRE_TICKS before the next target, computed from where the PWM
    // counter is now, so the schedule follows the MCPWM timer exactly.
    uint64_t g     = timer_group_get_counter_value_in_isr(GPT_GROUP, GPT_IDX);
    int32_t  now   = pwmPosition();
    int32_t  delta = (target - PRE_TICKS) - now;
    while (delta <= 2 * PRE_TICKS) delta += 2 * P;   // never re-fire within this period
    timer_group_set_alarm_value_in_isr(GPT_GROUP, GPT_IDX, g + (uint64_t)(delta / TICKS_PER_GPT));
    timer_group_enable_alarm_in_isr(GPT_GROUP, GPT_IDX);

    uint32_t dur = ccount() - c0;
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
}

void samplerResetStats() {
    // The interrupt runs on this core and can preempt this task, so mask it while resetting.
    // (Clearing s_running instead would stop the sampler: a skipped interrupt does not re-arm.)
    portDISABLE_INTERRUPTS();
    s_st_periods = s_st_late = 0;
    s_st_isr_sum = s_st_isr_max = 0;
    s_st_entry_min = INT32_MAX; s_st_entry_max = INT32_MIN; s_st_entry_sum = 0;
    s_st_start_sum = 0; s_st_start_max = INT32_MIN;
    for (int m = 0; m < 2; m++) {
        s_st_lo_ok[m] = s_st_mid_ok[m] = s_st_full[m] = s_st_off[m] = 0;
        s_ring[m].dropped = 0;
    }
    portENABLE_INTERRUPTS();
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

float samplerZeroVolts(int motor, int phase)    { return s_zero_v[motor & 1][phase % 3]; }
float samplerZeroNoiseLsb(int motor, int phase) { return s_zero_sd[motor & 1][phase % 3]; }

// Align all six MCPWM timers (both units) to count from zero together.  Both units run from the
// same clock with the same prescaler and period, so this holds from then on.  It makes the
// counters jump once, so only call it with every motor output off.
static void alignTimers(void (*say)(const char*)) {
    volatile mcpwm_dev_t* units[2] = { &MCPWM0, &MCPWM1 };
    for (int u = 0; u < 2; u++) {
        for (int t = 0; t < 3; t++) {
            units[u]->timer[t].timer_sync.timer_phase           = 0;
            units[u]->timer[t].timer_sync.timer_phase_direction = 0;   // count up after the sync
            units[u]->timer[t].timer_sync.timer_synci_en        = 1;
        }
    }
    portDISABLE_INTERRUPTS();
    for (int u = 0; u < 2; u++)
        for (int t = 0; t < 3; t++)
            units[u]->timer[t].timer_sync.timer_sync_sw = ~units[u]->timer[t].timer_sync.timer_sync_sw;
    portENABLE_INTERRUPTS();
    delayMicroseconds(200);

    // Measure: the six positions read back to back, many times.  Reads take ~6 APB accesses, so a
    // perfect alignment still shows a few ticks of spread; the read-order skew is measured too by
    // reading unit 0 timer 0 at both ends.
    int32_t worst = 0, skew = 0;
    for (int k = 0; k < 200; k++) {
        uint32_t v[7];
        portDISABLE_INTERRUPTS();
        v[0] = units[0]->timer[0].timer_status.val;
        v[1] = units[0]->timer[1].timer_status.val;
        v[2] = units[0]->timer[2].timer_status.val;
        v[3] = units[1]->timer[0].timer_status.val;
        v[4] = units[1]->timer[1].timer_status.val;
        v[5] = units[1]->timer[2].timer_status.val;
        v[6] = units[0]->timer[0].timer_status.val;
        portENABLE_INTERRUPTS();
        int32_t p[7];
        for (int i = 0; i < 7; i++) {
            int32_t val = (int32_t)(v[i] & 0xFFFF);
            p[i] = (v[i] & (1u << 16)) ? 2 * s_P - val : val;
        }
        int32_t lo = p[0], hi = p[0];
        bool wrapped = false;
        for (int i = 0; i < 7; i++) {
            if (abs(p[i] - p[0]) > s_P) wrapped = true;
            lo = min(lo, p[i]); hi = max(hi, p[i]);
        }
        if (wrapped) continue;   // read straddled the period wrap
        worst = max(worst, hi - lo);
        skew  = max(skew, p[6] - p[0]);
    }
    s_align_spread_us = worst * TICK_US;
    s_aligned         = true;
    char line[96];
    snprintf(line, sizeof(line), "SYNC timers aligned: 6-counter spread %.3f us (read skew %.3f us)",
             s_align_spread_us, skew * TICK_US);
    say(line);
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
    if (err == ESP_OK && !s_isr_handle)
        err = timer_isr_register(GPT_GROUP, GPT_IDX, samplerIsr, nullptr, ESP_INTR_FLAG_IRAM, &s_isr_handle);
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

void samplerEnd() {
    s_running = false;
    timer_pause(GPT_GROUP, GPT_IDX);
    timer_disable_intr(GPT_GROUP, GPT_IDX);
    // The interrupt runs on this core, so it is not mid-flight while this task runs.
}

int samplerRead(int motor, CurrentSample* out, int max) {
    Ring&    q     = s_ring[motor & 1];
    uint32_t head  = q.head;
    int      n     = 0;
    const float to_v = 3.3f / 4095.0f;
    while (q.tail != head && n < max) {
        const RawSample& r = q.buf[q.tail % RING_SIZE];
        CurrentSample&   s = out[n++];
        s.seq    = r.seq;
        s.flags  = r.flags;
        s.ph_lo  = r.ph_lo;
        s.ph_mid = r.ph_mid;
        for (int k = 0; k < 3; k++) {
            s.duty[k] = (float)r.cmp[k] / (float)s_P;
            s.i[k]    = 0.0f;
        }
        float i_lo  = ((r.raw_lo == 0xFFFF ? 0 : r.raw_lo) * to_v - s_zero_v[motor][r.ph_lo]) / CSA_GAIN_V_PER_A;
        float i_mid = ((r.raw_mid == 0xFFFF ? 0 : r.raw_mid) * to_v - s_zero_v[motor][r.ph_mid]) / CSA_GAIN_V_PER_A;
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
