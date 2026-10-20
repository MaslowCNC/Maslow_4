#ifdef SYNC_CURRENT_SENSE

#include "sync_sense.h"
#include "current_sampler.h"
#include "flux_observer.h"
#include "load_sense.h"
#include "config.h"
#include <math.h>
#include <stdarg.h>

static void (*s_usb)(const char*)  = nullptr;
static void (*s_link)(const char*) = nullptr;

static void out(bool to_link, const char* fmt, ...) {
    char    line[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (s_usb) s_usb(line);
    if (to_link && s_link) s_link(line);
}

static void sayBoth(const char* line) { out(true, "%s", line); }

static const float SQRT3     = 1.7320508f;
static const float INV_SQRT3 = 0.57735027f;
static const float SYNC_T_S  = 1.0f / PWM_FREQUENCY;   // one PWM period (s)
static float       s_dt_v    = 0.0f;                   // dead-time voltage error (V), set at start
static const float PHASE_COS[3] = { 1.0f, -0.5f, -0.5f };          // phase offsets 0, +120, -120 deg
static const float PHASE_SIN[3] = { 0.0f, 0.8660254f, -0.8660254f };
static const int   FIT_MAX   = 256;   // 2 readings x the sampler's 128-period ring

static inline float wrapPi(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a <= -PI) a += 2.0f * PI;
    return a;
}

// --- Settling histogram (USB "SETTLE") ---
// Middle-phase readings binned by time since that phase's low-side FET turned on (and, separately,
// by time left before it turns off), each compared with a sinusoid fitted to the lowest-duty
// readings alone (which are well inside their window).  Inside the settled window the normalised
// residual is ~0; where the sense output has not settled it is pulled towards -1 (reads ~0).
static const int   H_BINS   = 36;
static const float H_ON_MIN = -2.0f, H_OFF_MIN = -4.0f, H_STEP = 0.5f;
struct Hist {
    float    sum[H_BINS], sumsq[H_BINS];
    uint32_t n[H_BINS];
};

struct MotorState {
    FitPoint fit[FIT_MAX];
    int      fit_n = 0;

    FluxObserver     obs;
    ObserverEstimate est;
    bool     have_seq = false;
    uint32_t prev_seq = 0;
    float    vs_a = 0, vs_b = 0;              // volt-seconds since the last observer step
    float    is_a = 0, is_b = 0;              // amp-seconds since the last observer step
    uint32_t step_n = 0;                      // periods since the last observer step
    uint32_t obs_seq = 0;                     // period of the last observer step
    float    i_true[3] = { 0, 0, 0 };          // last currents, for the dead-time sign
    bool     cmd_init = false;
    float    cmd_prev = 0;
    double   cmd_unwrapped = 0;
    double   track_offset  = 0;               // (cmd - obs) unwrapped at observer start, less delta
    bool     track_init    = false;
    uint32_t obs_updates   = 0;
    float    delta_filt    = 0;
    float    delta_max     = -1e9f;
    double   spd_angle     = 0;
    uint32_t spd_seq       = 0;
    bool     spd_init      = false;
    uint32_t last_valid_ms = 0;
    CurrentSample last = {};                   // for the stale-sensing report
    uint32_t e_n = 0, e_lo = 0, e_off = 0;     // samples since the motor was last enabled
    uint32_t last_warn_ms  = 0;

    // Lowest-duty-only sinusoid (for the settling histogram): i = a cos(th - phi) + b sin(th - phi)
    float m_uu = 0, m_uw = 0, m_ww = 0, m_iu = 0, m_iw = 0, m_n = 0;
    Hist  h_on, h_off;

    // Log window
    uint32_t w_n = 0, w_full = 0, w_lo = 0, w_mid = 0, w_late = 0, w_off = 0;
    float    w_sumsq = 0;
    uint32_t w_cnt   = 0;
    float    w_peak  = 0;
    float    w_id = 0, w_iq = 0;
    uint32_t w_dq = 0;
    float    w_vmag = 0;
};
static MotorState s_m[2];

static bool     s_usb_log      = false;
static uint32_t s_started_ms   = 0;
static uint32_t s_last_usb_log = 0;
static uint32_t s_last_link_log= 0;
static float    s_obs_r[2]     = { SYNC_OBS_R_OHM, SYNC_OBS_R_OHM };
static float    s_obs_l        = SYNC_OBS_L_H;
static float    s_obs_flux     = SYNC_OBS_FLUX_WB;

// --- Capture (USB "SYNCCAP") ---
struct CapRec {
    uint32_t seq;
    int16_t  i_ma[3];      // sense polarity corrected
    uint16_t duty_q[3];    // duty x 10000
    int16_t  t_mid_ns;
    int16_t  on_mid_ns;
    int16_t  cmd_cdeg;     // commanded electrical angle, centidegrees
    int16_t  obs_cdeg;     // observer electrical angle, centidegrees (or -32768)
    uint8_t  flags, ph_lo, ph_mid, motor;
};
static CapRec*  s_cap       = nullptr;
static uint32_t s_cap_size  = 0, s_cap_n = 0, s_cap_dump = 0;
static bool     s_cap_on    = false;

