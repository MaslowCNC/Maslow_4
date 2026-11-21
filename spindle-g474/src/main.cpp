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
#include "cal.h"

HardwareSerial Link(PA_3_ALT1, PA_2_ALT1);   // USART2: RX PA3, TX PA2 (the ROM bootloader's port too)

static const char* LINK_IDENTITY = "I:Maslow-Spindle,proto=1";
static const char* FW_VERSION    = "g474-0.1";

// ------------------------------------------------------------------ link output

static volatile uint32_t g_rx_lines = 0;   // link lines received (#STAT, bring-up)
static volatile uint32_t g_tx_drops = 0;   // lines dropped because the TX buffer had no room
static const char*       g_boot_reason = "";   // this boot's reset reason (#STAT)
static uint32_t          g_prev[8];            // the previous boot's backup registers (crumbsInit)
static uint32_t g_spin_until = 0;   // #SPIN: when the timed bench spin ramps down (0 = none)
static bool     g_bench_spin = false;   // a #SPIN test is running: outside the XY board's job control
static int      g_gcal_step  = -1;      // #GCAL: -1 idle, else the voltage vector at step x 60 deg
static bool     g_gcal_avg   = false;   // averaging this step's currents (noise accumulator)
static uint32_t g_gcal_t     = 0;       // when this step's settling ends
static float    g_gcal_v     = 0.8f;    // calibration voltage
static float    g_gcal_m[6][2][3];      // mean phase currents per step, motor, phase (A, nominal gains)
static int      g_jm_state = 0;    // #JMEAS: 0 idle, 1 settle, 2 steady before, 3 ramp, 4 settle, 5 steady after
static bool     g_jm_acc   = false;
static uint32_t g_jm_t = 0, g_jm_ramp_t0 = 0, g_jm_ramp_ms = 0, g_jm_n[3];
static float    g_jm_from = 0, g_jm_to = 0, g_jm_cmd0 = 0, g_jm_cmd1 = 0;
static float    g_jm_before[2], g_jm_ramp[2], g_jm_after[2];
constexpr uint32_t JM_SETTLE_MS = 300, JM_STEADY_MS = 500, JM_SKIP_MS = 60;
constexpr uint32_t GCAL_SETTLE_MS = 400;
constexpr uint32_t GCAL_SAMPLES   = 10000;   // 0.5 s at 20 kHz
// #RXLOG (bring-up): the last 256 raw bytes received on the link.
static uint8_t  g_rxlog[256];
static uint32_t g_rxlog_n = 0;


