#ifdef ADC_TIMING_TEST

#include <Arduino.h>
#include <driver/mcpwm.h>
#include <soc/mcpwm_struct.h>
#include <soc/sens_reg.h>
#include <esp_intr_alloc.h>
#include "adc_timing_test.h"
#include "pins.h"

volatile bool g_adc_test_lockout = false;

// --- Raw SAR reads (RTC controller, the same registers IDF's adc1/adc2_get_raw use) ---
// Written here rather than taken from SimpleFOC: its ESP32-S3 adcRead() subtracts 10 from an ADC2
// channel and then picks the result register with "channel > 7", so GPIO15-17 (ADC2 ch 4-6) return
// ADC1's result.  The attenuation, power and controller set-up are left as analogRead() left them.
//
// S3 pads: GPIO1-10 = ADC1 ch 0-9, GPIO11-20 = ADC2 ch 0-9.
static const uint32_t CONV_TIMEOUT_CYCLES = 240 * 100;  // 100 us at 240 MHz: the ADC is wedged

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

// Sense inputs: index 0-2 = motor 1 A/B/C (ADC1 ch 4-6), 3-5 = motor 2 A/B/C (ADC2 ch 4-6).
static const int SENSE_PINS[6] = { CURA, CURB, CURC, CURA2, CURB2, CURC2 };
static const int ADC1_CH[3]    = { CURA - 1, CURB - 1, CURC - 1 };
static const int ADC2_CH[3]    = { CURA2 - 11, CURB2 - 11, CURC2 - 11 };

// --- What the interrupt does on each PWM peak ---
enum Mode : int {
    MODE_EMPTY = 0,     // latency and interrupt overhead only
    MODE_ADC1_ONE,      // M1 A
    MODE_ADC2_ONE,      // M2 A
    MODE_ADC1_TWO,      // M1 A then B, same ADC (sequential)
    MODE_PAIR_ONE,      // M1 A + M2 A together (ADC1 and ADC2 in parallel)
    MODE_PAIR_TWO,      // two parallel pairs: A1+A2 then B1+B2 - the four currents an observer needs
    MODE_PAIR_THREE,    // three parallel pairs: all six sense inputs
    MODE_COUNT
};
static const char* const MODE_NAMES[MODE_COUNT] = {
    "empty isr", "1x ADC1", "1x ADC2", "2x ADC1 seq", "ADC1+ADC2", "2 pairs", "3 pairs"
};

struct Stats {
    uint32_t n;
    uint32_t lat_min, lat_max, lat_sum;   // MCPWM ticks from the peak to the first instruction
    uint32_t dur_min, dur_max, dur_sum;   // CPU cycles spent reading
    uint32_t wrong_dir;                   // entries where the timer was not counting down
};

static volatile int      s_mode      = MODE_EMPTY;
static volatile bool     s_running   = false;
static volatile uint32_t s_target_n  = 0;
static uint32_t          s_period    = 0;   // timer_period (ticks); the counter runs 0..P..0
static Stats             s_stats;
static uint32_t          s_sum[6], s_min[6], s_max[6], s_cnt[6];
static uint64_t          s_sumsq[6];
static intr_handle_t     s_isr_handle = nullptr;

static inline void IRAM_ATTR noteValue(int i, uint16_t v) {
    if (v == 0xFFFF) return;
    s_sum[i] += v;
    s_sumsq[i] += (uint64_t)v * v;
    if (v < s_min[i]) s_min[i] = v;
    if (v > s_max[i]) s_max[i] = v;
    s_cnt[i]++;
}

