#include "load_sense.h"
#include "config.h"
#include <Preferences.h>
#include <stdarg.h>
#include "usb_console.h"

// ------------------- State -------------------

static bool g_load_print = false;   // [LOAD] console lines (USB "LOAD"); the fit always runs

// Adaptive voltage
static bool  s_adapt_enabled = ADAPT_DEFAULT_ON;
static float s_trim[2]       = { 0.0f, 0.0f };
static float s_trim_vel[2]   = { 0.0f, 0.0f };   // target velocity the trim was learned at
static bool  s_prev_warn[2]  = { false, false };
static uint32_t s_last_warn_ms[2] = { 0, 0 };     // millis() of the last warning on each motor

// Advisory load warning, per motor
static bool     s_warn[2]          = { false, false };
static uint32_t s_warn_pend_since[2] = { 0, 0 };   // 0 = not pending
static uint32_t s_clear_since[2]   = { 0, 0 };

// Per-motor least-squares accumulators (exponentially forgotten every pass).
struct Fit {
    float suu = 0, suw = 0, sww = 0, siu = 0, siw = 0, n = 0;
};
static Fit          s_fit[2];
static LoadEstimate s_est[2];
static float        s_r_ohm[2]   = { LOAD_R_OHM, LOAD_R_OHM };

// Electrical offsets of phases A, B, C as used by SimpleFOC's inverse Clarke: the commanded phase-k
// voltage is  -Uq * sin(theta_e - phi_k)  with phi = 0, +120, -120 deg.
static const float PHASE_PHI[3] = { 0.0f, 2.0943951f, -2.0943951f };

// No-load baseline (LOADREF), indexed by speed: point i is (i+1) * step RPM.
static constexpr uint32_t REF_MAGIC = 0x4C524546UL;  // 'LREF'
struct RefTable {
    uint32_t magic = 0;
    uint16_t n     = 0;
    uint16_t step  = 0;
    float    ip[2][LOAD_REF_MAX_POINTS];
    float    iq[2][LOAD_REF_MAX_POINTS];
};
static RefTable s_ref;

// RTEST / LOADREF sequencer
enum Mode : uint8_t { M_IDLE, M_RT_SETTLE, M_RT_MEASURE, M_REF_RAMP, M_REF_SETTLE, M_REF_MEASURE,
                     M_REF_STOPPING, M_VS_SETTLE, M_VS_MEASURE };

// VSWEEP state: the voltage offset applied to both motors, and the speed it was started at
static float s_vs_offset = 0.0f;
static float s_vs_target[2];
static int   s_vs_steps = 0;
static float s_vs_best_i[2], s_vs_best_dv[2];
static Mode     s_mode = M_IDLE;
static uint32_t s_timer = 0;
static bool     s_was_enabled[2];
static int      s_ref_idx = 0, s_ref_points = 0;
static float    s_sum_ip[2], s_sum_iq[2], s_sum_v[2];
static int      s_cnt[2];
static RefTable s_new_ref;

// ------------------- Console output -------------------

// Print a line, waiting up to 20 ms for room in the 256-byte USB TX buffer (core 0 only, so a short
// wait is harmless).  Lines must stay well under 256 bytes or they can never fit.
static void say(const char* fmt, ...) {
    if (!Serial) return;
    char    line[180];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
    for (int i = 0; i < 20 && !usbWriteIfRoom(line, n); i++) vTaskDelay(pdMS_TO_TICKS(1));
}

// ------------------- Duty weighting -------------------

// Fraction of a PWM period in which a phase's sense output reads its current: the low side
// conducts for (1 - duty) of the period, less half the dead time (the low side turns on dead/2
// after the high side turns off), less roughly half the sense output's settling time (readings
// in that interval are partly right).  The settling part is an estimate; see LOAD_SOX_SETTLE_US.
static const float WINDOW_LOSS = 0.5f * DEAD_ZONE + 0.5f * LOAD_SOX_SETTLE_US * 1e-6f * (float)PWM_FREQUENCY;

// ------------------- Fit -------------------

