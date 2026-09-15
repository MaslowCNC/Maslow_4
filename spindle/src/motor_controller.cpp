#include "motor_controller.h"
#include "pins.h"
#include <driver/mcpwm.h>
#include "drivers/hardware_specific/esp32/esp32_driver_mcpwm.h"

// --- Per-motor hard off ---
// SimpleFOC's 6-PWM disable() is NOT an "outputs off" on this hardware: with the ESP32's
// hardware dead-time the low-side output is the complement of the high side, so a duty of 0
// leaves all three LOW-SIDE inputs HIGH - i.e. the motor's windings shorted (braked).  The
// DRV8316 board hid that behind DRVOFF, which the MP6541A does not have; its only off switch
// is nSLEEP, which is shared by both drivers, so a motor disabled while the OTHER motor runs
// (every calibration sweep, and single-motor serial commands) would be braked - and the two
// motors are mechanically coupled through the Z phase mechanism.
//
// So disable the dead-time generator and zero both duties, which leaves HSx and LSx both low:
// outputs Hi-Z, exactly what DRVOFF used to do.  enable() restores the dead-time pairing
// before the motor drives any PWM.
static mcpwm_unit_t driverUnit(const BLDCDriver6PWM& driver) {
    return ((ESP32MCPWMDriverParams*)driver.params)->mcpwm_unit;
}

static void driverOutputsOff(BLDCDriver6PWM& driver) {
    if (!driver.params) return;
    mcpwm_unit_t unit = driverUnit(driver);
    for (int t = 0; t < 3; t++) {
        mcpwm_deadtime_disable(unit, (mcpwm_timer_t)t);
        mcpwm_set_duty(unit, (mcpwm_timer_t)t, MCPWM_OPR_A, 0.0f);
        mcpwm_set_duty(unit, (mcpwm_timer_t)t, MCPWM_OPR_B, 0.0f);
    }
}

static void driverOutputsOn(BLDCDriver6PWM& driver) {
    if (!driver.params) return;
    ESP32MCPWMDriverParams* p = (ESP32MCPWMDriverParams*)driver.params;
    // Same dead time SimpleFOC's own _configureTimerFrequency() installs, recomputed here
    // because it is not kept in the params struct (only the dead-zone fraction is).
    float dead_time = ((float)_MCPWM_FREQ / (float)p->pwm_frequency) * p->deadtime;
    for (int t = 0; t < 3; t++) {
        mcpwm_deadtime_enable(p->mcpwm_unit, (mcpwm_timer_t)t,
                              MCPWM_ACTIVE_HIGH_COMPLIMENT_MODE,
                              dead_time / 2.0f, dead_time / 2.0f);
    }
}

// --- Shared nSLEEP ---
// Both MP6541As share one nSLEEP line, and the calibration sweep runs one motor while the
// other is disabled, so the line must stay high while EITHER motor is enabled.  enable() and
// disable() are called from both the FOC task (core 1) and the housekeeping task (core 0),
// so the count is guarded.  The drivers need ~1ms (tPUD) after nSLEEP rises before they
// respond to the HSx/LSx inputs.
static portMUX_TYPE sleep_mux = portMUX_INITIALIZER_UNLOCKED;
static int drivers_awake_count = 0;
static volatile uint32_t drivers_awake_since = 0;  // millis() when nSLEEP last went high

void initDriverSleepPin() {
    pinMode(DRV_NSLEEP, OUTPUT);
    digitalWrite(DRV_NSLEEP, LOW);
    drivers_awake_count = 0;
}

// Returns true if this call woke the drivers (so the caller waits out tPUD).
static bool acquireDriverWake() {
    bool first;
    taskENTER_CRITICAL(&sleep_mux);
    first = (drivers_awake_count++ == 0);
    taskEXIT_CRITICAL(&sleep_mux);
    if (first) {
        digitalWrite(DRV_NSLEEP, HIGH);
        drivers_awake_since = millis();
    }
    return first;
}

static void releaseDriverWake() {
    bool last;
    taskENTER_CRITICAL(&sleep_mux);
    if (drivers_awake_count > 0) drivers_awake_count--;
    last = (drivers_awake_count == 0);
    taskEXIT_CRITICAL(&sleep_mux);
    if (last) digitalWrite(DRV_NSLEEP, LOW);
}

