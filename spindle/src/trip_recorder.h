#pragma once

#include <Arduino.h>

// Trip recorder: a RAM ring buffer of the last ~2 s of motor state, sampled every housekeeping
// pass (~2 ms).  When a fault stops the motors it records a short post-trigger tail, freezes, and
// streams the whole window as CSV to the USB console and to the XY board (which saves it to its
// flash as /spindle_trip.csv) so the moments leading up to the trip can be
// examined - in particular whether the measured current rose before the trip (a real stall) or
// jumped out of nowhere (a bad reading), and whether the FOC loop stalled just before it.
//
// USB console commands:  TRIP  - print the frozen recording again
//                        REARM - discard it and start recording again
// The recorder also re-arms itself whenever the spindle is started (link S<rpm> or a USB digit key).

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
#ifdef SYNC_CURRENT_SENSE
    // Shadow observer (sync_sense.h) and the synchronised samples behind it, for this pass.
    // Kept small: 1000 samples x 2 motors.  INT16_MIN / UINT16_MAX mean "no value".
    int16_t  load_deg;     // filtered load angle (rotor lag behind the commanded angle), whole degrees
    int16_t  load_max;     // largest unfiltered load angle this pass, whole degrees
    int16_t  load_r;       // the same two, resistance-corrected (against V - R*I: pull-out near 90)
    int16_t  load_r_max;
    uint16_t obs_rpm;      // observer's own speed estimate
    uint8_t  obs_ok;       // 1 = the observer's estimate is valid (see ObserverEstimate::valid)
    uint8_t  slips;        // pole slips the observer counted since the previous sample
    uint8_t  smp_n, smp_full, smp_lo, smp_off;   // samples this pass: all / all phases valid /
                                                 // lowest-duty phase valid / outputs off (255 = 255+)
#endif
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

// Re-arm for a fresh spindle start.  If a recording is still being printed, re-arming waits until
// the print has finished, so restarting right after a trip cannot cut the dump short.
void tripRecorderArmOnStart();

// Print the frozen recording again (no-op if nothing has been captured).
void tripRecorderRequestDump();

// Send the frozen recording to the XY board over the link again (link command "T").  Replies
// MSG:/WARN: on the link if there is nothing to send.
void tripRecorderRequestLinkDump();

// Link test (link command "T!"): re-arm, record for ~2 s, then trigger, so the whole path to the
// XY board's /spindle_trip.csv can be checked without a fault.  Refused (WARN: on the link) while
// a real trip recording is held, so a test can never discard one.
void tripRecorderLinkTest();

// Stream pending dump lines to USB and the link without blocking.  Call once per housekeeping pass.
void tripRecorderService();
