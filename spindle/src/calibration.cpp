#include "calibration.h"
#include <Preferences.h>

static Preferences cal_prefs;
static constexpr const char* CAL_PREF_NAMESPACE = "cal_lut";
static constexpr const char* CAL_PREF_MAGIC_KEY = "magic";
static constexpr const char* CAL_PREF_DATA_KEYS[] = { "m1", "m2" };
static constexpr uint32_t CAL_PREF_MAGIC = 0x43414C54UL; // 'CALT'

struct LUTPersistData {
    float voltage[CAL_LUT_SIZE];
    uint8_t recorded[CAL_LUT_SIZE];
};

// Minimum open-loop voltage needed to sustain rotation at a given speed.
// In open-loop velocity control the motor must overcome back-EMF (~Ke*omega),
// so the applied voltage floor scales with the target speed. Below this the
// motor cannot reach the checkpoint and merely vibrates/stalls.
static float speedVoltageFloor(float rad) {
    return constrain(CAL_RAMP_VOLT_PER_RAD * rad, BASE_VOLTAGE, MAX_VOLTAGE);
}

static bool lutFullyRecorded(const MotorController& mc) {
    for (int i = 0; i < CAL_LUT_SIZE; i++) {
        if (!mc.cal_lut_recorded[i]) return false;
    }
    return true;
}

void loadCalibrationLUT(MotorController& mc, int motor_idx, const float* defaults) {
    memcpy(mc.cal_lut_voltage, defaults, sizeof(float) * CAL_LUT_SIZE);
    memset(mc.cal_lut_recorded, 0, sizeof(mc.cal_lut_recorded));
    mc.cal_lut_valid = true;

    if (motor_idx < 0 || motor_idx > 1) {
        return;
    }

    cal_prefs.begin(CAL_PREF_NAMESPACE, true);
    uint32_t magic = cal_prefs.getUInt(CAL_PREF_MAGIC_KEY, 0);
    size_t expected_size = sizeof(LUTPersistData);
    if (magic == CAL_PREF_MAGIC && cal_prefs.isKey(CAL_PREF_DATA_KEYS[motor_idx])) {
        LUTPersistData data{};
        size_t loaded = cal_prefs.getBytes(CAL_PREF_DATA_KEYS[motor_idx], &data, expected_size);
        if (loaded == expected_size) {
            memcpy(mc.cal_lut_voltage, data.voltage, sizeof(data.voltage));
            memcpy(mc.cal_lut_recorded, data.recorded, sizeof(data.recorded));
            mc.cal_lut_valid = lutFullyRecorded(mc);
        }
    }
    cal_prefs.end();
}

void saveCalibrationLUT(const MotorController& mc, int motor_idx) {
    if (motor_idx < 0 || motor_idx > 1) {
        return;
    }

    cal_prefs.begin(CAL_PREF_NAMESPACE, false);
    cal_prefs.putUInt(CAL_PREF_MAGIC_KEY, CAL_PREF_MAGIC);

    LUTPersistData data{};
    memcpy(data.voltage, mc.cal_lut_voltage, sizeof(data.voltage));
    memcpy(data.recorded, mc.cal_lut_recorded, sizeof(data.recorded));
    cal_prefs.putBytes(CAL_PREF_DATA_KEYS[motor_idx], &data, sizeof(data));
    cal_prefs.end();
}

MotorController& Calibration::getActiveMotor(MotorController& mc1, MotorController& mc2) {
    return (active_motor_idx == 0) ? mc1 : mc2;
}

void Calibration::stopAllMotors(MotorController& mc1, MotorController& mc2) {
    mc1.emergencyStop();
    mc2.emergencyStop();
}

void Calibration::printPartialResults(MotorController& mc) {
    int first_rec = -1, last_rec = -1;
    for (int i = 0; i < CAL_LUT_SIZE; i++) {
        if (mc.cal_lut_recorded[i]) {
            if (first_rec < 0) first_rec = i;
            last_rec = i;
        }
    }
    if (first_rec < 0) return;

    int recorded = last_rec - first_rec + 1;
    Serial.println(F("\n=== PARTIAL CALIBRATION RESULTS ==="));
    Serial.printf("Recorded %d steps (%d-%d RPM).\n",
                  recorded, (first_rec + 1) * 100, (last_rec + 1) * 100);
    Serial.println(F(" RPM    Voltage"));
    for (int i = first_rec; i <= last_rec; i++) {
        Serial.printf(" %4.0f    %.3f V\n", (i + 1) * 100.0f, mc.cal_lut_voltage[i]);
    }
    Serial.println();
    Serial.println(F("// Copy-paste array (partial) for hard-coding:"));
    Serial.printf("// Replace entries %d..%d in cal_lut_voltage[]\n", first_rec, last_rec);
    for (int i = first_rec; i <= last_rec; i++) {
        Serial.printf("  %.3ff,  // %4.0f RPM\n", mc.cal_lut_voltage[i], (i + 1) * 100.0f);
    }
    Serial.println(F("====================================="));
}

