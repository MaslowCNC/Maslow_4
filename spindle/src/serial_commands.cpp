#include "serial_commands.h"
#include "pins.h"
#include "config.h"
#include <Preferences.h>

static Preferences fan_prefs;
static const char* FAN_PREF_NAMESPACE = "fan_ctrl";
static const char* FAN_PREF_LEVEL_KEY = "level";
static const char* FAN_PREF_ENABLED_KEY = "enabled";
static constexpr bool FAN_PERSIST_RUNTIME = false;

static const uint8_t fan_default_level = 49;
static uint8_t fan_speed_index = fan_default_level;
static bool fan_enabled = true;
static float fan_current_pwm = 0.0f;
static float fan_target_pwm = 0.0f;

static uint8_t clampFanLevel(int index) {
    if (index < 0) return 0;
    if (index >= FAN_LEVEL_COUNT) {
        return (uint8_t)(FAN_LEVEL_COUNT - 1);
    }
    return (uint8_t)index;
}

static uint8_t fanDutyForLevel(uint8_t level) {
    if (FAN_LEVEL_COUNT <= 1) return FAN_MAX_DUTY;
    uint16_t span = (uint16_t)(FAN_MAX_DUTY - FAN_MIN_DUTY);
    uint16_t scaled = (uint16_t)((uint32_t)span * level / (uint32_t)(FAN_LEVEL_COUNT - 1));
    return (uint8_t)(FAN_MIN_DUTY + scaled);
}

static void persistFanState() {
    if (!FAN_PERSIST_RUNTIME) {
        return;
    }
    fan_prefs.putUChar(FAN_PREF_LEVEL_KEY, fan_speed_index);
    fan_prefs.putBool(FAN_PREF_ENABLED_KEY, fan_enabled);
}

static void writeFanOutput(float pwm) {
    if (pwm < 0.0f) pwm = 0.0f;
    if (pwm > 255.0f) pwm = 255.0f;
    ledcWrite(FAN_PWM_CHANNEL, (uint8_t)(pwm + 0.5f));
}

static void updateFanTarget() {
    fan_target_pwm = fan_enabled ? fanDutyForLevel(fan_speed_index) : 0.0f;
}

static void setFanState(bool enabled) {
    fan_enabled = enabled;
    updateFanTarget();
    persistFanState();
}

static void setFanLevel(uint8_t level, bool keepEnabled = true) {
    fan_speed_index = clampFanLevel(level);
    if (keepEnabled) {
        fan_enabled = true;
    }
    updateFanTarget();
    persistFanState();
}

void initFanControl() {
    pinMode(FAN_PWM_PIN, OUTPUT);
    ledcSetup(FAN_PWM_CHANNEL, FAN_PWM_FREQUENCY, FAN_PWM_RESOLUTION_BITS);
    ledcAttachPin(FAN_PWM_PIN, FAN_PWM_CHANNEL);

    fan_prefs.begin(FAN_PREF_NAMESPACE, !FAN_PERSIST_RUNTIME);
    fan_speed_index = clampFanLevel(fan_prefs.getUChar(FAN_PREF_LEVEL_KEY, fan_default_level));
    if (!FAN_PERSIST_RUNTIME) {
        fan_prefs.end();
    }
    fan_enabled = false;
    fan_current_pwm = 0.0f;
    updateFanTarget();
    writeFanOutput(fan_current_pwm);
}

void updateFanControl(float dt) {
    if (dt <= 0.0f) return;

    float err = fan_target_pwm - fan_current_pwm;
    if (fabsf(err) < 0.5f) {
        fan_current_pwm = fan_target_pwm;
        writeFanOutput(fan_current_pwm);
        return;
    }

    float max_step = FAN_PWM_RAMP_UNITS_PER_SEC * dt;
    if (fabsf(err) <= max_step) {
        fan_current_pwm = fan_target_pwm;
    } else {
        fan_current_pwm += (err > 0.0f) ? max_step : -max_step;
    }
    writeFanOutput(fan_current_pwm);
}

int active_motor = 0;  // 0 = motor 1, 1 = motor 2, 2 = both
PhaseOffset phase_offset;

