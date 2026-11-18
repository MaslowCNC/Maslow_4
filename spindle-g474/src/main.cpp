// Spindle firmware for the 36 V Unified Control Board: STM32G474 + 2 x DRV8300 + 12 FETs driving
// the two spindle motors sensorless.  The Z axis is the relative angle of the two rotors.
//
// This is a port of the ESP32 spindle firmware (spindle/src) to the G474: the XY <-> spindle link
// protocol, the status line, the Z/tool state machine (power-up homing, tool load and removal on
// the top-of-travel beam), the hold release, the fan and the latched-fault semantics are the
// same, so the XY board needs no protocol change.  What changed is the drive underneath:
//   - FOC runs in the ADC interrupt at 20 kHz on the board's own shunts (foc.cpp);
//   - standstill and spin-up drive a CURRENT vector (I/f) instead of the calibrated voltage LUT;
//   - above 2000 RPM the motors run closed loop on the MXLEMMING flux observer (from the ESC1 bench),
//     with a Z loop holding the two rotors' angle sum;
//   - protection: comparator break in hardware, per-sample software trip, RMS trip, bus voltage,
//     NTC temperature, observer lock, closed-loop Z error.
// There is no USB or WiFi on this MCU: OTA requests are refused and the board is flashed from the
// XY board through the ROM bootloader (or SWD).  BRINGUP.md lists what has not run on hardware.

#include <Arduino.h>
#include <stdarg.h>
#include "board.h"
#include "config.h"
#include "hw.h"
#include "foc.h"
#include "link.h"
#include "trip.h"

HardwareSerial Link(PA_3_ALT1, PA_2_ALT1);   // USART2: RX PA3, TX PA2 (the ROM bootloader's port too)

static const char* LINK_IDENTITY = "I:Maslow-Spindle,proto=1";
static const char* FW_VERSION    = "g474-0.1";

// ------------------------------------------------------------------ link output

bool linkWriteIfRoom(const char* line, int n) {
    if (n <= 0) return true;
    if (Link.availableForWrite() < n) return false;
    Link.write(reinterpret_cast<const uint8_t*>(line), n);
    return true;
}

void linkEvent(const char* level, const char* fmt, ...) {
    char    buf[240];   // the XY board reads lines up to 400 bytes (SpindleBoard _rx_buf)
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    char line[256];
    int  n = snprintf(line, sizeof(line), "%s:%s\n", level, buf);
    if (n >= (int)sizeof(line)) {
        n            = sizeof(line) - 1;
        line[n - 1] = '\n';
    }
    linkWriteIfRoom(line, n);
}

// ------------------------------------------------------------------ shared state (as the ESP32's)

// 0 = OK, 1 = driver hardware fault (here: bus voltage, over-temperature, init failure),
// 2 = over-current / lost-rotor stop.  Latched: the XY board alarms on 0 -> non-zero only.
static uint8_t g_fault_code         = 0;
static bool    g_z_reference_valid  = false;
static uint8_t g_suction_level      = 100;
static bool    g_belt_cooling       = false;
static bool    g_hold_release_requested = false;
static bool    g_home_requested     = false;
static bool    g_remove_tool_requested = false;
static bool    g_spinning           = false;   // the ESP32's velocity_mode: a spindle speed is in force
static bool    g_init_ok            = false;
static const char* g_init_why       = "";
static uint32_t g_enable_allowed_ms = 0;       // coast lockout after a stop from speed
static uint8_t g_load_warn_mask     = 0;

// ------------------------------------------------------------------ helpers

static float rad2deg(float r) { return r * 180.0f / (float)M_PI; }

static bool beamBlocked() { return digitalRead(PIN_BEAM_DETECT) != 0; }

static void notifyZHomed() {
    linkWriteIfRoom("ZHOMED\n", 7);
}

// Enable the drive (open loop at the angles it last had) if it is safe to.
static bool enableDrive(const char* why) {
    if (foc::enabled()) return true;
    if (!g_init_ok) return false;
    uint32_t now = millis();
    if ((int32_t)(g_enable_allowed_ms - now) > 0) return false;   // still coasting down
    float vb = hw::vbus();
    if (vb < VBUS_MIN_V || vb > VBUS_MAX_V) {
        static uint32_t last = 0;
        if (now - last > 2000) {
            last = now;
            linkEvent("WARN", "%s refused: bus %.1f V outside %.0f-%.0f V", why, vb, VBUS_MIN_V, VBUS_MAX_V);
        }
        return false;
    }
    if (hw::breakLatched(0) || hw::breakLatched(1)) {
        if (!hw::clearBreak(0) || !hw::clearBreak(1)) {
            linkEvent("WARN", "%s refused: hardware over-current comparator still tripped (0x%02x)", why,
                      hw::comparatorOutputs());
            return false;
        }
    }
    return foc::enable();
}

static void disableDrive(bool from_speed) {
    bool was = foc::enabled();
    float rpm = foc::speedCommandRpm();
    foc::disable();
    // A drive turned off at speed coasts: do not catch the spinning rotors with a zero vector.
    if (was && from_speed && rpm > 100.0f) g_enable_allowed_ms = millis() + COAST_LOCKOUT_MS;
}

