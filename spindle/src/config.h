#pragma once

#include <Arduino.h>

// Motor parameters
const int   POLE_PAIRS = 1;
const float SUPPLY_VOLTAGE = 24.0f;

// Voltage limits
const float BASE_VOLTAGE = 1.3f;
const float MAX_VOLTAGE = 16.0f;

// PWM configuration
const long  PWM_FREQUENCY = 60000;   // 60 kHz
const float DEAD_ZONE = 0.02f;       // ~2% deadtime

// Fan PWM configuration
const long FAN_PWM_FREQUENCY = 25000;
const int  FAN_PWM_RESOLUTION_BITS = 8;
const int  FAN_PWM_CHANNEL = 7;
const uint8_t FAN_MIN_DUTY = 80;
const uint8_t FAN_MAX_DUTY = 255;
const int FAN_LEVEL_COUNT = 100;
const float FAN_PWM_RAMP_UNITS_PER_SEC = 320.0f;

// Velocity ramping
const float VELOCITY_RAMP_RATE = 20.0f;  // rad/s per second

// Phase offset ramping
const float PHASE_OFFSET_STEP = 45.0f * PI / 180.0f;          // 45 deg per keypress
const float PHASE_OFFSET_RAMP_RATE = 400.0f * PI / 180.0f;    // 400 deg/s ramp

// Inter-board link (UART to FluidNC XY board)
const long    LINK_BAUD = 115200;          // baud rate for the XY <-> spindle link
const uint32_t LINK_STATUS_INTERVAL_MS = 50;  // how often to report status to the XY board
const int     MAX_COMMAND_RPM = 10000;     // clamp for spindle speed commands

// Current filtering
const float CURRENT_FILTER_ALPHA = 0.001f;      // Slow filter for DC-equivalent telemetry
const float PROTECTION_FILTER_ALPHA = 0.1f;     // Fast filter for stall detection

// Overcurrent protection
const float OVERCURRENT_THRESHOLD = 5.0f;       // Phase-RMS trip point (A)
const float OVERCURRENT_THRESHOLD_CAL = 6.0f;   // Relaxed phase-RMS trip point during calibration
const int   OVERCURRENT_CONSECUTIVE_TRIPS = 100;

// Motor run timeout
const uint32_t MOTOR_RUN_DURATION = 6000000;     // 60 seconds in ms

// Calibration LUT
const int   CAL_LUT_SIZE = 140;                                     // 100, 200, ... 14000 RPM
const float CAL_TARGET_CURRENT = 3.5f;                              // Target phase-RMS current (A)
const float CAL_CHECKPOINT_STEP_RAD = 100.0f * 2.0f * PI / 60.0f;  // 100 RPM step in rad/s
const float CAL_MAX_RAD = 14000.0f * 2.0f * PI / 60.0f;            // 14000 RPM in rad/s
const uint32_t CAL_SETTLE_MS = 500;                                 // ms to wait after reaching speed
const float CAL_HUNT_VOLTAGE_MARGIN = 0.9f;                         // Max extra volts above seeded LUT at each step
const float CAL_RAMP_VOLT_PER_RAD = 0.0025f;                        // Open-loop spin-floor slope (volts per rad/s).
                                                                    // Kept BELOW the real current-limited curve so the hunt
                                                                    // can converge; spin-up between steps relies on the
                                                                    // previous step's voltage carried forward, not this floor.

// DRV8316 fault detection
const int OCP_CONSEC_LIMIT = 5;  // 5 x 100ms = 500ms persistent OCP -> disable
