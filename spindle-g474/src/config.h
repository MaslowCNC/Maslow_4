#pragma once
// Tunables for the G474 spindle firmware.  Values marked BENCH come from the ESC1 sensorless bench
// (branch ESC1-sensorless-test, spindle-esc1/src/main.cpp) or the ESP32 spindle firmware
// (spindle/src/config.h); values marked GUESS have not been run on any hardware yet.  Most can be
// changed at run time over the link with the '#' maintenance commands (see README.md) and are
// lost at reset.

#include <Arduino.h>
#include <math.h>

// ---- Motor (both spindle motors are the same part) ----
constexpr int   POLE_PAIRS      = 1;
constexpr float PHASE_R_OHM     = 0.32f;     // board 1 #GCAL (0.28-0.35 per axis, FETs + shunts + wiring); ESC1 bench 0.39
constexpr float PHASE_L_H       = 75e-6f;    // BENCH
constexpr float FLUX_WB         = 7.84e-3f;  // BENCH (ESC1, bus-corrected; the ESP32 VSWEEP gave 7.28)

// The two motors always turn in opposite senses; SPINDLE_DIRECTION flips both together.
// Same convention as spindle/src/main.cpp: motor 1 = +1 * SPINDLE_DIRECTION, motor 2 = -1 * it.
// The phase order at CN9/CN11 decides the real sense: check it at bring-up (BRINGUP.md step 6).
constexpr int   SPINDLE_DIRECTION = -1;

// ---- Speeds ----
constexpr float RPM_TO_RAD      = 2.0f * (float)M_PI / 60.0f;
// GUESS: the 36 V bus allows about 22k RPM before the back-EMF (0.82 V/1000 RPM) meets the
// linear modulation limit.  The XY board's SpindleBoard max_rpm caps what is asked for anyway.
constexpr float MAX_COMMAND_RPM = 18000.0f;
constexpr float SPIN_RAMP_RAD_S2      = 500.0f;   // BENCH (ESP32): ~4800 RPM/s ...
constexpr float SPIN_RAMP_HIGH_RAD_S2 = 150.0f;   // ... and ~1400 RPM/s above
constexpr float SPIN_RAMP_SLOW_ABOVE_RPM = 14000.0f;
// Open-loop spin-up to the hand-over is gentler: I/f at 3 A gives ~35 mNm, and in the simulation
// a rotor with 5x the guessed inertia lost sync at 500 rad/s^2.  ~0.8 s to 2000 RPM.
constexpr float SPIN_RAMP_OPEN_RAD_S2 = 250.0f;

// ---- Open loop (I/f): standstill Z holds and moves, spin-up below the closed-loop hand-over ----
// The ESP32 drove a calibrated VOLTAGE; with real current sensing the G474 drives a current
// vector at the commanded angle instead, which needs no per-motor calibration.
constexpr float HOLD_CURRENT_A  = 2.5f;      // GUESS: standing Z hold (ESP32: 1.3 V ~ 2.6 A)
constexpr float SPINUP_CURRENT_A = 3.0f;     // GUESS: open-loop spin-up
constexpr float Z_MOVE_BOOST_A  = 1.0f;      // GUESS: extra while the Z moves (ESP32: +1 V)
constexpr uint32_t Z_MOVE_HOLD_MS = 100;     // BENCH: bridges the gaps between streamed Z targets
constexpr float OL_CURRENT_SLEW_A_S = 50.0f; // open-loop current changes ramp at this rate

// ---- Current loops (BENCH: ESC1, ~400 Hz bandwidth: P = L*wc, I = R*wc) ----
constexpr float CUR_KP          = 0.2f;      // V/A
constexpr float CUR_KI          = 1000.0f;   // V/(A s)
constexpr float MAX_MODULATION  = 0.95f;     // of the linear SVPWM limit Vbus/sqrt(3)
// GUESS: effective dead time to compensate (the DRV8300's 200 ns plus the timer's 100 ns, minus
// FET switching overlap); measure it at bring-up (#DTC) - the ESC1 fit put the loss at ~0.42 V.
constexpr float DEADTIME_COMP_NS = 250.0f;
constexpr float DT_COMP_KNEE_A  = 0.2f;      // ESP32 sync-sense: SYNC_DT_COMP_KNEE_A