static float refAt(const float* table, float rpm) {
    if (s_ref.magic != REF_MAGIC || s_ref.n == 0 || s_ref.step == 0) return NAN;
    float x = rpm / (float)s_ref.step - 1.0f;   // point i is at (i+1)*step
    if (x <= 0.0f) return table[0];
    if (x >= s_ref.n - 1) return table[s_ref.n - 1];
    int   i = (int)x;
    float f = x - i;
    return table[i] * (1.0f - f) + table[i + 1] * f;
}

static void solve(int m, MotorController& mc) {
    Fit&          f = s_fit[m];
    LoadEstimate& e = s_est[m];
    e       = LoadEstimate();
    e.n_eff = f.n;
    float det = f.suu * f.sww - f.suw * f.suw;
    if (f.n < 30.0f || det <= 1e-4f * f.suu * f.sww) return;  // too few or too clustered readings

    // Rotation direction: an angle-domain shift is a time lag for forward rotation and a lead
    // for reverse, so flip Iq for reverse to report a time lag either way.
    float s = (mc.velocity_mode && mc.current_velocity < -0.5f) ? -1.0f : 1.0f;
    e.ip      = (f.siu * f.sww - f.siw * f.suw) / det;
    e.iq      = s * (f.siw * f.suu - f.siu * f.suw) / det;
    e.lag_deg = atan2f(e.iq, e.ip) * 180.0f / PI;
    e.valid   = true;

    float rpm = fabsf(mc.current_velocity) * 60.0f / (2.0f * PI);
    if (mc.velocity_mode && rpm > 0.5f * LOAD_REF_STEP_RPM) {
        float ip0 = refAt(s_ref.ip[m], rpm);
        float iq0 = refAt(s_ref.iq[m], rpm);
        if (!isnan(ip0)) e.dip = e.ip - ip0;
        if (!isnan(ip0) && !isnan(iq0)) {
            e.lag_ref = atan2f(iq0, ip0) * 180.0f / PI;
            float d   = e.lag_deg - e.lag_ref;
            while (d > 180.0f) d -= 360.0f;
            while (d <= -180.0f) d += 360.0f;
            e.lag_dev = d;
        }
    }

    // Load angle: E = V - (R + j w L) I, with I = Ip - j Iq in the frame of the applied voltage.
    float R = s_r_ohm[m], L = LOAD_L_H;
    if (R > 0.0f && L > 0.0f && mc.velocity_mode) {
        float V  = mc.motor.voltage_limit;
        float wL = fabsf(mc.current_velocity) * POLE_PAIRS * L;
        e.delta_deg = atan2f(wL * e.ip - R * e.iq, V - R * e.ip - wL * e.iq) * 180.0f / PI;
    }
}

// ------------------- Sequencer helpers -------------------

static void setSpindleTargets(MotorController* motors[2], float rpm) {
    for (int m = 0; m < 2; m++) {
        motors[m]->target_velocity     = rpm * 2.0f * PI / 60.0f * motors[m]->direction;
        motors[m]->velocity_mode       = (rpm > 0.0f) || motors[m]->velocity_mode;
        motors[m]->continuous_rotation = false;
    }
}

static void resetSums() {
    for (int m = 0; m < 2; m++) { s_sum_ip[m] = s_sum_iq[m] = s_sum_v[m] = 0.0f; s_cnt[m] = 0; }
}

static void accumulateSums(MotorController* motors[2]) {
    for (int m = 0; m < 2; m++) {
        if (!s_est[m].valid) continue;
        s_sum_ip[m] += s_est[m].ip;
        s_sum_iq[m] += s_est[m].iq;
        s_sum_v[m]  += motors[m]->motor.voltage_limit;
        s_cnt[m]++;
    }
}

static void endRTest(MotorController* motors[2]) {
    for (int m = 0; m < 2; m++) {
        if (!s_was_enabled[m]) motors[m]->disable();
    }
    s_mode = M_IDLE;
}