void syncSenseInit(void (*usb)(const char*), void (*link)(const char*)) {
    s_usb  = usb;
    s_link = link;
}

static void resetHist(Hist& h) { memset(&h, 0, sizeof(h)); }

static void applyObserverParams() {
    for (int m = 0; m < 2; m++) {
        float r = s_obs_r[m];
        if (r <= 0.0f) r = loadSenseResistance(m);   // RTEST's measurement, if run
        s_m[m].obs.R            = r > 0.0f ? r : 0.0f;
        s_m[m].obs.L            = s_obs_l;
        s_m[m].obs.flux_linkage = s_obs_flux;
    }
}

bool syncSenseStart() {
    for (int m = 0; m < 2; m++) {
        resetHist(s_m[m].h_on);
        resetHist(s_m[m].h_off);
    }
    applyObserverParams();
    bool ok = samplerBegin(sayBoth);
    s_dt_v  = SUPPLY_VOLTAGE * samplerDeadTimeUs() / samplerPeriodUs();
    if (ok) {
        s_started_ms = millis();
        SamplerTiming t = samplerTiming();
        out(true, "SYNC sampling on: period %.2f us, dead time %.3f us, lead %.1f us, settle %.1f, hold %.1f",
            samplerPeriodUs(), samplerDeadTimeUs(), t.lead_us, t.settle_us, t.hold_us);
    } else {
        out(true, "SYNC sampling FAILED: motors stay off (no over-current protection)");
    }
    return ok;
}

void syncSenseStop() { samplerEnd(); }

const FitPoint* syncFitPoints(int motor, int* count) {
    *count = s_m[motor & 1].fit_n;
    return s_m[motor & 1].fit;
}

const ObserverEstimate& syncObserver(int motor) { return s_m[motor & 1].est; }

void syncSenseDescribe(int motor) {
    const MotorState&    st = s_m[motor & 1];
    const CurrentSample& c  = st.last;
    out(true, "SYNC M%d since enable: %u samples, %u lo ok, %u off; running %d", motor + 1,
        (unsigned)st.e_n, (unsigned)st.e_lo, (unsigned)st.e_off, (int)samplerRunning());
    out(true, "SYNC M%d last: flags %02x duty %.3f %.3f %.3f lo %u t %.2f on %.2f | mid %u t %.2f on %.2f off %.2f",
        motor + 1, c.flags, c.duty[0], c.duty[1], c.duty[2], c.ph_lo, c.t_lo_us, c.on_lo_us, c.ph_mid,
        c.t_mid_us, c.on_mid_us, c.off_mid_us);
    out(true, "SYNC M%d last i %.2f %.2f %.2f A", motor + 1, c.i[0], c.i[1], c.i[2]);
}

bool syncSenseStale(const MotorController& mc, int motor) {
    if (!mc.enabled) return false;
    uint32_t since = max(s_m[motor & 1].last_valid_ms, mc.start_time + 50);
    return (int32_t)(millis() - since) > (int32_t)SYNC_STALE_MS;
}

