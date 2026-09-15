#pragma once

#include <SimpleFOC.h>
#include "config.h"

struct MotorController {
    // Hardware references
    BLDCMotor& motor;
    BLDCDriver6PWM& driver;
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

    // Measured zero-current output of each phase's sense termination (volts).  The MP6541A's
    // SOx pins source/sink a current that the board's 3.3k/3.3k divider turns into a voltage
    // centred on VREF; the real centre is set by resistor tolerance and the ADC's own offset,
    // so it is measured at boot (drivers asleep = zero phase current) instead of assumed.
    float cur_zero_a = CSA_VREF;
    float cur_zero_b = CSA_VREF;
    float cur_zero_c = CSA_VREF;

    // Motor timing
    uint32_t start_time = 0;
    bool enabled = false;
    bool reached_speed = false;

    // Calibration LUT
    float cal_lut_voltage[CAL_LUT_SIZE];
    bool cal_lut_valid = true;
    bool cal_lut_recorded[CAL_LUT_SIZE];

    MotorController(BLDCMotor& m, BLDCDriver6PWM& d,
                    int ca, int cb, int cc, int dir);

    // Initialization
    void initDriver();
    void initMotor();

    // Measure the zero-current level of the three sense inputs.  Must be called with the
    // drivers asleep (no phase current) - i.e. before the first enable().
    void calibrateCurrentZero();

    // Control
    void enable();
    void disable();
    void emergencyStop();
    void resetFilterState();

    // Per-loop updates
    void rampVelocity(float dt, float ramp_rate);
    void updateControlMode();
    void updateCurrent();
    void runMotorLoop();

    // Voltage from calibration LUT (interpolated)
    float lutVoltageForSpeed(float speed_rad) const;

    // Apply voltage limit from LUT or calibration hunt voltage
    void applyVoltageLimit(bool in_calibration, float hunt_voltage, float extra_voltage = 0.0f);
};

// Put both MP6541A drivers to sleep (nSLEEP low).  Called once from setup() before the
// drivers are configured; enable()/disable() manage nSLEEP from then on.
void initDriverSleepPin();

// True while nSLEEP is high (at least one motor enabled).  nFAULT is only meaningful then:
// a sleeping MP6541A releases its open-drain output, so the pin reads high regardless.
bool     driversAwake();
uint32_t driversAwakeSince();  // millis() when nSLEEP last went high