static void finishRTest(MotorController* motors[2]) {
    say("[RTEST] ===== standstill check (DC, so I = V/R and current in phase with voltage) =====\n");
    for (int m = 0; m < 2; m++) {
        if (s_cnt[m] == 0) {
            say("[RTEST] M%d: no valid readings - the low-side window never fit a conversion\n", m + 1);
            continue;
        }
        float ip = s_sum_ip[m] / s_cnt[m], iq = s_sum_iq[m] / s_cnt[m], v = s_sum_v[m] / s_cnt[m];
        float lag = atan2f(iq, ip) * 180.0f / PI;
        say("[RTEST] M%d: V=%.2f V  Ip=%.3f A  Iq=%.3f A  angle=%+.1f deg  (%d fits)\n",
            m + 1, v, ip, iq, lag, s_cnt[m]);
        if (ip <= 0.05f && fabsf(lag) > 165.0f) {
            say("[RTEST] M%d: SIGN INVERTED - flip LOAD_CURRENT_SIGN in config.h\n", m + 1);
        } else if (fabsf(lag) > 15.0f || ip <= 0.05f) {
            say("[RTEST] M%d: CHECK FAILED - expected angle ~0 deg; angle convention or window timing is off\n", m + 1);
        } else {
            s_r_ohm[m] = v / ip;
            say("[RTEST] M%d: OK. R = %.3f ohm per phase incl. driver (using it until reboot; "
                "set LOAD_R_OHM to keep)\n", m + 1, s_r_ohm[m]);
        }
    }
    endRTest(motors);
}

static void saveRef() {
    Preferences p;
    if (p.begin("loadref", false)) {
        p.putBytes("tbl", &s_ref, sizeof(s_ref));
        p.end();
    }
}

static void finishRefPoint(MotorController* motors[2]) {
    int rpm = (s_ref_idx + 1) * LOAD_REF_STEP_RPM;
    for (int m = 0; m < 2; m++) {
        s_new_ref.ip[m][s_ref_idx] = s_cnt[m] ? s_sum_ip[m] / s_cnt[m] : NAN;
        s_new_ref.iq[m][s_ref_idx] = s_cnt[m] ? s_sum_iq[m] / s_cnt[m] : NAN;
    }
    say("[LOADREF] %5d RPM  M1 Ip=%.3f Iq=%.3f  M2 Ip=%.3f Iq=%.3f A\n", rpm,
        s_new_ref.ip[0][s_ref_idx], s_new_ref.iq[0][s_ref_idx],
        s_new_ref.ip[1][s_ref_idx], s_new_ref.iq[1][s_ref_idx]);

    if (++s_ref_idx >= s_ref_points) {
        // Do NOT write flash yet: a flash write stalls both cores, including the FOC loop, and an
        // open-loop rotor at full speed with commutation frozen slips at once.  Save once stopped.
        setSpindleTargets(motors, 0.0f);
        s_new_ref.magic = REF_MAGIC;
        s_new_ref.n     = (uint16_t)s_ref_points;
        s_new_ref.step  = (uint16_t)LOAD_REF_STEP_RPM;
        say("[LOADREF] sweep complete - ramping down; will save once the spindle has stopped\n");
        s_timer = millis();
        s_mode  = M_REF_STOPPING;
        return;
    }
    setSpindleTargets(motors, (float)((s_ref_idx + 1) * LOAD_REF_STEP_RPM));
    s_timer = millis();
    s_mode  = M_REF_RAMP;
}

// ------------------- Public API -------------------

void loadSenseInit() {
    Preferences p;
    if (p.begin("loadref", true)) {
        RefTable t;
        if (p.getBytes("tbl", &t, sizeof(t)) == sizeof(t) && t.magic == REF_MAGIC) s_ref = t;
        p.end();
    }
}

static MotorController* s_motors[2] = { nullptr, nullptr };

static void turnOn() {
    if (g_load_print) return;
    g_load_print = true;
    say("[LOAD] on: Ip/Iq are A peak; lag = current behind voltage; dIp = Ip above the no-load "
        "baseline%s\n", (s_ref.magic == REF_MAGIC) ? "" : " (none yet - run LOADREF)");
}

void loadSenseToggle() {
    if (g_load_print && s_mode != M_IDLE) {
        say("[LOAD] stays on while RTEST/LOADREF runs\n");
        return;
    }
    if (g_load_print) {
        g_load_print = false;
        say("[LOAD] lines off (sensing and warnings keep running)\n");
        return;
    }
    turnOn();
}