bool linkWriteIfRoom(const char* line, int n) {
    if (n <= 0) return true;
    if (Link.availableForWrite() < n) {
        g_tx_drops++;
        return false;
    }
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

void bootTrace(const char* stage) {
    linkEvent("MSG", "boot: %s (t=%lu us)", stage, (unsigned long)micros());
    Link.flush();
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
static const char* g_drive_fail = "";

static bool enableDrive(const char* why) {
    if (foc::enabled()) return true;
    g_drive_fail = "";
    if (!g_init_ok) { g_drive_fail = "init failed"; return false; }
    uint32_t now = millis();
    if ((int32_t)(g_enable_allowed_ms - now) > 0) { g_drive_fail = "coast lockout"; return false; }   // still coasting down
    float vb = hw::vbus();
    if (vb < VBUS_MIN_V || vb > VBUS_MAX_V) {
        static uint32_t last = 0;
        if (now - last > 2000) {
            last = now;
            linkEvent("WARN", "%s refused: bus %.1f V outside %.0f-%.0f V", why, vb, VBUS_MIN_V, VBUS_MAX_V);
        }
        g_drive_fail = "bus voltage";
        return false;
    }
    if (hw::breakLatched(0) || hw::breakLatched(1)) {
        if (!hw::clearBreak(0) || !hw::clearBreak(1)) {
            linkEvent("WARN", "%s refused: hardware over-current comparator still tripped (0x%02x)", why,
                      hw::comparatorOutputs());
            g_drive_fail = "break would not clear";
            return false;
        }
    }
    if (!foc::enable()) {
        g_drive_fail = foc::enableFailReason();
        return false;
    }
    return true;
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
// A fault during a #SPIN bench test stops the motors and is reported, but not latched into the status
// line's fault code: the XY board would answer that with its job recovery ($STOP, then a Z re-home 'G'),
// i.e. motion nobody asked for on a bench.
static bool benchSpinFault(const char* detail) {
    if (!g_bench_spin) return false;
    g_bench_spin = false;
    g_spin_until = 0;
    linkEvent("WARN", "%s - bench spin stopped (not reported as a fault: no XY recovery or Z re-home)", detail);
    return true;
}

static void stopForOverCurrent(const char* detail) {
    tripTrigger(detail);
    disableDrive(true);
    g_spinning = false;
    foc::setSpeedTarget(0);
    requireRehome();
    if (benchSpinFault(detail)) return;
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
    if (benchSpinFault(detail)) return;
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
static bool     g_noise_pending = false;
static volatile uint32_t g_loop_count = 0;   // main-loop passes, reported per second by #STAT
static uint32_t g_align_avg_at  = 0;      // when to start averaging the align currents (millis)
static int      g_dump_print    = -1;     // next #DUMP line to print, -1 = none
static int      g_dump_rows     = 0;      // samples captured
static int      g_dump_chan     = 6;      // 0-5 = one channel (M1 a b c, M2 a b c), 6 = all
static uint32_t g_dump_last_ms  = 0;
constexpr int   DUMP_PER_LINE   = 16;

// Why a drive enable fails (#BRK, and after a refused ALIGN).
static void reportBreak() {
    uint16_t hi[6];
    hw::comparatorHighCounts(hi, 2000);
    uint32_t sr1 = hw::timerStatus(0), sr8 = hw::timerStatus(1);
    linkEvent("MSG", "BRK: fault %u | TIM1 break %s MOE %d SR %04lx BDTR %08lx AF1 %08lx | TIM8 break %s MOE %d SR %04lx "
                     "BDTR %08lx AF1 %08lx | comparator highs /2000: M1 %u %u %u M2 %u %u %u | threshold %.1f A",
              (unsigned)foc::faultCode(), (sr1 & TIM_SR_BIF) ? "LATCHED" : "clear", (int)hw::outputsEnabled(0),
              (unsigned long)sr1, (unsigned long)TIM1->BDTR, (unsigned long)TIM1->AF1,
              (sr8 & TIM_SR_BIF) ? "LATCHED" : "clear", (int)hw::outputsEnabled(1), (unsigned long)sr8,
              (unsigned long)TIM8->BDTR, (unsigned long)TIM8->AF1, hi[0], hi[1], hi[2], hi[3], hi[4], hi[5],
              hw::hardwareTripAmps());
}

// #MOE (bring-up, drive must be off): how long each timer's MOE takes to read back set.  Duties are 0,
// so MOE on means low sides on for ~10 us: no current path at standstill.
static void probeMoe() {
    if (foc::enabled()) {
        linkEvent("WARN", "MOE probe refused: the drive is enabled");
        return;
    }
    TIM_TypeDef* const tims[2] = { TIM1, TIM8 };
    for (int m = 0; m < 2; m++) {
        TIM_TypeDef* t = tims[m];
        __disable_irq();
        t->CCR1 = t->CCR2 = t->CCR3 = 0;
        t->BDTR |= TIM_BDTR_MOE;
        uint32_t at_once = t->BDTR;
        uint32_t c0      = DWT->CYCCNT;
        while (DWT->CYCCNT - c0 < SystemCoreClock / 1000000) {}        // 1 us
        uint32_t after1 = t->BDTR;
        while (DWT->CYCCNT - c0 < SystemCoreClock / 100000) {}         // 10 us
        uint32_t after10 = t->BDTR, sr = t->SR;
        t->BDTR &= ~TIM_BDTR_MOE;
        __enable_irq();
        linkEvent("MSG", "MOE probe TIM%d: MOE at once %d, after 1 us %d, after 10 us %d (SR %04lx)", m == 0 ? 1 : 8,
                  (int)((at_once & TIM_BDTR_MOE) != 0), (int)((after1 & TIM_BDTR_MOE) != 0),
                  (int)((after10 & TIM_BDTR_MOE) != 0), (unsigned long)sr);
    }
}

static void dumpRxLog() {
    uint32_t n     = g_rxlog_n;
    uint32_t count = n < 256 ? n : 256;
    char     line[200];
    int      len = 0;
    for (uint32_t i = n - count; i < n; i++) {
        uint8_t c = g_rxlog[i & 0xFF];
        if (len > 150) {
            linkEvent("MSG", "RXLOG %s", line);
            len = 0;
        }
        if (c == '\n') len += snprintf(line + len, sizeof(line) - len, "|");
        else if (c >= 0x20 && c < 0x7F && c != '|' && c != '\\') len += snprintf(line + len, sizeof(line) - len, "%c", c);
        else len += snprintf(line + len, sizeof(line) - len, "\\x%02x", c);
    }
    if (len) linkEvent("MSG", "RXLOG %s", line);
    linkEvent("MSG", "RXLOG end (%lu bytes received since boot; | = newline)", (unsigned long)n);
}

static void reportNoise() {
    static const char ph[3] = { 'a', 'b', 'c' };
    for (int k = 0; k < 2; k++) {
        char  buf[200];
        int   len = 0;
        for (int p = 0; p < 3; p++) {
            foc::NoiseStats st;
            foc::noiseResult(k, p, &st);
            len += snprintf(buf + len, sizeof(buf) - len, "%s%c mean %+.1f sd %.1f [%d..%d]", p ? " | " : "", ph[p],
                            st.mean - st.offset, st.sd, (int)st.min - (int)lroundf(st.offset),
                            (int)st.max - (int)lroundf(st.offset));
        }
        linkEvent("MSG", "NOISE M%d (counts vs zero, %.4f A/count): %s", k + 1, SENSE_A_PER_LSB, buf);
    }
}
static uint32_t g_stat_last    = 0;

static void printStat() {
    static uint32_t last_ms = 0, last_loops = 0;
    uint32_t        now_ms = millis(), loops = g_loop_count;
    float           loop_hz = last_ms && now_ms != last_ms ? (loops - last_loops) * 1000.0f / (now_ms - last_ms) : 0.0f;
    last_ms = now_ms;
    last_loops = loops;
    foc::MotorTelemetry t[2] = { foc::take(0), foc::take(1) };
    uint32_t            isr_max, isr_n;
    foc::takeIsrStats(&isr_max, &isr_n);
    const char* mode = foc::mode() == foc::Mode::Off ? "off" : foc::mode() == foc::Mode::Open ? "open" : "closed";
    linkEvent("MSG", "STAT up %lu s since %s; control interrupt overruns %lu, longest %lu us since boot",
              (unsigned long)(millis() / 1000), g_boot_reason, (unsigned long)foc::overruns(),
              (unsigned long)(TAMP->BKP1R / (SystemCoreClock / 1000000)));
    linkEvent("MSG", "STAT %s cmd %.0f/%.0f rpm phi %.1f/%.1f deg zerr %.1f deg vbus %.1f V T %.0f/%.0f C isr %lu us",
              mode, foc::speedCommandRpm(), foc::speedTargetRpm(), rad2deg(foc::phaseCurrent()),
              rad2deg(foc::phaseTarget()), rad2deg(foc::zErrorRad()), hw::vbus(), hw::temperature(0),
              hw::temperature(1), (unsigned long)isr_max);
    linkEvent("MSG", "STAT pins: vbus %.3f V, NTC1 %.3f V, NTC2 %.3f V (of 3.3 V); main loop %.0f Hz; link rx %lu lines, "
                     "tx drops %lu; uart errors seen ore %lu fe %lu ne %lu", hw::slowPinVolts(0), hw::slowPinVolts(1),
              hw::slowPinVolts(2), loop_hz, (unsigned long)g_rx_lines, (unsigned long)g_tx_drops,
              (unsigned long)foc::uartErrorsSeen(0), (unsigned long)foc::uartErrorsSeen(1), (unsigned long)foc::uartErrorsSeen(2));
    for (int k = 0; k < 2; k++) {
        linkEvent("MSG", "STAT M%d i %.2f %.2f %.2f dq %.2f %.2f v %.2f %.2f obs %.0f rpm flux %.2f max %.1f A", k + 1,
                  t[k].ia, t[k].ib, t[k].ic, t[k].id, t[k].iq, t[k].vd, t[k].vq, t[k].obs_rpm, t[k].obs_flux, t[k].imax);
    }
}

// Maintenance commands, "#..." on the link ($Spindle/G474=SEND <line> on the XY board passes them through).
// The ESP32 firmware ignores lines starting with '#', so the XY side can send them to either board.
// #GCAL result: per-phase current gains from the three +/- phase-axis tests.  The true phase currents of
// a star-connected motor sum to zero, so with the offsets removed (+ minus - halves) the three gains are
// the null vector of the measured 3x3 current pattern (the sum of its rows' cyclic cross products),
// normalised to a mean of 1.  V/I along each axis gives the apparent phase resistance.
static void finishGcal() {
    static const int pos[3] = { 0, 2, 4 }, neg[3] = { 3, 5, 1 };   // steps: 0 +A, 1 -C, 2 +B, 3 -A, 4 +C, 5 -B
    static const char ph[3] = { 'a', 'b', 'c' };
    for (int k = 0; k < 2; k++) {
        float d[3][3];
        for (int t = 0; t < 3; t++)
            for (int p = 0; p < 3; p++) d[t][p] = 0.5f * (g_gcal_m[pos[t]][k][p] - g_gcal_m[neg[t]][k][p]);
        for (int t = 0; t < 3; t++)
            linkEvent("MSG", "GCAL M%d +%c test: a %+.3f b %+.3f c %+.3f A (sum %+.3f)", k + 1, ph[t], d[t][0], d[t][1],
                      d[t][2], d[t][0] + d[t][1] + d[t][2]);
        float g[3] = { 0, 0, 0 };
        for (int t = 0; t < 3; t++) {
            const float* u = d[t];
            const float* w = d[(t + 1) % 3];
            g[0] += u[1] * w[2] - u[2] * w[1];
            g[1] += u[2] * w[0] - u[0] * w[2];
            g[2] += u[0] * w[1] - u[1] * w[0];
        }
        float sum = g[0] + g[1] + g[2];
        bool  ok  = fabsf(sum) > 1e-9f;
        if (ok)
            for (int p = 0; p < 3; p++) g[p] *= 3.0f / sum;
        for (int p = 0; p < 3; p++)
            if (!(g[p] > 0.6f && g[p] < 1.6f)) ok = false;
        float after[3], r[3];
        for (int t = 0; t < 3; t++) {
            after[t] = g[0] * d[t][0] + g[1] * d[t][1] + g[2] * d[t][2];
            float it = g[t] * d[t][t];
            r[t]     = it > 0.05f ? g_gcal_v / it : NAN;
        }
        if (ok) foc::setPhaseGains(k, g);
        linkEvent("MSG", "GCAL M%d gains a %.3f b %.3f c %.3f (%s) | sums after %+.3f %+.3f %+.3f A | apparent R a %.3f b %.3f c %.3f ohm",
                  k + 1, g[0], g[1], g[2], ok ? "applied; #GSAVE stores it" : "NOT applied: out of range", after[0], after[1], after[2], r[0],
                  r[1], r[2]);
    }
}

// #JMEAS <rpm>: rotor inertia from a closed-loop speed step.  The torque that accelerates a rotor on the
// 500 rad/s^2 ramp is J*alpha; drag is taken out as the mean of the steady iq before and after the step.
static void serviceJmeas(uint32_t now) {
    if (!g_jm_state) return;
    float dummy[2];
    if (!foc::enabled() || foc::mode() != foc::Mode::Closed) {
        g_jm_state = 0;
        foc::iqAccStop(dummy);
        linkEvent("WARN", "JMEAS aborted: the drive left closed loop");
        return;
    }
    switch (g_jm_state) {
        case 1:
            if (now - g_jm_t >= JM_SETTLE_MS) {
                foc::iqAccStart();
                g_jm_t     = now;
                g_jm_state = 2;
            }
            break;
        case 2:
            if (now - g_jm_t >= JM_STEADY_MS) {
                g_jm_n[0] = foc::iqAccStop(g_jm_before);
                setSpindleSpeed(g_jm_to);
                g_jm_t     = now;
                g_jm_acc   = false;
                g_jm_state = 3;
            }
            break;
        case 3: {
            float cmd = foc::speedCommandRpm();
            if (!g_jm_acc && now - g_jm_t >= JM_SKIP_MS) {   // past the ramp's start transient
                foc::iqAccStart();
                g_jm_acc     = true;
                g_jm_cmd0    = cmd;
                g_jm_ramp_t0 = now;
            }
            if (g_jm_acc && fabsf(cmd - g_jm_to) < 0.05f * fabsf(g_jm_to - g_jm_from)) {   // stop before its end
                g_jm_n[1]    = foc::iqAccStop(g_jm_ramp);
                g_jm_cmd1    = cmd;
                g_jm_ramp_ms = now - g_jm_ramp_t0;
                g_jm_t       = now;
                g_jm_state   = 4;
            } else if (now - g_jm_t > 20000) {
                g_jm_state = 0;
                foc::iqAccStop(dummy);
                linkEvent("WARN", "JMEAS aborted: the ramp did not finish");
            }
            break;
        }
        case 4:
            if (now - g_jm_t >= JM_SETTLE_MS) {
                foc::iqAccStart();
                g_jm_t     = now;
                g_jm_state = 5;
            }
            break;
        case 5:
            if (now - g_jm_t >= JM_STEADY_MS) {
                g_jm_n[2]   = foc::iqAccStop(g_jm_after);
                g_jm_state  = 0;
                float alpha = (g_jm_cmd1 - g_jm_cmd0) * RPM_TO_RAD / (g_jm_ramp_ms / 1000.0f);
                float kt    = 1.5f * POLE_PAIRS * foc::tuning().flux;
                for (int k = 0; k < 2; k++) {
                    float drive = g_jm_ramp[k] - 0.5f * (g_jm_before[k] + g_jm_after[k]);
                    linkEvent("MSG", "JMEAS %.0f -> %.0f RPM, %.0f rad/s^2 over %lu ms: M%d iq before %+.3f ramp %+.3f after %+.3f A -> J %.2e kg m^2",
                              g_jm_from, g_jm_to, alpha, (unsigned long)g_jm_ramp_ms, k + 1, g_jm_before[k], g_jm_ramp[k],
                              g_jm_after[k], fabsf(alpha) > 1.0f ? kt * drive / alpha : NAN);
                }
            }
            break;
    }
}

// #GCAL steps, from loop(): settle, average, record, turn the vector 60 degrees.
static void serviceGcal(uint32_t now) {
    if (g_gcal_step < 0) return;
    if (!foc::enabled()) {
        g_gcal_step = -1;
        linkEvent("WARN", "GCAL aborted: the drive turned off");
        return;
    }
    if (!g_gcal_avg) {
        if ((int32_t)(now - g_gcal_t) >= 0) {
            foc::startNoise(GCAL_SAMPLES);
            g_gcal_avg = true;
        }
        return;
    }
    if (!foc::noiseDone()) return;
    g_gcal_avg = false;
    for (int k = 0; k < 2; k++)
        for (int p = 0; p < 3; p++) {
            foc::NoiseStats st;
            foc::noiseResult(k, p, &st);
            g_gcal_m[g_gcal_step][k][p] = SENSE_SIGN * (st.mean - st.offset) * SENSE_A_PER_LSB;
        }
    if (++g_gcal_step >= 6) {
        g_gcal_step = -1;
        foc::disable();
        finishGcal();
        return;
    }
    foc::setAlignVector(g_gcal_v, g_gcal_step * (float)M_PI / 3.0f);
    g_gcal_t = now + GCAL_SETTLE_MS;
}

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
        } else if (!enableDrive("ALIGN")) {
            linkEvent("WARN", "ALIGN refused: drive enable failed (%s)", g_drive_fail);
            reportBreak();
        } else if (foc::disable(), !foc::startAlignTest(v)) {
            linkEvent("WARN", "ALIGN refused: align start failed (%s)", foc::enableFailReason());
            reportBreak();
        } else {
            uint32_t ms    = n >= 3 ? (uint32_t)(b < 50 ? 50 : b > 5000 ? 5000 : b) : 300;
            g_align_until  = millis() + ms;
            g_align_avg_at = millis() + ms / 2;   // average 2000 samples (100 ms) from halfway
            linkEvent("MSG", "ALIGN: %.2f V on the phase A axis of both motors for %lu ms", v, (unsigned long)ms);
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
    } else if (!strcmp(word, "NOISE")) {
        uint32_t samples = (n >= 2 && a >= 1) ? (uint32_t)a : 2000;
        foc::startNoise(samples);
        g_noise_pending = true;
        linkEvent("MSG", "NOISE: sampling %lu periods (%.0f ms)", (unsigned long)samples,
                  samples / (PWM_FREQUENCY_HZ / 1000.0f));
    } else if (!strcmp(word, "SPIN") && n >= 3) {
        // Bench spin that stops itself: it does not depend on the link delivering the S0.
        float    rpm = a < 0 ? 0 : a > 12000 ? 12000 : a;   // bench: loose motors
        uint32_t ms  = b < 100 ? 100 : b > 30000 ? 30000 : (uint32_t)b;
        setSpindleSpeed(rpm);
        g_spin_until = millis() + ms;
        if (!g_spin_until) g_spin_until = 1;
        if (rpm > 0) g_bench_spin = true;   // kept through a #SPIN 0 ramp-down; cleared once powered down
        linkEvent("MSG", "SPIN: %.0f RPM for %lu ms, then S0", rpm, (unsigned long)ms);
    } else if (!strcmp(word, "LOAD") && n >= 2) {
        uint32_t us = a < 0 ? 0 : a > 48 ? 48 : (uint32_t)a;
        foc::setFakeLoad(us);
        linkEvent("MSG", "LOAD: every ADC interrupt padded to %lu us (0 = off)", (unsigned long)us);
    } else if (!strcmp(word, "PREV")) {
        linkEvent("MSG", "PREV: this boot: %s, up %lu s; raw %08lx %08lx %08lx %08lx", g_boot_reason,
                  (unsigned long)(millis() / 1000), (unsigned long)g_prev[0], (unsigned long)g_prev[1],
                  (unsigned long)g_prev[2], (unsigned long)g_prev[3]);
        crumbsReport();
    } else if (!strcmp(word, "GCAL")) {
        float v = n >= 2 ? a : 0.8f;
        if (g_spinning || foc::enabled() || g_gcal_step >= 0 || g_align_until) {
            linkEvent("WARN", "GCAL refused: the drive is busy");
        } else if (v < 0.1f || v > 1.5f) {
            linkEvent("WARN", "GCAL refused: 0.1-1.5 V");
        } else if (!enableDrive("GCAL")) {
            linkEvent("WARN", "GCAL refused: drive enable failed (%s)", g_drive_fail);
        } else if (foc::disable(), !foc::startAlignTest(v)) {
            linkEvent("WARN", "GCAL refused: align start failed (%s)", foc::enableFailReason());
        } else {
            static const float nominal[3] = { 1.0f, 1.0f, 1.0f };
            foc::setPhaseGains(0, nominal);   // measure against the nominal scale
            foc::setPhaseGains(1, nominal);
            g_gcal_v    = v;
            g_gcal_step = 0;
            g_gcal_avg  = false;
            g_gcal_t    = millis() + 2 * GCAL_SETTLE_MS;   // the first snap can be up to 180 degrees
            linkEvent("MSG", "GCAL: %.2f V at 0-300 deg in 60 deg steps, %lu ms settle + %lu ms average each; the rotors "
                             "snap to each vector",
                      v, (unsigned long)GCAL_SETTLE_MS, (unsigned long)(GCAL_SAMPLES / (PWM_FREQUENCY_HZ / 1000)));
        }
    } else if (!strcmp(word, "JMEAS") && n >= 2) {
        if (g_jm_state) {
            linkEvent("WARN", "JMEAS refused: one is running");
        } else if (!foc::enabled() || foc::mode() != foc::Mode::Closed) {
            linkEvent("WARN", "JMEAS needs closed loop (spin up with #SPIN first)");
        } else if (a < CL_ENTER_RPM || a > 12000.0f || fabsf(a - foc::speedTargetRpm()) < 500.0f) {
            linkEvent("WARN", "JMEAS: target 2000-12000 RPM, at least 500 RPM from the present speed");
        } else {
            g_jm_from  = foc::speedTargetRpm();
            g_jm_to    = a;
            g_jm_t     = millis();
            g_jm_state = 1;
            linkEvent("MSG", "JMEAS: %.0f -> %.0f RPM (0.5 s steady, the ramp, 0.5 s steady)", g_jm_from, g_jm_to);
        }
    } else if (!strcmp(word, "GSAVE") || !strcmp(word, "GCLEAR")) {
        bool        save = word[1] == 'S';
        const char* why  = "";
        if (foc::enabled() || g_spinning || g_gcal_step >= 0 || g_jm_state) {
            linkEvent("WARN", "%s refused: the drive must be off (a flash erase stalls the CPU)", word);
        } else if (!save) {
            if (cal::clear(&why)) linkEvent("MSG", "GCLEAR: stored calibration erased (nominal gains after the next reset)");
            else linkEvent("WARN", "GCLEAR FAILED: %s", why);
        } else {
            cal::Record rec = {};
            foc::phaseGains(0, rec.gain[0]);
            foc::phaseGains(1, rec.gain[1]);
            rec.r    = tu.r;
            rec.l    = tu.l;
            rec.flux = tu.flux;
            if (cal::save(rec, &why))
                linkEvent("MSG", "GSAVE: stored M1 gains %.3f %.3f %.3f, M2 %.3f %.3f %.3f, obs R %.3f L %.1fuH flux %.2fmWb (0x%08lx, %s)",
                          rec.gain[0][0], rec.gain[0][1], rec.gain[0][2], rec.gain[1][0], rec.gain[1][1], rec.gain[1][2],
                          rec.r, rec.l * 1e6f, rec.flux * 1e3f, (unsigned long)cal::ADDR, cal::bankMode());
            else linkEvent("WARN", "GSAVE FAILED: %s", why);
        }
    } else if (!strcmp(word, "GAIN")) {
        for (int k = 0; k < 2; k++) {
            float g[3];
            foc::phaseGains(k, g);
            linkEvent("MSG", "GAIN M%d a %.3f b %.3f c %.3f", k + 1, g[0], g[1], g[2]);
        }
    } else if ((!strcmp(word, "GAIN1") || !strcmp(word, "GAIN2")) && n >= 4) {
        float g[3] = { a, b, c };
        bool  ok   = true;
        for (int p = 0; p < 3; p++)
            if (!(g[p] > 0.5f && g[p] < 2.0f)) ok = false;
        if (ok) foc::setPhaseGains(word[4] - '1', g);
        linkEvent(ok ? "MSG" : "WARN", "%s %.3f %.3f %.3f %s", word, g[0], g[1], g[2], ok ? "set" : "refused (0.5-2.0)");
    } else if (!strcmp(word, "RXLOG")) {
        dumpRxLog();
    } else if (!strcmp(word, "BRK")) {
        reportBreak();
    } else if (!strcmp(word, "MOE")) {
        probeMoe();
    } else if (!strcmp(word, "DUMP")) {
        if (g_dump_print >= 0) {
            linkEvent("WARN", "DUMP refused: still printing the last one");
        } else {
            g_dump_rows = foc::startDump(n >= 2 ? (int)a : 512);
            g_dump_chan = (n >= 3 && b >= 0 && b <= 5) ? (int)b : 6;
            linkEvent("MSG", "DUMP: %d consecutive samples at %lu Hz, counts vs zero, %d per line: "
                             "D <channel 0-5 = M1 a b c, M2 a b c> <first sample> <values>",
                      g_dump_rows, (unsigned long)PWM_FREQUENCY_HZ, DUMP_PER_LINE);
            g_dump_print = 0;
        }
    } else if (!strcmp(word, "SLOW") && n >= 2) {
        hw::setSlowMode((int)a);
        linkEvent("MSG", "SLOW: slow ADC2 conversions %s", a < 0.5f ? "free-running" : a < 1.5f ? "after the current samples" : "off");
    } else if (!strcmp(word, "VER")) {
        linkEvent("MSG", "spindle firmware %s, %s %s", FW_VERSION, __DATE__, __TIME__);
    } else {
        linkEvent("MSG", "# commands: STAT LOG <ms> ALIGN [V] [ms] CL <0|1> CUR <kp> <ki> POS <kp> <kd> <ki> "
                         "POSBW <min> <full rpm> OLKI <ki> DTC <ns> IHOLD/ISPIN/IBOOST/ILIM <A> OBS <R> <L> <flux> "
                         "TRIP <sw A> [hw A] NOISE [samples] SLOW <0|1|2> DUMP [samples] [channel] BRK MOE LOAD <us> SPIN <rpm> <ms> RXLOG PREV GCAL [V] GAIN GAIN1/GAIN2 <a> <b> <c> JMEAS <rpm> GSAVE GCLEAR VER");
        return;
    }
    if (strcmp(word, "STAT") && strcmp(word, "VER") && strcmp(word, "ALIGN") && strcmp(word, "NOISE") && strcmp(word, "SLOW") &&
        strcmp(word, "DUMP") && strcmp(word, "BRK") &&
        strcmp(word, "MOE") && strcmp(word, "LOAD") && strcmp(word, "SPIN") && strcmp(word, "RXLOG") &&
        strcmp(word, "PREV") && strcmp(word, "GCAL") && strncmp(word, "GAIN", 4) && strcmp(word, "JMEAS") &&
        strcmp(word, "GSAVE") && strcmp(word, "GCLEAR")) {
        linkEvent("MSG", "tuning: cur %.3f/%.0f pos %.2f/%.3f/%.1f bw %.2f/%.0f olki %.1f I hold %.1f spin %.1f "
                         "boost %.1f lim %.1f obs R %.3f L %.1fuH flux %.2fmWb trip %.1f A dtc %.0f ns CL %d",
                  tu.cur_kp, tu.cur_ki, tu.pos_kp, tu.pos_kd, tu.pos_ki, tu.pos_bw_min, tu.pos_bw_full_rpm, tu.ol_ki,
                  tu.hold_a, tu.spin_a, tu.boost_a, tu.iq_limit,
                  tu.r, tu.l * 1e6f, tu.flux * 1e3f, tu.sw_trip_a, tu.deadtime_ns, (int)tu.closed_loop_allowed);
    }
}

static void processLine(const char* line, size_t len) {
    if (len == 0) return;
    g_rx_lines++;
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
        g_rxlog[g_rxlog_n++ & 0xFF] = (uint8_t)ch;
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

// ------------------------------------------------------------------ reset-surviving diagnostics


static const char* crumbName(uint32_t step) {
    static const char* const names[] = { "?", "slow analog", "link input", "fault check", "2 ms sampling",
                                         "analog limits", "align", "spin supervision", "tool state machine",
                                         "reported state", "fan", "status line", "#LOG", "#NOISE report",
                                         "#DUMP print", "trip recorder", "LED", "end of loop" };
    return step < sizeof(names) / sizeof(names[0]) ? names[step] : "?";
}

void crumbsInit() {
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN | RCC_APB1ENR1_RTCAPBEN;
    (void)RCC->APB1ENR1;
    PWR->CR1 |= PWR_CR1_DBP;   // backup domain writable
    volatile uint32_t* bkp = &TAMP->BKP0R;
    for (int i = 0; i < 8; i++) {
        g_prev[i] = bkp[i];
        bkp[i]    = 0;
    }
}

void crumbsReport() {
    if ((g_prev[0] & 0xFFFF0000u) == 0xC0DE0000u)
        linkEvent("MSG", "previous boot: main loop last at '%s', longest ADC interrupt %lu us%s",
                  crumbName(g_prev[0] & 0xFFFF), (unsigned long)(g_prev[1] / (SystemCoreClock / 1000000)),
                  g_prev[2] ? " - it ended INSIDE the ADC interrupt" : "");
    if (g_prev[3] == 0xFA170000u)
        linkEvent("MSG", "previous boot: CPU fault pc=%08lx lr=%08lx cfsr=%08lx hfsr=%08lx", (unsigned long)g_prev[4],
                  (unsigned long)g_prev[5], (unsigned long)g_prev[6], (unsigned long)g_prev[7]);
}

void setup() {
    // Cycle counter first: the fault report's UART timeouts use it.
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    crumbsInit();

    const char* reason = resetReason();
    g_boot_reason      = reason;
    Link.begin(LINK_BAUD);
    linkEvent("MSG", "spindle reset reason: %s", reason);
    crumbsReport();
    Link.flush();   // all of it out before init starts reconfiguring the chip

    const char* why = "";
    g_init_ok       = hw::init(&why);
    BOOT_TRACE_POINT(g_init_ok ? "hw::init ok" : "hw::init FAILED");
    if (g_init_ok) {
        foc::init();
        BOOT_TRACE_POINT("foc::init done");
        // The board's calibration (#GCAL gains, #OBS constants), if one is stored.
        cal::Record rec;
        if (cal::load(&rec)) {
            foc::setPhaseGains(0, rec.gain[0]);
            foc::setPhaseGains(1, rec.gain[1]);
            foc::Tuning& tu = foc::tuning();
            tu.r            = rec.r;
            tu.l            = rec.l;
            tu.flux         = rec.flux;
            linkEvent("MSG", "calibration loaded: M1 gains %.3f %.3f %.3f, M2 %.3f %.3f %.3f, obs R %.3f L %.1fuH flux %.2fmWb",
                      rec.gain[0][0], rec.gain[0][1], rec.gain[0][2], rec.gain[1][0], rec.gain[1][1], rec.gain[1][2],
                      rec.r, rec.l * 1e6f, rec.flux * 1e3f);
        } else {
            linkEvent("MSG", "no stored calibration: nominal current gains (run #GCAL, then #GSAVE)");
        }
        g_init_ok = hw::start(&why);
        BOOT_TRACE_POINT(g_init_ok ? "hw::start ok, ADC interrupt on" : "hw::start FAILED");
    }
#ifdef BOOT_TRACE
    if (g_init_ok) {
        // 2 ms timed by the cycle counter, not SysTick: an interrupt that never ends would stop that too.
        uint32_t max_us = 0, count = 0;
        foc::takeIsrStats(&max_us, &count);   // restart the count for exactly this window
        uint32_t c0 = DWT->CYCCNT;
        while (DWT->CYCCNT - c0 < SystemCoreClock / 500) {}
        foc::takeIsrStats(&max_us, &count);
        char buf[80];
        snprintf(buf, sizeof(buf), "ADC interrupt ran %lu times in 2 ms (expect ~40), longest %lu us",
                 (unsigned long)count, (unsigned long)max_us);
        bootTrace(buf);
    }
#endif
    if (g_init_ok) {
        delay(20);   // a few hundred samples of the OPAMPs settling
        BOOT_TRACE_POINT("OPAMP settling delay done");
        g_init_ok = foc::calibrateOffsets(2000, &why);
        BOOT_TRACE_POINT(g_init_ok ? "offset calibration ok" : "offset calibration FAILED");
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
    g_loop_count++;
    hw::watchdogKick();
    uint32_t now = millis();
    if (g_spin_until && (int32_t)(now - g_spin_until) >= 0) {
        g_spin_until = 0;
        setSpindleSpeed(0);
        linkEvent("MSG", "SPIN: time up, ramping down");
    }
    if (g_bench_spin && !g_spin_until && !g_spinning && !foc::enabled()) g_bench_spin = false;   // ramped down

    CRUMB(1);
    hw::serviceSlowAnalog();
    CRUMB(2);
    pollLink();

    // Faults the interrupt caught (outputs already off).
    char detail[96];
    CRUMB(3);
    uint8_t f = foc::takeFault(detail, sizeof(detail));
    if (f != foc::FaultNone) stopForOverCurrent(detail);

    static uint32_t last_2ms = 0;
    if (now - last_2ms >= 2) {
        last_2ms = now;
        CRUMB(4);
        sample2ms(now);
        CRUMB(5);
        checkAnalogLimits(now);
    }

    // ALIGN test: report what the shunts saw, then off.
    CRUMB(6);
    if (g_align_until && (int32_t)(now - g_align_until) >= 0) {
        g_align_until = 0;
        if (g_noise_pending && foc::noiseDone()) {
            g_noise_pending = false;
            for (int k = 0; k < 2; k++) {
                float amps[3], sd[3], g[3];
                foc::phaseGains(k, g);   // the same per-phase gains the drive uses (#GCAL)
                for (int p = 0; p < 3; p++) {
                    foc::NoiseStats st;
                    foc::noiseResult(k, p, &st);
                    amps[p] = SENSE_SIGN * (st.mean - st.offset) * SENSE_A_PER_LSB * g[p];
                    sd[p]   = st.sd * SENSE_A_PER_LSB * g[p];
                }
                linkEvent("MSG", "ALIGN M%d averaged over 100 ms: ia %+.2f ib %+.2f ic %+.2f A, sum %+.2f (sample sd %.2f %.2f %.2f A)",
                          k + 1, amps[0], amps[1], amps[2], amps[0] + amps[1] + amps[2], sd[0], sd[1], sd[2]);
            }
        }
        printStat();
        foc::disable();
        linkEvent("MSG", "ALIGN done: expect ia ~ +V/R, ib ~ ic ~ -ia/2 on both motors (sign check)");
    }

    serviceGcal(now);
    serviceJmeas(now);
    if (g_align_until == 0 && g_gcal_step < 0) {
        CRUMB(7);
        superviseSpin();
        CRUMB(8);
        updateToolStateMachine();
    }
    CRUMB(9);
    updateReportedState();

    static uint32_t last_fan = 0;
    if (now - last_fan >= 10) {
        CRUMB(10);
        updateFan((now - last_fan) / 1000.0f);
        last_fan = now;
    }

    static uint32_t last_status = 0;
    if (now - last_status >= LINK_STATUS_INTERVAL_MS && Link.availableForWrite() >= 48) {
        last_status = now;
        CRUMB(11);
        sendStatus();
    }
    if (g_align_avg_at && (int32_t)(now - g_align_avg_at) >= 0) {
        g_align_avg_at = 0;
        foc::startNoise(2000);
        g_noise_pending = true;   // reported by the align-done block, not as #NOISE
    }
    if (g_noise_pending && g_align_until == 0 && foc::noiseDone()) {
        g_noise_pending = false;
        CRUMB(13);
        reportNoise();
    }
    // #DUMP lines: DUMP_PER_LINE samples of one channel each, one line every 10 ms so the XY board's
    // log keeps up.  g_dump_print counts lines: channel-major over the captured samples.
    if (g_dump_print >= 0 && foc::dumpDone() && now - g_dump_last_ms >= 10 && Link.availableForWrite() >= 160) {
        g_dump_last_ms = now;
        int lines_per_chan = (g_dump_rows + DUMP_PER_LINE - 1) / DUMP_PER_LINE;
        int nchan          = g_dump_chan == 6 ? 6 : 1;
        int ch             = g_dump_chan == 6 ? g_dump_print / lines_per_chan : g_dump_chan;
        int first          = (g_dump_print % lines_per_chan) * DUMP_PER_LINE;
        char buf[160];
        int  len = snprintf(buf, sizeof(buf), "D %d %d", ch, first);
        for (int i = first; i < first + DUMP_PER_LINE && i < g_dump_rows; i++) {
            int v[6];
            foc::dumpRow(i, v);
            len += snprintf(buf + len, sizeof(buf) - len, " %d", v[ch]);
        }
        linkEvent("MSG", "%s", buf);
        if (++g_dump_print >= lines_per_chan * nchan) {
            g_dump_print = -1;
            linkEvent("MSG", "DUMP done");
        }
    }
    if (g_stat_every && now - g_stat_last >= g_stat_every) {
        g_stat_last = now;
        CRUMB(12);
        printStat();
    }
    if (!g_init_ok) {
        static uint32_t last = 0;
        if (now - last > 5000) {
            last = now;
            linkEvent("ERR", "spindle drive init failed: %s", g_init_why);
        }
    }

    CRUMB(15);
    tripService();

    // Status LED: slow blink idle, fast blink driving, solid on fault.
    uint32_t period = g_fault_code ? 0 : foc::enabled() ? 200 : 1000;
    digitalWrite(PIN_STATUS_LED, period == 0 ? HIGH : ((now / (period / 2)) & 1) ? HIGH : LOW);
}