bool driversAwake() {
    return drivers_awake_count > 0;
}

uint32_t driversAwakeSince() {
    return drivers_awake_since;
}

MotorController::MotorController(BLDCMotor& m, BLDCDriver6PWM& d,
                                 int ca, int cb, int cc, int dir)
    : motor(m), driver(d),
      cur_a_pin(ca), cur_b_pin(cb), cur_c_pin(cc),
      direction(dir)
{
    for (int i = 0; i < CAL_LUT_SIZE; i++) {
        cal_lut_voltage[i] = BASE_VOLTAGE;
        cal_lut_recorded[i] = false;
    }
}

// The MP6541A has no configuration interface: PWM mode (independent HSx/LSx inputs), slew
// rate, OVP (~45V), OCP level (16-20A, 2us deglitch, 2ms auto-retry) and the current-sense
// ratio are all fixed in silicon, so there is nothing to set up beyond the PWM peripheral.
// driver.init() itself is called from initMotor() via motor.init().
void MotorController::initDriver() {
    pinMode(cur_a_pin, INPUT);
    pinMode(cur_b_pin, INPUT);
    pinMode(cur_c_pin, INPUT);

    driver.voltage_power_supply = SUPPLY_VOLTAGE;
    driver.voltage_limit        = SUPPLY_VOLTAGE * 0.8f;
    driver.pwm_frequency        = PWM_FREQUENCY;
    driver.dead_zone            = DEAD_ZONE;

    // NOTE: pwm_frequency and dead_zone are only read by init(), so they must be set first.
    // (On the DRV8316 board DRV8316Driver6PWM::init() ran BEFORE these assignments, so they
    // never reached SimpleFOC and the PWM ran at its 20kHz default - see config.h.)
    driver.init();

    // MCPWM starts with duty 0, which (see driverOutputsOff) means all three low-side inputs
    // high.  The drivers are still asleep here, but force the outputs off so this motor stays
    // Hi-Z when the OTHER motor wakes the shared nSLEEP line.
    driverOutputsOff(driver);

    // Measure the sense-input zero now, while nSLEEP is still low and no phase current flows.
    calibrateCurrentZero();
}

void MotorController::calibrateCurrentZero() {
    const int N = 64;
    uint32_t sum_a = 0, sum_b = 0, sum_c = 0;
    for (int i = 0; i < N; i++) {
        sum_a += analogRead(cur_a_pin);
        sum_b += analogRead(cur_b_pin);
        sum_c += analogRead(cur_c_pin);
    }
    cur_zero_a = ((float)sum_a / N / 4095.0f) * 3.3f;
    cur_zero_b = ((float)sum_b / N / 4095.0f) * 3.3f;
    cur_zero_c = ((float)sum_c / N / 4095.0f) * 3.3f;
}

void MotorController::initMotor() {
    motor.linkDriver(&driver);
    motor.voltage_limit = BASE_VOLTAGE;
    motor.controller = MotionControlType::angle_openloop;
    motor.useMonitoring(Serial);
    motor.init();

    motor.target = 0.0f;
    motor.disable();
}

void MotorController::enable() {
    if (enabled) return;
    resetFilterState();
    // Wake the (shared) drivers and give them tPUD ~1ms to come up before driving the inputs.
    if (acquireDriverWake()) delay(2);
    driverOutputsOn(driver);   // restore complementary HS/LS with dead time
    motor.enable();
    enabled = true;
    start_time = millis();
}

void MotorController::disable() {
    motor.disable();
    driverOutputsOff(driver);  // HSx and LSx both low -> outputs Hi-Z, no braking
    if (enabled) releaseDriverWake();
    enabled = false;
    reached_speed = false;
}

void MotorController::emergencyStop() {
    disable();
    target_velocity = 0.0f;
    current_velocity = 0.0f;
    velocity_mode = false;
    continuous_rotation = false;
    resetFilterState();
}

void MotorController::resetFilterState() {
    filtered_current = 0.0f;
    protection_current = 0.0f;
}