void printCommandHelp() {
    Serial.println(F("\nCommands:"));
    Serial.println(F("  'q' select motor 1 (default)"));
    Serial.println(F("  'w' select motor 2 (spins opposite direction)"));
    Serial.println(F("  'e' select both motors (motor 2 spins opposite to motor 1)"));
    Serial.println(F("  '+' step angle forward (active motor)"));
    Serial.println(F("  '-' step angle backward (active motor)"));
    Serial.println(F("  'r' start/stop continuous rotation (active motor)"));
    Serial.println(F("  's' set angle to 0 (stop and reset) (active motor)"));
    Serial.println(F("  'p' advance relative phase between motors (+45 deg)"));
    Serial.println(F("  'l' retard  relative phase between motors (-45 deg)"));
    Serial.println(F("  'i' print current angle and status (active motor)"));
    Serial.println(F("  'a' print status of all motors"));

    Serial.println(F("  '0-9' set velocity (0=0RPM, 1=1000RPM, ..., 9=10000RPM)"));
    Serial.println(F("  'f'   toggle fan on/off"));
    Serial.println(F("  'F'   cycle fan speed through 100 levels"));
    Serial.println(F("  'C'   auto-calibrate voltage LUT"));
    Serial.println(F("  'Q'   manual calibration Motor 1"));
    Serial.println(F("  'W'   manual calibration Motor 2"));
    Serial.println(F("  'B'   resume manual calibration from first unrecorded step"));
    Serial.println(F("          't'/'g' = voltage +/-0.05V  |  'y'/'h' = +/-0.01V"));
    Serial.println(F("          SPACE = record and advance to next step"));
    Serial.println(F("  'x'   emergency stop: disable both motors, zero velocity"));
    Serial.println();
}

static bool affectsMotor(int idx) {
    return active_motor == idx || active_motor == 2;
}

static void printMotorStatus(MotorController& mc, int idx) {
    Serial.printf("Motor %d - Target: %.1f deg | Continuous: %s | Status: %s\n",
                  idx + 1,
                  mc.target_angle * 180.0f / PI,
                  mc.continuous_rotation ? "YES" : "NO",
                  mc.enabled ? "ENABLED" : "DISABLED");
}