static void IRAM_ATTR peakIsr(void*) {
    bool peak = MCPWM0.int_st.timer0_tep_int_st;
    MCPWM0.int_clr.val = MCPWM0.int_st.val;
    if (!s_running || !peak) return;

    // At the peak the counter equals P and turns to count down, so P - value is the time since.
    uint32_t value = MCPWM0.timer[0].timer_status.timer_value;
    bool     down  = MCPWM0.timer[0].timer_status.timer_direction;
    uint32_t c0    = ccount();

    switch (s_mode) {
        case MODE_ADC1_ONE: adc1Start(ADC1_CH[0]); noteValue(0, adc1End()); break;
        case MODE_ADC2_ONE: adc2Start(ADC2_CH[0]); noteValue(3, adc2End()); break;
        case MODE_ADC1_TWO:
            adc1Start(ADC1_CH[0]); noteValue(0, adc1End());
            adc1Start(ADC1_CH[1]); noteValue(1, adc1End());
            break;
        case MODE_PAIR_ONE:
        case MODE_PAIR_TWO:
        case MODE_PAIR_THREE: {
            int pairs = s_mode - MODE_PAIR_ONE + 1;
            for (int k = 0; k < pairs; k++) {
                adc1Start(ADC1_CH[k]);
                adc2Start(ADC2_CH[k]);
                noteValue(k, adc1End());
                noteValue(k + 3, adc2End());
            }
            break;
        }
        default: break;
    }

    uint32_t dur = ccount() - c0;
    uint32_t lat = down ? (s_period - value) : value;   // "up" here would mean the event came very late
    if (!down) s_stats.wrong_dir++;
    if (lat < s_stats.lat_min) s_stats.lat_min = lat;
    if (lat > s_stats.lat_max) s_stats.lat_max = lat;
    s_stats.lat_sum += lat;
    if (dur < s_stats.dur_min) s_stats.dur_min = dur;
    if (dur > s_stats.dur_max) s_stats.dur_max = dur;
    s_stats.dur_sum += dur;
    if (++s_stats.n >= s_target_n) s_running = false;
}

// --- Result lines: kept so they can be sent again ---
static const int LINE_LEN  = 96;
static const int MAX_LINES = 20;
static char      s_lines[MAX_LINES][LINE_LEN];
static int       s_line_count = 0;

