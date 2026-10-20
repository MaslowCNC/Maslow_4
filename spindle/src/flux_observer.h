#pragma once

// MXLEMMING flux observer, ported from simplefoc/Arduino-FOC-drivers
// (src/encoders/MXLEMMING_observer, MIT licence, Copyright (c) 2021 Richard Unger), which ported
// it from the MESC firmware.  Observer algorithm by David Molony (MESC FOC project); see the
// note in flux_observer.cpp, which its licence requires to be kept.
//
// Differences from the library version, so it can run without upgrading SimpleFOC (2.3.2 here)
// and without a SimpleFOC CurrentSense/Sensor object:
//   - it takes alpha/beta currents and voltages as arguments instead of reading them from the
//     motor, and the time step from the PWM period count instead of micros();
//   - R, L and the flux linkage are fields set by the caller;
//   - the shaft-angle bookkeeping (full_rotations, angle_track) is replaced by an unwrapped
//     electrical angle, which is what the load-angle and slip checks need.

struct FluxObserver {
    // Motor parameters (phase values, alpha/beta amplitude-invariant frame).
    float R            = 0.0f;   // ohm
    float L            = 0.0f;   // H
    float flux_linkage = 0.0f;   // Wb (V s per electrical rad), = back-EMF amplitude / omega_e

    // State
    float flux_alpha   = 0.0f;
    float flux_beta    = 0.0f;
    float i_alpha_prev = 0.0f;
    float i_beta_prev  = 0.0f;
    float electrical_angle      = 0.0f;   // 0..2pi
    float electrical_angle_prev = 0.0f;
    double angle_unwrapped      = 0.0;    // accumulated electrical angle (rad)
    bool  first = true;

    bool ready() const { return R > 0.0f && L > 0.0f && flux_linkage > 0.0f; }
    void reset();

    // One step: currents (A) and the average applied voltages (V) over the last dt seconds.
    void update(float i_alpha, float i_beta, float v_alpha, float v_beta, float dt);
};