// One sample: protection sums, fit readings, settle histogram, observer.  This runs ~40 times per
// motor per 2 ms pass on core 0, which the sampling interrupt already loads to ~45%, so the
// common path has no trig: the applied angle comes as cos/sin straight from the duties, phase
// shifts are rotations, and the observer's atan2 runs every SYNC_OBS_DECIM-th full sample.
static void processSample(MotorController& mc, int m, const CurrentSample& s, uint32_t now_ms) {
    MotorState& st = s_m[m];
    const float T  = SYNC_T_S;

    uint32_t gap = st.have_seq ? (s.seq - st.prev_seq) : 1;
    if (gap == 0 || gap > 1000) gap = 1;
    st.prev_seq = s.seq;
    st.have_seq = true;
    if (!(s.flags & SAMPLE_LO_OK)) st.last = s;   // the stale report wants the failing ones
    st.e_n++;
    if (s.flags & SAMPLE_LO_OK) st.e_lo++;
    if (s.flags & SAMPLE_OUTPUTS_OFF) st.e_off++;

    st.w_n++;
    if (s.flags & SAMPLE_LATE) st.w_late++;
    if (s.flags & SAMPLE_OUTPUTS_OFF) {
        st.w_off++;
        st.last_valid_ms = now_ms;
        st.obs.reset();
        st.track_init = false;
        st.obs_updates = 0;
        st.cmd_init = false;
        st.vs_a = st.vs_b = st.is_a = st.is_b = 0;
        st.step_n = 0;
        return;
    }

    // Applied voltages from the duties in force (no bus divider on this board: nominal bus).
    float d_mean = (s.duty[0] + s.duty[1] + s.duty[2]) / 3.0f;
    float va0 = (s.duty[0] - d_mean) * SUPPLY_VOLTAGE;
    float vb0 = (s.duty[1] - d_mean) * SUPPLY_VOLTAGE;
    float vc0 = (s.duty[2] - d_mean) * SUPPLY_VOLTAGE;
    float valpha0 = (2.0f * va0 - vb0 - vc0) / 3.0f;
    float vbeta0  = (vb0 - vc0) * INV_SQRT3;
    float vmag    = sqrtf(valpha0 * valpha0 + vbeta0 * vbeta0);
    // SimpleFOC: U_alpha = -sin(th) Uq, U_beta = cos(th) Uq (Ud = 0).
    bool  have_th = vmag > 0.05f;
    float inv_v   = have_th ? 1.0f / vmag : 0.0f;
    float c_th    = have_th ? vbeta0 * inv_v : 1.0f;
    float s_th    = have_th ? -valpha0 * inv_v : 0.0f;
    st.w_vmag += vmag;

    const bool lo_ok  = s.flags & SAMPLE_LO_OK;
    const bool mid_ok = s.flags & SAMPLE_MID_OK;
    const bool full   = s.flags & SAMPLE_FULL;
    float it[3];
    for (int k = 0; k < 3; k++) it[k] = LOAD_CURRENT_SIGN * s.i[k];

    if (lo_ok) { st.w_lo++; st.last_valid_ms = now_ms; }
    if (mid_ok) st.w_mid++;

    // Protection: phase RMS over every valid reading (lo alone, if that is all there is).
    if (full) {
        st.w_full++;
        st.w_sumsq += s.i[0] * s.i[0] + s.i[1] * s.i[1] + s.i[2] * s.i[2];
        st.w_cnt   += 3;
        for (int k = 0; k < 3; k++) st.w_peak = max(st.w_peak, fabsf(s.i[k]));
        mc.last_current_a = s.i[0];
        mc.last_current_b = s.i[1];
        mc.last_current_c = s.i[2];
        for (int k = 0; k < 3; k++) st.i_true[k] = it[k];
    } else if (lo_ok) {
        float i = s.i[s.ph_lo];
        st.w_sumsq += i * i;
        st.w_cnt   += 1;
        st.w_peak = max(st.w_peak, fabsf(i));
        st.i_true[s.ph_lo] = it[s.ph_lo];
    }

    // Load-sense fit readings (true currents, so no duty weighting).
    if (have_th) {
        if (lo_ok && st.fit_n < FIT_MAX) st.fit[st.fit_n++] = { c_th, s_th, it[s.ph_lo], s.ph_lo };
        if (mid_ok && st.fit_n < FIT_MAX) st.fit[st.fit_n++] = { c_th, s_th, it[s.ph_mid], s.ph_mid };
    }

    // Settling histogram: lowest-duty-only sinusoid, then the middle phase's residual.
    // cos/sin(th - phi) = rotation of (c_th, s_th) by phi.
    if (have_th) {
        const float lam = 1.0f - T / 0.02f;   // ~20 ms memory
        st.m_uu *= lam; st.m_uw *= lam; st.m_ww *= lam; st.m_iu *= lam; st.m_iw *= lam; st.m_n *= lam;
        if (lo_ok) {
            int   k = s.ph_lo;
            float u = c_th * PHASE_COS[k] + s_th * PHASE_SIN[k];
            float w = s_th * PHASE_COS[k] - c_th * PHASE_SIN[k];
            float i = it[k];
            st.m_uu += u * u; st.m_uw += u * w; st.m_ww += w * w; st.m_iu += i * u; st.m_iw += i * w; st.m_n += 1;
        }
        float det = st.m_uu * st.m_ww - st.m_uw * st.m_uw;
        if (st.m_n > 200 && det > 1e-3f * st.m_uu * st.m_ww && s.ph_mid != s.ph_lo) {
            float a   = (st.m_iu * st.m_ww - st.m_iw * st.m_uw) / det;
            float b   = (st.m_iw * st.m_uu - st.m_iu * st.m_uw) / det;
            float amp = sqrtf(a * a + b * b);
            if (amp > 0.3f) {
                int   k     = s.ph_mid;
                float model = a * (c_th * PHASE_COS[k] + s_th * PHASE_SIN[k]) + b * (s_th * PHASE_COS[k] - c_th * PHASE_SIN[k]);
                float r     = (it[k] - model) / amp;
                int   bon   = (int)floorf((s.on_mid_us - H_ON_MIN) / H_STEP);
                int   boff  = (int)floorf((s.off_mid_us - H_OFF_MIN) / H_STEP);
                if (bon >= 0 && bon < H_BINS)  { st.h_on.sum[bon] += r;  st.h_on.sumsq[bon] += r * r;  st.h_on.n[bon]++; }
                if (boff >= 0 && boff < H_BINS){ st.h_off.sum[boff] += r; st.h_off.sumsq[boff] += r * r; st.h_off.n[boff]++; }
            }
        }
    }

    // Observer.  Integrate the applied voltage (dead-time corrected with the last known current
    // signs) and the current every period; step the observer every SYNC_OBS_DECIM full samples.
    {
        const float dt_v = s_dt_v;   // dead-time voltage error, ~0.24 V
        float v[3];
        for (int k = 0; k < 3; k++) {
            float ik = st.i_true[k];
            float sg = ik / (fabsf(ik) + SYNC_DT_COMP_KNEE_A);   // smooth sign
            v[k] = (s.duty[k] - d_mean) * SUPPLY_VOLTAGE - dt_v * sg;
        }
        float dt = gap * T;
        st.vs_a += (2.0f * v[0] - v[1] - v[2]) / 3.0f * dt;
        st.vs_b += (v[1] - v[2]) * INV_SQRT3 * dt;
        st.is_a += (2.0f * st.i_true[0] - st.i_true[1] - st.i_true[2]) / 3.0f * dt;
        st.is_b += (st.i_true[1] - st.i_true[2]) * INV_SQRT3 * dt;
        st.step_n++;
    }
    float th = NAN;   // computed only where needed
    if (full && st.obs.ready() && st.step_n >= SYNC_OBS_DECIM) {
        float ialpha = (2.0f * it[0] - it[1] - it[2]) / 3.0f;
        float ibeta  = (it[1] - it[2]) * INV_SQRT3;
        st.obs.update(ialpha, ibeta, st.vs_a, st.vs_b, st.is_a, st.is_b);
        st.vs_a = st.vs_b = st.is_a = st.is_b = 0;
        st.step_n = 0;
        st.obs_updates++;
        st.obs_seq = s.seq;

        if (have_th) {
            // d/q in the commanded frame (d along th, the angle SimpleFOC treats as the rotor's).
            st.w_id += ialpha * c_th + ibeta * s_th;
            st.w_iq += -ialpha * s_th + ibeta * c_th;
            st.w_dq++;

            th = atan2f(s_th, c_th);
            if (st.cmd_init) st.cmd_unwrapped += wrapPi(th - st.cmd_prev);
            st.cmd_prev = th;
            st.cmd_init = true;
        }

        // Load angle = commanded - observed, in the direction of rotation.
        if (have_th && st.obs_updates > 50) {
            float dir   = (mc.current_velocity < 0.0f) ? -1.0f : 1.0f;
            double diff = st.cmd_unwrapped - st.obs.angle_unwrapped;
            if (!st.track_init) {
                // Start the unwrapped track at the wrapped angle, so it begins inside +/-180.
                float wrapped   = wrapPi(th - st.obs.electrical_angle);
                st.track_offset = diff - wrapped;
                st.track_init   = true;
                st.delta_filt   = dir * wrapped;
            }
            float delta = dir * (float)(diff - st.track_offset);
            // A pole slip: the rotor fell a whole electrical turn behind.  Count it and re-base.
            if (delta > SYNC_SLIP_DEG * PI / 180.0f) {
                st.est.slips++;
                st.track_offset += dir * 2.0 * PI;
                out(true, "SYNC SLIP M%d at %.0f rpm: observed rotor fell >%.0f deg behind (slip #%u)",
                    m + 1, fabsf(mc.current_velocity) * 60.0f / (2.0f * PI), SYNC_SLIP_DEG, (unsigned)st.est.slips);
                delta -= 2.0f * PI;
            } else if (delta < -SYNC_SLIP_DEG * PI / 180.0f) {
                st.track_offset -= dir * 2.0 * PI;   // rotor ahead (e.g. coasting down): re-base silently
                delta += 2.0f * PI;
            }
            float a = T * SYNC_OBS_DECIM / 0.01f;   // ~10 ms filter
            st.delta_filt += (delta - st.delta_filt) * (a > 1.0f ? 1.0f : a);
            st.delta_max = fmaxf(st.delta_max, delta * (180.0f / (float)PI));
        }
    }

    // Capture.
    if (s_cap_on && s_cap_n < s_cap_size) {
        if (isnan(th)) th = atan2f(s_th, c_th);
        CapRec& c = s_cap[s_cap_n++];
        c.seq   = s.seq;
        for (int k = 0; k < 3; k++) {
            c.i_ma[k]   = (int16_t)constrain(lroundf(it[k] * 1000.0f), -32000, 32000);
            c.duty_q[k] = (uint16_t)lroundf(s.duty[k] * 10000.0f);
        }
        c.t_mid_ns  = (int16_t)constrain(lroundf(s.t_mid_us * 1000.0f), -32000, 32000);
        c.on_mid_ns = (int16_t)constrain(lroundf(s.on_mid_us * 1000.0f), -32000, 32000);
        c.cmd_cdeg  = (int16_t)lroundf(th * 18000.0f / PI);
        c.obs_cdeg  = st.obs.first ? (int16_t)-32768
                                   : (int16_t)lroundf(wrapPi(st.obs.electrical_angle) * 18000.0f / PI);
        c.flags  = s.flags;
        c.ph_lo  = s.ph_lo;
        c.ph_mid = s.ph_mid;
        c.motor  = (uint8_t)m;
    }
}

