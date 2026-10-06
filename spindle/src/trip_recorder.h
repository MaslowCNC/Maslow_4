#pragma once

#include <Arduino.h>

// Trip recorder: a RAM ring buffer of the last ~2 s of motor state, sampled every housekeeping
// pass (~2 ms).  When a fault stops the motors it records a short post-trigger tail, freezes, and
// streams the whole window to the USB console as CSV so the moments leading up to the trip can be
// examined - in particular whether the measured current rose before the trip (a real stall) or
// jumped out of nowhere (a bad reading), and whether the FOC loop stalled just before it.
//
// USB console commands:  TRIP  - print the frozen recording again
//                        REARM - discard it and start recording again
// The recorder also re-arms itself on the next non-zero spindle speed command.

struct TripMotorSample {
    float   rpm;          // commanded (open-loop) speed, signed
    float   vlim;         // applied voltage limit (V)
    float   ia, ib, ic;   // instantaneous per-phase current from the ADC (A)
    float   prot;         // protection_current: the filtered value the 6 A trip compares
    uint8_t enabled;
    uint8_t nf_edges;     // nFAULT falling edges since the previous sample (driver OCP retries)
    uint8_t nf_low;       // nFAULT pin level at sample time (1 = low = fault asserted)
    float   ip, iq;       // load-sense fit: in-phase / lagging current (A peak); NaN unless LOAD is on
    float   lag;          // load-sense fit: current lag behind the applied voltage (deg)
};

struct TripSample {
    uint32_t        t_ms;
    uint16_t        foc_loops;   // FOC iterations completed since the previous sample (0 = core 1 stalled)
    uint16_t        foc_max_us;  // longest single FOC iteration since the previous sample
    float           phase_cur;   // phase offset (Z), rad
    float           phase_tgt;
    TripMotorSample m[2];
};

// Record one sample.  Ignored once the recording is frozen.
void tripRecorderPush(const TripSample& s);

// A fault stopped the motors: capture a short tail, then freeze and dump.  Only the first
// trigger after arming is kept, so a cascade of follow-on faults cannot overwrite it.
void tripRecorderTrigger(const char* reason);

// Discard any frozen recording and start recording again.
void tripRecorderArm();

// Print the frozen recording again (no-op if nothing has been captured).
void tripRecorderRequestDump();

// Stream pending dump lines to USB without blocking.  Call once per housekeeping pass.
void tripRecorderService();