void Calibration::printFullResults(MotorController& mc) {
    Serial.println(F(" RPM    Voltage"));
    for (int i = 0; i < CAL_LUT_SIZE; i++) {
        Serial.printf(" %4.0f    %.3f V\n", (i + 1) * 100.0f, mc.cal_lut_voltage[i]);
    }
    Serial.println();
    Serial.println(F("// Copy-paste array for hard-coding:"));
    Serial.printf("float cal_lut_voltage[%d] = {\n", CAL_LUT_SIZE);
    for (int i = 0; i < CAL_LUT_SIZE; i++) {
        Serial.printf("  %.3ff%s  // %4.0f RPM\n",
                      mc.cal_lut_voltage[i],
                      (i < CAL_LUT_SIZE - 1) ? "," : " ",
                      (i + 1) * 100.0f);
    }
    Serial.println(F("};"));
    Serial.println(F("====================================="));
}

void Calibration::disableCalMotor(MotorController& mc) {
    mc.disable();
    mc.target_velocity = 0.0f;
    mc.current_velocity = 0.0f;
    mc.velocity_mode = false;
    Serial.printf("Motor (dir=%+d) disabled - coasting to stop.\n", mc.direction);
}

void Calibration::advanceToNextStep(MotorController& mc) {
    checkpoint = min(checkpoint + CAL_CHECKPOINT_STEP_RAD, CAL_MAX_RAD);
    mc.target_velocity = checkpoint * mc.direction;
    // Raise the applied voltage to the speed floor so the motor can actually
    // spin up to the new (faster) checkpoint instead of stalling/vibrating.
    hunt_voltage = fmaxf(hunt_voltage, speedVoltageFloor(checkpoint));
    mc.start_time = millis();
    timer = millis();
}

void Calibration::abort(MotorController& mc1, MotorController& mc2, const char* reason) {
    if (state == CAL_IDLE) return;

    MotorController& mc = getActiveMotor(mc1, mc2);
    state = CAL_IDLE;
    Serial.printf("Calibration aborted: %s\n", reason);
    printPartialResults(mc);
}

void Calibration::accumulateCurrentSample(float instantaneous_current, int motor_idx) {
    if (state == CAL_HUNT && active_motor_idx == motor_idx) {
        current_sum += instantaneous_current;
        current_count++;
    }
}

void Calibration::startAuto(MotorController& mc1, MotorController& mc2) {
    if (state != CAL_IDLE) {
        Serial.println(F("Calibration already in progress."));
        return;
    }
    Serial.println(F("Starting voltage calibration on Motor 1..."));
    Serial.printf("Stepping 100-%.0f RPM in 100 RPM increments.\n",
                  CAL_MAX_RAD * 60.0f / (2.0f * PI));
    Serial.printf("Voltage is adjusted at each step until I_phase_rms = %.1f A.\n",
                  CAL_TARGET_CURRENT);

    stopAllMotors(mc1, mc2);
    active_motor_idx = 0;
    checkpoint = CAL_CHECKPOINT_STEP_RAD;
    hunt_voltage = speedVoltageFloor(checkpoint);
    mc1.cal_lut_valid = false;
    memset(mc1.cal_lut_recorded, 0, sizeof(mc1.cal_lut_recorded));

    mc1.target_velocity = checkpoint;
    mc1.current_velocity = 0.0f;
    mc1.velocity_mode = true;
    mc1.enable();

    timer = millis();
    state = CAL_RAMP;
    Serial.printf("Ramping to first point: %.0f RPM...\n",
                  checkpoint * 60.0f / (2.0f * PI));
}