static uint32_t s_pass_us_max = 0, s_pass_us_sum = 0, s_pass_n = 0;

void syncSenseUpdate(MotorController& mc1, MotorController& mc2) {
    MotorController* motors[2] = { &mc1, &mc2 };
    uint32_t t0 = micros();
    applyObserverParams();
    for (int m = 0; m < 2; m++) {
        MotorController& mc = *motors[m];
        MotorState&      st = s_m[m];
        st.fit_n = 0;
        float    sumsq0 = st.w_sumsq;
        uint32_t cnt0   = st.w_cnt;

        const uint32_t now_ms = millis();
        // At most one ring's worth per pass.  Reading "until empty" never ends if processing is
        // slower than the interrupt produces (it starved core 0 into the task watchdog).
        CurrentSample buf[16];
        int n, total = 0;
        while (total < SYNC_MAX_PER_PASS && (n = samplerRead(m, buf, 16)) > 0) {
            for (int i = 0; i < n; i++) processSample(mc, m, buf[i], now_ms);
            total += n;
        }

        if (!mc.enabled) {
            st.e_n = st.e_lo = st.e_off = 0;
            st.obs.reset();
            st.track_init = false;
            st.obs_updates = 0;
            st.est.valid = false;
        }

        // This pass's phase RMS (keep the previous value if nothing valid arrived).
        uint32_t cnt = st.w_cnt - cnt0;
        if (cnt > 0) mc.last_instantaneous_current = sqrtf((st.w_sumsq - sumsq0) / cnt);
        else if (!mc.enabled) mc.last_instantaneous_current = 0.0f;
        float instantaneous = mc.last_instantaneous_current;

        mc.protection_current = mc.protection_current * (1.0f - PROTECTION_FILTER_ALPHA)
                              + instantaneous * PROTECTION_FILTER_ALPHA;
        float dc_current = instantaneous * (3.0f * 1.4142f * fabsf(mc.motor.voltage.q)) / (2.0f * SUPPLY_VOLTAGE);
        mc.filtered_current = mc.filtered_current * (1.0f - CURRENT_FILTER_ALPHA)
                            + dc_current * CURRENT_FILTER_ALPHA;

        // Observer speed and validity, once per pass.
        ObserverEstimate& e = st.est;
        float rpm_cmd = fabsf(mc.current_velocity) * 60.0f / (2.0f * PI);
        if (st.obs.ready() && !st.obs.first) {
            if (st.spd_init && st.obs_seq != st.spd_seq) {
                float dt = (st.obs_seq - st.spd_seq) * samplerPeriodUs() * 1e-6f;
                float w  = (float)(st.obs.angle_unwrapped - st.spd_angle) / dt;
                float r  = fabsf(w) * 60.0f / (2.0f * PI * POLE_PAIRS);
                e.speed_rpm = isnan(e.speed_rpm) ? r : e.speed_rpm + (r - e.speed_rpm) * 0.1f;
            }
            st.spd_angle = st.obs.angle_unwrapped;
            st.spd_seq   = st.obs_seq;
            st.spd_init  = true;
            e.flux_ratio = sqrtf(st.obs.flux_alpha * st.obs.flux_alpha + st.obs.flux_beta * st.obs.flux_beta) /
                           st.obs.flux_linkage;
        } else {
            st.spd_init = false;
            e.speed_rpm = NAN;
        }
        e.valid    = mc.enabled && st.obs.ready() && st.track_init && rpm_cmd >= SYNC_OBS_MIN_RPM;
        e.load_deg = st.track_init ? st.delta_filt * 180.0f / PI : NAN;

        // Advisory: near pull-out (shadow mode: logged only).
        if (e.valid && e.load_deg > SYNC_LOAD_WARN_DEG && millis() - st.last_warn_ms > 1000) {
            st.last_warn_ms = millis();
            out(true, "SYNC M%d load angle %.0f deg at %.0f rpm (pull-out near 90)", m + 1, e.load_deg, rpm_cmd);
        }
    }
    uint32_t us = micros() - t0;
    s_pass_us_sum += us;
    s_pass_n++;
    if (us > s_pass_us_max) s_pass_us_max = us;
}

