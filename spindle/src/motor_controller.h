#pragma once

#include <SimpleFOC.h>
#include "drivers/drv8316/drv8316.h"
#include "config.h"

struct MotorController {
    // Hardware references
    BLDCMotor& motor;
    DRV8316Driver6PWM& driver;
    const int cur_a_pin, cur_b_pin, cur_c_pin;

    // Direction multiplier: +1 for motor 1, -1 for motor 2
    const int direction;

    // Angle control
    float target_angle = 0.0f;
    float angle_increment = 0.1f;
    bool continuous_rotation = false;

    // Velocity control
    float target_velocity = 0.0f;
    float current_velocity = 0.0f;
    bool velocity_mode = false;

    // Current filtering
    float filtered_current = 0.0f;
    float protection_current = 0.0f;
    float last_instantaneous_current = 0.0f;  // Raw phase RMS from last sample

    // Motor timing
    uint32_t start_time = 0;
    bool enabled = false;
    bool reached_speed = false;

    // Calibration LUT
    float cal_lut_voltage[CAL_LUT_SIZE];
    bool cal_lut_valid = true;
    bool cal_lut_recorded[CAL_LUT_SIZE];

    MotorController(BLDCMotor& m, DRV8316Driver6PWM& d,
                    int ca, int cb, int cc, int dir);

    // Initialization
    void initDriver(SPIClass* spi);
    void initMotor();

    // Control
    void enable();
    void disable();
    void emergencyStop();
    void resetFilterState();

    // Per-loop updates
    void rampVelocity(float dt);
    void updateControlMode();
    void updateCurrent();
    void runMotorLoop();

    // Voltage from calibration LUT (interpolated)
    float lutVoltageForSpeed(float speed_rad) const;

    // Apply voltage limit from LUT or calibration hunt voltage
    void applyVoltageLimit(bool in_calibration, float hunt_voltage, float extra_voltage = 0.0f);

    // DRV8316 status
    void printFaultStatus();
};