void adaptToggle() {
    s_adapt_enabled = !s_adapt_enabled;
    if (s_adapt_enabled)
        say("[LOAD] adaptive voltage ENABLED: holding each motor's Iq at %.1f A above %.0f RPM\n",
            ADAPT_IQ_TARGET_A, ADAPT_MIN_RPM);
    else
        say("[LOAD] adaptive voltage disabled until reboot (ADAPT to re-enable) - calibrated voltage only\n");
}

bool adaptEnabled() {
    return s_adapt_enabled;
}

float adaptTrim(int motor_idx) {
    return s_trim[motor_idx & 1];
}

static bool vsweepActive() {
    return s_mode == M_VS_SETTLE || s_mode == M_VS_MEASURE;
}

// Per-motor adaptive voltage trim (see ADAPT_* in config.h), handed to the FOC loop through
// load_boost_v.  VSWEEP owns that offset while it runs.
static void updateAdaptive(MotorController* motors[2], bool allow, float dt, uint32_t now) {
    if (vsweepActive()) {
        for (int m = 0; m < 2; m++) {
            s_trim[m]               = 0.0f;
            motors[m]->load_boost_v = s_vs_offset;
        }
        return;
    }
    for (int m = 0; m < 2; m++) {
        MotorController&    mc  = *motors[m];
        const LoadEstimate& e   = s_est[m];
        float               rpm = fabsf(mc.current_velocity) * 60.0f / (2.0f * PI);

        // Stopped, Z-holding, or a new speed commanded: back to the calibrated voltage.
        if (!s_adapt_enabled || !mc.velocity_mode || !mc.enabled ||
            fabsf(mc.target_velocity - s_trim_vel[m]) > 0.5f) {
            s_trim[m]     = 0.0f;
            s_trim_vel[m] = mc.target_velocity;
        }
        bool at_speed = fabsf(mc.current_velocity - mc.target_velocity) < 1.0f;
        bool active   = s_adapt_enabled && allow && s_mode == M_IDLE && mc.velocity_mode && mc.enabled &&
                        at_speed && rpm >= ADAPT_MIN_RPM && e.valid && !isnan(e.iq);

        // A load warning means the slower loop below was not fast enough: add voltage at once.
        bool warn_onset = s_warn[m] && !s_prev_warn[m];
        s_prev_warn[m]  = s_warn[m];
        if (s_warn[m]) s_last_warn_ms[m] = now;
        bool warn_recent = (now - s_last_warn_ms[m]) < ADAPT_WARN_HOLD_MS && s_last_warn_ms[m] != 0;

        if (active) {
            float err = ADAPT_IQ_TARGET_A - e.iq;   // > 0: Iq too low -> more voltage
            float rate;
            if (e.iq < ADAPT_IQ_FAST_A) {
                rate = ADAPT_UP_MAX_V_PER_S;          // near or past the edge
            } else if (err > 0.0f) {
                rate = fminf(ADAPT_K_UP * err, ADAPT_UP_MAX_V_PER_S);
            } else if (e.iq > ADAPT_IQ_HIGH_A && !warn_recent) {
                rate = fmaxf(ADAPT_K_DOWN_FAST * err, -ADAPT_DOWN_FAST_V_PER_S);
            } else {
                rate = fmaxf(ADAPT_K_DOWN * err, -ADAPT_DOWN_MAX_V_PER_S);
            }
            s_trim[m] += rate * dt;
            if (warn_onset) s_trim[m] += ADAPT_WARN_STEP_V;
            s_trim[m] = constrain(s_trim[m], ADAPT_TRIM_MIN_V, ADAPT_TRIM_MAX_V);
        }
        // Inactive (ramping, a fit gap, calibrating, ...): hold the trim as it is.
        mc.load_boost_v = s_trim[m];
    }
}

uint8_t loadWarnMask() {
    return (s_warn[0] ? 1 : 0) | (s_warn[1] ? 2 : 0);
}