// ------------------------------------------------------------------ machine state (verbatim port)

enum class MachineState : uint8_t {
    Booting, NeedsHoming, Homing, IdleToolLoaded, IdleToolUnloaded, LoadingTool, UnloadingTool,
    SpindleRunning, Calibrating, Fault, OtaUpdate,
};

static MachineState g_tool_state    = MachineState::Booting;
static MachineState g_machine_state = MachineState::Booting;

static const char* machineStateName(MachineState s) {
    switch (s) {
        case MachineState::Booting: return "Booting";
        case MachineState::NeedsHoming: return "Needs Homing";
        case MachineState::Homing: return "Homing";
        case MachineState::IdleToolLoaded: return "Idle - Tool Loaded";
        case MachineState::IdleToolUnloaded: return "Idle - Tool Unloaded";
        case MachineState::LoadingTool: return "Loading Tool";
        case MachineState::UnloadingTool: return "Unloading Tool";
        case MachineState::SpindleRunning: return "Spindle On";
        case MachineState::Calibrating: return "Calibrating";
        case MachineState::Fault: return "Fault";
        case MachineState::OtaUpdate: return "OTA Update";
    }
    return "?";
}

static void setMachineState(MachineState s) {
    if (s == g_machine_state) return;
    MachineState prev = g_machine_state;
    g_machine_state   = s;
    linkEvent("MSG", "state: %s -> %s", machineStateName(prev), machineStateName(s));
}

static float    g_z_homing_start_phase   = 0.0f;
static float    g_z_load_start_phase     = 0.0f;
static bool     g_beam_prev_blocked      = false;
static bool     g_tool_load_confirming   = false;
static uint32_t g_tool_load_clear_since  = 0;
static bool     g_tool_remove_confirming = false;
static uint32_t g_tool_remove_clear_since = 0;
static bool     g_tool_remove_seen_blocked = false;

static void requireRehome() {
    g_z_reference_valid = false;
    g_tool_state        = MachineState::NeedsHoming;
}

// Stop for an over-current or a lost rotor: the relative phase (the Z) no longer means anything.
// Fault code 2 stays latched until a fresh non-zero speed or Z move (as on the ESP32).
static void stopForOverCurrent(const char* detail) {
    tripTrigger(detail);
    disableDrive(true);
    g_spinning = false;
    foc::setSpeedTarget(0);
    requireRehome();
    g_fault_code = 2;
    linkEvent("WARN", "%s - spindle stopped; re-home the Z, then send a new speed", detail);
}

// A hardware condition (bus voltage, temperature): fault code 1.
static void stopForDriverFault(const char* detail) {
    tripTrigger(detail);
    disableDrive(true);
    g_spinning = false;
    foc::setSpeedTarget(0);
    requireRehome();
    g_fault_code = 1;
    linkEvent("ERR", "%s", detail);
}

static void enableZMotors() {
    if (!foc::enabled()) enableDrive("Z move");
}

static void finishZMove() {
    disableDrive(false);
    foc::zeroPhase();
    g_beam_prev_blocked = beamBlocked();
    g_z_reference_valid = true;
    notifyZHomed();
}

static void beginHoming() {
    float cur = foc::phaseCurrent();
    if (beamBlocked()) {
        g_z_load_start_phase   = cur;
        foc::setPhaseTarget(cur - Z_HOMING_PHASE_DIR * Z_TOOL_LOAD_MAX_RAD);
        g_tool_load_confirming = false;
        enableZMotors();
        g_tool_state = MachineState::LoadingTool;
        linkEvent("MSG", "Z homing: beam already interrupted, lowering Z until the beam clears");
        return;
    }
    g_z_homing_start_phase = cur;
    foc::setPhaseTarget(cur + Z_HOMING_PHASE_DIR * Z_HOMING_MAX_RAD);
    enableZMotors();
    g_tool_state = MachineState::Homing;
    linkEvent("MSG", "Z homing: raising to find the top-of-travel beam");
}

static void beginToolRemoval() {
    float cur                  = foc::phaseCurrent();
    g_z_load_start_phase       = cur;
    foc::setPhaseTarget(cur + Z_HOMING_PHASE_DIR * Z_TOOL_REMOVE_MAX_RAD);
    g_beam_prev_blocked        = beamBlocked();
    g_tool_remove_confirming   = false;
    g_tool_remove_seen_blocked = beamBlocked();
    enableZMotors();
    g_tool_state = MachineState::UnloadingTool;
    linkEvent("MSG", "Tool removal: raising Z to eject any loaded tool");
}

static void stopZHere() { foc::setPhaseTarget(foc::phaseCurrent()); }

