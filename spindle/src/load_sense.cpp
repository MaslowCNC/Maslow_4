#include "load_sense.h"
#ifdef SYNC_CURRENT_SENSE
#include "sync_sense.h"
#endif
#include "config.h"
#include <Preferences.h>
#include <stdarg.h>
#include "usb_console.h"
#ifdef SLIP_TEST
#include "trip_recorder.h"
void requireRehome();   // main.cpp
#endif

// ------------------- State -------------------

static bool g_load_print = false;   // [LOAD] console lines (USB "LOAD"); the fit always runs

// Adaptive voltage
static bool  s_adapt_enabled = ADAPT_DEFAULT_ON;
static float s_trim[2]       = { 0.0f, 0.0f };
static float s_trim_vel[2]   = { 0.0f, 0.0f };   // target velocity the trim was learned at
static bool  s_prev_warn[2]  = { false, false };
static uint32_t s_last_warn_ms[2] = { 0, 0 };     // millis() of the last warning on each motor
static float s_trim_f[2]     = { 0.0f, 0.0f };   // the trim as the fit sees it (LOAD_FILTER_TAU_S lag)
static float s_load_slope[2] = { 0.0f, 0.0f };   // smoothed d(load Iq)/dt (A/s) while the loop is active
static float s_prev_load[2]  = { 0.0f, 0.0f };
static bool  s_slope_valid[2] = { false, false };
static uint32_t s_last_z_move_ms = 0;            // millis() of the last pass with the Z moving

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
#ifdef SYNC_CURRENT_SENSE
static const float PHASE_COS[3] = { 1.0f, -0.5f, -0.5f };
static const float PHASE_SIN[3] = { 0.0f, 0.8660254f, -0.8660254f };
#endif

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
                     M_REF_STOPPING, M_VS_SETTLE, M_VS_MEASURE, M_ST_RAMP, M_ST_SETTLE, M_ST_STEP, M_ST_HOLD };

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

// Bench slip test (SLIP_TEST builds).  Each "T!" runs the next entry of SLIP_SCHEDULE: a motor, a
// speed, how the voltage comes down (a ramp of SLIPTEST_STEP_V steps, or a sudden drop to just
// below the estimated pull-out voltage, then the same ramp), and whether the slip detector rescues
// it (restores the calibrated voltage) or only reports when it would have fired.
struct SlipRun { uint8_t motor; float rpm; bool sudden; bool rescue; };
// 10-26 runs: at 4k and 8k a ramp slips (8 A ~10 ms later); at 12k lowering the voltage first runs
// up the current (past the V-curve minimum) and hits 8 A before any slip, so rescues stay at 4k/8k.
// Rescues with the voltage ramped back (SLIPTEST_RESTORE_MS): a stepped restore at 8k spiked 9.8 A.
static const SlipRun SLIP_SCHEDULE[] = {
    { 0,  4000, false, true  }, { 0,  8000, false, true  }, { 0,  8000, true,  true  },
};
static const int SLIP_RUNS = sizeof(SLIP_SCHEDULE) / sizeof(SLIP_SCHEDULE[0]);
static int      s_st_idx   = 0;          // next schedule entry
static SlipRun  s_st;                    // the running one
static int      s_st_run   = 0;          // its number (1-based)
static int      s_st_motor = 0;
static uint32_t s_st_start_ms = 0;
static uint32_t s_st_slips0   = 0;       // observer slip count when stepping began
static uint32_t s_st_valid_ms = 0;       // last pass the observer was valid
// Detector, two rules, each SLIPTEST_DET_N passes in a row; the first to fire is the detection:
//  - ld rising SLIPTEST_DET_DEG over its own ~SLIPTEST_DET_TAU_MS average.  The average restarts
//    SLIPTEST_DET_REBASE_MS after a sudden drop, which moves ld by itself (less voltage = less Id =
//    less R*Id tilt).
//  - the load-sense fit's Iq below zero (current no longer lagging the voltage: under-excited).
//    Earliest signal on the 10-26 ramps: 32-73 ms before 8 A at 4k/8k, ~900 ms at 12k.
//  - the observer's speed SLIPTEST_DET_SPEED below the command.  Not held off after a drop.
//  - one pass's unfiltered maximum ld (pass_load_max) SLIPTEST_DET_MX_DEG over the pre-event
//    average of ld (~SLIPTEST_DET_TAU_MS, never restarted, so a drop does not reset it).  A
//    single pass, no hold-off: on the 10-26 8k drop it was +17 deg within 10 ms, when the
//    filtered speed was still inside 2%.  NB a sudden drop shifts ld by itself as well.
static float    s_det_base    = NAN;
static int      s_det_count   = 0;
static int      s_det_iq_count = 0;
static int      s_det_sp_count = 0;
static float    s_mx_base     = NAN;     // ldmx rule's average of ld
static uint32_t s_det_mx_ms   = 0;
static uint32_t s_det_ld_ms   = 0;       // when each rule fired (0 = not yet)
static uint32_t s_det_iq_ms   = 0;
static uint32_t s_det_sp_ms   = 0;
static float    s_restore_from = 0.0f;   // rescue: offset the voltage is ramped back from
static uint32_t s_det_ms      = 0;       // when the first one fired (0 = not yet)
static float    s_det_ld = NAN, s_det_v = NAN;
static uint32_t s_det_arm_ms  = 0;       // detector ignores passes before this
static float    s_hold_max_a  = 0.0f;    // rescue: worst phase RMS during the hold

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