static void addLine(void (*emit)(const char*), const char* fmt, ...) {
    char buf[LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (s_line_count < MAX_LINES) strcpy(s_lines[s_line_count++], buf);
    emit(buf);
    delay(20);   // let the 115200-baud link drain between lines
}

static void resetStats() {
    s_stats = Stats{ 0, UINT32_MAX, 0, 0, UINT32_MAX, 0, 0, 0 };
    for (int i = 0; i < 6; i++) {
        s_sum[i] = 0; s_sumsq[i] = 0; s_cnt[i] = 0; s_min[i] = UINT32_MAX; s_max[i] = 0;
    }
}

static void readAllAnalog(int out[6]) {
    for (int i = 0; i < 6; i++) {
        uint32_t s = 0;
        for (int k = 0; k < 16; k++) s += analogRead(SENSE_PINS[i]);
        out[i] = (int)(s / 16);
    }
}

void adcTimingTestRun(void (*emit)(const char*)) {
    s_line_count = 0;
    const float cpu_mhz = (float)getCpuFrequencyMhz();

    // MCPWM tick: 160 MHz / (clk_prescale + 1) / (timer_prescale + 1).  Full PWM period = 2P ticks.
    s_period = MCPWM0.timer[0].timer_cfg0.timer_period;
    float tick_ns = 1000.0f * (MCPWM0.clk_cfg.clk_prescale + 1) * (MCPWM0.timer[0].timer_cfg0.timer_prescale + 1) / 160.0f;
    addLine(emit, "ADCT start: P=%u ticks, tick %.2f ns, PWM period %.2f us, CPU %.0f MHz",
            (unsigned)s_period, tick_ns, 2.0f * s_period * tick_ns / 1000.0f, cpu_mhz);

    // Phase of unit 1's timer 0 against unit 0's, as a position in the 0..2P triangle.
    {
        int32_t dmin = INT32_MAX, dmax = INT32_MIN;
        int64_t dsum = 0;
        const int N = 1000;
        for (int k = 0; k < N; k++) {
            noInterrupts();
            uint32_t v0 = MCPWM0.timer[0].timer_status.val;
            uint32_t v1 = MCPWM1.timer[0].timer_status.val;
            interrupts();
            int32_t p0 = (v0 & 0xFFFF), p1 = (v1 & 0xFFFF);
            if (v0 & (1 << 16)) p0 = 2 * (int32_t)s_period - p0;   // counting down
            if (v1 & (1 << 16)) p1 = 2 * (int32_t)s_period - p1;
            int32_t d = p1 - p0;
            while (d > (int32_t)s_period) d -= 2 * s_period;
            while (d < -(int32_t)s_period) d += 2 * s_period;
            dmin = min(dmin, d); dmax = max(dmax, d); dsum += d;
        }
        addLine(emit, "ADCT unit1-unit0 timer phase: avg %.2f us (min %.2f, max %.2f)",
                (float)dsum / N * tick_ns / 1000.0f, dmin * tick_ns / 1000.0f, dmax * tick_ns / 1000.0f);
    }

    int before[6];
    readAllAnalog(before);
    addLine(emit, "ADCT analogRead before: M1 %d %d %d  M2 %d %d %d",
            before[0], before[1], before[2], before[3], before[4], before[5]);

    // Interrupt on unit 0, timer 0's peak.  IRAM so flash activity can't delay it.
    MCPWM0.int_clr.val = UINT32_MAX;
    esp_err_t err = mcpwm_isr_register(MCPWM_UNIT_0, peakIsr, nullptr, ESP_INTR_FLAG_IRAM, &s_isr_handle);
    if (err != ESP_OK) {
        addLine(emit, "ADCT FAILED: mcpwm_isr_register error %d", (int)err);
        return;
    }
    MCPWM0.int_ena.timer0_tep_int_ena = 1;

    const uint32_t N_PER_MODE = 10000;   // 0.5 s at 20 kHz
    for (int m = 0; m < MODE_COUNT; m++) {
        resetStats();
        s_timeouts = 0;
        s_mode     = m;
        s_target_n = N_PER_MODE;
        s_running  = true;
        uint32_t t0 = millis();
        while (s_running && millis() - t0 < 3000) delay(10);
        s_running = false;
        delay(2);

        uint32_t n = s_stats.n ? s_stats.n : 1;
        addLine(emit, "ADCT %-11s n=%u lat %.2f/%.2f/%.2f us, read %.2f/%.2f/%.2f us%s%s",
                MODE_NAMES[m], (unsigned)s_stats.n,
                s_stats.lat_min * tick_ns / 1000.0f, (float)s_stats.lat_sum / n * tick_ns / 1000.0f,
                s_stats.lat_max * tick_ns / 1000.0f,
                s_stats.dur_min / cpu_mhz, (float)s_stats.dur_sum / n / cpu_mhz, s_stats.dur_max / cpu_mhz,
                s_timeouts ? " TIMEOUTS" : "", s_stats.wrong_dir ? " LATE" : "");
        if (s_timeouts || s_stats.wrong_dir) {
            addLine(emit, "ADCT   %s: %u conversion timeouts, %u entries after the timer turned",
                    MODE_NAMES[m], (unsigned)s_timeouts, (unsigned)s_stats.wrong_dir);
        }

        // Zero-current noise from the all-channels pass (drivers asleep: SOx sits at the divider).
        if (m == MODE_PAIR_THREE) {
            char part[2][48];
            for (int mot = 0; mot < 2; mot++) {
                int len = 0;
                for (int ph = 0; ph < 3; ph++) {
                    int i = mot * 3 + ph;
                    float mean = s_cnt[i] ? (float)s_sum[i] / s_cnt[i] : 0.0f;
                    float var  = s_cnt[i] ? (float)((double)s_sumsq[i] / s_cnt[i] - (double)mean * mean) : 0.0f;
                    len += snprintf(part[mot] + len, sizeof(part[mot]) - len, " %.0f/%.1f/%u",
                                    mean, sqrtf(var > 0 ? var : 0), (unsigned)(s_max[i] - s_min[i]));
                }
            }
            addLine(emit, "ADCT noise mean/sd/p-p LSB M1%s", part[0]);
            addLine(emit, "ADCT noise mean/sd/p-p LSB M2%s", part[1]);
        }
    }

    MCPWM0.int_ena.timer0_tep_int_ena = 0;
    esp_intr_free(s_isr_handle);
    s_isr_handle = nullptr;

    // The over-current protection uses analogRead(); make sure the raw reads left it working.
    int after[6];
    readAllAnalog(after);
    int worst = 0;
    for (int i = 0; i < 6; i++) worst = max(worst, abs(after[i] - before[i]));
    addLine(emit, "ADCT analogRead after:  M1 %d %d %d  M2 %d %d %d",
            after[0], after[1], after[2], after[3], after[4], after[5]);
    if (worst > 60) {
        g_adc_test_lockout = true;
        addLine(emit, "ADCT analogRead moved %d LSB after the test: MOTORS LOCKED OUT, power-cycle", worst);
    } else {
        addLine(emit, "ADCT done; analogRead unchanged (max %d LSB). Z not homed: no boot homing", worst);
    }
}

void adcTimingTestRepeatLine(int index, void (*emit)(const char*), int* sent) {
    *sent = 0;
    if (index < 0 || index >= s_line_count) return;
    emit(s_lines[index]);
    *sent = 1;
}

#endif