static void updateToolStateMachine() {
    if (g_home_requested) {
        g_home_requested = false;
        g_tool_state     = MachineState::Booting;
        linkEvent("MSG", "Z home requested (ota=0 cal=0 fault=%d beam=%d)", (int)g_fault_code, (int)beamBlocked());
    }
    // Never home or move a tool during a HARD fault (code 1).  An over-current stop (2) does not
    // block homing: re-homing is how the operator recovers the Z reference.
    if (g_fault_code == 1 || !g_init_ok) return;
    // The drive is in its coast lockout after a stop at speed: wait (the Z must not jerk the
    // still-turning rotors), the state machine resumes once the drive can be enabled.
    if ((int32_t)(g_enable_allowed_ms - millis()) > 0 &&
        (g_tool_state == MachineState::Booting || g_remove_tool_requested))
        return;

    if (g_remove_tool_requested) {
        g_remove_tool_requested = false;
        beginToolRemoval();
        return;
    }

    float cur = foc::phaseCurrent();
    switch (g_tool_state) {
        case MachineState::NeedsHoming: break;

        case MachineState::Booting:
            if (millis() < BOOT_HOMING_DELAY_MS) break;
            beginHoming();
            break;

        case MachineState::Homing:
            if (beamBlocked()) {
                stopZHere();
                g_z_reference_valid = true;
                g_tool_state        = MachineState::IdleToolLoaded;
                linkEvent("MSG", "Z homed: top-of-travel beam interrupted, tool loaded");
                finishZMove();
            } else if (fabsf(cur - g_z_homing_start_phase) >= Z_HOMING_MAX_RAD) {
                stopZHere();
                g_tool_state = MachineState::IdleToolUnloaded;
                linkEvent("WARN", "Z homing: no beam within %.0fmm - no tool loaded", Z_HOMING_MAX_MM);
                finishZMove();
            }
            break;

        case MachineState::IdleToolUnloaded: {
            bool blocked = beamBlocked();
            if (blocked && !g_beam_prev_blocked) {
                g_z_load_start_phase = cur;
                foc::setPhaseTarget(cur - Z_HOMING_PHASE_DIR * Z_TOOL_LOAD_MAX_RAD);
                g_tool_load_confirming = false;
                enableZMotors();
                g_tool_state = MachineState::LoadingTool;
                linkEvent("MSG", "Tool loading: beam interrupted, lowering Z until the beam clears");
            }
            g_beam_prev_blocked = blocked;
            break;
        }

        case MachineState::LoadingTool: {
            bool blocked   = beamBlocked();
            bool hit_limit = fabsf(cur - g_z_load_start_phase) >= Z_TOOL_LOAD_MAX_RAD;
            if (blocked) {
                g_tool_load_confirming = false;
                if (hit_limit) {
                    stopZHere();
                    g_tool_state = MachineState::IdleToolUnloaded;
                    linkEvent("WARN", "Tool loading: beam still blocked after %.0fmm - aborting", Z_TOOL_LOAD_MAX_MM);
                    finishZMove();
                }
            } else {
                if (!g_tool_load_confirming) {
                    g_tool_load_confirming  = true;
                    g_tool_load_clear_since = millis();
                }
                if ((millis() - g_tool_load_clear_since) >= Z_TOOL_LOAD_CONFIRM_MS || hit_limit) {
                    stopZHere();
                    g_tool_state = MachineState::IdleToolLoaded;
                    linkEvent("MSG", "Tool loaded: beam clear for %ums", (unsigned)Z_TOOL_LOAD_CONFIRM_MS);
                    finishZMove();
                }
            }
            break;
        }

        case MachineState::UnloadingTool: {
            bool blocked   = beamBlocked();
            bool hit_limit = fabsf(cur - g_z_load_start_phase) >= Z_TOOL_REMOVE_MAX_RAD;
            if (blocked) {
                g_tool_remove_seen_blocked = true;
                g_tool_remove_confirming   = false;
                if (hit_limit) {
                    stopZHere();
                    g_tool_state = MachineState::IdleToolLoaded;
                    linkEvent("WARN", "Tool removal: beam still blocked after %.0fmm - stopping", Z_TOOL_REMOVE_MAX_MM);
                    finishZMove();
                }
            } else if (!g_tool_remove_seen_blocked) {
                if (hit_limit) {
                    stopZHere();
                    g_tool_state = MachineState::IdleToolUnloaded;
                    linkEvent("MSG", "Tool removal: no tool detected within %.0fmm - already unloaded",
                              Z_TOOL_REMOVE_MAX_MM);
                    finishZMove();
                }
            } else {
                if (!g_tool_remove_confirming) {
                    g_tool_remove_confirming  = true;
                    g_tool_remove_clear_since = millis();
                }
                if ((millis() - g_tool_remove_clear_since) >= Z_TOOL_REMOVE_CONFIRM_MS || hit_limit) {
                    stopZHere();
                    g_tool_state = MachineState::IdleToolUnloaded;
                    linkEvent("MSG", "Tool removed: beam clear for %ums", (unsigned)Z_TOOL_REMOVE_CONFIRM_MS);
                    finishZMove();
                }
            }
            break;
        }

        case MachineState::IdleToolLoaded:
        default: break;
    }
}

static void updateReportedState() {
    MachineState reported;
    if (g_fault_code != 0) reported = MachineState::Fault;
    else if (g_spinning) reported = MachineState::SpindleRunning;
    else reported = g_tool_state;
    setMachineState(reported);
}

// ------------------------------------------------------------------ link commands

