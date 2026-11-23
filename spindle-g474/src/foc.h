#pragma once
// Field-oriented control of the two spindle motors, run entirely in the ADC interrupt at the PWM
// rate (20 kHz), so nothing the main loop does (link traffic, formatting, flash) can stall the
// commutation.  The ESC1 bench found that ANY loop stall at speed is a real current surge.
//
// Drive modes (both motors together, as on the ESP32 board):
//   Off     - MOE clear, all six FETs of each bridge off, the motors coast.
//   Open    - "I/f": a current vector of fixed size at the commanded angle, driven as a voltage
//             (R*I + back-EMF feed-forward) with a slow trim onto the current, which keeps the
//             back-EMF damping of the ESP32's voltage drive.  Used for standstill Z holds and
//             moves, for spin-up below the hand-over speed and for spin-down.
//   Closed  - sensorless FOC on the MXLEMMING flux observer's angle (ported from the ESC1 bench):
//             dq current loops and, per motor, a PID that steers the observed rotor angle onto the
//             same reference angle the open loop drives - so the Z (the rotors' relative angle)
//             is held in angle, not just in speed, and survives both hand-overs.
//
// Angles: each motor has its own electrical frame; motor i's reference angle in both modes is
//   ref_i = dir_i * alpha + phi + c_i
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
    float zErrorRad();                   // mean rotor angle error = phase (Z) error, rad (0 when not tracked)
    float maxAngleErrorRad();            // the worse motor's |angle error| against its reference, rad:
                                         // closed loop always, open loop above OBS_TRUST_RPM
    float maxSpeedErrorFraction();       // closed loop: worst |obs - ref| / command

    // Faults the interrupt detected (it has already turned the outputs off).  Read-and-clear.
    enum Fault : uint8_t { FaultNone = 0, FaultOverCurrent = 1, FaultBreak = 2, FaultOverrun = 3 };
    uint8_t takeFault(char* detail, size_t n);
    uint8_t faultCode();   // the latched fault, without clearing it
    void setFakeLoad(uint32_t us);   // bring-up (#LOAD): pad every ADC interrupt to `us` (0 = off)
    uint32_t uartErrorsSeen(int which);   // bring-up: USART2 0 = ORE, 1 = FE, 2 = NE flags the interrupt saw
    uint32_t overruns();   // interrupts that ended with the next sample already waiting, since boot
    // Bring-up (#JMEAS): average each motor's measured iq along its direction of rotation (A).
    void     iqAccStart();
    uint32_t iqAccStop(float mean[2]);   // returns the number of interrupts averaged
    const char* enableFailReason();   // why the last enable() / startAlignTest() returned false
    // Bring-up (#BRKDUMP): the interrupts up to the last fault (i = 0 newest; a comparator break or a software
    // over-current): phase currents
    // (A, no rebuild) and duties in force, M1 a b c then M2 a b c, and the comparator outputs read at the
    // interrupt's start.  False once the drive has been enabled again, or past the recorded entries.
    bool brkDump(int i, float amps[6], float duty[6], uint8_t* cmp);

    // Snapshot and reset the per-interval telemetry of motor m.
    MotorTelemetry take(int m);

    // High-speed bring-up (#STAT, #VLOG): the voltage headroom of motor m.  Read-and-restart; take()
    // (every 2 ms, main loop) leaves these alone.
    struct VoltStats {
        float    vmax;                    // the current loops' voltage limit now (V)
        float    u, u_max;                // voltage demand / limit: this period, and the largest since the last read
        uint32_t sat, n;                  // periods the limit clipped the demand, periods counted
        float    fw_id;                   // field-weakening d current (A, >= 0; #FW)
        uint32_t late;                    // duty writes made after the valley (in time for the next peak's update)
        float    id, iq, vd, vq, obs_rpm; // this period
    };
    VoltStats takeVolt(int m);
    // Interrupt timing: longest ISR (us) and count since the last call.
    void takeIsrStats(uint32_t* max_us, uint32_t* count);

    // Bring-up (#NOISE): raw statistics of the six sense channels over `samples` interrupts.
    struct NoiseStats {
        float    mean, offset, sd;   // counts (offset = the zero from calibrateOffsets)
        uint16_t min, max;
        uint32_t n;
    };
    void startNoise(uint32_t samples);
    bool noiseDone();
    void noiseResult(int m, int phase, NoiseStats* out);
    // The same samples' phase sum per motor (gain-corrected, in the sense chain's amps): a star motor's true sum
    // is zero, so this is the measurement error, also while the motor runs.
    void noisePhaseSum(int m, float* mean_a, float* sd_a, float* min_a, float* max_a);
    // ... and the control signals' ripple over the same samples.  sig: 0 angle error eps (deg), 1 observer speed
    // (RPM), 2 iq_ref, 3 iq, 4 id (A).
    void noiseRipple(int m, int sig, float* mean, float* sd, float* mn, float* mx);
    // Bring-up (#SCOPE): 1024 rows of one motor, a row every 2 x decim interrupts (channels 0-3 in one, 4-7 in the
    // next): flux a, flux b (1e-4 of the flux constant), eps (0.01 deg), observer speed (RPM), iq_ref, iq, id (mA),
    // reference angle (0.01 deg).
    constexpr int SCOPE_CH = 8;
    void    startScope(int motor, int decim);
    bool    scopeDone();
    int16_t scopeValue(int row, int ch);
    // Bring-up (#DUMP): consecutive raw samples of all six channels, counts relative to each zero.
    int  startDump(int samples);   // returns the number it will capture (max 1024)
    bool dumpDone();
    void dumpRow(int i, int out[6]);   // M1 a b c, M2 a b c

    // Run-time tuning (the '#' link commands).
    struct Tuning {
        float cur_kp, cur_ki;
        float pos_kp, pos_kd, pos_ki;   // closed-loop angle PID: A/rad, A/(rad/s), A/(rad s), at full speed
        float pos_bw_min, pos_bw_full_rpm;   // its bandwidth scale at CL_EXIT_RPM, rising to 1 at this speed
        float ol_ki;                    // open-loop current trim integrator, V/(A s)
        float hold_a, spin_a, boost_a;
        float iq_limit;
        float r, l, flux;
        float sw_trip_a;
        float deadtime_ns;              // dead-time compensation (0 = off)
        float dt_knee_a;                // ... reaches its full size at this phase current (#DTC <ns> [knee A])
        bool  closed_loop_allowed;
        float fw_max_a;                 // closed loop: field-weakening d current limit (A, 0 = off; #FW)
        float fw_target;                // ... starts when the voltage demand passes this share of the limit
    };
    Tuning& tuning();
    void setVbus(float v);

    // Bring-up: hold motor m's current vector at electrical angle 0 (phase A axis) with `amps`,
    // the other motor off; returns false if refused.  Stop with disable().
    bool startAlignTest(float amps);
    // #GCAL: turn the running align test's voltage vector (angle 0 = phase A axis).
    void setAlignVector(float volts, float angle_rad);
    // Per-phase current gain corrections (#GCAL / #GAIN), 1 = nominal.  Not kept across a reboot.
    void setPhaseGains(int m, const float g[3]);
    void phaseGains(int m, float g[3]);
}