// ---- Sensorless closed loop ----
constexpr float CL_ENTER_RPM    = 2000.0f;   // BENCH: the observer is not trusted below this
constexpr float CL_EXIT_RPM     = 1500.0f;   // GUESS: hand back to open loop below this
constexpr float CL_LOCK_TOL     = 0.10f;     // BENCH: observer within 10% of the command ...
constexpr uint32_t CL_LOCK_MS   = 100;       // ... for this long before the hand-over
constexpr uint32_t CL_LOCK_TIMEOUT_MS = 1500;// GUESS: no lock this long after reaching CL_ENTER = fault
constexpr float CL_ID_RAMP_A_S  = 40.0f;     // the open loop's d current fades out at this rate
constexpr float IQ_LIMIT_A      = 8.0f;      // GUESS: closed-loop torque current limit per motor
// Closed loop holds each rotor's ANGLE on its reference (PID, D on the observer speed), which keeps
// the Z.  The ESC1's speed-only loop (P 0.01 A per rad/s, I 0.2 A/rad) let a 15 mNm load step pull
// the rotors ~50 deg apart in the simulation (test/sim).  Chosen in the simulation over rotor
// inertias 3e-6..5e-5 kg m^2 (the real one is unknown) and motor-constant errors: retune on the
// bench (#POS).  These are the gains at and above POS_BW_FULL_RPM.
// Board 1 (2026-11-20): the simulation's 10 / 0.12 / 100 (for J 1e-5) oscillated at ~52 Hz right after
// the hand-over and tripped the comparator.  #JMEAS measured the rotors at J 2.9e-6 (M1) / 3.4e-6 (M2)
// kg m^2 over 3000-12000 RPM, so the gains are scaled to J 3e-6 (x0.3).  On board 1 they held closed loop
// from the hand-over to 12000 RPM; in the simulation (J 2e-6..5e-6, R/flux errors) no slips or hunting,
// the 36 mNm load step passes, the 15 mNm one moves the Z ~12 deg (limit 10): retune for stiffness once
// the U9 sense noise is fixed (BRINGUP.md).
constexpr float POS_KP          = 3.0f;      // A per rad of angle error
constexpr float POS_KD          = 0.036f;    // A per rad/s of speed error
constexpr float POS_KI          = 30.0f;     // A per rad s
// Below POS_BW_FULL_RPM the angle loop's bandwidth b falls linearly to POS_BW_MIN at CL_EXIT_RPM
// (Kp*b^2, Kd*b, Ki*b^3: same damping, foc.cpp posGains).  At full gains the loop limit-cycled
// between 1500 and 3000 RPM in the simulation (+-5 A bursts at ~25 Hz: the observer is weakest at
// low back-EMF), and with R or the flux 5-20 % off a third of the runs slipped a rotor at the
// hand-back; scaled, none did (test/sim/sweep.sh).  Load steps below 4000 RPM move the Z more.
// GUESS: tune on the bench (#POSBW).
constexpr float POS_BW_MIN      = 0.5f;
constexpr float POS_BW_FULL_RPM = 4000.0f;
constexpr float CL_ANGLE_TRIP_RAD = 1.57f;   // GUESS: closed loop, a rotor 90 deg off its reference = stop
// Open loop: the rotor lags its current vector by the load angle (pull-out at 90 deg); past 150 deg
// it has slipped.  Followed only above OBS_TRUST_RPM once the observer tracks within OBS_TRUST_TOL.
constexpr float OL_SLIP_TRIP_RAD = 2.62f;
constexpr float OBS_TRUST_RPM   = 1000.0f;
constexpr float OBS_TRUST_TOL   = 0.2f;
// Open loop is a voltage drive (R*I + back-EMF feed-forward) trimmed onto the target current by an
// integrator of ~5 Hz (Ki = R * 2 pi * 5).  Slow on purpose: the rotor's swings keep the back-EMF
// damping a stiff current loop would cancel (see foc.cpp currentLoop).
constexpr float OL_CUR_KI       = 12.0f;     // V per A s
constexpr float OL_CARRY_FADE_A_S = 40.0f;   // closed-loop torque current fades at hand-back
constexpr float OBS_SPEED_TAU_S = 0.005f;    // BENCH: observer speed filter
constexpr float OBS_LOCK_TAU_S  = 0.030f;    // the hand-over lock test's own speed average: board 1's observers
                                             // ripple ~5-8 % at 1x / 2x electrical under the 3 A open-loop hold