static void setSpindleSpeed(float rpm) {
    if (rpm < 0.0f) rpm = 0.0f;
    if (rpm > MAX_COMMAND_RPM) rpm = MAX_COMMAND_RPM;
    if (rpm > 0.0f) g_fault_code = 0;   // a fresh non-zero speed is the deliberate restart
    if (rpm > 0.0f) tripArmOnStart();
    // S0 only spins down a spindle that is spinning (the XY board sends S0 on every boot).
    if (rpm == 0.0f && !g_spinning) return;
    g_hold_release_requested = false;
    g_spinning               = true;
    foc::setSpeedTarget(rpm);
}

static void setPhaseTargetDeg(float deg) {
    float new_target = deg * (float)M_PI / 180.0f;
    if (!g_z_reference_valid) {
        static uint32_t last = 0;
        uint32_t        now  = millis();
        if (last == 0 || now - last > 5000) {
            last = now ? now : 1;
            linkEvent("WARN", "Z move to %.1f deg refused - Z not homed since the over-current stop", deg);
        }
        return;
    }
    if (fabsf(new_target - foc::phaseCurrent()) <= PHASE_MOVE_COMPLETE_EPS_RAD) {
        foc::setPhaseTarget(new_target);
        return;
    }
    g_fault_code             = 0;
    g_hold_release_requested = false;
    foc::setPhaseTarget(new_target);
    enableZMotors();
}

static void sendStatus() {
    const char* state = g_fault_code ? "FAULT" : (foc::enabled() ? "RUN" : "IDLE");
    float       rpm   = foc::enabled() ? foc::speedCommandRpm() : 0.0f;
    char        line[64];
    int         n = snprintf(line, sizeof(line), "T:%s,P:%.1f,R:%.0f,F:%u,L:%u\n", state, rad2deg(foc::phaseCurrent()),
                             rpm, (unsigned)g_fault_code, (unsigned)g_load_warn_mask);
    linkWriteIfRoom(line, n);
}

static uint32_t g_align_until  = 0;
static uint32_t g_stat_every   = 0;
static uint32_t g_stat_last    = 0;

static void printStat() {
    foc::MotorTelemetry t[2] = { foc::take(0), foc::take(1) };
    uint32_t            isr_max, isr_n;
    foc::takeIsrStats(&isr_max, &isr_n);
    const char* mode = foc::mode() == foc::Mode::Off ? "off" : foc::mode() == foc::Mode::Open ? "open" : "closed";
    linkEvent("MSG", "STAT %s cmd %.0f/%.0f rpm phi %.1f/%.1f deg zerr %.1f deg vbus %.1f V T %.0f/%.0f C isr %lu us",
              mode, foc::speedCommandRpm(), foc::speedTargetRpm(), rad2deg(foc::phaseCurrent()),
              rad2deg(foc::phaseTarget()), rad2deg(foc::zErrorRad()), hw::vbus(), hw::temperature(0),
              hw::temperature(1), (unsigned long)isr_max);
    for (int k = 0; k < 2; k++) {
        linkEvent("MSG", "STAT M%d i %.2f %.2f %.2f dq %.2f %.2f v %.2f %.2f obs %.0f rpm flux %.2f max %.1f A", k + 1,
                  t[k].ia, t[k].ib, t[k].ic, t[k].id, t[k].iq, t[k].vd, t[k].vq, t[k].obs_rpm, t[k].obs_flux, t[k].imax);
    }
}

