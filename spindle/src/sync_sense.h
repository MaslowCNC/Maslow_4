#pragma once

#include "motor_controller.h"

// Consumer side of the PWM-synchronised current samples (build flag SYNC_CURRENT_SENSE; the
// sampler itself is current_sampler.h).  Runs on the core-0 housekeeping task, once per pass:
//   - drains each motor's samples and turns them into what updateCurrent() used to provide:
//     the phase-RMS protection current, the last per-phase currents for the trip recorder, and
//     the calibration/telemetry values;
//   - hands the load-sense fit (load_sense.cpp) every valid reading with its exact applied angle;
//   - runs the MXLEMMING flux observer in shadow mode (flux_observer.h): it estimates the rotor's
//     electrical angle from the currents and the applied voltages, and the difference from the
//     commanded angle (the load angle) is logged.  It does NOT drive the motors;
//   - bench diagnostics: [SYNC] log lines, a settling histogram and a raw capture (USB commands
//     SYNC, SYNCLOG, SYNCSET, SETTLE, SYNCCAP, OBS - see syncSenseCommand).

#ifdef SYNC_CURRENT_SENSE

// One reading for the load-sense fit: a phase current (sense polarity corrected) and the
// commanded electrical angle in force when it was sampled, as its cosine and sine.
struct FitPoint {
    float   cos_th, sin_th;
    float   i;
    uint8_t phase;
};

// Shadow-mode observer output for one motor.
struct ObserverEstimate {
    bool  valid       = false;   // parameters set, speed above SYNC_OBS_MIN_RPM, enough samples
    float load_deg    = NAN;     // commanded minus observed electrical angle, in the direction
                                 // of rotation (rotor lag), filtered over ~10 ms
    float load_max    = NAN;     // largest unfiltered value since the last log line
    float load_r_deg  = NAN;     // load_deg measured against V - R*I instead of V: the classical
                                 // load angle (pull-out near 90), free of the R*Id drop
    float speed_rpm   = NAN;     // observer's own speed estimate
    float flux_ratio  = NAN;     // |flux| / flux_linkage (1.0 = clamped: parameters are off)
    uint32_t slips    = 0;       // pole slips seen since boot
    // The last syncSenseUpdate() pass, for the trip recorder:
    float    pass_load_max = NAN;  // largest unfiltered load angle (deg); NaN if none was computed
    float    pass_load_max2 = NAN; // second-largest (deg): one noisy outlier does not move it
    float    pass_load_r_max = NAN;  // the same for the resistance-corrected angle
    uint16_t pass_n    = 0;        // samples read
    uint16_t pass_full = 0;        // ... with all three phases valid
    uint16_t pass_lo   = 0;        // ... with the lowest-duty phase valid
    uint16_t pass_off  = 0;        // ... taken with the outputs off (driver asleep or in fault)
};

// Line sinks: USB console and the inter-board link (MSG: lines in the XY board's log).
void syncSenseInit(void (*usb)(const char* line), void (*link)(const char* line));

// Start the sampler (call at the top of the housekeeping task, drivers asleep).  False: sensing
// failed and the motors must stay off.
bool syncSenseStart();

// Stop before OTA; restart with syncSenseStart().
void syncSenseStop();

// Drain this pass's samples for both motors and update their current fields.  Call where
// updateCurrent() used to be called.
void syncSenseUpdate(MotorController& mc1, MotorController& mc2);

// One line to the USB console and the link (an MSG line in the XY board's log).
void syncSenseSay(const char* line);

// The fit readings gathered by the last syncSenseUpdate() for motor 0/1.
const FitPoint* syncFitPoints(int motor, int* count);

const ObserverEstimate& syncObserver(int motor);

// Over-current protection must not run blind: true once a motor that is enabled has had no
// valid sample for SYNC_STALE_MS.
bool syncSenseStale(const MotorController& mc, int motor);

// Send what the last samples looked like to the link (after a stale-sensing stop).
void syncSenseDescribe(int motor);

// Periodic output (log lines, capture dump).  Call once per pass.
void syncSenseService(MotorController& mc1, MotorController& mc2);

// USB console word commands; true if `line` was one of them.
bool syncSenseCommand(const char* line);

#endif
