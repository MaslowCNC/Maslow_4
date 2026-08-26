#include "serial_commands.h"
#include "pins.h"
#include "config.h"
#include "ota_service.h"
#include <Preferences.h>
#include <string.h>

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

// Drive the fan automatically whenever local work (spindle spinning, a Z move, calibration)
// or the XY belt motors need cooling.  Called every control-loop iteration; updateFanControl()
// ramps toward the target.  Note that merely having the drivers energized is NOT enough - see
// coolingNeeded() in main.cpp.
void applyFanForMotorState(bool localCoolingNeeded) {
    if ((localCoolingNeeded || g_belt_cooling_requested) && g_suction_level > 0) {
        // Map the 0-100 suction percentage onto the fan's level index (0..FAN_LEVEL_COUNT-1).
        fan_speed_index = (uint8_t)((uint32_t)g_suction_level * (FAN_LEVEL_COUNT - 1) / 100u);
        fan_enabled     = true;
    } else {
        fan_enabled = false;
    }
    updateFanTarget();
}

int active_motor = 0;  // 0 = motor 1, 1 = motor 2, 2 = both
PhaseOffset phase_offset;
volatile uint8_t g_fault_code = 0;  // 0 = OK, 1 = DRV8316 fault, 2 = overcurrent

// Suction/cooling fan power (0-100), configured by the XY board over the link via
// the 'C' command.  The fan runs at this level whenever local motors are enabled or
// the XY board reports active belt motors.
volatile uint8_t g_suction_level = 100;
volatile bool    g_belt_cooling_requested = false;

// Set true when the XY board reports the machine is idle (the 'D' command) so the
// Z-axis BLDC drivers may be powered down once their phase move has settled.  Cleared
// as soon as any new motion command (spindle speed or Z target) arrives.
volatile bool g_hold_release_requested = false;
volatile bool g_speed_command_flag = false;
volatile bool g_home_requested = false;
volatile bool g_remove_tool_requested = false;

// Identity string returned to the XY board in response to a handshake ('H') request.
static const char* LINK_IDENTITY = "I:Maslow-Spindle,proto=1";
// Set true the first time a valid command arrives over the inter-board link, so we
// announce first contact on the USB console exactly once.
static bool g_link_seen = false;