// Maintenance commands, "#..." on the link ($Spindle/G474=SEND <line> on the XY board passes them through).
// The ESP32 firmware ignores lines starting with '#', so the XY side can send them to either board.
static void maintenance(const char* line) {
    foc::Tuning& tu = foc::tuning();
    float        a = 0, b = 0, c = 0;
    char         word[12] = { 0 };
    int          n = sscanf(line + 1, "%11s %f %f %f", word, &a, &b, &c);
    if (n < 1) return;
    if (!strcmp(word, "STAT")) {
        printStat();
    } else if (!strcmp(word, "LOG")) {
        g_stat_every = n >= 2 && a > 0 ? (uint32_t)a : 0;
    } else if (!strcmp(word, "ALIGN")) {
        float v = n >= 2 ? a : 0.5f;
        if (g_spinning || foc::enabled()) {
            linkEvent("WARN", "ALIGN refused: the drive is enabled");
        } else if (!enableDrive("ALIGN") || (foc::disable(), !foc::startAlignTest(v))) {
            linkEvent("WARN", "ALIGN refused (0.1-3 V, drive must be enable-able)");
        } else {
            g_align_until = millis() + 300;
            linkEvent("MSG", "ALIGN: %.2f V on the phase A axis of both motors for 300 ms", v);
        }
    } else if (!strcmp(word, "CL")) {
        tu.closed_loop_allowed = a != 0;
    } else if (!strcmp(word, "CUR") && n >= 3) {
        tu.cur_kp = a;
        tu.cur_ki = b;
    } else if (!strcmp(word, "POS") && n >= 4) {
        tu.pos_kp = a;
        tu.pos_kd = b;
        tu.pos_ki = c;
    } else if (!strcmp(word, "POSBW") && n >= 3 && a > 0 && a <= 1 && b > CL_EXIT_RPM && b <= MAX_COMMAND_RPM) {
        tu.pos_bw_min      = a;
        tu.pos_bw_full_rpm = b;
    } else if (!strcmp(word, "OLKI") && n >= 2 && a >= 0 && a <= 1000) {
        tu.ol_ki = a;
    } else if (!strcmp(word, "DTC") && n >= 2 && a >= 0 && a <= 1000) {
        tu.deadtime_ns = a;
    } else if (!strcmp(word, "IHOLD") && n >= 2 && a >= 0 && a <= 6) {
        tu.hold_a = a;
    } else if (!strcmp(word, "ISPIN") && n >= 2 && a >= 0 && a <= 8) {
        tu.spin_a = a;
    } else if (!strcmp(word, "IBOOST") && n >= 2 && a >= 0 && a <= 4) {
        tu.boost_a = a;
    } else if (!strcmp(word, "ILIM") && n >= 2 && a > 0 && a <= 15) {
        tu.iq_limit = a;
    } else if (!strcmp(word, "OBS") && n >= 4) {
        tu.r    = a;
        tu.l    = b;
        tu.flux = c;
    } else if (!strcmp(word, "TRIP") && n >= 2) {
        if (a > 0 && a <= 20) tu.sw_trip_a = a;
        if (n >= 3 && b > 0 && b <= 21) hw::setHardwareTripAmps(b);
    } else if (!strcmp(word, "VER")) {
        linkEvent("MSG", "spindle firmware %s, %s %s", FW_VERSION, __DATE__, __TIME__);
    } else {
        linkEvent("MSG", "# commands: STAT LOG <ms> ALIGN [V] CL <0|1> CUR <kp> <ki> POS <kp> <kd> <ki> "
                         "POSBW <min> <full rpm> OLKI <ki> DTC <ns> IHOLD/ISPIN/IBOOST/ILIM <A> OBS <R> <L> <flux> "
                         "TRIP <sw A> [hw A] VER");
        return;
    }
    if (strcmp(word, "STAT") && strcmp(word, "VER") && strcmp(word, "ALIGN")) {
        linkEvent("MSG", "tuning: cur %.3f/%.0f pos %.2f/%.3f/%.1f bw %.2f/%.0f olki %.1f I hold %.1f spin %.1f "
                         "boost %.1f lim %.1f obs R %.3f L %.1fuH flux %.2fmWb trip %.1f A dtc %.0f ns CL %d",
                  tu.cur_kp, tu.cur_ki, tu.pos_kp, tu.pos_kd, tu.pos_ki, tu.pos_bw_min, tu.pos_bw_full_rpm, tu.ol_ki,
                  tu.hold_a, tu.spin_a, tu.boost_a, tu.iq_limit,
                  tu.r, tu.l * 1e6f, tu.flux * 1e3f, tu.sw_trip_a, tu.deadtime_ns, (int)tu.closed_loop_allowed);
    }
}

static void processLine(const char* line, size_t len) {
    if (len == 0) return;
    switch (line[0]) {
        case 'S': setSpindleSpeed(strtof(line + 1, nullptr)); break;
        case 'Z': setPhaseTargetDeg(strtof(line + 1, nullptr)); break;
        case 'E':
            disableDrive(true);
            g_spinning = false;
            foc::setSpeedTarget(0);
            break;
        case '?': sendStatus(); break;
        case 'C': {
            long lvl = strtol(line + 1, nullptr, 10);
            g_suction_level = (uint8_t)(lvl < 0 ? 0 : lvl > 100 ? 100 : lvl);
            break;
        }
        case 'B': g_belt_cooling = line[1] == '1'; break;
        case 'D': g_hold_release_requested = true; break;
        case 'G': g_home_requested = true; break;
        case 'R': g_remove_tool_requested = true; break;
        case 'T':
            if (line[1] == '!') tripLinkTest();
            else tripRequestLinkDump();
            break;
        case 'H':
            linkWriteIfRoom(LINK_IDENTITY, strlen(LINK_IDENTITY));
            linkWriteIfRoom("\n", 1);
            break;
        case 'W':
            // The ESP32 joined WiFi for OTA.  This MCU has no radio: the XY board flashes it.
            {
                static const char msg[] = "OTA_ERR:the 36 V spindle (STM32G474) has no WiFi - update it with $Spindle/G474=FLASH <file.bin>\n";
                linkWriteIfRoom(msg, sizeof(msg) - 1);
            }
            break;
        case '#': maintenance(line); break;
        default: break;   // unknown: ignored, as the ESP32 does on the link
    }
}

static void pollLink() {
    static char   buf[160];
    static size_t len = 0;
    while (Link.available()) {
        char ch = (char)Link.read();
        if (ch == '\r') continue;
        if (ch == '\n') {
            buf[len] = 0;
            processLine(buf, len);
            len = 0;
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = ch;
        } else {
            len = 0;
        }
    }
}

// ------------------------------------------------------------------ fan (VAC_CONTROL)

static float g_fan_pwm = 0;