// Advisory stall warning for one motor.  Only judged at a steady commanded speed with a baseline;
// otherwise any warning is dropped (a stopped or ramping motor is not comparable to the baseline).
static void updateWarning(int m, const MotorController& mc, bool allow, uint32_t now) {
    const LoadEstimate& e = s_est[m];
    bool at_speed = mc.velocity_mode && mc.enabled &&
                    fabsf(mc.current_velocity - mc.target_velocity) < 1.0f &&
                    fabsf(mc.current_velocity) * 60.0f / (2.0f * PI) >= 0.5f * LOAD_REF_STEP_RPM;
    if (!allow || s_mode != M_IDLE || !at_speed || !e.valid || isnan(e.lag_dev)) {
        s_warn[m] = false;
        s_warn_pend_since[m] = s_clear_since[m] = 0;
        return;
    }
    float dev = fabsf(e.lag_dev);
    if (!s_warn[m]) {
        if (dev > LOAD_WARN_LAG_DEG) {
            if (!s_warn_pend_since[m]) s_warn_pend_since[m] = now ? now : 1;
            if (now - s_warn_pend_since[m] >= LOAD_WARN_HOLD_MS) {
                s_warn[m]         = true;
                s_clear_since[m]  = 0;
            }
        } else {
            s_warn_pend_since[m] = 0;
        }
    } else {
        if (dev < LOAD_WARN_CLEAR_DEG) {
            if (!s_clear_since[m]) s_clear_since[m] = now ? now : 1;
            if (now - s_clear_since[m] >= LOAD_WARN_CLEAR_MS) {
                s_warn[m]            = false;
                s_warn_pend_since[m] = 0;
            }
        } else {
            s_clear_since[m] = 0;
        }
    }
}

bool loadSenseBusy() {
    return s_mode != M_IDLE;
}

void loadSenseAbort(const char* why) {
    if (s_mode == M_IDLE) return;
    if (vsweepActive()) {
        s_vs_offset = 0.0f;   // back to the calibrated voltage at once (more voltage is the safe way)
        if (s_motors[0]) s_motors[0]->load_boost_v = 0.0f;
        if (s_motors[1]) s_motors[1]->load_boost_v = 0.0f;
        s_mode = M_IDLE;
        say("[VSWEEP] stopped (%s) - voltage restored to the calibrated value\n", why);
        return;
    }
    if (s_mode == M_RT_SETTLE || s_mode == M_RT_MEASURE) {
        endRTest(s_motors);
        say("[RTEST] aborted: %s\n", why);
    } else {
        if (s_mode != M_REF_STOPPING) setSpindleTargets(s_motors, 0.0f);
        s_mode = M_IDLE;
        say("[LOADREF] aborted (%s) - baseline not changed\n", why);
    }
}

void loadSenseStartRTest(MotorController& mc1, MotorController& mc2) {
    MotorController* motors[2] = { &mc1, &mc2 };
    if (s_mode != M_IDLE) { say("[RTEST] busy\n"); return; }
    for (int m = 0; m < 2; m++) {
        if (motors[m]->velocity_mode || motors[m]->continuous_rotation) {
            say("[RTEST] refused: stop the spindle first\n");
            return;
        }
    }
    s_motors[0] = &mc1; s_motors[1] = &mc2;
    turnOn();
    // Hold both motors still in angle mode at their current target (as a Z hold does).
    for (int m = 0; m < 2; m++) {
        s_was_enabled[m] = motors[m]->enabled;
        if (!motors[m]->enabled) {
            motors[m]->resetFilterState();
            motors[m]->reached_speed    = false;
            motors[m]->motor.controller = MotionControlType::angle_openloop;
            motors[m]->enable();
        }
    }
    s_timer = millis();
    s_mode  = M_RT_SETTLE;
    say("[RTEST] holding both motors at standstill for 2 s...\n");
}

void loadSenseStartBaseline(MotorController& mc1, MotorController& mc2) {
    MotorController* motors[2] = { &mc1, &mc2 };
    if (s_mode != M_IDLE) { say("[LOADREF] busy\n"); return; }
    for (int m = 0; m < 2; m++) {
        if (motors[m]->velocity_mode || motors[m]->continuous_rotation) {
            say("[LOADREF] refused: stop the spindle first\n");
            return;
        }
    }
    s_motors[0] = &mc1; s_motors[1] = &mc2;
    turnOn();
    s_ref_points = min(MAX_COMMAND_RPM / LOAD_REF_STEP_RPM, LOAD_REF_MAX_POINTS);
    s_ref_idx    = 0;
    s_new_ref    = RefTable();
    for (int m = 0; m < 2; m++)
        for (int i = 0; i < LOAD_REF_MAX_POINTS; i++) s_new_ref.ip[m][i] = s_new_ref.iq[m][i] = NAN;
    say("[LOADREF] no-load sweep %d-%d RPM in %d RPM steps - NO cutting load. 'x' aborts.\n",
        LOAD_REF_STEP_RPM, s_ref_points * LOAD_REF_STEP_RPM, LOAD_REF_STEP_RPM);
    setSpindleTargets(motors, (float)LOAD_REF_STEP_RPM);
    s_timer = millis();
    s_mode  = M_REF_RAMP;
}