// ------------------- Logging -------------------

static void logLine(MotorController& mc, int m, bool to_link) {
    MotorState& st = s_m[m];
    ObserverEstimate& e = st.est;
    uint32_t n   = st.w_n ? st.w_n : 1;
    float    rpm = fabsf(mc.current_velocity) * 60.0f / (2.0f * PI);
    float    rms = st.w_cnt ? sqrtf(st.w_sumsq / st.w_cnt) : 0.0f;
    float    id  = st.w_dq ? st.w_id / st.w_dq : NAN;
    float    iq  = st.w_dq ? st.w_iq / st.w_dq : NAN;
    float    v   = st.w_vmag / n;
    if (to_link) {
        out(true, "SYNC M%d %.0frpm %.1fV rms %.2f d%+.2f q%+.2f full %.0f%% ld %+.0f mx %+.0f sp %.0f fx %.2f sl %u",
            m + 1, rpm, v, rms, id, iq, 100.0f * st.w_full / n, e.load_deg,
            st.delta_max > -1e8f ? st.delta_max : NAN, e.speed_rpm, e.flux_ratio, (unsigned)e.slips);
    } else {
        out(false, "[SYNC] M%d %5.0f rpm %5.2f V | n %u full %.0f%% lo %.0f%% mid %.0f%% late %u | rms %.2f pk %.2f "
                   "Id %+.2f Iq %+.2f | obs ld %+.1f max %+.1f spd %.0f flux %.2f slips %u\n",
            m + 1, rpm, v, (unsigned)st.w_n, 100.0f * st.w_full / n, 100.0f * st.w_lo / n, 100.0f * st.w_mid / n,
            (unsigned)st.w_late, rms, st.w_peak, id, iq, e.load_deg,
            st.delta_max > -1e8f ? st.delta_max : NAN, e.speed_rpm, e.flux_ratio, (unsigned)e.slips);
    }
}