static bool slipTestActive() {
    return s_mode == M_ST_RAMP || s_mode == M_ST_SETTLE || s_mode == M_ST_STEP || s_mode == M_ST_HOLD;
}

// Per-motor adaptive voltage trim (see ADAPT_* in config.h), handed to the FOC loop through
// load_boost_v.  VSWEEP and the slip test own that offset while they run.
static void updateAdaptive(MotorController* motors[2], bool allow, float dt, uint32_t now, bool z_moving) {
    if (vsweepActive() || slipTestActive()) {
        for (int m = 0; m < 2; m++) {
            s_trim[m]               = 0.0f;
            motors[m]->load_boost_v = s_vs_offset;
        }
        return;
    }
    if (z_moving) s_last_z_move_ms = now;
    bool z_settled = !z_moving && now - s_last_z_move_ms >= ADAPT_Z_SETTLE_MS;
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

        // Load trend, only while the loop is tracking this motor at a steady speed.  It is Iq less
        // what the loop's own voltage changes did to it (seen through the fit's lag), so trimming
        // the voltage down is not mistaken for a rising load and fought straight back.  Not judged
        // during or just after a Z move, whose end brings Iq down without any load change.
        bool  trend_ok = active && z_settled;
        float a_per_v = fmaxf(ADAPT_IQ_A_PER_V_PER_KRPM * rpm / 1000.0f, 1.0f);
        if (dt > 0.0f) s_trim_f[m] += dt / (LOAD_FILTER_TAU_S + dt) * (s_trim[m] - s_trim_f[m]);
        float load_iq = e.iq - a_per_v * s_trim_f[m];
        if (trend_ok && s_slope_valid[m] && dt > 0.0f) {
            float raw = (load_iq - s_prev_load[m]) / dt;
            s_load_slope[m] += dt / (ADAPT_TREND_TAU_S + dt) * (raw - s_load_slope[m]);
        } else if (!trend_ok) {
            s_load_slope[m] = 0.0f;
        }
        s_slope_valid[m] = trend_ok;
        s_prev_load[m]   = load_iq;

        if (active) {
            // A falling load Iq means the load is rising.  The fit trails it by about
            // LOAD_FILTER_TAU_S, so judge the target on where Iq is by now.  Only near the target:
            // well above it the trend is the loop's own voltage (see ADAPT_TREND_MAX_IQ_A).
            float falling = e.iq < ADAPT_TREND_MAX_IQ_A
                                ? fmaxf(-s_load_slope[m] - ADAPT_TREND_DEADBAND_A_PER_S, 0.0f)   // A/s
                                : 0.0f;
            float iq_now  = e.iq - falling * LOAD_FILTER_TAU_S;
            float err     = ADAPT_IQ_TARGET_A - iq_now;                 // > 0: Iq too low -> more voltage
            float rate;
            if (e.iq < ADAPT_IQ_FAST_A) {
                rate = ADAPT_UP_MAX_V_PER_S;          // near or past the edge
            } else if (err > 0.0f) {
                rate = fminf(ADAPT_K_UP * err, ADAPT_UP_MAX_V_PER_S);
            } else if (e.iq > ADAPT_IQ_HIGH_A && !warn_recent) {
                rate = fmaxf(ADAPT_K_DOWN_FAST * (ADAPT_IQ_TARGET_A - e.iq), -ADAPT_DOWN_FAST_V_PER_S);
            } else {
                rate = fmaxf(ADAPT_K_DOWN * (ADAPT_IQ_TARGET_A - e.iq), -ADAPT_DOWN_MAX_V_PER_S);
            }
            // Feed-forward: add the voltage that would hold Iq level, ADAPT_TREND_GAIN times over, so
            // the margin is rebuilt while the load climbs rather than after the fit reaches the edge.
            if (falling > 0.0f) {
                rate = fminf(rate + ADAPT_TREND_GAIN * falling / a_per_v, ADAPT_UP_MAX_V_PER_S);
            }
            // Never take voltage away during a Z move or its settle window: the Z drive loads the
            // motors (M2 most), raising Iq, and the cut load is back at full strength the moment
            // the move ends (see ADAPT_Z_SETTLE_MS).
            if (!z_settled && rate < 0.0f) {
                rate = 0.0f;
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

#ifdef SLIP_TEST
static void stSay(const char* fmt, ...) {
    char    line[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    syncSenseSay(line);
}

// End the test: record first (so the 2 s before this moment and a short tail are kept), then cut
// the motor at once - it coasts, nothing is coupled to it with the tool out.
static void finishSlipTest(const char* why) {
    MotorController& mc = *s_motors[s_st_motor];
    uint32_t now = millis();
    float v  = mc.motor.voltage_limit;
    float dv = s_vs_offset;
    char  reason[96];
    snprintf(reason, sizeof(reason), "manual test - slip run %d M%d %.0fk %s%s: %s", s_st_run, s_st_motor + 1,
             s_st.rpm / 1000.0f, s_st.sudden ? "drop" : "ramp", s_st.rescue ? " rescue" : "", why);
    tripRecorderTrigger(reason);
    mc.emergencyStop();
    s_vs_offset = 0.0f;
    if (s_motors[0]) s_motors[0]->load_boost_v = 0.0f;
    if (s_motors[1]) s_motors[1]->load_boost_v = 0.0f;
    s_mode = M_IDLE;
    stSay("SLIPTEST run %d M%d stopped at %.2f V (dV %+.2f) after %.1f s: %s; recording follows", s_st_run,
          s_st_motor + 1, v, dv, (now - s_st_start_ms) / 1000.0f, why);
    char ld_t[24] = "never", iq_t[24] = "never", sp_t[24] = "never", mx_t[24] = "never";
    if (s_det_mx_ms) snprintf(mx_t, sizeof(mx_t), "%lu ms", (unsigned long)(now - s_det_mx_ms));
    if (s_det_ld_ms) snprintf(ld_t, sizeof(ld_t), "%lu ms", (unsigned long)(now - s_det_ld_ms));
    if (s_det_iq_ms) snprintf(iq_t, sizeof(iq_t), "%lu ms", (unsigned long)(now - s_det_iq_ms));
    if (s_det_sp_ms) snprintf(sp_t, sizeof(sp_t), "%lu ms", (unsigned long)(now - s_det_sp_ms));
    stSay("SLIPTEST run %d detector before this stop: ld %s, Iq<0 %s, speed %s, ldmx %s (first at %.2f V)",
          s_st_run, ld_t, iq_t, sp_t, mx_t, s_det_v);
}
#endif

void loadSenseAbort(const char* why) {
    if (s_mode == M_IDLE) return;
#ifdef SLIP_TEST
    if (slipTestActive()) {
        finishSlipTest(why);
        return;
    }
#endif
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

#ifdef SLIP_TEST
bool loadSenseStartSlipTest(MotorController& mc1, MotorController& mc2) {
    if (s_mode != M_IDLE) { stSay("SLIPTEST refused: another test is running"); return true; }
    if (mc1.enabled || mc2.enabled || mc1.velocity_mode || mc2.velocity_mode) {
        stSay("SLIPTEST refused: both motors must be stopped");
        return true;
    }
    s_motors[0] = &mc1; s_motors[1] = &mc2;
    s_st       = SLIP_SCHEDULE[s_st_idx];
    s_st_run   = s_st_idx + 1;
    s_st_idx   = (s_st_idx + 1) % SLIP_RUNS;
    s_st_motor = s_st.motor;
    MotorController& mc = *s_motors[s_st_motor];
    // Spinning one motor alone moves the relative phase, i.e. the Z: it must be homed afterwards.
    requireRehome();
    tripRecorderArm();
    s_vs_offset  = 0.0f;
    s_det_base   = NAN;
    s_det_count  = 0;
    s_det_iq_count = 0;
    s_det_sp_count = 0;
    s_det_ld_ms  = 0;
    s_det_iq_ms  = 0;
    s_det_sp_ms  = 0;
    s_det_mx_ms  = 0;
    s_mx_base    = NAN;
    s_det_ms     = 0;
    s_det_v      = NAN;
    s_det_arm_ms = 0;
    s_hold_max_a = 0.0f;
    mc.target_velocity     = s_st.rpm * 2.0f * PI / 60.0f * mc.direction;
    mc.velocity_mode       = true;
    mc.continuous_rotation = false;
    s_st_start_ms = s_timer = millis();
    s_mode = M_ST_RAMP;
    stSay("SLIPTEST run %d/%d M%d: %.0f RPM alone, %s, then -%.2f V every %u ms; detector %s (stops itself; E aborts)",
          s_st_run, SLIP_RUNS, s_st_motor + 1, s_st.rpm,
          s_st.sudden ? "sudden drop to below pull-out" : "no drop", SLIPTEST_STEP_V, (unsigned)SLIPTEST_STEP_MS,
          s_st.rescue ? "RESCUES" : "only reports");
    return true;
}

// The slip detector, every pass while the voltage is coming down.  Both rules keep running so each
// one's time is reported; true on the pass the first of them fires.
static bool detectorPass(const ObserverEstimate& e, const LoadEstimate& le, float v, uint32_t now, float dt) {
    if (!s_det_mx_ms && e.valid && !isnan(e.load_deg)) {
        if (isnan(s_mx_base)) {
            s_mx_base = e.load_deg;
        } else if (!isnan(e.pass_load_max) && e.pass_load_max - s_mx_base > SLIPTEST_DET_MX_DEG) {
            s_det_mx_ms = now;
        } else {
            s_mx_base += (e.load_deg - s_mx_base) * fminf(dt * 1000.0f / SLIPTEST_DET_TAU_MS, 1.0f);
        }
    }
    if (!s_det_sp_ms && e.valid && !isnan(e.speed_rpm)) {
        s_det_sp_count = e.speed_rpm < (1.0f - SLIPTEST_DET_SPEED) * s_st.rpm ? s_det_sp_count + 1 : 0;
        if (s_det_sp_count >= SLIPTEST_DET_N) s_det_sp_ms = now;
    }
    if (now >= s_det_arm_ms && !s_det_ld_ms && e.valid && !isnan(e.load_deg)) {
        if (isnan(s_det_base)) {
            s_det_base = e.load_deg;
        } else if (e.load_deg - s_det_base > SLIPTEST_DET_DEG) {
            if (++s_det_count >= SLIPTEST_DET_N) { s_det_ld_ms = now; s_det_ld = e.load_deg; }
            // the average is not updated while ld is out of line
        } else {
            s_det_count = 0;
            s_det_base += (e.load_deg - s_det_base) * fminf(dt * 1000.0f / SLIPTEST_DET_TAU_MS, 1.0f);
        }
    }
    if (now >= s_det_arm_ms && !s_det_iq_ms && le.valid && !isnan(le.iq)) {
        s_det_iq_count = le.iq < 0.0f ? s_det_iq_count + 1 : 0;
        if (s_det_iq_count >= SLIPTEST_DET_N) s_det_iq_ms = now;
    }
    if (!s_det_ms && (s_det_ld_ms || s_det_iq_ms || s_det_sp_ms || s_det_mx_ms)) {
        s_det_ms = now;
        s_det_v  = v;
        return true;
    }
    return false;
}

static void updateSlipTest(uint32_t now) {
    static uint32_t last = 0;
    float dt = last ? (now - last) * 0.001f : 0.002f;
    last = now;
    MotorController&        mc = *s_motors[s_st_motor];
    const ObserverEstimate& e  = syncObserver(s_st_motor);
    if (!mc.enabled && s_mode != M_ST_RAMP) {   // a protection stop got there first
        finishSlipTest("motor already stopped by the firmware's protection");
        return;
    }
    if (now - s_st_start_ms > SLIPTEST_TIMEOUT_MS) { finishSlipTest("timed out"); return; }
    if (mc.enabled && mc.last_instantaneous_current > SLIPTEST_MAX_A) {
        char why[48];
        snprintf(why, sizeof(why), "phase current %.1f A", mc.last_instantaneous_current);
        finishSlipTest(why);
        return;
    }
    if (e.valid) s_st_valid_ms = now;
    switch (s_mode) {
        case M_ST_RAMP:
            if (mc.enabled && fabsf(mc.current_velocity - mc.target_velocity) < 0.5f) {
                s_timer = now;
                s_mode  = M_ST_SETTLE;
            } else if (now - s_timer > 30000) {
                finishSlipTest("speed not reached in 30 s");
            }
            break;
        case M_ST_SETTLE:
            if (now - s_timer < SLIPTEST_SETTLE_MS) break;
            if (!e.valid) { finishSlipTest("observer not valid at speed"); break; }
            s_st_slips0   = e.slips;
            s_st_valid_ms = now;
            stSay("SLIPTEST run %d M%d at speed: %.2f V, observer %.0f RPM, ld %+.0f lr %+.0f deg; stepping down",
                  s_st_run, s_st_motor + 1, mc.motor.voltage_limit, e.speed_rpm, e.load_deg, e.load_r_deg);
            if (s_st.sudden) {
                // Just below the estimated pull-out: back-EMF + dead-time loss + the friction drop seen at 4k.
                float w      = fabsf(mc.current_velocity);
                float target = w * SYNC_OBS_FLUX_WB + SLIPTEST_DT_V + SLIPTEST_FRICTION_V - SLIPTEST_DROP_BELOW_V;
                s_vs_offset  = target - mc.motor.voltage_limit;
                s_det_arm_ms = now + SLIPTEST_DET_REBASE_MS;
                s_det_base   = NAN;
                stSay("SLIPTEST run %d M%d sudden drop to %.2f V (dV %+.2f)", s_st_run, s_st_motor + 1, target,
                      s_vs_offset);
            }
            s_timer = now;
            s_mode  = M_ST_STEP;
            break;
        case M_ST_STEP:
            if (e.slips != s_st_slips0) { finishSlipTest("observer counted a pole slip"); break; }
            if (now - s_st_valid_ms > SLIPTEST_LOST_MS) { finishSlipTest("observer lost lock"); break; }
            if (detectorPass(e, s_est[s_st_motor], mc.motor.voltage_limit, now, dt)) {
                stSay("SLIPTEST run %d M%d DETECTOR (%s) at %.2f V: ld %+.0f vs average %+.0f, Iq %+.2f, %.0f RPM%s",
                      s_st_run, s_st_motor + 1,
                      s_det_mx_ms ? "ldmx rule" : s_det_sp_ms ? "speed rule" : s_det_ld_ms ? "ld rule" : "Iq<0 rule",
                      mc.motor.voltage_limit, e.load_deg, s_det_base, s_est[s_st_motor].iq, e.speed_rpm,
                      s_st.rescue ? " - ramping the voltage back" : "");
                if (s_st.rescue) {
                    // Half straight back (a full step spiked 9.8 A at 8k; a 40 ms ramp from the
                    // bottom was too slow at 4k), the rest ramped in M_ST_HOLD.
                    s_vs_offset   *= SLIPTEST_RESTORE_STEP;
                    s_restore_from = s_vs_offset;
                    s_timer        = now;
                    s_mode      = M_ST_HOLD;
                    break;
                }
            }
            if (now - s_timer < SLIPTEST_STEP_MS) break;
            if (s_vs_offset - SLIPTEST_STEP_V < SLIPTEST_MIN_DV || mc.motor.voltage_limit <= BASE_VOLTAGE + 0.01f) {
                finishSlipTest("no slip down to the lowest voltage");
                break;
            }
            stSay("SLIPTEST M%d %.2f V rms %.2f ld %+.0f lr %+.0f sp %.0f", s_st_motor + 1, mc.motor.voltage_limit,
                  mc.last_instantaneous_current, e.load_deg, e.load_r_deg, e.speed_rpm);
            s_vs_offset -= SLIPTEST_STEP_V;
            s_timer = now;
            break;
        case M_ST_HOLD: {
            // Rescue: did the restored voltage pull it back?  The stops above still apply.
            s_hold_max_a = fmaxf(s_hold_max_a, mc.last_instantaneous_current);
            // The rest of the way back to the calibrated voltage over SLIPTEST_RESTORE_MS.
            float f     = fminf((now - s_timer) / (float)SLIPTEST_RESTORE_MS, 1.0f);
            s_vs_offset = s_restore_from * (1.0f - f);
            if (e.slips != s_st_slips0) { finishSlipTest("rescue FAILED: observer counted a pole slip"); break; }
            if (now - s_st_valid_ms > SLIPTEST_LOST_MS) { finishSlipTest("rescue FAILED: observer lost lock"); break; }
            if (now - s_timer < SLIPTEST_HOLD_MS) break;
            float cmd = s_st.rpm;
            bool  ok  = e.valid && fabsf(e.speed_rpm - cmd) < 0.02f * cmd;
            char  why[80];
            snprintf(why, sizeof(why), "rescue %s: %.0f RPM, ld %+.0f, worst %.1f A in the hold", ok ? "HELD" : "FAILED",
                     e.speed_rpm, e.load_deg, s_hold_max_a);
            finishSlipTest(why);
            break;
        }
        default:
            break;
    }
}
#endif

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

float loadSenseResistance(int motor_idx) {
    return s_r_ohm[motor_idx & 1];
}

#ifdef SYNC_CURRENT_SENSE
// ------------------- Shadow slip guard -------------------
// The four slip-detector rules from the 10-26 bench slip tests, run on every motor that is spinning,
// taking NO action: each would-fire is logged over the link, and once a second the worst margin of
// each rule, so real cuts show how often they would fire falsely.  Thresholds: SLIPTEST_DET_* and
// GUARD_* in config.h.  Context flags: Z = Z moving, R = ramping (speed not yet at its target).
//   ld   : filtered ld SLIPTEST_DET_DEG over its own ~200 ms average, SLIPTEST_DET_N passes
//   iq   : load-sense Iq < 0, SLIPTEST_DET_N passes
//   sp   : observer speed SLIPTEST_DET_SPEED below the command, SLIPTEST_DET_N passes
//   mx   : one pass's unfiltered max ld SLIPTEST_DET_MX_DEG over the ~200 ms average of ld
struct GuardState {
    float    base = NAN;                 // ~200 ms average of ld (held while the ld rule is over)
    int      n_ld = 0, n_iq = 0, n_sp = 0;
    bool     on[4] = { false, false, false, false };   // condition currently met (ld, iq, sp, mx)
    uint32_t clear_ms[4] = { 0, 0, 0, 0 };            // last pass it was not met
    uint32_t last_event_ms[4] = { 0, 0, 0, 0 };
    uint16_t fires[4] = { 0, 0, 0, 0 };               // this second
    uint16_t quiet[4] = { 0, 0, 0, 0 };               // fires not logged (rate limit) this second
    // Worst margins this second
    float    w_ld = -1e9f, w_mx = -1e9f, w_iq = 1e9f, w_sp = -1e9f;
    bool     w_z = false, w_r = false;
    uint32_t sec_ms = 0;
    uint32_t total[4] = { 0, 0, 0, 0 };               // since boot
};
static GuardState s_guard[2];
static const char* const GUARD_RULE[4] = { "ld", "iq", "sp", "mx" };

static void guardReset(GuardState& g) {
    g.base = NAN;
    g.n_ld = g.n_iq = g.n_sp = 0;
    for (int r = 0; r < 4; r++) g.on[r] = false;
}

static void guardFire(int m, GuardState& g, int r, uint32_t now, const char* detail, bool z, bool ramp) {
    g.fires[r]++;
    g.total[r]++;
    if (now - g.last_event_ms[r] < GUARD_EVENT_GAP_MS) { g.quiet[r]++; return; }
    g.last_event_ms[r] = now;
    char line[150];
    snprintf(line, sizeof(line), "GUARD M%d would fire %s: %s%s%s (#%lu)", m + 1, GUARD_RULE[r], detail,
             z ? " Z" : "", ramp ? " R" : "", (unsigned long)g.total[r]);
    syncSenseSay(line);
}

static void guardUpdate(MotorController* motors[2], uint32_t now, float dt, bool z_moving) {
    for (int m = 0; m < 2; m++) {
        MotorController&        mc = *motors[m];
        GuardState&             g  = s_guard[m];
        const ObserverEstimate& e  = syncObserver(m);
        const LoadEstimate&     le = s_est[m];
        float cmd  = fabsf(mc.target_velocity) * 60.0f / (2.0f * PI);
        float rpm  = fabsf(mc.current_velocity) * 60.0f / (2.0f * PI);
        bool  spin = mc.enabled && mc.velocity_mode && rpm >= SYNC_OBS_MIN_RPM && e.valid && !isnan(e.load_deg);
        if (!spin) {
            guardReset(g);
            g.sec_ms = 0;
            continue;
        }
        bool ramp = fabsf(mc.current_velocity - mc.target_velocity) > 0.5f;
        if (isnan(g.base)) g.base = e.load_deg;
        if (g.sec_ms == 0) g.sec_ms = now;

        bool  cond[4];
        float d_ld = e.load_deg - g.base;
        g.n_ld  = d_ld > SLIPTEST_DET_DEG ? g.n_ld + 1 : 0;
        cond[0] = g.n_ld >= SLIPTEST_DET_N;
        bool iq_ok = le.valid && !isnan(le.iq);
        g.n_iq  = iq_ok && le.iq < 0.0f ? g.n_iq + 1 : 0;
        cond[1] = g.n_iq >= SLIPTEST_DET_N;
        float sp_def = (!isnan(e.speed_rpm) && rpm > 0.0f) ? (rpm - e.speed_rpm) / rpm : NAN;
        g.n_sp  = !isnan(sp_def) && sp_def > SLIPTEST_DET_SPEED ? g.n_sp + 1 : 0;
        cond[2] = g.n_sp >= SLIPTEST_DET_N;
        float d_mx = isnan(e.pass_load_max) ? NAN : e.pass_load_max - g.base;
        cond[3] = !isnan(d_mx) && d_mx > SLIPTEST_DET_MX_DEG;

        // The average follows ld except while the ld rule is over its threshold.
        if (d_ld <= SLIPTEST_DET_DEG) g.base += d_ld * fminf(dt * 1000.0f / SLIPTEST_DET_TAU_MS, 1.0f);

        // Worst margins this second.
        g.w_ld = fmaxf(g.w_ld, d_ld);
        if (!isnan(d_mx)) g.w_mx = fmaxf(g.w_mx, d_mx);
        if (iq_ok) g.w_iq = fminf(g.w_iq, le.iq);
        if (!isnan(sp_def)) g.w_sp = fmaxf(g.w_sp, sp_def);
        g.w_z |= z_moving;
        g.w_r |= ramp;

        // A would-fire is the condition becoming true after it was clear for GUARD_REARM_MS.
        for (int r = 0; r < 4; r++) {
            if (!cond[r]) {
                if (g.on[r]) g.clear_ms[r] = now;
                g.on[r] = false;
                continue;
            }
            if (g.on[r]) continue;
            g.on[r] = true;
            if (g.clear_ms[r] && now - g.clear_ms[r] < GUARD_REARM_MS) continue;   // same episode
            char detail[96];
            snprintf(detail, sizeof(detail), "ld %+.0f avg %+.0f mx %+.0f iq %+.2f sp %.0f/%.0f, %.1f V", e.load_deg,
                     g.base, e.pass_load_max, iq_ok ? le.iq : NAN, e.speed_rpm, cmd, mc.motor.voltage_limit);
            guardFire(m, g, r, now, detail, z_moving, ramp);
        }

        if (now - g.sec_ms >= 1000) {
            char line[180];
            snprintf(line, sizeof(line),
                     "GUARD M%d %.0frpm worst: ld %+.0f/%+.0f mx %+.0f/%+.0f iq %+.2f sp %.1f/%.1f%% | fires ld%u iq%u sp%u mx%u%s%s",
                     m + 1, cmd, g.w_ld, SLIPTEST_DET_DEG, g.w_mx, SLIPTEST_DET_MX_DEG, g.w_iq,
                     100.0f * g.w_sp, 100.0f * SLIPTEST_DET_SPEED, g.fires[0], g.fires[1], g.fires[2], g.fires[3],
                     g.w_z ? " Z" : "", g.w_r ? " R" : "");
            syncSenseSay(line);
            for (int r = 0; r < 4; r++) g.fires[r] = g.quiet[r] = 0;
            g.w_ld = g.w_mx = g.w_sp = -1e9f;
            g.w_iq = 1e9f;
            g.w_z = g.w_r = false;
            g.sec_ms = now;
        }
    }
}
#endif

void loadSenseUpdate(MotorController& mc1, MotorController& mc2, float dt, bool allow_warn, bool z_moving) {
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
#ifdef SYNC_CURRENT_SENSE
        // Synchronised samples (sync_sense.cpp): true phase currents, each tagged with the exact
        // commanded angle of its PWM period, so every reading has full weight (g = 1).
        int             npts = 0;
        const FitPoint* pts  = syncFitPoints(m, &npts);
        for (int j = 0; j < npts; j++) {
            int   k  = pts[j].phase;
            float c  = pts[j].cos_th, sn = pts[j].sin_th;
            // sin/cos(th - phi) by rotation: no trig per reading (this runs ~80 times a pass).
            float u  = -(sn * PHASE_COS[k] - c * PHASE_SIN[k]);
            float w  =   c * PHASE_COS[k] + sn * PHASE_SIN[k];
            float i  = pts[j].i;
            f.suu += u * u; f.suw += u * w; f.sww += w * w;
            f.siu += i * u; f.siw += i * w; f.n   += 1.0f;
        }
#else
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
#endif
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

#ifdef SLIP_TEST
        case M_ST_RAMP:
        case M_ST_SETTLE:
        case M_ST_STEP:
        case M_ST_HOLD:
            updateSlipTest(now);
            break;
#endif

        default:
            break;
    }

    for (int m = 0; m < 2; m++) updateWarning(m, *motors[m], allow_warn, now);
    updateAdaptive(motors, allow_warn, dt, now, z_moving);
#ifdef SYNC_CURRENT_SENSE
    guardUpdate(motors, now, dt, z_moving);
#endif

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
