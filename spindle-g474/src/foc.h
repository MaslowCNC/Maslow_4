#pragma once
// Field-oriented control of the two spindle motors, run entirely in the ADC interrupt at the PWM
// rate (20 kHz), so nothing the main loop does (link traffic, formatting, flash) can stall the
// commutation.  The ESC1 bench found that ANY loop stall at speed is a real current surge.
//
// Drive modes (both motors together, as on the ESP32 board):
//   Off     - MOE clear, all six FETs of each bridge off, the motors coast.
//   Open    - "I/f": a current vector of fixed size at the commanded angle.  Used for standstill
//             Z holds and moves, for spin-up below the hand-over speed and for spin-down.
//   Closed  - sensorless FOC on the MXLEMMING flux observer's angle (ported from the ESC1 bench):
//             dq current loops, a speed loop per motor, and a Z loop that keeps the SUM of the
//             two rotors' angles where the phase offset says (the Z is their relative angle).
//
// Angles: each motor has its own electrical frame; motor i's commanded angle in open loop is
//   theta_i = dir_i * alpha + phi + c_i
// alpha = the integrated spin angle, phi = the phase offset (Z), c_i = a constant set at each
// hand-back.  The motors turn in opposite senses (dir = +-1), so phi adds to both and
// theta_1 + theta_2 = 2 phi + const: exactly the ESP32 firmware's convention.

#include <Arduino.h>

namespace foc {
    enum class Mode : uint8_t { Off, Open, Closed };

    struct MotorTelemetry {
        float ia, ib, ic;        // A, last sample (after the two-shunt rebuild)
        float id, iq;            // A, in the control frame
        float vd, vq;            // V
        float obs_rpm;           // observer speed, signed, motor frame
        float obs_flux;          // |flux| / nominal (1.0 = locked on the right flux)
        float imax;              // largest |phase current| since the last read (A)
        float i2_sum;            // sum of (ia^2+ib^2+ic^2)/1.5 since the last read
        uint32_t n;              // samples in i2_sum
    };

    void init();          // after hw::start
    bool calibrateOffsets(uint32_t samples, const char** why);   // outputs off, motors still

    // Commands from the main loop.  All are safe to call at any time.
    bool enable();                    // precharge then Open at the current angles; false if refused
    void disable();                   // coast now
    bool enabled();
    Mode mode();
    void setSpeedTarget(float rpm);   // >= 0, magnitude; the motors' senses are fixed
    float speedCommandRpm();          // ramped command
    float speedTargetRpm();
    void setPhaseTarget(float rad);
    float phaseCurrent();             // phi now (rad)
    float phaseTarget();
    void zeroPhase();                 // phi = target = 0 and the open-loop angles keep their place
    void setZMoving(bool moving);     // open-loop current boost while the Z moves
    void requestClosedLoop();
    void requestOpenLoop();
    bool observersTracking(float tol);   // both observers within tol of the command
    float zErrorRad();                   // closed loop: angle-sum error (rad); 0 otherwise
    float maxSpeedErrorFraction();       // closed loop: worst |obs - ref| / command

    // Faults the interrupt detected (it has already turned the outputs off).  Read-and-clear.
    enum Fault : uint8_t { FaultNone = 0, FaultOverCurrent = 1, FaultBreak = 2 };
    uint8_t takeFault(char* detail, size_t n);

    // Snapshot and reset the per-interval telemetry of motor m.
    MotorTelemetry take(int m);
    // Interrupt timing: longest ISR (us) and count since the last call.
    void takeIsrStats(uint32_t* max_us, uint32_t* count);

    // Run-time tuning (the '#' link commands).
    struct Tuning {
        float cur_kp, cur_ki;
        float vel_kp, vel_ki;
        float z_kp;
        float hold_a, spin_a, boost_a;
        float iq_limit;
        float r, l, flux;
        float sw_trip_a;
        bool  closed_loop_allowed;
    };
    Tuning& tuning();
    void setVbus(float v);

    // Bring-up: hold motor m's current vector at electrical angle 0 (phase A axis) with `amps`,
    // the other motor off; returns false if refused.  Stop with disable().
    bool startAlignTest(float amps);
}