static void resetWindow(MotorState& st) {
    st.w_n = st.w_full = st.w_lo = st.w_mid = st.w_late = st.w_off = 0;
    st.w_sumsq = 0; st.w_cnt = 0; st.w_peak = 0;
    st.w_id = st.w_iq = 0; st.w_dq = 0; st.w_vmag = 0;
    st.delta_max = -1e9f;
}

static void serviceCapture() {
    if (s_cap_on && s_cap_n >= s_cap_size) {
        s_cap_on   = false;
        s_cap_dump = 0;
        out(false, "[SYNCCAP] captured %u samples; dumping\n", (unsigned)s_cap_n);
        out(false, "cap,seq,motor,ia_mA,ib_mA,ic_mA,dA,dB,dC,ph_lo,ph_mid,flags,t_mid_ns,on_mid_ns,cmd_cdeg,obs_cdeg\n");
    }
    if (!s_cap || s_cap_on) return;
    // A few lines per pass, only while there is room (never blocks this task).
    for (int k = 0; k < 6 && s_cap_dump < s_cap_n; k++) {
        const CapRec& c = s_cap[s_cap_dump];
        char line[140];
        int  len = snprintf(line, sizeof(line), "cap,%u,%u,%d,%d,%d,%u,%u,%u,%u,%u,%u,%d,%d,%d,%d\n",
                            (unsigned)c.seq, c.motor + 1, c.i_ma[0], c.i_ma[1], c.i_ma[2],
                            c.duty_q[0], c.duty_q[1], c.duty_q[2], c.ph_lo, c.ph_mid, c.flags,
                            c.t_mid_ns, c.on_mid_ns, c.cmd_cdeg, c.obs_cdeg);
        if (!Serial || Serial.availableForWrite() < len) break;
        Serial.write(reinterpret_cast<const uint8_t*>(line), len);
        s_cap_dump++;
    }
    if (s_cap_dump >= s_cap_n) {
        out(false, "[SYNCCAP] done\n");
        free(s_cap);
        s_cap = nullptr;
        s_cap_n = s_cap_size = 0;
    }
}

// Sampler health to the XY board's log 5, 25 and 65 s after start (the XY board may not be
// listening at first), so the idle numbers are available without USB.
static void reportHealth(uint32_t now) {
    static const uint32_t AT_MS[] = { 5000, 25000, 65000 };
    static int next = 0;
    if (!s_started_ms || next >= 3 || now - s_started_ms < AT_MS[next]) return;
    next++;
    SamplerStats s = samplerStats();
    uint32_t     n = s.periods ? s.periods : 1;
    out(true, "SYNC health: %u periods, isr %.1f/%.1f us avg/max (%.0f%% core 0), late %u, dropped %u/%u",
        (unsigned)s.periods, s.isr_us_avg, s.isr_us_max, 100.0f * s.isr_us_avg / samplerPeriodUs(),
        (unsigned)s.late, (unsigned)s.dropped[0], (unsigned)s.dropped[1]);
    out(true, "SYNC entry ahead %.2f/%.2f/%.2f us min/avg/max; start err avg %+.3f max %+.3f us; off %.0f%%/%.0f%%",
        s.entry_us_min, s.entry_us_avg, s.entry_us_max, s.start_us_avg, s.start_us_max,
        100.0f * s.off[0] / n, 100.0f * s.off[1] / n);
    out(true, "SYNC pass processing %.0f us avg, %u us max (both motors)",
        s_pass_n ? (float)s_pass_us_sum / s_pass_n : 0.0f, (unsigned)s_pass_us_max);
    out(true, "SYNC timer spread %.3f us; zero sd M1 %.1f %.1f %.1f M2 %.1f %.1f %.1f LSB", samplerAlignSpreadUs(),
        samplerZeroNoiseLsb(0, 0), samplerZeroNoiseLsb(0, 1), samplerZeroNoiseLsb(0, 2),
        samplerZeroNoiseLsb(1, 0), samplerZeroNoiseLsb(1, 1), samplerZeroNoiseLsb(1, 2));
}