void MotorController::rampVelocity(float dt, float ramp_rate) {
    if (!velocity_mode || dt <= 0) return;

    float velocity_error = target_velocity - current_velocity;
    float max_change = ramp_rate * dt;

    if (fabsf(velocity_error) <= max_change) {
        current_velocity = target_velocity;
    } else {
        current_velocity += (velocity_error > 0) ? max_change : -max_change;
    }

    // Power down when velocity reaches 0
    if (enabled && fabsf(current_velocity) < 0.1f && fabsf(target_velocity) < 0.1f) {
        disable();
        velocity_mode = false;
        Serial.printf("Motor (dir=%+d) powered down - velocity at 0\n", direction);
    }

    // Re-enable when target is non-zero and motor is disabled
    if (!enabled && fabsf(target_velocity) > 0.1f) {
        enable();
        Serial.printf("Motor (dir=%+d) re-enabled\n", direction);
    }
}

void MotorController::updateControlMode() {
    if (!enabled) return;

    if (velocity_mode) {
        motor.controller = MotionControlType::velocity_openloop;
        motor.target = current_velocity;
    } else if (continuous_rotation) {
        motor.controller = MotionControlType::angle_openloop;
        target_angle += 0.05f * direction;
        motor.target = target_angle;
    } else {
        motor.controller = MotionControlType::angle_openloop;
    }
}

void MotorController::updateCurrent() {
    int adc_a = analogRead(cur_a_pin);
    int adc_b = analogRead(cur_b_pin);
    int adc_c = analogRead(cur_c_pin);

    // MP6541A: SOx sources/sinks ILOAD/11000, turned into a voltage by the board's 3.3k/3.3k
    // termination (Vref = 1.65V, Rref = 1.65k) -> CSA_GAIN_V_PER_A volts per amp.  Only the
    // low-side FET current is sensed, as on the DRV8316.
    float current_a = (((adc_a / 4095.0f) * 3.3f) - cur_zero_a) / CSA_GAIN_V_PER_A;
    float current_b = (((adc_b / 4095.0f) * 3.3f) - cur_zero_b) / CSA_GAIN_V_PER_A;
    float current_c = (((adc_c / 4095.0f) * 3.3f) - cur_zero_c) / CSA_GAIN_V_PER_A;

    float instantaneous = sqrtf((current_a * current_a +
                                 current_b * current_b +
                                 current_c * current_c) / 3.0f);

    last_instantaneous_current = instantaneous;

    // Fast path: raw phase RMS for stall detection and calibration
    protection_current = protection_current * (1.0f - PROTECTION_FILTER_ALPHA)
                       + instantaneous * PROTECTION_FILTER_ALPHA;

    // Convert phase RMS to DC-bus equivalent for display
    float dc_current = instantaneous * (3.0f * 1.4142f * fabsf(motor.voltage.q))
                     / (2.0f * SUPPLY_VOLTAGE);

    // Slow path: DC-bus equivalent for telemetry
    filtered_current = filtered_current * (1.0f - CURRENT_FILTER_ALPHA)
                     + dc_current * CURRENT_FILTER_ALPHA;
}

void MotorController::runMotorLoop() {
    if (!enabled) return;
    motor.loopFOC();
    motor.move();
}

float MotorController::lutVoltageForSpeed(float speed_rad) const {
    if (speed_rad <= 0.0f || !cal_lut_valid) return BASE_VOLTAGE;

    const float min_rad = CAL_CHECKPOINT_STEP_RAD;
    if (speed_rad <= min_rad) {
        return BASE_VOLTAGE + (cal_lut_voltage[0] - BASE_VOLTAGE) * speed_rad / min_rad;
    }

    float idx_f = (speed_rad - min_rad) / CAL_CHECKPOINT_STEP_RAD;
    int i = (int)idx_f;
    if (i >= CAL_LUT_SIZE - 1) return cal_lut_voltage[CAL_LUT_SIZE - 1];
    float frac = idx_f - (float)i;
    return cal_lut_voltage[i] * (1.0f - frac) + cal_lut_voltage[i + 1] * frac;
}

void MotorController::applyVoltageLimit(bool in_calibration, float hunt_voltage, float extra_voltage) {
    if (!enabled) return;
    float v;
    if (in_calibration)
        v = hunt_voltage;
    else
        v = lutVoltageForSpeed(fabsf(current_velocity)) + extra_voltage;
    motor.voltage_limit = constrain(v, BASE_VOLTAGE, MAX_VOLTAGE);
}