constexpr float LOST_FRACTION   = 0.30f;     // BENCH: observer this far off the command ...
constexpr uint32_t LOST_MS      = 100;       // ... this long = lost lock (stop)
constexpr uint32_t CL_SETTLE_MS = 300;       // BENCH: no lost-lock check right after the hand-over

// ---- Protection ----
// Hardware: the comparators trip the timer break in ~1 us.  The ADC saturates at ~21 A.
constexpr float HW_TRIP_A       = 20.0f;     // GUESS: comparator threshold, conduction direction (below)
constexpr float SW_TRIP_A       = 15.0f;     // GUESS: any phase sample above this ...
constexpr int   SW_TRIP_COUNT   = 3;         // ... this many PWM periods in a row (ESC1 used 12 A x 3)
constexpr float RMS_TRIP_A      = 9.0f;      // GUESS: phase RMS, 50 ms filter, sustained ...
constexpr uint32_t RMS_TRIP_MS  = 200;       // ... this long
constexpr float VBUS_MAX_V      = 44.0f;     // GUESS: 60 V FETs, 63 V bulk: coast above this
constexpr float VBUS_MIN_V      = 20.0f;     // GUESS: refuse to drive below this
constexpr float TEMP_WARN_C     = 85.0f;     // GUESS
constexpr float TEMP_TRIP_C     = 100.0f;    // GUESS: fault code 1 (driver hardware)
constexpr uint32_t COAST_LOCKOUT_MS = 5000;  // GUESS: after a coast from speed, no re-enable for this long
constexpr uint32_t PRECHARGE_PERIODS = 4;    // low sides on before the first high side (bootstrap)
constexpr float LOAD_WARN_FRACTION = 0.9f;   // L: bit set while |Iq| stays above this share of the limit ...
constexpr uint32_t LOAD_WARN_MS = 200;       // ... this long

// ---- Z axis (unchanged from spindle/src/config.h) ----
constexpr float PHASE_OFFSET_RAMP_RAD_S = 400.0f * (float)M_PI / 180.0f;
// GUESS: 400 deg/s reached in 0.1 s.  The ESP32 stepped straight to 400 deg/s, which rings the rotors
// on a current vector (test/sim).  The XY board plans the Z at 10 mm/s^2 = 450 deg/s^2: unaffected.
constexpr float PHASE_OFFSET_ACCEL_RAD_S2 = 4000.0f * (float)M_PI / 180.0f;
constexpr float PHASE_MOVE_COMPLETE_EPS_RAD = 0.001f;
constexpr float PHASE_DEG_PER_MM   = 45.0f;
constexpr float Z_HOMING_MAX_MM    = 70.0f;
constexpr float Z_HOMING_PHASE_DIR = +1.0f;
constexpr float Z_HOMING_MAX_RAD   = Z_HOMING_MAX_MM * PHASE_DEG_PER_MM * (float)M_PI / 180.0f;
constexpr uint32_t BOOT_HOMING_DELAY_MS = 1000;
constexpr float Z_TOOL_LOAD_MAX_MM = 200.0f;
constexpr float Z_TOOL_LOAD_MAX_RAD = Z_TOOL_LOAD_MAX_MM * PHASE_DEG_PER_MM * (float)M_PI / 180.0f;
constexpr uint32_t Z_TOOL_LOAD_CONFIRM_MS = 400;
constexpr float Z_TOOL_REMOVE_MAX_MM = 150.0f;
constexpr float Z_TOOL_REMOVE_MAX_RAD = Z_TOOL_REMOVE_MAX_MM * PHASE_DEG_PER_MM * (float)M_PI / 180.0f;
constexpr uint32_t Z_TOOL_REMOVE_CONFIRM_MS = 500;

// ---- Link ----
constexpr uint32_t LINK_BAUD = 115200;
constexpr uint32_t LINK_STATUS_INTERVAL_MS = 50;
constexpr uint32_t LINK_TRIP_LINE_MS = 30;   // trip recording lines, one per 30 ms (as the ESP32)

// ---- Fan (VAC_CONTROL, same levels as the ESP32 firmware) ----
constexpr uint32_t FAN_PWM_HZ = 25000;
constexpr uint8_t  FAN_MIN_DUTY = 80;        // of 255
constexpr uint8_t  FAN_MAX_DUTY = 255;
constexpr float    FAN_RAMP_UNITS_PER_S = 320.0f;

// ---- Main loop / watchdog ----
constexpr uint32_t WATCHDOG_MS = 250;