void loadSenseStartVSweep(MotorController& mc1, MotorController& mc2) {
    MotorController* motors[2] = { &mc1, &mc2 };
    if (s_mode != M_IDLE) { say("[VSWEEP] busy\n"); return; }
    for (int m = 0; m < 2; m++) {
        const MotorController& mc = *motors[m];
        if (!mc.velocity_mode || !mc.enabled || fabsf(mc.target_velocity) < 1.0f ||
            fabsf(mc.current_velocity - mc.target_velocity) > 1.0f) {
            say("[VSWEEP] refused: spin the spindle at a steady speed first (e.g. 6 = 12000 RPM)\n");
            return;
        }
    }
    s_motors[0] = &mc1; s_motors[1] = &mc2;
    for (int m = 0; m < 2; m++) {
        s_vs_target[m]  = motors[m]->target_velocity;
        s_vs_best_i[m]  = 1e9f;
        s_vs_best_dv[m] = 0.0f;
    }
    s_vs_offset = VSWEEP_START_V;
    s_vs_steps  = 0;
    say("[VSWEEP] %.0f RPM, no cutting load: dV from %+.2f V down in %.2f V steps; stops when lag < %.0f deg. 'x' aborts.\n",
        fabsf(mc1.current_velocity) * 60.0f / (2.0f * PI), VSWEEP_START_V, VSWEEP_STEP_V, VSWEEP_STOP_LAG_DEG);
    say("[VSWEEP]   dV  |  M1 V   |I|pk   Ip    Iq   lag |  M2 V   |I|pk   Ip    Iq   lag\n");
    s_timer = millis();
    s_mode  = M_VS_SETTLE;
}

static void finishVSweep(const char* why) {
    s_vs_offset = 0.0f;
    if (s_motors[0]) s_motors[0]->load_boost_v = 0.0f;
    if (s_motors[1]) s_motors[1]->load_boost_v = 0.0f;
    s_mode = M_IDLE;
    say("[VSWEEP] done (%s) after %d steps; voltage restored\n", why, s_vs_steps);
    for (int m = 0; m < 2; m++)
        say("[VSWEEP] M%d lowest current %.2f A pk at dV=%+.2f V\n", m + 1, s_vs_best_i[m], s_vs_best_dv[m]);
}

const LoadEstimate& loadEstimate(int motor_idx) {
    return s_est[motor_idx & 1];
}

