#pragma once

#include "motor_controller.h"

// Load sensing: an OPT-IN DIAGNOSTIC that estimates how hard each open-loop motor is working, to
// see whether a stall (pull-out) can be predicted.  It takes NO protective action.
//
// Everything runs on the core-0 housekeeping task; the core-1 FOC task is only READ from (the
// commanded duty cycles and shaft angle it already writes).
//
// Method (duty-weighted fit of the existing current reads - no extra ADC reads):
//  1. The MP6541A's sense output for a phase is only valid while that phase's LOW-SIDE FET
//     conducts; otherwise it reads ~0.  analogRead takes ~65 us (more than a 50 us PWM period),
//     so reads cannot be gated into the low-side window.  Instead updateCurrent() tags each of
//     its reads with the commanded electrical angle and that phase's PWM duty.  The reads fall
//     at effectively random points in the PWM period, so a read's expected value is
//     i * g, where g = the low-side fraction (1 - duty, less dead time and settling).
//  2. Least-squares fit per motor over all reads of all three phases (exponential forgetting,
//     LOAD_FILTER_TAU_S):  E[read_k] = g * ( Ip * (-sin(th - phi_k)) + Iq * cos(th - phi_k) ),
//     where -sin(th - phi_k) is the shape of the commanded phase-k voltage.  So Ip is the current
//     in phase with the applied voltage and Iq the part lagging it (amplitudes, A peak).  Fitting
//     reads individually copes with the phases being read at different instants and with uneven
//     angle coverage.  Each read is full-or-zero at random, so the estimate is noisy per read and
//     is averaged over ~150 ms.
//
// Outputs per motor: Ip, Iq, the current's lag behind the voltage, and - against a no-load
// baseline recorded with LOADREF - the extra in-phase current (dIp), which rises with load torque.
// A true load angle (rotor lag; pull-out near 90 deg) also needs the phase resistance and
// inductance: R is measured by RTEST (standstill, DC, so I = V/R); L must be supplied in config.h.

struct LoadEstimate {
    bool  valid    = false;
    float ip       = NAN;  // in-phase current amplitude (A peak)
    float iq       = NAN;  // lagging current amplitude (A peak), corrected for rotation direction
    float lag_deg  = NAN;  // current lag behind the applied voltage
    float dip      = NAN;  // ip minus the no-load baseline at this speed (NaN without a baseline)
    float delta_deg= NAN;  // estimated load angle (NaN unless R and L are known)
    float n_eff    = 0.0f; // effective number of reads in the fit window
};

void loadSenseInit();  // call once from setup(): loads the no-load baseline from NVS

// USB console commands
void loadSenseToggle();                                         // LOAD
void loadSenseStartRTest(MotorController& mc1, MotorController& mc2);     // RTEST
void loadSenseStartBaseline(MotorController& mc1, MotorController& mc2);  // LOADREF
void loadSenseAbort(const char* why);                           // e.g. emergency stop

// True while LOAD sensing is on (housekeeping then dithers the current reads).
bool loadSenseOn();

// True while RTEST or LOADREF is driving the motors (link motion commands must be ignored).
bool loadSenseBusy();

// Call every housekeeping pass, after MotorController::updateCurrent().
void loadSenseUpdate(MotorController& mc1, MotorController& mc2, float dt);

const LoadEstimate& loadEstimate(int motor_idx);
