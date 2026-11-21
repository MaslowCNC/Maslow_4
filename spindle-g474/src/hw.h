#pragma once
// Peripheral bring-up for the spindle drive: TIM1/TIM8 complementary PWM, OPAMP PGAs, ADC
// injected sampling, COMP + DAC hardware over-current break, slow analog (VBUS, NTCs), fan PWM,
// beam/LED GPIO and the independent watchdog.  The control maths lives in foc.cpp.

#include <Arduino.h>

namespace hw {
    // Configure everything with both bridges OFF (MOE = 0, all gate inputs driven low).  Returns
    // false if a peripheral failed to start; the reason goes to *why.
    bool init(const char** why);

    // Start the PWM timers (outputs still off) and the sampling interrupt.
    bool start(const char** why);

    // Outputs.  enableOutputs sets MOE (the gate drivers follow the duty registers); it fails
    // while a break is latched.  disableOutputs clears MOE: all six FETs of that bridge turn off.
    bool enableOutputs(int motor);
    void disableOutputs(int motor);
    bool outputsEnabled(int motor);
    void writeDuty(int motor, uint16_t a, uint16_t b, uint16_t c);

    // Hardware over-current: the comparator tripped the timer's break input.  Latched until
    // clearBreak (which only succeeds once the comparator output is low again).
    bool breakLatched(int motor);
    bool clearBreak(int motor);
    uint8_t comparatorOutputs();   // bit per phase: m1 a,b,c = bits 0-2, m2 = bits 3-5

    // Set the comparator threshold (phase amps in the low-side conduction direction), both motors or
    // one (DAC3 feeds motor 1's three comparators, DAC4 motor 2's).
    void setHardwareTripAmps(float amps);
    void setHardwareTripAmps(int motor, float amps);
    float hardwareTripAmps(int motor);
    // Bring-up: the timers' break input filter (TIMx_BDTR.BKF, 0-15; 9 = fDTS/8 x 8 = 0.38 us at boot).
    void setBreakFilter(uint32_t bkf);
    uint32_t breakFilter();
    // Bring-up (#BRK): how many of `reads` reads found each comparator's output high (m1 a,b,c, m2 a,b,c),
    // a timer's status register (BIF = break latched), and the comparator threshold in amps.
    void     comparatorHighCounts(uint16_t counts[6], int reads);
    uint32_t timerStatus(int motor);
    float    hardwareTripAmps();

    // Raw injected samples of the last PWM period, motor m, phase p.
    uint16_t rawCurrent(int m, int p);

    // Slow analog, round robin, call from the main loop.  Values are filtered.
    void serviceSlowAnalog();
    void slowKickFromIsr();   // ADC interrupt: start a slow conversion the main loop has set up
    void setSlowMode(int mode);   // bring-up A/B: 0 free-running, 1 after the current samples (default), 2 off
    float vbus();          // V
    float temperature(int n);  // deg C, NTC 0 or 1; NaN if open/shorted
    float slowPinVolts(int i);  // filtered pin voltage: 0 = VBUS divider, 1 = NTC1, 2 = NTC2 (bring-up)

    void setFanDuty(uint8_t duty);   // 0..255 on VAC_CONTROL

    void watchdogStart();
    void watchdogKick();

    // Where the timer update event lands; written at start() for the boot log.
    extern const char* g_update_phase;
}