void loadSenseUpdate(MotorController& mc1, MotorController& mc2, float dt, bool allow_warn) {
    MotorController* motors[2] = { &mc1, &mc2 };
    s_motors[0] = &mc1; s_motors[1] = &mc2;

    float lambda = (dt > 0.0f) ? expf(-dt / LOAD_FILTER_TAU_S) : 1.0f;
    for (int m = 0; m < 2; m++) {
        Fit& f = s_fit[m];
        if (!motors[m]->enabled) {   // outputs off: no low-side conduction, and no current anyway
            f        = Fit();
            s_est[m] = LoadEstimate();
            continue;
        }
        f.suu *= lambda; f.suw *= lambda; f.sww *= lambda;
        f.siu *= lambda; f.siw *= lambda; f.n   *= lambda;
        // The three reads updateCurrent() just made.  Each one lands at an effectively random
        // point in the PWM period, so it reads the phase current with probability g = the
        // low-side fraction, and ~0 otherwise: its expected value is i * g.  Fitting against the
        // voltage-shaped basis functions multiplied by g therefore estimates i without bias.
        const MotorController& mc = *motors[m];
        const float            meas[3] = { LOAD_CURRENT_SIGN * mc.last_current_a,
                                           LOAD_CURRENT_SIGN * mc.last_current_b,
                                           LOAD_CURRENT_SIGN * mc.last_current_c };
        for (int k = 0; k < 3; k++) {
            float g = 1.0f - mc.sample_duty[k] - WINDOW_LOSS;
            if (g <= 0.02f) continue;                         // phase near 100% duty: no information
            float th = mc.sample_theta_e[k];
            float u  = -sinf(th - PHASE_PHI[k]) * g;          // commanded phase-voltage shape x g
            float w  =  cosf(th - PHASE_PHI[k]) * g;          // the same, lagging 90 deg, x g
            float i  = meas[k];
            f.suu += u * u; f.suw += u * w; f.sww += w * w;
            f.siu += i * u; f.siw += i * w; f.n   += 1.0f;
        }
        solve(m, *motors[m]);
    }

    // ---- RTEST / LOADREF sequencing ----
    uint32_t now = millis();
    switch (s_mode) {
        case M_RT_SETTLE:
        case M_RT_MEASURE:
            if (!mc1.enabled || !mc2.enabled || mc1.velocity_mode || mc2.velocity_mode) {
                loadSenseAbort("a motor was stopped or commanded to spin");
                break;
            }
            if (s_mode == M_RT_SETTLE) {
                if (now - s_timer >= 1000) { resetSums(); s_timer = now; s_mode = M_RT_MEASURE; }
            } else {
                accumulateSums(motors);
                if (now - s_timer >= 1000) finishRTest(motors);
            }
            break;

        case M_REF_RAMP:
        case M_REF_SETTLE:
        case M_REF_MEASURE:
            if (!mc1.velocity_mode || !mc2.velocity_mode) {   // E-stop or fault cleared them
                loadSenseAbort("spindle was stopped");
                break;
            }
            if (s_mode == M_REF_RAMP) {
                bool there = fabsf(mc1.current_velocity - mc1.target_velocity) < 0.5f &&
                             fabsf(mc2.current_velocity - mc2.target_velocity) < 0.5f;
                if (there) { s_timer = now; s_mode = M_REF_SETTLE; }
                else if (now - s_timer > 15000) loadSenseAbort("speed not reached in 15 s");
            } else if (s_mode == M_REF_SETTLE) {
                if (now - s_timer >= LOAD_REF_SETTLE_MS) { resetSums(); s_timer = now; s_mode = M_REF_MEASURE; }
            } else {
                accumulateSums(motors);
                if (now - s_timer >= LOAD_REF_MEASURE_MS) finishRefPoint(motors);
            }
            break;

        case M_VS_SETTLE:
        case M_VS_MEASURE: {
            bool same_speed = true;
            for (int m = 0; m < 2; m++)
                same_speed = same_speed && motors[m]->velocity_mode && motors[m]->enabled &&
                             fabsf(motors[m]->target_velocity - s_vs_target[m]) < 0.5f;
            if (!same_speed) { loadSenseAbort("spindle stopped or speed changed"); break; }
            for (int m = 0; m < 2; m++) {
                if (s_est[m].valid && (s_est[m].lag_deg < VSWEEP_ABORT_LAG_DEG || s_est[m].iq < VSWEEP_ABORT_IQ_A)) {
                    char why[64];
                    snprintf(why, sizeof(why), "M%d near the edge: lag %.0f deg, Iq %.2f A", m + 1,
                             s_est[m].lag_deg, s_est[m].iq);
                    finishVSweep(why);
                    break;
                }
            }
            if (s_mode == M_IDLE) break;
            if (s_mode == M_VS_SETTLE) {
                if (now - s_timer >= VSWEEP_SETTLE_MS) { resetSums(); s_timer = now; s_mode = M_VS_MEASURE; }
                break;
            }
            accumulateSums(motors);
            if (now - s_timer < VSWEEP_MEASURE_MS) break;
            // Step complete: report it, then step down or stop.
            float ip[2], iq[2], v[2], lag[2], mag[2];
            for (int m = 0; m < 2; m++) {
                int c  = s_cnt[m] ? s_cnt[m] : 1;
                ip[m]  = s_sum_ip[m] / c;
                iq[m]  = s_sum_iq[m] / c;
                v[m]   = s_sum_v[m] / c;
                lag[m] = atan2f(iq[m], ip[m]) * 180.0f / PI;
                mag[m] = sqrtf(ip[m] * ip[m] + iq[m] * iq[m]);
                if (s_cnt[m] && mag[m] < s_vs_best_i[m]) { s_vs_best_i[m] = mag[m]; s_vs_best_dv[m] = s_vs_offset; }
            }
            s_vs_steps++;
            say("[VSWEEP] %+5.2f | %5.2f %5.2f %5.2f %5.2f %4.0f | %5.2f %5.2f %5.2f %5.2f %4.0f\n",
                s_vs_offset, v[0], mag[0], ip[0], iq[0], lag[0], v[1], mag[1], ip[1], iq[1], lag[1]);
            if (lag[0] < VSWEEP_STOP_LAG_DEG || lag[1] < VSWEEP_STOP_LAG_DEG ||
                iq[0] < VSWEEP_STOP_IQ_A || iq[1] < VSWEEP_STOP_IQ_A) {
                finishVSweep("lag or Iq reached the stop threshold");
            } else if (s_vs_offset - VSWEEP_STEP_V < VSWEEP_MIN_V ||
                       v[0] <= BASE_VOLTAGE + 0.05f || v[1] <= BASE_VOLTAGE + 0.05f) {
                finishVSweep("reached the lowest allowed voltage");
            } else {
                s_vs_offset -= VSWEEP_STEP_V;
                s_timer = now;
                s_mode  = M_VS_SETTLE;
            }
            break;
        }

        case M_REF_STOPPING: {
            bool stopped = !mc1.velocity_mode && !mc2.velocity_mode &&
                           fabsf(mc1.current_velocity) < 0.5f && fabsf(mc2.current_velocity) < 0.5f;
            if (stopped) {
                s_ref = s_new_ref;
                saveRef();
                say("[LOADREF] done - %d points saved\n", s_ref_points);
                s_mode = M_IDLE;
            } else if (now - s_timer > 20000) {
                loadSenseAbort("spindle did not stop within 20 s, so the baseline was not saved");
            }
            break;
        }

        default:
            break;
    }

    for (int m = 0; m < 2; m++) updateWarning(m, *motors[m], allow_warn, now);
    updateAdaptive(motors, allow_warn, dt, now);

    // ---- Periodic console line ----
    if (!g_load_print) return;
    static uint32_t last_log = 0;
    if (now - last_log < LOAD_LOG_INTERVAL_MS) return;
    last_log = now;
    for (int m = 0; m < 2; m++) {
        const MotorController& mc = *motors[m];
        if (!mc.enabled) continue;
        const LoadEstimate& e = s_est[m];
        float rpm = mc.current_velocity * 60.0f / (2.0f * PI);
        if (!e.valid) {
            say("[LOAD] M%d %6.0frpm V=%.2f n=%.0f no fit  prot=%.2f\n", m + 1, rpm,
                mc.motor.voltage_limit, e.n_eff, mc.protection_current);
            continue;
        }
        char dip[12] = "--", dev[12] = "--", extra[48] = "";
        if (!isnan(e.dip)) snprintf(dip, sizeof(dip), "%+.2f", e.dip);
        if (!isnan(e.lag_dev)) snprintf(dev, sizeof(dev), "%+.0f", e.lag_dev);
        if (!isnan(e.delta_deg))
            snprintf(extra, sizeof(extra), " delta=%.0f load=%.0f%%", e.delta_deg, e.delta_deg / 0.9f);
        char boost[16] = "";
        if (s_trim[m] != 0.0f) snprintf(boost, sizeof(boost), " trim%+.2f", s_trim[m]);
        say("[LOAD] M%d %6.0frpm V=%.2f n=%.0f Ip=%.2f Iq=%.2f lag=%.0f dev=%s dIp=%s prot=%.2f%s%s%s\n",
            m + 1, rpm, mc.motor.voltage_limit, e.n_eff, e.ip, e.iq, e.lag_deg, dev, dip,
            mc.protection_current, extra, s_warn[m] ? " WARN" : "", boost);
    }
}
