#pragma once

// Bench test for synchronised low-side current sampling (sensorless investigation, 2026-10-20).
// Only built into the adc-timing-ota environment (-D ADC_TIMING_TEST); the normal build has none
// of it.
//
// It answers one question: can the ESP32-S3's SAR ADCs, read from an interrupt on the PWM timer's
// peak (the centre of the window in which all three low-side FETs conduct, so the MP6541A's SOx
// outputs show true phase current), sample two phases per motor inside that window?  It measures
// interrupt latency, conversion time for single, sequential (same ADC) and parallel (ADC1 + ADC2)
// reads, the zero-current noise of each sense input, and the phase offset between the two MCPWM
// units.  It runs once from setup() with the drivers asleep, before any motor task exists.

#ifdef ADC_TIMING_TEST
#ifdef SYNC_CURRENT_SENSE
#error "ADC_TIMING_TEST and SYNC_CURRENT_SENSE both drive the SAR ADCs from interrupts; build one or the other"
#endif

// Run the test.  Call after both drivers are initialised (MCPWM timers running, outputs off) and
// before the housekeeping and FOC tasks start.  emit() receives each result line.
void adcTimingTestRun(void (*emit)(const char* line));

// Send stored result line `index` again (the XY board may still have been handshaking the first
// time).  *sent is 0 once index is past the last line.
void adcTimingTestRepeatLine(int index, void (*emit)(const char* line), int* sent);

// Set when analogRead() disagreed with its pre-test values after the test: the over-current
// protection's reads can't be trusted, so MotorController::enable() refuses to run the motors.
extern volatile bool g_adc_test_lockout;

#endif