void printCommandHelp() {
    Serial.println(F("\nInter-board link protocol (also accepted on USB, newline terminated):"));
    Serial.println(F("  'S<rpm>' set spindle speed (0 = stop)"));
    Serial.println(F("  'Z<deg>' set absolute target phase offset in degrees (Z position)"));
    Serial.println(F("  'E'      emergency stop (disable both motors)"));
    Serial.println(F("  '?'      request a status report"));
    Serial.println(F("  'H'      handshake: reply with identity string"));
    Serial.println(F("  'C<lvl>' set suction/cooling fan power (0-100); fan auto-runs while motors enabled"));
    Serial.println(F("  'D'      machine idle: power down the Z-axis drivers once the move has settled"));
    Serial.println(F("  'G'      run the Z homing cycle (raise until the top-of-travel beam breaks)"));
    Serial.println(F("  'R'      remove tool: raise the Z until the loaded tool clears the beam"));
    Serial.println(F("\nLegacy single-character commands (USB maintenance/calibration):"));
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

// ------------------- Line-based inter-board protocol -------------------

// Set spindle speed on both motors (rpm; 0 = stop).
static void setSpindleSpeed(float rpm, MotorController& mc1, MotorController& mc2) {
    if (rpm < 0.0f) rpm = 0.0f;
    if (rpm > MAX_COMMAND_RPM) rpm = MAX_COMMAND_RPM;

    g_fault_code = 0;  // a fresh command clears any latched fault
    g_speed_command_flag = true;  // let the over-current retry logic see a fresh operator command
    g_hold_release_requested = false;  // motion commanded: cancel any pending Z-hold release

    MotorController* motors[] = { &mc1, &mc2 };
    for (int i = 0; i < 2; i++) {
        motors[i]->target_velocity = rpm * 2.0f * PI / 60.0f * motors[i]->direction;
        motors[i]->velocity_mode   = true;
        motors[i]->continuous_rotation = false;
    }
}

// Set the absolute target phase offset (Z position) in degrees.
static void setPhaseTarget(float deg, MotorController& mc1, MotorController& mc2) {
    float new_target = deg * PI / 180.0f;

    // Ignore a redundant target equal to where the Z already is (e.g. the XY board
    // re-sending its Z on connect, or its home matching the post-homing zero).  Acting on
    // it would clear the idle hold-release and needlessly energize the motors, leaving them
    // locked in position with the fan running until the next idle release - which never
    // arrives while the XY board sits in its boot alarm.
    if (fabsf(new_target - phase_offset.current) <= PHASE_MOVE_COMPLETE_EPS_RAD) {
        phase_offset.target = new_target;
        return;
    }

    g_fault_code = 0;  // a fresh move clears any latched fault
    g_hold_release_requested = false;  // a new Z move cancels any pending Z-hold release

    phase_offset.target = new_target;

    // Re-enable motors if disabled so the ramp can take effect (matches 'p'/'l').
    MotorController* motors[] = { &mc1, &mc2 };
    for (int i = 0; i < 2; i++) {
        if (!motors[i]->enabled) {
            motors[i]->resetFilterState();
            motors[i]->reached_speed = false;
            motors[i]->motor.controller = MotionControlType::angle_openloop;
            motors[i]->enable();
        }
    }
}

void sendStatus(Stream& out, MotorController& mc1, MotorController& mc2) {
    const char* state = g_fault_code ? "FAULT" : ((mc1.enabled || mc2.enabled) ? "RUN" : "IDLE");
    float phase_deg = phase_offset.current * 180.0f / PI;
    float rpm = fabsf(mc1.enabled ? mc1.current_velocity : 0.0f) * 60.0f / (2.0f * PI);
    out.printf("T:%s,P:%.1f,R:%.0f,F:%u\n", state, phase_deg, rpm, (unsigned)g_fault_code);
}

// Process one complete command line from either the USB or the inter-board link.
// allowLegacy is true only for the USB console: single-character maintenance /
// calibration commands are NEVER honored on the inter-board link, so electrical
// noise on the link cannot toggle the fan, start a calibration, or move a motor.
static void processCommandLine(const char* line, size_t len,
                               MotorController& mc1, MotorController& mc2, Calibration& cal,
                               Stream& reply, bool allowLegacy) {
    if (len == 0) return;

    // Announce first contact over the link on the USB console (once).
    if (!allowLegacy && !g_link_seen) {
        g_link_seen = true;
        Serial.printf("Link: first command received from XY board: '%s'\n", line);
    }

    // USB-only maintenance word-commands.  These are multi-character so they do not
    // collide with the single-letter inter-board protocol (e.g. auto-calibration 'C'
    // would otherwise be shadowed by the 'C' fan-power command).  Never honored on the
    // link so line noise cannot start a calibration.
    if (allowLegacy) {
        if (strcmp(line, "CAL") == 0) {
            // Force the cooling/suction fan to full for the whole calibration run so the
            // motors and drivers stay cool while sweeping to high RPM.
            g_suction_level = 100;
            Serial.println(F("Cooling/suction fan forced ON (100%) for calibration."));
            cal.startAuto(mc1, mc2);
            return;
        }
        if (strcmp(line, "DUMP") == 0) {
            Serial.println(F("\n===== MOTOR 1 voltage LUT ====="));
            cal.printResults(mc1);
            Serial.println(F("\n===== MOTOR 2 voltage LUT ====="));
            cal.printResults(mc2);
            return;
        }
    }

    char cmd = line[0];

    // While a calibration sweep is running, ignore motion setpoints arriving over the
    // inter-board link so the XY board cannot disturb it.  Emergency stop ('E') is left
    // working on purpose.
    if (!allowLegacy && cal.isActive() && (cmd == 'S' || cmd == 'Z')) {
        return;
    }

    switch (cmd) {
        case 'S':  // set spindle speed
            setSpindleSpeed(strtof(line + 1, nullptr), mc1, mc2);
            break;
        case 'Z':  // set absolute target phase offset (Z position)
            setPhaseTarget(strtof(line + 1, nullptr), mc1, mc2);
            break;
        case 'E':  // emergency stop
            handleSerialCommand('x', mc1, mc2, cal);
            break;
        case '?':  // status request
            sendStatus(reply, mc1, mc2);
            break;
        case 'C': {  // set suction/cooling fan power (0-100)
            long lvl = strtol(line + 1, nullptr, 10);
            if (lvl < 0) lvl = 0;
            if (lvl > 100) lvl = 100;
            g_suction_level = (uint8_t)lvl;
            break;
        }
        case 'B':  // XY belt motors active: request vacuum cooling
            g_belt_cooling_requested = line[1] == '1';
            break;
        case 'D':  // machine idle: power the Z-axis drivers down once the move has settled
            g_hold_release_requested = true;
            break;
        case 'G':  // (re-)run the Z homing cycle (raise until the top-of-travel beam breaks)
            g_home_requested = true;
            break;
        case 'R':  // remove tool: raise the Z until the loaded tool clears the beam
            g_remove_tool_requested = true;
            break;
        case 'H':  // handshake / identity request
            reply.print(LINK_IDENTITY);
            reply.print('\n');
            if (!allowLegacy) {
                Serial.println(F("Link: handshake request received from XY board -> replied identity"));
            }
            break;
        case 'W': {  // enable WiFi + OTA using credentials pushed by the XY board
            // Payload is "<ssid>\t<password>" (tab-delimited, since neither field can
            // contain a tab).  The XY board sends this in response to $Spindle/EnableOTA.
            const char* rest = line + 1;
            const char* tab  = strchr(rest, '\t');
            if (!tab) {
                // No "<ssid>\t<pass>" payload -> not an OTA request.  On the USB console a
                // bare 'W' is the legacy manual-calibration (Motor 2) command; the link
                // never triggers calibration, so ignore it there.
                if (allowLegacy && len == 1) {
                    handleSerialCommand('W', mc1, mc2, cal);
                }
                break;
            }
            char   ssid[33];
            char   pass[65];
            size_t slen = (size_t)(tab - rest);
            if (slen > sizeof(ssid) - 1) {
                slen = sizeof(ssid) - 1;
            }
            memcpy(ssid, rest, slen);
            ssid[slen] = '\0';
            strncpy(pass, tab + 1, sizeof(pass) - 1);
            pass[sizeof(pass) - 1] = '\0';
            requestSpindleOTA(ssid, pass);
            if (!allowLegacy) {
                Serial.println(F("Link: OTA enable request received from XY board"));
            }
            break;
        }
        default:
            // Single-character legacy commands are honored ONLY on the USB console.
            if (allowLegacy && len == 1) {
                handleSerialCommand(cmd, mc1, mc2, cal);
            }
            break;
    }
}

// Shared line-buffer size for the USB console and the inter-board link.  It must be
// large enough for the longest command, which is the 'W' OTA request carrying the XY
// board's WiFi SSID (<=32) and password (<=63) separated by a tab.
static constexpr size_t CMD_LINE_BUF_SIZE = 160;

// Accumulate bytes from a stream into a line buffer and dispatch on newline.
static void feedStream(Stream& in, char* buf, size_t& len,
                       MotorController& mc1, MotorController& mc2, Calibration& cal,
                       bool allowLegacy) {
    while (in.available()) {
        char c = (char)in.read();
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            buf[len] = '\0';
            processCommandLine(buf, len, mc1, mc2, cal, in, allowLegacy);
            len = 0;
        } else if (len < CMD_LINE_BUF_SIZE - 1) {
            buf[len++] = c;
        } else {
            len = 0;  // overflow: drop the malformed line
        }
    }
}

void handleSerialCommands(MotorController& mc1, MotorController& mc2, Calibration& cal) {
    static char   usb_buf[CMD_LINE_BUF_SIZE];
    static size_t usb_len = 0;
    static char   link_buf[CMD_LINE_BUF_SIZE];
    static size_t link_len = 0;

    feedStream(Serial, usb_buf, usb_len, mc1, mc2, cal, /*allowLegacy=*/true);
    feedStream(Serial1, link_buf, link_len, mc1, mc2, cal, /*allowLegacy=*/false);
}