void syncSenseService(MotorController& mc1, MotorController& mc2) {
    MotorController* motors[2] = { &mc1, &mc2 };
    uint32_t now = millis();
    reportHealth(now);
    bool spinning = mc1.enabled || mc2.enabled;

    if (s_usb_log && now - s_last_usb_log >= 250) {
        s_last_usb_log = now;
        for (int m = 0; m < 2; m++) if (motors[m]->enabled) logLine(*motors[m], m, false);
        for (int m = 0; m < 2; m++) resetWindow(s_m[m]);
        s_last_link_log = now;
    } else if (!s_usb_log && spinning && now - s_last_link_log >= SYNC_LINK_LOG_MS) {
        // Bench build: a summary line per motor reaches the XY board's log without USB.
        s_last_link_log = now;
        for (int m = 0; m < 2; m++) if (motors[m]->enabled) logLine(*motors[m], m, true);
        for (int m = 0; m < 2; m++) resetWindow(s_m[m]);
    } else if (!spinning && !s_usb_log) {
        for (int m = 0; m < 2; m++) resetWindow(s_m[m]);
    }
    serviceCapture();
}

// ------------------- Commands -------------------

static void printStats() {
    SamplerStats  s = samplerStats();
    SamplerTiming t = samplerTiming();
    uint32_t      n = s.periods ? s.periods : 1;
    out(false, "[SYNC] %s, period %.2f us, dead time %.3f us, timer spread %.3f us\n",
        samplerRunning() ? "running" : "STOPPED", samplerPeriodUs(), samplerDeadTimeUs(), samplerAlignSpreadUs());
    out(false, "[SYNC] timing: lead %.2f us, mid %+.2f us, settle %.2f us, hold %.2f us\n",
        t.lead_us, t.mid_us, t.settle_us, t.hold_us);
    out(false, "[SYNC] %u periods, isr %.2f avg / %.2f max us (%.0f%% of core 0), late %u\n",
        (unsigned)s.periods, s.isr_us_avg, s.isr_us_max, 100.0f * s.isr_us_avg / samplerPeriodUs(), (unsigned)s.late);
    out(false, "[SYNC] entry ahead of target %.2f/%.2f/%.2f us (min/avg/max), start error avg %+.3f max %+.3f us\n",
        s.entry_us_min, s.entry_us_avg, s.entry_us_max, s.start_us_avg, s.start_us_max);
    for (int m = 0; m < 2; m++) {
        out(false, "[SYNC] M%d: lo ok %.1f%%, mid ok %.1f%%, full %.1f%%, outputs off %.1f%%, dropped %u\n",
            m + 1, 100.0f * s.lo_ok[m] / n, 100.0f * s.mid_ok[m] / n, 100.0f * s.full[m] / n,
            100.0f * s.off[m] / n, (unsigned)s.dropped[m]);
        out(false, "[SYNC] M%d zero %.3f/%.3f/%.3f V, noise sd %.1f/%.1f/%.1f LSB\n", m + 1,
            samplerZeroVolts(m, 0), samplerZeroVolts(m, 1), samplerZeroVolts(m, 2),
            samplerZeroNoiseLsb(m, 0), samplerZeroNoiseLsb(m, 1), samplerZeroNoiseLsb(m, 2));
    }
    out(false, "[SYNC] observer: R %.3f/%.3f ohm, L %.1f uH, flux %.5f Wb%s\n", s_m[0].obs.R, s_m[1].obs.R,
        s_obs_l * 1e6f, s_obs_flux, (s_m[0].obs.ready() && s_m[1].obs.ready()) ? "" : " (NOT READY: set with OBS)");
}

static void printHist() {
    for (int m = 0; m < 2; m++) {
        out(false, "[SETTLE] M%d middle-phase residual vs time since its low side turned on (us: mean sd n)\n", m + 1);
        for (int b = 0; b < H_BINS; b++) {
            const Hist& h = s_m[m].h_on;
            if (!h.n[b]) continue;
            float mean = h.sum[b] / h.n[b];
            float sd   = sqrtf(fmaxf(h.sumsq[b] / h.n[b] - mean * mean, 0.0f));
            out(false, "[SETTLE] M%d on %+5.1f %+.3f %.3f %u\n", m + 1, H_ON_MIN + (b + 0.5f) * H_STEP, mean, sd, (unsigned)h.n[b]);
        }
        out(false, "[SETTLE] M%d middle-phase residual vs time left before its low side turns off\n", m + 1);
        for (int b = 0; b < H_BINS; b++) {
            const Hist& h = s_m[m].h_off;
            if (!h.n[b]) continue;
            float mean = h.sum[b] / h.n[b];
            float sd   = sqrtf(fmaxf(h.sumsq[b] / h.n[b] - mean * mean, 0.0f));
            out(false, "[SETTLE] M%d off %+5.1f %+.3f %.3f %u\n", m + 1, H_OFF_MIN + (b + 0.5f) * H_STEP, mean, sd, (unsigned)h.n[b]);
        }
    }
}