void Calibration::startManual(int motor_idx, MotorController& mc1, MotorController& mc2) {
    if (state != CAL_IDLE) {
        Serial.println(F("Calibration already in progress."));
        return;
    }

    active_motor_idx = motor_idx;
    MotorController& mc = getActiveMotor(mc1, mc2);

    Serial.printf("Starting MANUAL voltage calibration on Motor %d...\n", motor_idx + 1);
    Serial.println(F("At each RPM step: 't' = +0.05 V, 'g' = -0.05 V, SPACE = record & advance."));

    stopAllMotors(mc1, mc2);
    hunt_voltage = BASE_VOLTAGE;
    mc.cal_lut_valid = false;
    memset(mc.cal_lut_recorded, 0, sizeof(mc.cal_lut_recorded));

    checkpoint = CAL_CHECKPOINT_STEP_RAD;
    mc.target_velocity = checkpoint * mc.direction;
    mc.current_velocity = 0.0f;
    mc.velocity_mode = true;
    mc.enable();

    timer = millis();
    state = MCAL_RAMP;
    Serial.printf("Ramping to first point: %.0f RPM...\n",
                  checkpoint * 60.0f / (2.0f * PI));
}

void Calibration::resumeManual(MotorController& mc1, MotorController& mc2) {
    if (state != CAL_IDLE) {
        Serial.println(F("Calibration already in progress."));
        return;
    }

    MotorController& mc = getActiveMotor(mc1, mc2);

    int resume_idx = -1;
    for (int i = 0; i < CAL_LUT_SIZE; i++) {
        if (!mc.cal_lut_recorded[i]) { resume_idx = i; break; }
    }
    if (resume_idx < 0) {
        Serial.println(F("All LUT entries already recorded. Use 'Q' or 'W' to redo from scratch."));
        return;
    }

    float resume_rpm = (resume_idx + 1) * 100.0f;
    Serial.printf("Resuming manual calibration (Motor %d) from %.0f RPM.\n",
                  active_motor_idx + 1, resume_rpm);
    Serial.println(F("At each RPM step: 't' = +0.05 V, 'g' = -0.05 V, SPACE = record & advance."));

    stopAllMotors(mc1, mc2);
    hunt_voltage = mc.cal_lut_voltage[resume_idx];
    checkpoint = (resume_idx + 1) * CAL_CHECKPOINT_STEP_RAD;

    mc.target_velocity = checkpoint * mc.direction;
    mc.current_velocity = 0.0f;
    mc.velocity_mode = true;
    mc.enable();

    timer = millis();
    state = MCAL_RAMP;
    Serial.printf("Ramping Motor %d to %.0f RPM...\n", active_motor_idx + 1, resume_rpm);
}

void Calibration::handleManualInput(char cmd, MotorController& mc1, MotorController& mc2) {
    MotorController& mc = getActiveMotor(mc1, mc2);

    if ((cmd == 't' || cmd == 'g' || cmd == 'y' || cmd == 'h') && state == MCAL_HOLD) {
        float step = (cmd == 't' || cmd == 'g') ? 0.05f : 0.01f;
        if (cmd == 'g' || cmd == 'h') step = -step;
        hunt_voltage = constrain(hunt_voltage + step, BASE_VOLTAGE, MAX_VOLTAGE);
        mc.motor.voltage_limit = hunt_voltage;
        Serial.printf("[MCAL] Motor %d  %.0f RPM  V=%.3f  I=%.3f A  (%+.2f)\n",
                      active_motor_idx + 1, checkpoint * 60.0f / (2.0f * PI),
                      hunt_voltage, mc.filtered_current, step);
    }
    else if (cmd == ' ' && state == MCAL_HOLD) {
        int idx = (int)roundf(checkpoint / CAL_CHECKPOINT_STEP_RAD) - 1;
        if (idx >= 0 && idx < CAL_LUT_SIZE) {
            mc.cal_lut_voltage[idx] = hunt_voltage;
            mc.cal_lut_recorded[idx] = true;
        }
        Serial.printf("[MCAL] Motor %d  %.0f RPM -> %.3f V recorded.\n",
                      active_motor_idx + 1, checkpoint * 60.0f / (2.0f * PI), hunt_voltage);

        if (checkpoint >= CAL_MAX_RAD - 0.5f) {
            state = MCAL_DONE;
        } else {
            advanceToNextStep(mc);
            state = MCAL_RAMP;
            Serial.printf("[MCAL] Ramping Motor %d to %.0f RPM...\n",
                          active_motor_idx + 1, checkpoint * 60.0f / (2.0f * PI));
        }
    }
}

