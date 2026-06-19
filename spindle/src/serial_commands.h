#pragma once

#include "motor_controller.h"
#include "calibration.h"

// Phase offset state (shared between serial commands and main loop)
struct PhaseOffset {
    float current = 0.0f;
    float target = 0.0f;
};

// Active motor selection: 0 = motor 1, 1 = motor 2, 2 = both
extern int active_motor;
extern PhaseOffset phase_offset;

// Fault code reported to the XY board over the link:
//   0 = OK, 1 = DRV8316 hardware fault, 2 = overcurrent
extern volatile uint8_t g_fault_code;

// Suction/cooling fan power (0-100), set by the XY board over the link ('C' command).
extern volatile uint8_t g_suction_level;

void initFanControl();
void updateFanControl(float dt);

// Drive the cooling fan from the motor-enable state: runs at g_suction_level whenever
// either motor is enabled (spindle running or Z moving), off otherwise.
void applyFanForMotorState(bool motorsEnabled);

// Line-based command protocol (shared by USB Serial and the inter-board link).
// Supported commands (newline terminated):
//   S<rpm>  set spindle speed (0 = stop)
//   Z<deg>  set absolute target phase offset in degrees (Z-axis position)
//   E       emergency stop (disable both motors)
//   ?       request a status report immediately
void handleSerialCommands(MotorController& mc1, MotorController& mc2, Calibration& cal);

// Emit a status line ("T:<state>,P:<deg>,R:<rpm>,F:<code>") to the given stream.
void sendStatus(Stream& out, MotorController& mc1, MotorController& mc2);

void printCommandHelp();
