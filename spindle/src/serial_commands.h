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

void initFanControl();
void updateFanControl(float dt);
void handleSerialCommand(char cmd, MotorController& mc1, MotorController& mc2, Calibration& cal);
void handleSerialCommands(MotorController& mc1, MotorController& mc2, Calibration& cal);
void printCommandHelp();