static void updateFan(float dt) {
    bool  on     = (foc::enabled() || g_belt_cooling) && g_suction_level > 0;
    float target = 0;
    if (on) {
        uint32_t level = (uint32_t)g_suction_level * 99u / 100u;   // 0..99, as the ESP32's 100 levels
        target         = FAN_MIN_DUTY + (float)(FAN_MAX_DUTY - FAN_MIN_DUTY) * level / 99.0f;
    }
    g_fan_pwm = target > g_fan_pwm ? fminf(g_fan_pwm + FAN_RAMP_UNITS_PER_S * dt, target)
                                   : fmaxf(g_fan_pwm - FAN_RAMP_UNITS_PER_S * dt, target);
    hw::setFanDuty((uint8_t)(g_fan_pwm + 0.5f));
}

// ------------------------------------------------------------------ drive supervision

static uint32_t g_lock_since      = 0;
static uint32_t g_at_handover_ms  = 0;
static uint32_t g_closed_since    = 0;
static bool     g_handback_pending = false;
static uint32_t g_lost_since      = 0;
static uint32_t g_rms_over_since  = 0;
static float    g_rms[2]          = { 0, 0 };
static uint32_t g_load_since[2]   = { 0, 0 };
static uint32_t g_last_z_move_ms  = 0;
static float    g_last_phase      = 0;

static void superviseSpin() {
    uint32_t now = millis();

    // Spin-up enables the drive (after any coast lockout); ramped to zero = power down.
    if (g_spinning) {
        if (!foc::enabled() && foc::speedTargetRpm() > 0) enableDrive("spindle start");
        if (foc::enabled() && foc::speedTargetRpm() == 0 && foc::speedCommandRpm() == 0 &&
            foc::mode() == foc::Mode::Open) {
            // Spun down.  Power down as the ESP32 did - unless a Z move is still under way, which
            // the hold release ('D') then ends once it has settled.
            if (fabsf(foc::phaseTarget() - foc::phaseCurrent()) < PHASE_MOVE_COMPLETE_EPS_RAD) disableDrive(false);
            g_spinning = false;
        }
        if (!foc::enabled() && foc::speedTargetRpm() == 0) g_spinning = false;
    }

    // Open -> closed: hold at the hand-over speed until both observers track.
    foc::Tuning& tu = foc::tuning();
    if (foc::enabled() && foc::mode() == foc::Mode::Open && tu.closed_loop_allowed &&
        foc::speedTargetRpm() >= CL_ENTER_RPM && foc::speedCommandRpm() >= CL_ENTER_RPM - 1.0f) {
        if (!g_at_handover_ms) g_at_handover_ms = now;
        if (foc::observersTracking(CL_LOCK_TOL)) {
            if (!g_lock_since) g_lock_since = now;
            else if (now - g_lock_since >= CL_LOCK_MS) {
                foc::requestClosedLoop();
                linkEvent("MSG", "closed loop: observers locked at %.0f RPM", foc::speedCommandRpm());
                g_closed_since = now;
                g_lost_since   = 0;
                g_lock_since   = 0;
                g_at_handover_ms = 0;
            }
        } else {
            g_lock_since = 0;
        }
        if (g_at_handover_ms && now - g_at_handover_ms > CL_LOCK_TIMEOUT_MS) {
            g_at_handover_ms = 0;
            stopForOverCurrent("observer did not lock at the closed-loop hand-over speed");
            return;
        }
    } else {
        g_at_handover_ms = 0;
        g_lock_since     = 0;
    }

    if (foc::mode() != foc::Mode::Closed) g_handback_pending = false;
    if (foc::enabled() && foc::mode() == foc::Mode::Closed) {
        if (foc::speedTargetRpm() < CL_EXIT_RPM && foc::speedCommandRpm() <= CL_EXIT_RPM) {
            // The interrupt switches on its next pass; announce it once.
            if (!g_handback_pending) linkEvent("MSG", "open loop below %.0f RPM", CL_EXIT_RPM);
            g_handback_pending = true;
            foc::requestOpenLoop();
            return;
        }
        if (now - g_closed_since > CL_SETTLE_MS) {
            if (foc::maxSpeedErrorFraction() > LOST_FRACTION) {
                if (!g_lost_since) g_lost_since = now;
                else if (now - g_lost_since > LOST_MS) {
                    stopForOverCurrent("observer lost lock (speed off the command)");
                    return;
                }
            } else {
                g_lost_since = 0;
            }
        }
    }

    // A rotor too far off its reference angle has slipped (open loop: past pull-out) or the closed
    // loop has lost it: the Z is no longer known.
    if (foc::enabled() && foc::mode() != foc::Mode::Off) {
        float lim = foc::mode() == foc::Mode::Closed ? CL_ANGLE_TRIP_RAD : OL_SLIP_TRIP_RAD;
        if (foc::maxAngleErrorRad() > lim) {
            char d[96];
            snprintf(d, sizeof(d), "a rotor fell %.0f deg off its reference angle in %s loop (slip)",
                     rad2deg(foc::maxAngleErrorRad()), foc::mode() == foc::Mode::Closed ? "closed" : "open");
            stopForOverCurrent(d);
            return;
        }
    }

    // Z move flag: a phase ramp in progress, or the phase moved within Z_MOVE_HOLD_MS.
    float phase = foc::phaseCurrent();
    if (fabsf(foc::phaseTarget() - phase) > PHASE_MOVE_COMPLETE_EPS_RAD || phase != g_last_phase) g_last_z_move_ms = now;
    g_last_phase = phase;
    foc::setZMoving(now - g_last_z_move_ms < Z_MOVE_HOLD_MS);

    // Hold release ('D'): power down once the Z move has settled, never while spinning.
    if (g_hold_release_requested && !g_spinning &&
        fabsf(foc::phaseTarget() - foc::phaseCurrent()) < PHASE_MOVE_COMPLETE_EPS_RAD && g_align_until == 0) {
        if (foc::enabled()) disableDrive(false);
        g_hold_release_requested = false;
    }
}

