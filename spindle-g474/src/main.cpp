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
static const char* FW_VERSION    = "g474-0.2";

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
// Bus voltage when the current-sense zero was last measured (NAN after the boot zero, until a steady reading).
static float    g_zero_vbus          = NAN;
static uint32_t g_zero_ms            = 0;
// The bus is "steady" once it has moved less than 0.5 V per 100 ms for half a second.
static float    g_vbus_track         = NAN;
static uint32_t g_vbus_track_ms      = 0;
static uint32_t g_vbus_steady_since  = 0;
// The U9 sync clock is meant to be running (boot default or #SYNC 1); it can still be stopped by measureSenseZero.
static bool     g_sync_wanted        = false;

// ------------------------------------------------------------------ helpers

static float rad2deg(float r) { return r * 180.0f / (float)M_PI; }

static bool beamBlocked() { return digitalRead(PIN_BEAM_DETECT) != 0; }

static void notifyZHomed() {
    linkWriteIfRoom("ZHOMED\n", 7);
}

// Measure the current-sense zero (drive off).  With U9 locked to the sync clock its ripple is part of the zero;
// at a bad clock phase that can push a channel past the sense-fault limit (board 2, 36 V, phases 280-320: M1b
// more than 310 counts out).  The clock must never be what keeps the drive from running, so it is stopped then
// and the zero measured without it.
static bool measureSenseZero(const char* what, bool boot, const char** why) {
    bool ok = foc::calibrateOffsets(2000, why);
    if (!ok && hw::syncRunning()) {
        linkEvent("WARN", "%s: current-sense zero failed with the U9 sync clock on (%s): clock stopped, measuring without it",
                  what, *why);
        hw::syncStop();
        delay(5);
        ok = foc::calibrateOffsets(2000, why);
    }
    g_zero_vbus = boot ? NAN : hw::vbus();
    g_zero_ms   = millis();
    return ok;
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
static int      g_scope_print   = -1;     // next #SCOPE line to print, -1 = none
static uint32_t g_scope_last_ms = 0;
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
    // The zeros themselves: nominal 2010 counts (1.62 V), a sense fault beyond +-310.  With the sync clock on they
    // include U9's ripple at the sample instant.
    foc::NoiseStats z[6];
    for (int c = 0; c < 6; c++) foc::noiseResult(c / 3, c % 3, &z[c]);
    linkEvent("MSG", "NOISE zero (counts, nominal 2010, fault beyond +-310): M1 %.0f %.0f %.0f M2 %.0f %.0f %.0f", z[0].offset,
              z[1].offset, z[2].offset, z[3].offset, z[4].offset, z[5].offset);
    // A star motor's three currents sum to zero: what the sum shows is measurement error, at any speed.
    float ps[2][4];
    for (int k = 0; k < 2; k++) foc::noisePhaseSum(k, &ps[k][0], &ps[k][1], &ps[k][2], &ps[k][3]);
    linkEvent("MSG", "NOISE phase sum (A, %lu samples, true sum 0): M1 mean %+.3f sd %.3f [%+.2f..%+.2f] | M2 mean %+.3f sd %.3f [%+.2f..%+.2f]",
              (unsigned long)z[0].n, ps[0][0], ps[0][1], ps[0][2], ps[0][3], ps[1][0], ps[1][1], ps[1][2], ps[1][3]);
    // The control signals over the same samples (drive running): what repeats every turn shows here, not in #VLOG.
    for (int k = 0; k < 2; k++) {
        float r[5][4];
        for (int j = 0; j < 5; j++) foc::noiseRipple(k, j, &r[j][0], &r[j][1], &r[j][2], &r[j][3]);
        linkEvent("MSG", "NOISE ripple M%d: angle err %+.1f sd %.2f [%+.1f..%+.1f] deg | speed est sd %.0f [%.0f..%.0f] rpm | "
                         "iq_ref %+.2f sd %.2f [%+.2f..%+.2f] | iq sd %.2f | id %+.2f sd %.2f A",
                  k + 1, r[0][0], r[0][1], r[0][2], r[0][3], r[1][1], r[1][2], r[1][3], r[2][0], r[2][1], r[2][2], r[2][3], r[3][1],
                  r[4][0], r[4][1]);
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
    foc::VoltStats v[2] = { foc::takeVolt(0), foc::takeVolt(1) };
    linkEvent("MSG", "STAT volts: limit %.2f V | M1 demand %.3f (peak %.3f) clipped %lu of %lu fw %.2f A after-valley %lu | M2 demand "
                     "%.3f (peak %.3f) clipped %lu of %lu fw %.2f A after-valley %lu",
              v[0].vmax, v[0].u, v[0].u_max, (unsigned long)v[0].sat, (unsigned long)v[0].n, v[0].fw_id, (unsigned long)v[0].late,
              v[1].u, v[1].u_max, (unsigned long)v[1].sat, (unsigned long)v[1].n, v[1].fw_id, (unsigned long)v[1].late);
}

// #VLOG <ms>: one compact line per interval for high-speed runs - speeds, dq currents and voltages, the
// voltage demand against the limit (last / peak since the previous line), clipped periods, field weakening.
static uint32_t g_vlog_every = 0, g_vlog_last = 0;
static uint32_t g_cmp_hi[6], g_cmp_polls;   // comparator highs seen by the main loop's polls (loop())

static void printVlog() {
    foc::VoltStats v[2] = { foc::takeVolt(0), foc::takeVolt(1) };
    char           m[2][110];
    for (int k = 0; k < 2; k++)
        snprintf(m[k], sizeof(m[k]), "M%d %.0f dq %+.2f %+.2f v %+.2f %+.2f u %.3f/%.3f sat %lu fw %.2f", k + 1, v[k].obs_rpm,
                 v[k].id, v[k].iq, v[k].vd, v[k].vq, v[k].u, v[k].u_max, (unsigned long)v[k].sat, v[k].fw_id);
    linkEvent("MSG", "VLOG %.0f rpm %s vbus %.2f lim %.2f zerr %+.1f | %s | %s | cmp %lu %lu %lu %lu %lu %lu /%lu", foc::speedCommandRpm(),
              foc::mode() == foc::Mode::Closed ? "CL" : foc::mode() == foc::Mode::Open ? "OL" : "off", hw::vbus(), v[0].vmax,
              rad2deg(foc::zErrorRad()), m[0], m[1], (unsigned long)g_cmp_hi[0], (unsigned long)g_cmp_hi[1],
              (unsigned long)g_cmp_hi[2], (unsigned long)g_cmp_hi[3], (unsigned long)g_cmp_hi[4], (unsigned long)g_cmp_hi[5],
              (unsigned long)g_cmp_polls);
    for (int i = 0; i < 6; i++) g_cmp_hi[i] = 0;
    g_cmp_polls = 0;
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
    } else if (!strcmp(word, "VLOG")) {
        g_vlog_every = n >= 2 && a > 0 ? (uint32_t)(a < 20 ? 20 : a) : 0;
        foc::takeVolt(0);
        foc::takeVolt(1);
    } else if (!strcmp(word, "FW") && n >= 2 && a >= 0 && a <= 6) {
        tu.fw_max_a = a;
        if (n >= 3 && b >= 0.5f && b <= 1.0f) tu.fw_target = b;
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
    } else if (!strcmp(word, "PLL") && n >= 2 && a >= 0 && a <= 400) {
        tu.pll_hz = a;      // tracking filter on the observer's angle, Hz (0 = off: the raw angle and its filtered rate)
    } else if (!strcmp(word, "HARM")) {
        // #HARM [gain]: the learned once / twice per turn pattern of each observer's angle; a gain sets the learning
        // rate (0 = off and forgotten).
        if (n >= 2 && a >= 0 && a <= 1) tu.harm_gain = a;
        float h[2][4];
        for (int k = 0; k < 2; k++) foc::harmonics(k, &h[k][0], &h[k][1], &h[k][2], &h[k][3]);
        linkEvent("MSG", "HARM gain %.3f: M1 1x %.2f deg at %+.0f, 2x %.2f deg at %+.0f | M2 1x %.2f deg at %+.0f, 2x %.2f deg at %+.0f",
                  tu.harm_gain, h[0][0], h[0][1], h[0][2], h[0][3], h[1][0], h[1][1], h[1][2], h[1][3]);
    } else if (!strcmp(word, "CUR2")) {
        // #CUR2 [gain]: the twice-per-turn current compensation each motor has learned; a gain sets the learning rate
        // (0 = off and forgotten).
        if (n >= 2 && a >= 0 && a <= 0.5f) tu.cur2_gain = a;
        float sh[2], dg[2];
        for (int k = 0; k < 2; k++) foc::cur2(k, &sh[k], &dg[k]);
        linkEvent("MSG", "CUR2 gain %.3f: M1 %.2f %% of the flux at %+.0f deg | M2 %.2f %% at %+.0f deg", tu.cur2_gain,
                  sh[0] * 100.0f, dg[0], sh[1] * 100.0f, dg[1]);
    } else if (!strcmp(word, "OBSK") && n >= 2 && a >= 0 && a <= 5) {
        tu.obs_gain = a;    // flux re-centring gain per radian (0 = off: the clamp only)
    } else if (!strcmp(word, "DTC") && n >= 2 && a >= 0 && a <= 1000) {
        tu.deadtime_ns = a;
        if (n >= 3 && b >= 0.05f && b <= 10) tu.dt_knee_a = b;
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
        // #TRIP <sw A> [comparator A] [motor 2 comparator A]: the comparators see the sense input before
        // the PGA, up to ~40 A (the ADC, and so the software trip, stops at ~21 A).
        if (a > 0 && a <= 20) tu.sw_trip_a = a;
        if (n >= 3 && b > 0 && b <= 40) hw::setHardwareTripAmps(b);
        if (n >= 4 && c > 0 && c <= 40) hw::setHardwareTripAmps(1, c);
        linkEvent("MSG", "TRIP: software %.1f A x %d periods, comparators M1 %.1f A M2 %.1f A, break filter %lu",
                  tu.sw_trip_a, SW_TRIP_COUNT, hw::hardwareTripAmps(0), hw::hardwareTripAmps(1),
                  (unsigned long)hw::breakFilter());
    } else if (!strcmp(word, "BKF") && n >= 2 && a >= 0 && a <= 15) {
        if (foc::enabled()) {
            linkEvent("WARN", "BKF refused: the drive must be off");
        } else {
            uint32_t before = TIM1->BDTR;
            hw::setBreakFilter((uint32_t)a);
            linkEvent("MSG", "BKF: break filter %lu (9 = fDTS/8 x 8 = 0.38 us, 12 = fDTS/16 x 8 = 0.75 us, 15 = fDTS/32 x 8 = 1.5 us); "
                             "TIM1 BDTR %08lx -> %08lx, TIM8 %08lx",
                      (unsigned long)hw::breakFilter(), (unsigned long)before, (unsigned long)TIM1->BDTR,
                      (unsigned long)TIM8->BDTR);
        }
    } else if (!strcmp(word, "CMPTEST") && n >= 2) {
        // #CMPTEST <A> [ms] (bring-up, drive off): would the comparators break at this threshold on the idle
        // sense noise alone?  Clears both break flags, watches for ms (max 100), reports which latched and how
        // often each comparator read high, then puts the thresholds back.
        if (foc::enabled() || g_spinning) {
            linkEvent("WARN", "CMPTEST refused: the drive must be off");
        } else {
            float    old0 = hw::hardwareTripAmps(0), old1 = hw::hardwareTripAmps(1);
            uint32_t ms   = n >= 3 && b >= 1 ? (b > 100 ? 100 : (uint32_t)b) : 100;
            hw::setHardwareTripAmps(a);
            delayMicroseconds(50);
            TIM1->SR = ~TIM_SR_BIF;
            TIM8->SR = ~TIM_SR_BIF;
            uint32_t hi[6] = { 0 }, reads = 0;
            uint32_t t0    = millis();
            while (millis() - t0 < ms) {
                uint16_t c6[6];
                hw::comparatorHighCounts(c6, 100);
                for (int i = 0; i < 6; i++) hi[i] += c6[i];
                reads += 100;
            }
            bool b1 = TIM1->SR & TIM_SR_BIF, b8 = TIM8->SR & TIM_SR_BIF;
            hw::setHardwareTripAmps(0, old0);
            hw::setHardwareTripAmps(1, old1);
            delayMicroseconds(50);
            TIM1->SR = ~TIM_SR_BIF;
            TIM8->SR = ~TIM_SR_BIF;
            linkEvent("MSG", "CMPTEST %.1f A, %lu ms, filter %lu: break M1 %s M2 %s | comparator highs per %lu reads: M1 %lu %lu %lu M2 %lu %lu %lu",
                      a, (unsigned long)ms, (unsigned long)hw::breakFilter(), b1 ? "LATCHED" : "no", b8 ? "LATCHED" : "no",
                      (unsigned long)reads, (unsigned long)hi[0], (unsigned long)hi[1], (unsigned long)hi[2], (unsigned long)hi[3],
                      (unsigned long)hi[4], (unsigned long)hi[5]);
        }
    } else if (!strcmp(word, "NOISE")) {
        uint32_t samples = (n >= 2 && a >= 1) ? (uint32_t)a : 2000;
        foc::startNoise(samples);
        g_noise_pending = true;
        linkEvent("MSG", "NOISE: sampling %lu periods (%.0f ms)", (unsigned long)samples,
                  samples / (PWM_FREQUENCY_HZ / 1000.0f));
    } else if (!strcmp(word, "SPIN") && n >= 3) {
        // Bench spin that stops itself: it does not depend on the link delivering the S0.
        float    rpm = a < 0 ? 0 : a > MAX_COMMAND_RPM ? MAX_COMMAND_RPM : a;
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
    } else if (!strcmp(word, "DBANK") && n >= 2) {
        bool        dual = a > 0.5f;
        const char* why  = "";
        if (foc::enabled() || g_spinning || g_gcal_step >= 0 || g_jm_state) {
            linkEvent("WARN", "DBANK refused: the drive must be off");
        } else if (dual == cal::dualBankMode()) {
            linkEvent("MSG", "DBANK: already %s", cal::bankMode());
        } else {
            linkEvent("MSG", "DBANK: erasing the flash and switching to %s mode; the G474 resets with an empty flash - "
                             "reprogram it with $Spindle/G474=FLASH, then #GSAVE the calibration again",
                      dual ? "dual-bank" : "single-bank");
            Link.flush();
            delay(10);
            if (!cal::setBankModeAndWipe(dual, &why)) linkEvent("WARN", "DBANK not changed: %s", why);
        }
    } else if (!strcmp(word, "FLASHCRC") && n >= 2) {
        uint32_t len = a < 1 ? 1 : a > 0x20000 ? 0x20000 : (uint32_t)a;
        linkEvent("MSG", "FLASHCRC 0x08000000 + %lu: %08lx", (unsigned long)len,
                  (unsigned long)cal::crc32Range(0x08000000, len));
    } else if (!strcmp(word, "CALDBG")) {
        uint32_t bank = 0, page = 0;
        cal::erasePageOf(&bank, &page);
        const volatile uint32_t* r = reinterpret_cast<const volatile uint32_t*>(cal::ADDR);
        const volatile uint32_t* b1 = reinterpret_cast<const volatile uint32_t*>(0x0800F800);
        linkEvent("MSG", "CALDBG OPTR %08lx MEMRMP %08lx size %u KB | erase bank %lu page %lu | WRP1A %08lx WRP1B %08lx WRP2A %08lx WRP2B %08lx",
                  (unsigned long)FLASH->OPTR, (unsigned long)SYSCFG->MEMRMP, (unsigned)*(const uint16_t*)FLASHSIZE_BASE,
                  (unsigned long)bank, (unsigned long)page, (unsigned long)FLASH->WRP1AR, (unsigned long)FLASH->WRP1BR,
                  (unsigned long)FLASH->WRP2AR, (unsigned long)FLASH->WRP2BR);
        linkEvent("MSG", "CALDBG at 0x0801F800: %08lx %08lx %08lx %08lx | at 0x0800F800: %08lx %08lx %08lx %08lx | SR %08lx",
                  (unsigned long)r[0], (unsigned long)r[1], (unsigned long)r[2], (unsigned long)r[3], (unsigned long)b1[0],
                  (unsigned long)b1[1], (unsigned long)b1[2], (unsigned long)b1[3], (unsigned long)FLASH->SR);
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
    } else if (!strcmp(word, "BRKDUMP")) {
        // #BRKDUMP [first]: six entries from `first` back (0 = the interrupt that saw the break), newest first;
        // more at once overflow the link's transmit buffer.
        int first = n >= 2 && a >= 0 && a < 16 ? (int)a : 0;
        int shown = 0;
        for (int i = first; i < first + 6 && i < 16; i++) {
            float   amps[6], duty[6];
            uint8_t cmp;
            if (!foc::brkDump(i, amps, duty, &cmp)) continue;
            linkEvent("MSG", "BRKD %3d us: M1 %+6.2f %+6.2f %+6.2f M2 %+6.2f %+6.2f %+6.2f A | cmp %02x | duty M1 %.3f %.3f %.3f M2 %.3f %.3f %.3f",
                      -50 * i, amps[0], amps[1], amps[2], amps[3], amps[4], amps[5], cmp, duty[0], duty[1], duty[2], duty[3],
                      duty[4], duty[5]);
            shown++;
        }
        if (!shown) linkEvent("MSG", "BRKDUMP: no fault recorded since the drive was last enabled");
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
    } else if (!strcmp(word, "SYNC")) {
        // #SYNC: status.  #SYNC 1 [rise ticks after the peak]: start the U9 sync clock on PB4.  #SYNC 0: stop
        // it (PB4 driven low).  The buck needs about 80 us to lock; it free-runs on R30 (~480 kHz) otherwise.
        // Starting, stopping or moving it shifts the sense zero (the buck's ripple lands on the samples as an
        // offset), so it is refused while the drive runs and the zero is measured again afterwards.
        if (n >= 2 && (foc::enabled() || g_spinning)) {
            linkEvent("WARN", "SYNC refused: the drive must be off");
        } else if (n >= 2) {
            const char* why = "";
            if (a < 0.5f) {
                hw::syncStop();
                g_sync_wanted = false;
            } else {
                uint32_t rise = n >= 3 && b >= 0 ? (uint32_t)b : SYNC_RISE_TICKS;
                g_sync_wanted = hw::syncStart(rise, &why);
                if (!g_sync_wanted) linkEvent("WARN", "SYNC start failed: %s", why);
            }
            delay(5);
            if (!measureSenseZero("SYNC", false, &why)) linkEvent("WARN", "SYNC: current-sense zero not measured: %s", why);
        }
        int32_t spread = 0;
        int32_t err    = hw::syncRunning() ? hw::syncPhaseError(&spread) : 0;
        linkEvent("MSG", "SYNC: %s, %lu Hz, rising edge %lu ticks after the PWM peak, phase error %ld ticks (spread %ld) | "
                         "TIM3 CR1 %lx SMCR %lx, clock %lu Hz | PB4 mode %lu AF %lu | PWR CR3 %08lx",
                  hw::syncRunning() ? "RUNNING" : g_sync_wanted ? "STOPPED (sense zero out of range with it on; PB4 low)" : "off (PB4 low)",
                  (unsigned long)(TIMER_CLOCK_HZ / SYNC_PERIOD_TICKS),
                  (unsigned long)hw::syncRise(), (long)err, (long)spread, (unsigned long)TIM3->CR1, (unsigned long)TIM3->SMCR,
                  (unsigned long)hw::tim3ClockHz(), (unsigned long)((GPIOB->MODER >> 8) & 3),
                  (unsigned long)((GPIOB->AFR[0] >> 16) & 15), (unsigned long)PWR->CR3);
    } else if (!strcmp(word, "PINCAP")) {
        // #PINCAP [reps]: rise time on the internal pull-up of PB4 (the sync clock pin) and three unconnected
        // pins for reference.  Extra capacitance on PB4 (the bodge wire, the capacitor) shows as extra time.
        if (foc::enabled() || g_spinning) {
            linkEvent("WARN", "PINCAP refused: the drive must be off");
        } else {
            int reps = n >= 2 && a >= 1 ? (a > 500 ? 500 : (int)a) : 100;
            struct P { char port; int pin; const char* name; } pins[] = {
                { 'B', 3, "PB3 pin 56" }, { 'B', 5, "PB5 pin 58" }, { 'D', 2, "PD2 pin 55" }, { 'B', 4, "PB4 pin 57" } };
            uint32_t avg[4], mx[4];
            for (int i = 0; i < 4; i++) avg[i] = hw::pinRiseCycles(pins[i].port, pins[i].pin, reps, &mx[i]);
            float ref = (avg[0] + avg[1] + avg[2]) / 3.0f;
            float ns  = 1e9f / SystemCoreClock;
            linkEvent("MSG", "PINCAP %d reps, rise to logic 1 on the pull-up, ns avg/max: %s %.0f/%.0f, %s %.0f/%.0f, %s %.0f/%.0f, "
                             "%s %.0f/%.0f | PB4 = %.1f x the reference pins (0 = timed out)",
                      reps, pins[0].name, avg[0] * ns, mx[0] * ns, pins[1].name, avg[1] * ns, mx[1] * ns, pins[2].name, avg[2] * ns,
                      mx[2] * ns, pins[3].name, avg[3] * ns, mx[3] * ns, ref > 0 ? avg[3] / ref : 0.0f);
        }
    } else if (!strcmp(word, "SCOPE")) {
        // #SCOPE <motor 1|2> [every n-th sample]: 1024 samples of that motor's flux a/b, angle error, observer
        // speed, iq_ref, iq, id and reference angle, then one hex line per 32 samples and channel:
        // "X <channel 0-7> <first sample> <32 x 4 hex digits, int16>".  tools/scope.py reads them.
        if (g_scope_print >= 0) {
            linkEvent("WARN", "SCOPE refused: still printing the last one");
        } else {
            int motor = n >= 2 && a >= 1.5f ? 1 : 0;
            int decim = n >= 3 && b >= 1 ? (b > 100 ? 100 : (int)b) : 1;
            foc::startScope(motor, decim);
            g_scope_print = 0;
            // A row takes two interrupts (channels 0-3, then 4-7).
            linkEvent("MSG", "SCOPE: motor %d, 1024 samples, every %d (%.1f ms), flux %.3f mWb: X <ch: 0 flux a, 1 flux b (1e-4 of flux), "
                             "2 angle err (0.01 deg), 3 speed (rpm), 4 iq_ref, 5 iq, 6 id (mA), 7 ref (0.01 deg)> <first> <hex>",
                      motor + 1, 2 * decim, 1024.0f * 2 * decim * 1000.0f / PWM_FREQUENCY_HZ, foc::tuning().flux * 1e3f);
        }
    } else if (!strcmp(word, "SLOW") && n >= 2) {
        hw::setSlowMode((int)a);
        linkEvent("MSG", "SLOW: slow ADC2 conversions %s", a < 0.5f ? "free-running" : a < 1.5f ? "after the current samples" : "off");
    } else if (!strcmp(word, "VER")) {
        linkEvent("MSG", "spindle firmware %s, %s %s", FW_VERSION, __DATE__, __TIME__);
    } else {
        linkEvent("MSG", "# commands: STAT LOG <ms> VLOG <ms> ALIGN [V] [ms] CL <0|1> CUR <kp> <ki> POS <kp> <kd> <ki> POSBW <min> <full rpm> "
                         "OLKI <ki> DTC <ns> [knee A] PLL <Hz> OBSK <gain> HARM [gain] CUR2 [gain] IHOLD/ISPIN/IBOOST/ILIM <A> "
                         "OBS <R> <L> <flux> FW <A> [share] TRIP <sw A> [hw A] [hw M2] "
                         "SPIN <rpm> <ms> JMEAS <rpm> VER");
        linkEvent("MSG", "# bring-up: NOISE [samples] SLOW <0|1|2> DUMP [samples] [channel] BRK BKF <0-15> CMPTEST <A> [ms] BRKDUMP "
                         "[first] MOE LOAD <us> RXLOG PREV GCAL [V] GAIN GAIN1/GAIN2 <a> <b> <c> GSAVE GCLEAR FLASHCRC <len> CALDBG "
                         "DBANK <0|1> SYNC [0|1 [rise]] PINCAP [reps] SCOPE <motor> [n]");
        return;
    }
    if (strcmp(word, "STAT") && strcmp(word, "VER") && strcmp(word, "ALIGN") && strcmp(word, "NOISE") && strcmp(word, "SLOW") &&
        strcmp(word, "DUMP") && strcmp(word, "BRK") &&
        strcmp(word, "MOE") && strcmp(word, "LOAD") && strcmp(word, "SPIN") && strcmp(word, "RXLOG") &&
        strcmp(word, "PREV") && strcmp(word, "GCAL") && strncmp(word, "GAIN", 4) && strcmp(word, "JMEAS") &&
        strcmp(word, "GSAVE") && strcmp(word, "GCLEAR") &&
        strcmp(word, "FLASHCRC") && strcmp(word, "CALDBG") && strcmp(word, "DBANK") && strcmp(word, "VLOG") &&
        strcmp(word, "TRIP") && strcmp(word, "BKF") && strcmp(word, "CMPTEST") && strcmp(word, "BRKDUMP") &&
        strcmp(word, "SYNC") && strcmp(word, "PINCAP") && strcmp(word, "SCOPE") && strcmp(word, "HARM") && strcmp(word, "CUR2")) {
        linkEvent("MSG", "tuning: cur %.3f/%.0f pos %.2f/%.3f/%.1f bw %.2f/%.0f olki %.1f I hold %.1f spin %.1f "
                         "boost %.1f lim %.1f obs R %.3f L %.1fuH flux %.2fmWb trip %.1f A dtc %.0f ns/%.2f A CL %d fw %.1f A/%.2f "
                         "pll %.0f Hz obsk %.2f cur2 %.3f",
                  tu.cur_kp, tu.cur_ki, tu.pos_kp, tu.pos_kd, tu.pos_ki, tu.pos_bw_min, tu.pos_bw_full_rpm, tu.ol_ki,
                  tu.hold_a, tu.spin_a, tu.boost_a, tu.iq_limit,
                  tu.r, tu.l * 1e6f, tu.flux * 1e3f, tu.sw_trip_a, tu.deadtime_ns, tu.dt_knee_a, (int)tu.closed_loop_allowed, tu.fw_max_a,
                  tu.fw_target, tu.pll_hz, tu.obs_gain, tu.cur2_gain);
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
    foc::background();
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
                                         "#VLOG print", "trip recorder", "LED", "end of loop" };
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
        if (g_init_ok && SYNC_AT_BOOT) {
            // Before the offset calibration below (board.h); the buck locks in ~80 us, well inside the settling delay.
            const char* swhy = "";
            if (hw::syncStart(SYNC_RISE_TICKS, &swhy)) {
                g_sync_wanted = true;
                BOOT_TRACE_POINT("U9 sync clock on");
            } else {
                linkEvent("WARN", "U9 sync clock did not start: %s", swhy);
            }
        }
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
        g_init_ok = measureSenseZero("boot", true, &why);
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
    {   // #VLOG: how often each over-current comparator reads high (spikes the break filter rejects)
        uint8_t bits = hw::comparatorOutputs();
        for (int i = 0; i < 6; i++)
            if (bits & (1u << i)) g_cmp_hi[i]++;
        g_cmp_polls++;
    }
    uint32_t now = millis();
    if (g_spin_until && (int32_t)(now - g_spin_until) >= 0) {
        g_spin_until = 0;
        setSpindleSpeed(0);
        linkEvent("MSG", "SPIN: time up, ramping down");
    }
    if (g_bench_spin && !g_spin_until && !g_spinning && !foc::enabled()) g_bench_spin = false;   // ramped down

    CRUMB(1);
    hw::serviceSlowAnalog();
    // With U9 locked its ripple sits on the samples as an offset that depends on U9's input voltage: if the bus
    // has moved (USB first, then 36 V; or the 36 V switched off and on) measure the zero again, drive off only,
    // and only once the bus is steady (the bus capacitors take seconds to drain).
    if (now - g_vbus_track_ms >= 100) {
        float vb = hw::vbus();
        if (isnan(g_vbus_track) || fabsf(vb - g_vbus_track) > 0.5f) g_vbus_steady_since = now;
        g_vbus_track    = vb;
        g_vbus_track_ms = now;
    }
    if (g_init_ok && now - g_vbus_steady_since > 500 && now - g_zero_ms > 500) {
        if (isnan(g_zero_vbus)) {
            g_zero_vbus = g_vbus_track;   // the boot zero: the slow analog was not running yet when it was taken
        } else if (g_sync_wanted && fabsf(g_vbus_track - g_zero_vbus) > SYNC_REZERO_V && !foc::enabled() && !g_spinning &&
                   g_align_until == 0 && g_gcal_step < 0) {
            float       from = g_zero_vbus;
            const char* why  = "";
            if (!hw::syncRunning()) {   // stopped at the old bus voltage: it may be fine at this one
                if (hw::syncStart(hw::syncRise(), &why)) delay(5);
            }
            if (measureSenseZero("bus change", false, &why)) {
                linkEvent("MSG", "current-sense zero re-measured: bus %.1f -> %.1f V (U9's ripple offset follows its input)", from,
                          g_vbus_track);
            } else {
                linkEvent("WARN", "current-sense zero not measured after a bus change: %s", why);
            }
        }
    }
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
    // #SCOPE lines: 32 samples of one channel each as hex, one line every 20 ms.
    if (g_scope_print >= 0 && foc::scopeDone() && now - g_scope_last_ms >= 20 && Link.availableForWrite() >= 180) {
        g_scope_last_ms = now;
        const int per = 32, lines_per_chan = 1024 / per;
        int       ch = g_scope_print / lines_per_chan, first = (g_scope_print % lines_per_chan) * per;
        char      buf[160];
        int       len = snprintf(buf, sizeof(buf), "X %d %d ", ch, first);
        for (int i = first; i < first + per; i++) len += snprintf(buf + len, sizeof(buf) - len, "%04x", (uint16_t)foc::scopeValue(i, ch));
        linkEvent("MSG", "%s", buf);
        if (++g_scope_print >= lines_per_chan * foc::SCOPE_CH) {
            g_scope_print = -1;
            linkEvent("MSG", "SCOPE done");
        }
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
    if (g_vlog_every && now - g_vlog_last >= g_vlog_every) {
        g_vlog_last = now;
        CRUMB(14);
        printVlog();
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
