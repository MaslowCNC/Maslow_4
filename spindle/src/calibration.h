#pragma once

#include "motor_controller.h"

enum CalState {
    CAL_IDLE,
    CAL_RAMP,
    CAL_SETTLE,
    CAL_HUNT,
    CAL_RAMP_DOWN,
    CAL_DONE,
    MCAL_RAMP,
    MCAL_SETTLE,
    MCAL_HOLD,
    MCAL_RAMP_DOWN,
    MCAL_DONE
};

struct Calibration {
    CalState state = CAL_IDLE;
    uint32_t timer = 0;
    float checkpoint = 0.0f;        // Current speed target (rad/s)
    float hunt_voltage = 0.0f;      // Voltage being applied during calibration
    int hunt_iter = 0;              // Iteration counter within one hunt step
    int active_motor_idx = 0;       // Which motor is being calibrated (0 or 1)
    float current_sum = 0.0f;       // Accumulator for averaged phase-RMS current
    int current_count = 0;          // Number of samples accumulated

    // Run the calibration state machine. Call each loop iteration.
    void update(MotorController& mc1, MotorController& mc2);

    // Start auto-calibration on motor 1 (chains to motor 2 automatically)
    void startAuto(MotorController& mc1, MotorController& mc2);

    // Start manual calibration on a specific motor
    void startManual(int motor_idx, MotorController& mc1, MotorController& mc2);

    // Resume manual calibration from first unrecorded step
    void resumeManual(MotorController& mc1, MotorController& mc2);

    // Handle manual calibration input (t/g/y/h for voltage, space to record)
    void handleManualInput(char cmd, MotorController& mc1, MotorController& mc2);

    // Abort calibration and print partial results
    void abort(MotorController& mc1, MotorController& mc2, const char* reason);

    // Accumulate current sample during hunt phase
    void accumulateCurrentSample(float instantaneous_current, int motor_idx);

    bool isActive() const { return state != CAL_IDLE; }

    // Print a motor's full 140-entry voltage LUT as a copy-paste C array (USB "DUMP").
    void printResults(MotorController& mc);

private:
    MotorController& getActiveMotor(MotorController& mc1, MotorController& mc2);
    void stopAllMotors(MotorController& mc1, MotorController& mc2);
    void printPartialResults(MotorController& mc);
    void printFullResults(MotorController& mc);
    void disableCalMotor(MotorController& mc);
    void advanceToNextStep(MotorController& mc);
};

void loadCalibrationLUT(MotorController& mc, int motor_idx, const float* defaults);
void saveCalibrationLUT(const MotorController& mc, int motor_idx);
