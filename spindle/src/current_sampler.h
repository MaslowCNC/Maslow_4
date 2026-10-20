#pragma once

#include <Arduino.h>

// PWM-synchronised phase-current sampling (build flag SYNC_CURRENT_SENSE).
//
// The MP6541A's SOx outputs only show phase current while that phase's LOW-SIDE FET conducts.
// With centre-aligned PWM all three low sides conduct around the timer peak, so a sample taken
// there - inside each phase's low-side window, after the sense output has settled - reads the
// true instantaneous phase current.  Per motor and per PWM period the sampler reads the two
// phases with the lowest duty (the widest low-side windows) and the third follows from
// ia + ib + ic = 0.
//
// This header is the interface the rest of the firmware uses.  The ESP32-S3 SAR implementation
// is in sar_sync_sampler.cpp; the 36 V board's ADS7038 driver is meant to replace it behind the
// same calls.  Everything here is called from the core-0 housekeeping task.

#ifdef SYNC_CURRENT_SENSE

// One motor, one PWM period.
struct CurrentSample {
    uint32_t seq;          // PWM period number, shared by both motors (gaps = periods not sampled)
    float    i[3];         // phase currents A, B, C (A, sense polarity as read); see flags
    float    duty[3];      // duties in force this period (0..1; see SAMPLE_DUTY_MOVED)
    float    t_lo_us;      // sample instants relative to the PWM peak (us, negative = before)
    float    t_mid_us;
    float    on_lo_us;     // time since that phase's low-side FET turned on, at the sample instant
    float    on_mid_us;
    float    off_mid_us;   // time left before the middle phase's low side turns off
    uint8_t  ph_lo;        // phase read first: lowest duty (widest window), ~8 us before the peak
    uint8_t  ph_mid;       // phase read second: middle duty, at the peak
    uint8_t  flags;
};

// CurrentSample::flags
const uint8_t SAMPLE_LO_OK      = 0x01;  // ph_lo was inside its window: i[ph_lo] is a current
const uint8_t SAMPLE_MID_OK     = 0x02;  // ph_mid was inside its window
const uint8_t SAMPLE_FULL       = 0x04;  // both: all three of i[] are valid (third reconstructed)
const uint8_t SAMPLE_OUTPUTS_OFF= 0x08;  // all duties 0 (outputs off): currents are ~0, not sampled
const uint8_t SAMPLE_LATE       = 0x10;  // the interrupt started more than 1 us after its target
const uint8_t SAMPLE_DUTY_MOVED = 0x20;  // a duty changed since the previous period: windows
                                         // were checked with that change as extra margin

// Tunable timing, all in microseconds.  Defaults come from config.h; USB "SYNCSET" changes them
// until reboot for the settling sweep.
struct SamplerTiming {
    float lead_us;     // first conversion pair starts this long before the peak
    float mid_us;      // second pair starts no earlier than this long after the peak
    float settle_us;   // a sample needs this long after its low side turns on
    float hold_us;     // ...and must start this long before its low side turns off
    float stagger_us;  // ADC2 (motor 2) starts this long after ADC1 (motor 1)
};

// Health and timing statistics since the last samplerResetStats().
struct SamplerStats {
    uint32_t periods;          // interrupts serviced
    uint32_t late;             // first pair started > 1 us after its target
    uint32_t dropped[2];       // samples lost because the housekeeping task had not drained them
    uint32_t lo_ok[2], mid_ok[2], full[2], off[2];
    float    isr_us_avg, isr_us_max;        // interrupt body duration
    float    entry_us_min, entry_us_avg, entry_us_max;  // entry time before the first pair's target
    float    start_us_avg, start_us_max;    // first pair start relative to its target (+ = late)
};

// Start sampling (sets up the timers and the interrupt on the calling core, so call it from the
// core-0 housekeeping task).  First call: also aligns the two MCPWM units' counters (only safe
// with every output off - at boot), and measures the zero-current offsets when the drivers are
// asleep.  Returns false (and logs why) if anything failed; the caller must then keep the motors
// off, because over-current protection depends on these samples.
bool samplerBegin(void (*say)(const char* line));

// Stop sampling and release the ADCs (before WiFi/OTA).  No interrupt runs after this returns.
void samplerEnd();

bool samplerRunning();

// Copy out up to `max` new samples for motor 0/1, oldest first.  Returns the number copied.
int samplerRead(int motor, CurrentSample* out, int max);

// Zero-current output of each sense input, volts (measured at begin, drivers asleep).
float samplerZeroVolts(int motor, int phase);
float samplerZeroNoiseLsb(int motor, int phase);   // standard deviation at the zero measurement

// Measure the zero again (USB "SYNCZERO").  Only with the drivers asleep; false otherwise.
bool samplerRezero();

SamplerTiming samplerTiming();
void          samplerSetTiming(const SamplerTiming& t);
SamplerStats  samplerStats();
void          samplerResetStats();

// Phase spread of the six MCPWM timer counters after alignment (us, max - min), measured at begin.
float samplerAlignSpreadUs();

// PWM period (us) and dead time per edge (us) as configured in the MCPWM hardware.
float samplerPeriodUs();
float samplerDeadTimeUs();

#endif