void handleSerialCommand(char cmd, MotorController& mc1, MotorController& mc2, Calibration& cal) {
    MotorController* motors[] = { &mc1, &mc2 };
    if (cmd == '\r' || cmd == '\n') return;

    // Manual calibration input takes priority
    if (cmd == 't' || cmd == 'g' || cmd == 'y' || cmd == 'h' || cmd == ' ') {
        cal.handleManualInput(cmd, mc1, mc2);
        return;
    }

    // Motor selection
    if (cmd == 'q') {
        active_motor = 0;
        Serial.println(F("Active motor: 1"));
    }
    else if (cmd == 'w') {
        active_motor = 1;
        Serial.println(F("Active motor: 2"));
    }
    else if (cmd == 'e') {
        active_motor = 2;
        Serial.println(F("Active motor: BOTH"));
    }

    // Angle step forward
    else if (cmd == '+') {
        for (int i = 0; i < 2; i++) {
            if (affectsMotor(i) && motors[i]->enabled) {
                motors[i]->target_angle += motors[i]->angle_increment * motors[i]->direction;
                motors[i]->motor.target = motors[i]->target_angle;
                motors[i]->continuous_rotation = false;
            }
        }
    }

    // Angle step backward
    else if (cmd == '-') {
        for (int i = 0; i < 2; i++) {
            if (affectsMotor(i) && motors[i]->enabled) {
                motors[i]->target_angle -= motors[i]->angle_increment * motors[i]->direction;
                motors[i]->motor.target = motors[i]->target_angle;
                motors[i]->continuous_rotation = false;
            }
        }
    }

    // Toggle continuous rotation
    else if (cmd == 'r') {
        for (int i = 0; i < 2; i++) {
            if (affectsMotor(i) && motors[i]->enabled) {
                motors[i]->continuous_rotation = !motors[i]->continuous_rotation;
                Serial.printf("Motor %d: %s continuous rotation\n",
                              i + 1, motors[i]->continuous_rotation ? "Started" : "Stopped");
            }
        }
    }

    // Emergency stop
    else if (cmd == 'x') {
        if (cal.isActive()) {
            cal.abort(mc1, mc2, "user emergency stop");
        }
        mc1.emergencyStop();
        mc2.emergencyStop();
        setFanState(false);
        Serial.println(F("STOP: both motors disabled, velocity zeroed."));
    }

    // Reset angle to 0
    else if (cmd == 's') {
        for (int i = 0; i < 2; i++) {
            if (affectsMotor(i) && motors[i]->enabled) {
                motors[i]->target_angle = 0.0f;
                motors[i]->motor.target = 0.0f;
                motors[i]->continuous_rotation = false;
                Serial.printf("Motor %d: Angle reset to 0 degrees\n", i + 1);
            }
        }
    }

    // Velocity commands (0-9)
    else if (cmd >= '0' && cmd <= '9') {
        int rpm;
        if      (cmd == '0') rpm = 0;
        else                 rpm = (cmd - '0') * 2000;

        for (int i = 0; i < 2; i++) {
            if (affectsMotor(i)) {
                motors[i]->target_velocity = rpm * 2.0f * PI / 60.0f * motors[i]->direction;
                motors[i]->velocity_mode = true;
                motors[i]->continuous_rotation = false;
                Serial.printf("Motor %d velocity set to: %d RPM\n", i + 1, rpm);
            }
        }
    }

    // Phase offset adjust
    else if (cmd == 'p' || cmd == 'l') {
        float dir = (cmd == 'p') ? 1.0f : -1.0f;
        phase_offset.target += dir * PHASE_OFFSET_STEP;

        // Re-enable motors if disabled so ramp can take effect
        for (int i = 0; i < 2; i++) {
            if (!motors[i]->enabled) {
                motors[i]->resetFilterState();
                motors[i]->reached_speed = false;
                motors[i]->motor.controller = MotionControlType::angle_openloop;
                motors[i]->enable();
            }
        }

        Serial.printf("[p/l] target phase offset: %+.1f deg  (current: %+.1f deg)\n",
                      phase_offset.target * 180.0f / PI,
                      phase_offset.current * 180.0f / PI);
    }

    // Print status
    else if (cmd == 'i') {
        if (active_motor == 2) {
            printMotorStatus(mc1, 0);
            printMotorStatus(mc2, 1);
        } else {
            printMotorStatus(*motors[active_motor], active_motor);
        }
    }
    else if (cmd == 'a') {
        Serial.println(F("=== ALL MOTOR STATUS ==="));
        printMotorStatus(mc1, 0);
        printMotorStatus(mc2, 1);
        Serial.println(F("========================"));
    }

    // Fan control
    else if (cmd == 'f') {
        if (fan_enabled) {
            setFanState(false);
            Serial.println(F("Fan: OFF"));
        } else {
            setFanState(true);
            Serial.printf("Fan: ON (%d/100)\n", fan_speed_index + 1);
        }
    }
    else if (cmd == 'F') {
        uint8_t next_index = (uint8_t)((fan_speed_index + 1) % FAN_LEVEL_COUNT);
        setFanLevel(next_index, true);
        Serial.printf("Fan speed: %d/100\n", fan_speed_index + 1);
        if (!fan_enabled) {
            Serial.println(F("Fan: OFF"));
        }
    }

    // Calibration commands
    else if (cmd == 'C') {
        cal.startAuto(mc1, mc2);
    }
    else if (cmd == 'Q') {
        cal.startManual(0, mc1, mc2);
    }
    else if (cmd == 'W') {
        cal.startManual(1, mc1, mc2);
    }
    else if (cmd == 'B') {
        cal.resumeManual(mc1, mc2);
    }
}

void handleSerialCommands(MotorController& mc1, MotorController& mc2, Calibration& cal) {
    while (Serial.available()) {
        handleSerialCommand(Serial.read(), mc1, mc2, cal);
    }
}