void Calibration::update(MotorController& mc1, MotorController& mc2) {
    if (state == CAL_IDLE) return;

    MotorController& mc = getActiveMotor(mc1, mc2);

    switch (state) {

    case CAL_RAMP: {
        float cal_cv = fabsf(mc.current_velocity);
        if (fabsf(cal_cv - checkpoint) < 0.5f) {
            timer = millis();
            state = CAL_SETTLE;
        } else {
            if (fabsf(cal_cv - checkpoint) > CAL_CHECKPOINT_STEP_RAD) {
                timer = millis();
            } else if (millis() - timer > 5000UL) {
                Serial.printf("[CAL] Motor %d stalled before %.0f RPM - stopping.\n",
                              active_motor_idx + 1, checkpoint * 60.0f / (2.0f * PI));
                state = CAL_DONE;
            }
        }
        break;
    }

    case CAL_SETTLE:
        if (millis() - timer >= CAL_SETTLE_MS) {
            hunt_iter = 0;
            current_sum = 0.0f;
            current_count = 0;
            timer = millis();
            state = CAL_HUNT;
        }
        break;

    case CAL_HUNT: {
        if (millis() - timer < 400UL) break;
        timer = millis();

        int idx = (int)roundf(checkpoint / CAL_CHECKPOINT_STEP_RAD) - 1;
        float v_floor = speedVoltageFloor(checkpoint);
        float seeded_v = v_floor;
        if (idx >= 0 && idx < CAL_LUT_SIZE) {
            seeded_v = fmaxf(seeded_v, mc.cal_lut_voltage[idx]);
            if (idx > 0) {
                seeded_v = fmaxf(seeded_v, mc.cal_lut_voltage[idx - 1]);
            }
        }
        float hunt_ceiling = constrain(seeded_v + CAL_HUNT_VOLTAGE_MARGIN,
                                       v_floor + 0.3f,
                                       MAX_VOLTAGE);

        // Thermal guard: the DRV8316 asserts an over-temperature WARNING (OTW)
        // before it hard-faults with an over-temperature SHUTDOWN. At very low
        // RPM the target current can be thermally unreachable (little rotation,
        // current concentrated in a few FETs), so the part heats until it trips.
        // When the warning is active, stop climbing, record the best value so
        // far, and advance instead of letting it escalate to an OT shutdown that
        // aborts the whole calibration.
        DRV8316Status thermal_st = mc.driver.getStatus();
        if (thermal_st.isOverTemperatureWarning() || thermal_st.isOverTemperatureShutdown()) {
            hunt_voltage = fmaxf(hunt_voltage - 0.10f, v_floor);
            if (idx >= 0 && idx < CAL_LUT_SIZE) {
                mc.cal_lut_voltage[idx] = hunt_voltage;
                mc.cal_lut_recorded[idx] = true;
            }
            Serial.printf("[CAL] %.0f RPM -> %.3f V  (thermal warning, backing off)\n",
                          checkpoint * 60.0f / (2.0f * PI), hunt_voltage);
            if (checkpoint >= CAL_MAX_RAD - 0.5f) {
                state = CAL_DONE;
            } else {
                advanceToNextStep(mc);
                state = CAL_RAMP;
            }
            break;
        }

        float I = (current_count > 0)
                  ? (current_sum / (float)current_count)
                  : mc.protection_current;
        current_sum = 0.0f;
        current_count = 0;

        float err = CAL_TARGET_CURRENT - I;
        float dV = constrain(err * 0.2f, -0.08f, 0.08f);
        hunt_voltage = constrain(hunt_voltage + dV, v_floor, hunt_ceiling);
        Serial.printf("[CAL] %5.0f RPM  V=%5.3f  I_rms=%5.3f A\n",
                      checkpoint * 60.0f / (2.0f * PI), hunt_voltage, I);
        hunt_iter++;

        bool hit_ceiling = hunt_voltage >= (hunt_ceiling - 0.01f);
        // At the speed floor the minimum spin voltage may already draw more than
        // the target current. The hunt wants to lower voltage but can't go below
        // the floor, so it would otherwise burn all iterations. Accept the floor
        // voltage immediately in that case.
        bool hit_floor = (err < 0.0f) && (hunt_voltage <= v_floor + 0.01f);
        if (fabsf(err) <= 0.04f || hunt_iter >= 40 || hit_ceiling || hit_floor) {
            if (idx >= 0 && idx < CAL_LUT_SIZE) {
                mc.cal_lut_voltage[idx] = hunt_voltage;
                mc.cal_lut_recorded[idx] = true;
            }
            Serial.printf("[CAL] %.0f RPM -> %.3f V  (I_rms=%.3f A%s)\n",
                          checkpoint * 60.0f / (2.0f * PI), hunt_voltage, I,
                          hunt_iter >= 40 ? ", max iters"
                              : (hit_ceiling ? ", voltage ceiling"
                              : (hit_floor ? ", spin floor" : "")));

            if (checkpoint >= CAL_MAX_RAD - 0.5f) {
                state = CAL_DONE;
            } else {
                advanceToNextStep(mc);
                state = CAL_RAMP;
            }
        }
        break;
    }

    case CAL_DONE: {
        mc.cal_lut_valid = true;
        // Ramp down exactly like pressing '0': set target to 0 and let
        // rampVelocity + applyVoltageLimit in the main loop handle it.
        mc.target_velocity = 0.0f;
        state = CAL_RAMP_DOWN;
        break;
    }

    case CAL_RAMP_DOWN: {
        // target_velocity was set to 0 in CAL_DONE; rampVelocity in the main
        // loop is decelerating current_velocity at VELOCITY_RAMP_RATE exactly
        // as the '0' key does. Just wait here until the motor has stopped.
        if (fabsf(mc.current_velocity) >= 0.5f) {
            break;
        }
        // Make sure the finished motor is fully disabled before starting the next one.
        disableCalMotor(mc);

        // If motor 1 just finished, immediately start motor 2.
        // Avoid blocking output here so handoff timing stays deterministic.
        if (active_motor_idx == 0) {
            Serial.println(F("\n--- Starting auto-calibration for Motor 2 ---"));
            saveCalibrationLUT(mc, active_motor_idx);
            active_motor_idx = 1;
            MotorController& mc2_ref = mc2;
            hunt_voltage = BASE_VOLTAGE;
            mc2_ref.cal_lut_valid = false;
            memset(mc2_ref.cal_lut_recorded, 0, sizeof(mc2_ref.cal_lut_recorded));
            checkpoint = CAL_CHECKPOINT_STEP_RAD;
            mc2_ref.target_velocity = -checkpoint;
            mc2_ref.current_velocity = 0.0f;
            mc2_ref.velocity_mode = true;
            mc2_ref.resetFilterState();
            mc2_ref.enable();
            timer = millis();
            state = CAL_RAMP;
        } else {
            Serial.printf("\n=== AUTO CALIBRATION COMPLETE (Motor %d) ===\n", active_motor_idx + 1);
            saveCalibrationLUT(mc, active_motor_idx);
            state = CAL_IDLE;
        }
        break;
    }

    // ---- Manual calibration states ----

    case MCAL_RAMP: {
        float cv = fabsf(mc.current_velocity);
        if (fabsf(cv - checkpoint) < 0.5f) {
            timer = millis();
            state = MCAL_SETTLE;
        } else {
            if (fabsf(cv - checkpoint) > CAL_CHECKPOINT_STEP_RAD)
                timer = millis();
            else if (millis() - timer > 5000UL) {
                Serial.printf("[MCAL] Motor %d stalled before %.0f RPM - stopping.\n",
                              active_motor_idx + 1, checkpoint * 60.0f / (2.0f * PI));
                state = MCAL_DONE;
            }
        }
        break;
    }

    case MCAL_SETTLE:
        if (millis() - timer >= CAL_SETTLE_MS) {
            timer = millis();
            state = MCAL_HOLD;
            Serial.printf("[MCAL] Motor %d  %.0f RPM ready.  V=%.3f\n"
                          "       Coarse: 't'(+0.05V) / 'g'(-0.05V)   Fine: 'y'(+0.01V) / 'h'(-0.01V)\n"
                          "       Press SPACE to record and advance.\n",
                          active_motor_idx + 1, checkpoint * 60.0f / (2.0f * PI), hunt_voltage);
        }
        break;

    case MCAL_HOLD: {
        if (millis() - timer >= 3000UL) {
            timer = millis();
            Serial.printf("[MCAL] Motor %d  %.0f RPM  V=%.3f  I=%.3f A  (t/g +/-0.05V, y/h +/-0.01V, SPACE=record)\n",
                          active_motor_idx + 1, checkpoint * 60.0f / (2.0f * PI),
                          hunt_voltage, mc.filtered_current);
        }
        break;
    }

    case MCAL_DONE: {
        mc.cal_lut_valid = true;
        mc.target_velocity = 0.0f;
        state = MCAL_RAMP_DOWN;
        break;
    }

    case MCAL_RAMP_DOWN: {
        if (fabsf(mc.current_velocity) >= 0.5f) break;
        Serial.printf("\n=== MANUAL CALIBRATION COMPLETE (Motor %d) ===\n", active_motor_idx + 1);
        saveCalibrationLUT(mc, active_motor_idx);
        printFullResults(mc);
        state = CAL_IDLE;
        break;
    }

    default:
        break;
    }
}
