#include "motor_controller.h"

MotorController::MotorController(BLDCMotor& m, DRV8316Driver6PWM& d,
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

void MotorController::initDriver(SPIClass* spi) {
    pinMode(cur_a_pin, INPUT);
    pinMode(cur_b_pin, INPUT);
    pinMode(cur_c_pin, INPUT);

    driver.init(spi);
    driver.setRegistersLocked(false);
    delayMicroseconds(1);
    driver.setPWMMode(DRV8316_PWMMode::PWM6_Mode);
    delayMicroseconds(1);
    driver.setSlew(DRV8316_Slew::Slew_25Vus);
    delayMicroseconds(1);
    driver.setOvervoltageProtection(true);
    delayMicroseconds(1);
    driver.setOvervoltageLevel(DRV8316_OVP::OVP_SEL_32V);
    delayMicroseconds(1);
    driver.setCurrentSenseGain(DRV8316_CSAGain::Gain_0V25);
    delayMicroseconds(1);
    driver.setOCPDeglitchTime(DRV8316_OCPDeglitch::Deglitch_1us1);
    delayMicroseconds(1);
    driver.setOCPMode(DRV8316_OCPMode::AutoRetry_Fault);
    delayMicroseconds(1);
    driver.setOCPLevel(DRV8316_OCPLevel::Curr_24A);
    delayMicroseconds(1);
    driver.setOCPClearInPWMCycleChange(true);
    delayMicroseconds(1);

    driver.voltage_power_supply = SUPPLY_VOLTAGE;
    driver.voltage_limit        = SUPPLY_VOLTAGE * 0.8f;
    driver.pwm_frequency        = PWM_FREQUENCY;
    driver.dead_zone            = DEAD_ZONE;
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
    driver.setDriverOffEnabled(false);
    delayMicroseconds(1);
    motor.enable();
    enabled = true;
    start_time = millis();
}

void MotorController::disable() {
    motor.disable();
    driver.setDriverOffEnabled(true);
    delayMicroseconds(1);
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

    float current_a = (((adc_a / 4095.0f) * 3.3f) - 1.65f) / 0.25f;
    float current_b = (((adc_b / 4095.0f) * 3.3f) - 1.65f) / 0.25f;
    float current_c = (((adc_c / 4095.0f) * 3.3f) - 1.65f) / 0.25f;

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

void MotorController::printFaultStatus() {
    DRV8316Status st = driver.getStatus();

    if (st.isFault() || st.isOverTemperature() || st.isOverCurrent() ||
        st.isOverVoltage() || st.isSPIError() || st.isBuckError() ||
        st.isOverCurrent_Ah() || st.isOverCurrent_Al() ||
        st.isOverCurrent_Bh() || st.isOverCurrent_Bl() ||
        st.isOverCurrent_Ch() || st.isOverCurrent_Cl()) {

        Serial.println(F("DRV8316 FAULT DETECTED:"));
        Serial.print(F("  Fault=")); Serial.print(st.isFault());
        Serial.print(F("  OT="));    Serial.print(st.isOverTemperature());
        Serial.print(F("  OCP="));   Serial.print(st.isOverCurrent());
        Serial.print(F("  OVP="));   Serial.print(st.isOverVoltage());
        Serial.print(F("  SPIErr="));Serial.print(st.isSPIError());
        Serial.print(F("  BuckErr="));Serial.print(st.isBuckError());
        Serial.print(F("  POR="));   Serial.println(st.isPowerOnReset());

        Serial.print(F("  OCP A(h,l)=(")); Serial.print(st.isOverCurrent_Ah()); Serial.print(','); Serial.print(st.isOverCurrent_Al()); Serial.println(')');
        Serial.print(F("  OCP B(h,l)=(")); Serial.print(st.isOverCurrent_Bh()); Serial.print(','); Serial.print(st.isOverCurrent_Bl()); Serial.println(')');
        Serial.print(F("  OCP C(h,l)=(")); Serial.print(st.isOverCurrent_Ch()); Serial.print(','); Serial.print(st.isOverCurrent_Cl()); Serial.println(')');
    }

    if (st.isFault()) {
        driver.clearFault();
        delayMicroseconds(1);
    }
}