// Parse "key=value" pairs from `args` into `t`; false on an unknown key.
static bool parseTiming(const char* args, SamplerTiming& t) {
    char buf[96];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    for (char* tok = strtok(buf, " "); tok; tok = strtok(nullptr, " ")) {
        char* eq = strchr(tok, '=');
        if (!eq) return false;
        *eq = 0;
        float v = atof(eq + 1);
        if      (!strcasecmp(tok, "lead"))   t.lead_us   = v;
        else if (!strcasecmp(tok, "mid"))    t.mid_us    = v;
        else if (!strcasecmp(tok, "settle")) t.settle_us = v;
        else if (!strcasecmp(tok, "hold"))   t.hold_us   = v;
        else return false;
    }
    return true;
}

bool syncSenseCommand(const char* line) {
    if (!strcmp(line, "SYNC")) {
        printStats();
        return true;
    }
    if (!strcmp(line, "SYNCRESET")) {
        samplerResetStats();
        for (int m = 0; m < 2; m++) { resetHist(s_m[m].h_on); resetHist(s_m[m].h_off); }
        out(false, "[SYNC] statistics and settling histogram reset\n");
        return true;
    }
    if (!strcmp(line, "SYNCLOG")) {
        s_usb_log = !s_usb_log;
        out(false, "[SYNC] USB log %s\n", s_usb_log ? "ON (4 lines/s per motor)" : "OFF");
        return true;
    }
    if (!strcmp(line, "SETTLE")) {
        printHist();
        return true;
    }
    if (!strncmp(line, "SYNCSET", 7)) {
        SamplerTiming t = samplerTiming();
        if (line[7] && !parseTiming(line + 8, t)) {
            out(false, "[SYNC] usage: SYNCSET lead=<us> mid=<us> settle=<us> hold=<us>\n");
            return true;
        }
        if (t.lead_us < 0.0f || t.lead_us > 24.0f || fabsf(t.mid_us) > 10.0f) {
            out(false, "[SYNC] refused: lead 0..24 us, mid -10..10 us\n");
            return true;
        }
        samplerSetTiming(t);
        for (int m = 0; m < 2; m++) { resetHist(s_m[m].h_on); resetHist(s_m[m].h_off); }
        samplerResetStats();
        out(false, "[SYNC] lead %.2f us, mid %+.2f us, settle %.2f us, hold %.2f us (histogram reset)\n",
            t.lead_us, t.mid_us, t.settle_us, t.hold_us);
        return true;
    }
    if (!strncmp(line, "SYNCCAP", 7)) {
        if (s_cap) { out(false, "[SYNCCAP] busy\n"); return true; }
        int periods = line[7] ? atoi(line + 8) : 1000;
        periods = constrain(periods, 10, 4000);
        uint32_t size = 2u * periods;
        s_cap = (CapRec*)malloc(size * sizeof(CapRec));
        if (!s_cap) { out(false, "[SYNCCAP] no memory for %u samples\n", (unsigned)size); return true; }
        s_cap_size = size;
        s_cap_n    = 0;
        s_cap_on   = true;
        out(false, "[SYNCCAP] capturing %d periods of both motors\n", periods);
        return true;
    }
    if (!strncmp(line, "OBS", 3) && (line[3] == 0 || line[3] == ' ')) {
        char buf[96];
        strncpy(buf, line + 3, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        for (char* tok = strtok(buf, " "); tok; tok = strtok(nullptr, " ")) {
            char* eq = strchr(tok, '=');
            if (!eq) continue;
            *eq = 0;
            float v = atof(eq + 1);
            if      (!strcasecmp(tok, "R"))  s_obs_r[0] = s_obs_r[1] = v;
            else if (!strcasecmp(tok, "R1")) s_obs_r[0] = v;
            else if (!strcasecmp(tok, "R2")) s_obs_r[1] = v;
            else if (!strcasecmp(tok, "L"))  s_obs_l = v;
            else if (!strcasecmp(tok, "F"))  s_obs_flux = v;
        }
        applyObserverParams();
        for (int m = 0; m < 2; m++) { s_m[m].obs.reset(); s_m[m].track_init = false; s_m[m].obs_updates = 0; }
        out(false, "[OBS] R %.3f/%.3f ohm, L %.1f uH, flux %.5f Wb (%.3f V per 1000 rpm)%s\n",
            s_m[0].obs.R, s_m[1].obs.R, s_obs_l * 1e6f, s_obs_flux, s_obs_flux * 1000.0f * 2.0f * PI / 60.0f * POLE_PAIRS,
            (s_m[0].obs.ready() && s_m[1].obs.ready()) ? "" : "  NOT READY: needs R, L and flux");
        return true;
    }
    return false;
}

#endif
