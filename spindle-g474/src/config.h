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
constexpr float PHASE_R_OHM     = 0.39f;     // BENCH: dead-time corrected
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

// ---- Sensorless closed loop ----
constexpr float CL_ENTER_RPM    = 2000.0f;   // BENCH: the observer is not trusted below this
constexpr float CL_EXIT_RPM     = 1500.0f;   // GUESS: hand back to open loop below this
constexpr float CL_LOCK_TOL     = 0.10f;     // BENCH: observer within 10% of the command ...
constexpr uint32_t CL_LOCK_MS   = 100;       // ... for this long before the hand-over
constexpr uint32_t CL_LOCK_TIMEOUT_MS = 1500;// GUESS: no lock this long after reaching CL_ENTER = fault
constexpr float CL_ID_RAMP_A_S  = 40.0f;     // the open loop's d current fades out at this rate
constexpr float VEL_KP          = 0.01f;     // BENCH: A per rad/s (soft; tune with #VEL)
constexpr float VEL_KI          = 0.2f;      // BENCH: A per rad
constexpr float VEL_IQ_RAMP_A_S = 200.0f;    // BENCH: velocity loop output slew
constexpr float IQ_LIMIT_A      = 8.0f;      // GUESS: closed-loop torque current limit per motor
// Z in closed loop: the two rotors' observed angles must keep the sum the phase offset sets.
// Each motor's speed reference gets -Z_KP * error (rad/s per rad of angle-sum error).
constexpr float Z_KP            = 10.0f;     // GUESS
constexpr float Z_ERR_TRIP_RAD  = 3.14f;     // GUESS: angle-sum error = 2 x phase error; 3.14 rad ~ 2 mm
constexpr float OBS_SPEED_TAU_S = 0.005f;    // BENCH: observer speed filter
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