// Every 2 ms: telemetry, RMS protection, load warnings, trip recorder.
static void sample2ms(uint32_t now) {
    foc::MotorTelemetry t[2] = { foc::take(0), foc::take(1) };
    uint32_t            isr_max, isr_n;
    foc::takeIsrStats(&isr_max, &isr_n);

    bool over = false;
    for (int k = 0; k < 2; k++) {
        float rms = t[k].n ? sqrtf(t[k].i2_sum / t[k].n / 2.0f) : 0.0f;   // phase RMS of the sampled vector
        g_rms[k] += (rms - g_rms[k]) * (2.0f / 50.0f);                    // ~50 ms filter
        if (foc::enabled() && g_rms[k] > RMS_TRIP_A) over = true;

        bool loaded = foc::enabled() && foc::mode() == foc::Mode::Closed &&
                      fabsf(t[k].iq) > LOAD_WARN_FRACTION * foc::tuning().iq_limit;
        if (loaded) {
            if (!g_load_since[k]) g_load_since[k] = now;
        } else {
            g_load_since[k] = 0;
        }
        uint8_t bit = 1u << k;
        bool    warn = g_load_since[k] && now - g_load_since[k] >= LOAD_WARN_MS;
        if (warn && !(g_load_warn_mask & bit)) linkEvent("WARN", "load warning M%d: Iq %.1f A near the %.1f A limit",
                                                         k + 1, t[k].iq, foc::tuning().iq_limit);
        if (!warn && (g_load_warn_mask & bit)) linkEvent("MSG", "load warning M%d cleared", k + 1);
        g_load_warn_mask = warn ? (g_load_warn_mask | bit) : (g_load_warn_mask & ~bit);
    }
    if (over) {
        if (!g_rms_over_since) g_rms_over_since = now;
        else if (now - g_rms_over_since > RMS_TRIP_MS) {
            char d[80];
            snprintf(d, sizeof(d), "over-current on M%s (%.1f A / %.1f A RMS)", g_rms[0] > g_rms[1] ? "1" : "2",
                     g_rms[0], g_rms[1]);
            g_rms_over_since = 0;
            stopForOverCurrent(d);
        }
    } else {
        g_rms_over_since = 0;
    }

    TripSample s;
    s.t_ms         = now;
    s.isr_count    = (uint16_t)min<uint32_t>(isr_n, 65535);
    s.isr_max_us   = (uint16_t)min<uint32_t>(isr_max, 65535);
    s.phase_deg10  = (int16_t)constrain(lroundf(rad2deg(foc::phaseCurrent()) * 10), -32767L, 32767L);
    s.target_deg10 = (int16_t)constrain(lroundf(rad2deg(foc::phaseTarget()) * 10), -32767L, 32767L);
    s.zerr_deg10   = (int16_t)constrain(lroundf(rad2deg(foc::zErrorRad()) * 10), -32767L, 32767L);
    s.vbus10       = (uint16_t)constrain(lroundf(hw::vbus() * 10), 0L, 65535L);
    const int dirs[2] = { +1 * SPINDLE_DIRECTION, -1 * SPINDLE_DIRECTION };
    for (int k = 0; k < 2; k++) {
        auto c100 = [](float x) { return (int16_t)constrain(lroundf(x * 100), -32767L, 32767L); };
        TripMotorSample& m = s.m[k];
        m.enabled = foc::enabled();
        m.mode    = (uint8_t)foc::mode();
        m.rpm     = (int16_t)constrain(lroundf(dirs[k] * foc::speedCommandRpm()), -32767L, 32767L);
        m.obs_rpm = (int16_t)constrain(lroundf(t[k].obs_rpm), -32767L, 32767L);
        m.ia      = c100(t[k].ia);
        m.ib      = c100(t[k].ib);
        m.ic      = c100(t[k].ic);
        m.id      = c100(t[k].id);
        m.iq      = c100(t[k].iq);
        m.imax    = c100(t[k].imax);
        m.vmag    = c100(sqrtf(t[k].vd * t[k].vd + t[k].vq * t[k].vq));
    }
    tripPush(s);
}

static void checkAnalogLimits(uint32_t now) {
    float vb = hw::vbus();
    foc::setVbus(vb > 5.0f ? vb : 36.0f);
    if (foc::enabled() && g_align_until == 0 && (vb > VBUS_MAX_V || vb < VBUS_MIN_V)) {
        char d[64];
        snprintf(d, sizeof(d), "bus voltage %.1f V outside %.0f-%.0f V", vb, VBUS_MIN_V, VBUS_MAX_V);
        stopForDriverFault(d);
    }
    static bool warned_hot = false, warned_open = false;
    for (int n = 0; n < 2; n++) {
        float t = hw::temperature(n);
        if (isnan(t)) {
            if (!warned_open && now > 2000) {
                warned_open = true;
                linkEvent("WARN", "NTC %d reads open or shorted - its temperature is not monitored", n + 1);
            }
            continue;
        }
        if (t > TEMP_TRIP_C && foc::enabled()) {
            char d[64];
            snprintf(d, sizeof(d), "power stage over-temperature: NTC %d at %.0f C", n + 1, t);
            stopForDriverFault(d);
        } else if (t > TEMP_WARN_C && !warned_hot) {
            warned_hot = true;
            linkEvent("WARN", "power stage hot: NTC %d at %.0f C (stops at %.0f C)", n + 1, t, TEMP_TRIP_C);
        } else if (t < TEMP_WARN_C - 10) {
            warned_hot = false;
        }
    }
}

// ------------------------------------------------------------------ reset reason

static const char* resetReason() {
    uint32_t    csr = RCC->CSR;
    const char* r   = "unknown";
    if (csr & RCC_CSR_LPWRRSTF) r = "low-power";
    else if (csr & RCC_CSR_WWDGRSTF) r = "window watchdog";
    else if (csr & RCC_CSR_IWDGRSTF) r = "watchdog (main loop stalled)";
    else if (csr & RCC_CSR_SFTRSTF) r = "software";
    else if (csr & RCC_CSR_BORRSTF) r = "power-on/brownout";
    else if (csr & RCC_CSR_OBLRSTF) r = "option-byte load";
    else if (csr & RCC_CSR_PINRSTF) r = "reset pin (XY board, SW2 or debugger)";
    RCC->CSR |= RCC_CSR_RMVF;
    return r;
}

// ------------------------------------------------------------------ setup / loop

void setup() {
    const char* reason = resetReason();
    Link.begin(LINK_BAUD);
    linkEvent("MSG", "spindle reset reason: %s", reason);

    const char* why = "";
    g_init_ok       = hw::init(&why);
    if (g_init_ok) {
        foc::init();
        g_init_ok = hw::start(&why);
    }
    if (g_init_ok) {
        delay(20);   // a few hundred samples of the OPAMPs settling
        g_init_ok = foc::calibrateOffsets(2000, &why);
    }
    if (!g_init_ok) {
        g_init_why   = why;
        g_fault_code = 1;
        foc::disable();
        linkEvent("ERR", "spindle drive init failed: %s - motors stay off", why);
    } else {
        linkEvent("MSG", "spindle %s ready: G474 FOC, PWM %lu Hz, update on the %s", FW_VERSION,
                  (unsigned long)PWM_FREQUENCY_HZ, hw::g_update_phase);
    }
    hw::watchdogStart();

#ifdef BENCH_NO_HOMING
    g_tool_state = MachineState::NeedsHoming;   // nothing moves at boot; home with 'G'
#endif
}

void loop() {
    hw::watchdogKick();
    uint32_t now = millis();

    hw::serviceSlowAnalog();
    pollLink();

    // Faults the interrupt caught (outputs already off).
    char detail[96];
    uint8_t f = foc::takeFault(detail, sizeof(detail));
    if (f != foc::FaultNone) stopForOverCurrent(detail);

    static uint32_t last_2ms = 0;
    if (now - last_2ms >= 2) {
        last_2ms = now;
        sample2ms(now);
        checkAnalogLimits(now);
    }

    // ALIGN test: report what the shunts saw, then off.
    if (g_align_until && (int32_t)(now - g_align_until) >= 0) {
        g_align_until = 0;
        printStat();
        foc::disable();
        linkEvent("MSG", "ALIGN done: expect ia ~ +V/R, ib ~ ic ~ -ia/2 on both motors (sign check)");
    }

    if (g_align_until == 0) {
        superviseSpin();
        updateToolStateMachine();
    }
    updateReportedState();

    static uint32_t last_fan = 0;
    if (now - last_fan >= 10) {
        updateFan((now - last_fan) / 1000.0f);
        last_fan = now;
    }

    static uint32_t last_status = 0;
    if (now - last_status >= LINK_STATUS_INTERVAL_MS && Link.availableForWrite() >= 48) {
        last_status = now;
        sendStatus();
    }
    if (g_stat_every && now - g_stat_last >= g_stat_every) {
        g_stat_last = now;
        printStat();
    }
    if (!g_init_ok) {
        static uint32_t last = 0;
        if (now - last > 5000) {
            last = now;
            linkEvent("ERR", "spindle drive init failed: %s", g_init_why);
        }
    }

    tripService();

    // Status LED: slow blink idle, fast blink driving, solid on fault.
    uint32_t period = g_fault_code ? 0 : foc::enabled() ? 200 : 1000;
    digitalWrite(PIN_STATUS_LED, period == 0 ? HIGH : ((now / (period / 2)) & 1) ? HIGH : LOW);
}
