// The control interrupt took 44-45 us of the 50 us period on boards 1 and 2 (two motors: observer, angle loop,
// current loops, modulator), and a few us more with a bring-up capture running starved the main loop until the
// watchdog reset the chip (board 2, 2026-11-23).  About 30 calls per interrupt went to the library's fminf /
// fmaxf / sqrtf: with these options the compiler emits them inline (no NaN or errno handling, which nothing
// here relies on: no isnan / isfinite in this file).
#pragma GCC optimize("O3", "fast-math")

#include "foc.h"
#include "hw.h"
#include "board.h"
#include "config.h"
#include <math.h>

namespace foc {
    namespace {
        constexpr float TWO_PI_F  = 2.0f * (float)M_PI;
        constexpr float PI_F      = (float)M_PI;
        constexpr float SQRT3     = 1.7320508f;
        constexpr float INV_SQRT3 = 0.57735027f;
        constexpr float DT        = PWM_PERIOD_S;
        // The highest duty: the low side then stays on >= 1 us per period (bootstrap refresh).  The
        // highest-duty phase's sample is never used (two-shunt rebuild).  modulate() fits the three
        // duties into 0..DUTY_MAX; voltageLimit() keeps the current loops inside that.
        constexpr float DUTY_MAX  = 0.98f;
        // The duties computed now load at the next peak (hw.cpp alignUpdateToPeak) and hold for the period
        // after it, centred on its valley: 1.5 periods after the sample the angle was measured at.  So the
        // period that ends at a sample ran on the duties computed two interrupts earlier (Motor::duty_prev,
        // va_prev / vb_prev): the two-shunt rebuild and the observer use those.
        constexpr float PWM_DELAY_PERIODS = 1.5f;

        inline float wrapPi(float a) {
            while (a > PI_F) a -= TWO_PI_F;
            while (a < -PI_F) a += TWO_PI_F;
            return a;
        }
        // Fast sin/cos and atan2 for the 20 kHz interrupt.  newlib's sinf/cosf/atan2f (full argument
        // reduction, ~150-400 cycles each, ~20 calls per interrupt) took most of the 50 us budget: on
        // board 1 the interrupt overran its period in an open-loop spin and the watchdog reset the chip.
        // Quadrant reduction + degree-7/8 Taylor on [-pi/4, pi/4]: error < 4e-7, any x below ~1e9.
        inline void fastSinCos(float x, float* s, float* c) {
            constexpr float TWO_OVER_PI = 0.636619772f;
            constexpr float PIO2_HI     = 1.5707963705062866f;   // pi/2 in two parts (Cody-Waite)
            constexpr float PIO2_LO     = -4.371139000186243e-8f;
            float           fq          = x * TWO_OVER_PI;
            int             q           = (int)(fq + (fq >= 0.0f ? 0.5f : -0.5f));
            float           r           = (x - (float)q * PIO2_HI) - (float)q * PIO2_LO;
            float           r2          = r * r;
            float           sn          = r * (1.0f + r2 * (-1.6666667e-1f + r2 * (8.3333333e-3f + r2 * -1.9841270e-4f)));
            float cs = 1.0f + r2 * (-0.5f + r2 * (4.1666667e-2f + r2 * (-1.3888889e-3f + r2 * 2.4801587e-5f)));
            switch (q & 3) {
                case 0: *s = sn; *c = cs; break;
                case 1: *s = cs; *c = -sn; break;
                case 2: *s = -sn; *c = -cs; break;
                default: *s = -cs; *c = sn; break;
            }
        }
        // Octant reduction + odd degree-9 polynomial for atan on [0, 1]: error < 1e-5 rad.
        inline float fastAtan2(float y, float x) {
            float ax = fabsf(x), ay = fabsf(y);
            float mx = fmaxf(ax, ay), mn = fminf(ax, ay);
            if (mx == 0.0f) return 0.0f;
            float a = mn / mx;
            float q = a * a;
            float r = ((((0.0208351f * q - 0.0851330f) * q + 0.1801410f) * q - 0.3302995f) * q + 0.9998660f) * a;
            if (ay > ax) r = 1.57079637f - r;
            if (x < 0.0f) r = 3.14159274f - r;
            if (y < 0.0f) r = -r;
            return r;
        }

        inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
        inline float slew(float x, float target, float step) {
            if (x < target) return fminf(x + step, target);
            if (x > target) return fmaxf(x - step, target);
            return x;
        }

        struct Motor {
            int   dir;
            float off[3];                 // ADC zero, counts
            float gain[3];                // per-phase current gain correction (#GCAL), 1 = nominal
            float ia, ib, ic;             // A
            float ial, ibe;               // A, stationary frame
            float duty[3];                // duties computed this interrupt (in force from the next peak)
            float duty_prev[3];           // ... the previous interrupt's: in force during this sample
            // MXLEMMING flux observer (SimpleFOCDrivers' MXLEMMINGObserverSensor, as on the ESC1)
            float flux_a, flux_b;
            float ial_prev, ibe_prev;
            float obs_angle, obs_w;       // rad, rad/s (electrical = mechanical, 1 pole pair)
            float obs_w_lock;             // rad/s, slower average for the hand-over lock test
            float obs_da;                 // observer angle step this period
            // control
            float theta;
            float id, iq, id_ref, iq_ref;
            float int_d, int_q;
            float vd, vq;
            float valpha, vbeta;          // last commanded voltage
            float va_prev, vb_prev;       // ... the one before: in force over the period that just ended (the observer's input)
            // Both modes drive toward the same reference angle, ref = dir*alpha + phi + c: open loop
            // puts the current vector there, closed loop steers the observed rotor angle onto it.
            float c;                      // reference angle constant
            float ref;                    // reference angle this period (wrapped)
            float eps;                    // reference minus observed angle, unwrapped (rad); see eps_valid
            bool  eps_valid;              // eps is tracked: closed loop, or open loop with the observer locked
            float pos_int;                // closed loop: integral term (A)
            float id_cl;                  // closed loop: the open loop's d current, fading out
            float iq_carry;               // open loop: the closed loop's torque current, fading out
            float speed_err_frac;
            int   over_count;
            float fw_id;                  // closed loop: field-weakening d current magnitude (#FW), A
            // voltage headroom (takeVolt): demand / limit, its peak, clipped and counted periods, duty
            // writes made after the valley
            float    u, u_max;
            uint32_t sat_n, u_n, late_n;
            // telemetry since the last take()
            float    imax, i2sum;
            uint32_t n;
        };

        Motor mot[2];

        volatile bool  g_enabled  = false;
        volatile Mode  g_mode     = Mode::Off;
        volatile float g_omega_target = 0;   // rad/s, magnitude
        float          g_omega_cmd    = 0;
        float          g_alpha        = 0;   // spin angle, wrapped
        volatile float g_phi          = 0;   // phase offset (Z), rad
        volatile float g_phi_target   = 0;
        float          g_phi_rate     = 0;
        volatile bool  g_z_moving     = false;
        float          g_ol_current   = 0;
        volatile float g_zerr         = 0;   // closed loop: mean of the two angle errors (= phase error)
        volatile float g_angle_err_max = 0;  // closed loop: worse of the two |angle errors|
        volatile float g_vbus         = 36.0f;
        uint32_t       g_precharge    = 0;
        volatile bool  g_req_closed   = false;
        volatile bool  g_req_open     = false;
        volatile float g_align_volts  = 0;   // > 0: voltage-mode align test at angle 0
        volatile float g_align_va = 0, g_align_vb = 0;   // the align vector (alpha, beta), V

        volatile uint8_t g_fault       = FaultNone;
        volatile int8_t  g_fault_motor = -1;
        volatile int8_t  g_fault_phase = -1;
        volatile float   g_fault_amps  = 0;

        volatile bool  g_calibrating = false;
        volatile uint32_t g_cal_n    = 0;
        float          g_cal_sum[2][3];

        volatile uint32_t g_isr_max_cycles = 0;
        volatile uint32_t g_isr_count      = 0;
        volatile uint32_t g_fake_load_cycles = 0;   // #LOAD: pad the interrupt to this length (bring-up)
        volatile uint32_t g_uart_err[3]      = { 0, 0, 0 };   // USART2 ORE / FE / NE seen by the interrupt (bring-up)
        volatile uint32_t g_overruns    = 0;   // interrupts that ended with the next sample already waiting
        // #JMEAS: each motor's measured iq along its own direction of rotation, summed per interrupt.
        volatile bool     g_iqacc_on = false;
        float             g_iqacc_sum[2];
        volatile uint32_t g_iqacc_n = 0;
        uint32_t          g_overrun_run = 0;   // ... in a row
        constexpr uint32_t OVERRUN_TRIP = 8;   // this many in a row starve the main loop: motors off

        // #NOISE: raw statistics of all six sense channels over a fixed number of samples (bring-up).
        volatile bool     g_noise_on = false;
        volatile uint32_t g_noise_n  = 0;
        uint32_t          g_noise_target = 0;
        uint32_t          g_noise_sum[2][3];
        uint64_t          g_noise_sq[2][3];
        uint16_t          g_noise_min[2][3], g_noise_max[2][3];
        // ... and of each motor's phase sum, gain-corrected, in 1/16 counts.  A star-connected motor's three
        // currents sum to zero, so this is the measurement's own error (common-mode noise, zero and gain
        // errors) even while the motor runs, when the channels themselves show the real currents.
        int64_t           g_noise_ps_sum[2], g_noise_ps_sq[2];
        int32_t           g_noise_ps_min[2], g_noise_ps_max[2];
        // ... and of each motor's control signals over the same samples (what a ripple once or more per turn does
        // to them; #VLOG's 200 ms lines are a whole number of turns apart at most speeds and do not show it):
        // angle error eps (0.1 mrad), observer speed (0.1 rad/s), iq_ref, iq, id (mA).
        // One motor's one signal per interrupt, in turn: all ten at once took more than the ~4 us the closed
        // loop leaves of the 50 us period, and eight overruns in a row stopped the drive (board 2).
        constexpr int     RIP_N = 5;
        struct Rip {
            int64_t  sum, sq;
            int32_t  mn, mx;
            uint32_t n;
        };
        Rip               g_rip[2][RIP_N];
        uint32_t          g_rip_i = 0;

        // #SCOPE: one motor's internal signals at the sample rate (or every n-th sample), for the ripple's shape.
        constexpr int     SCOPE_MAX = 1024;
        int16_t           g_scope[SCOPE_MAX][SCOPE_CH];
        volatile bool     g_scope_on = false;
        volatile int      g_scope_n  = 0;
        int               g_scope_motor = 0, g_scope_decim = 1, g_scope_skip = 0;
        bool              g_scope_half = false;

        // Fault post-mortem (#BRKDUMP): the last BRK_RING interrupts' raw samples, the comparator outputs read
        // at each interrupt's start and the duties in force during each sample, frozen when a fault (a
        // comparator break, a software over-current) stops the bridges (bring-up: a real current ramp, or noise?).
        constexpr int BRK_RING = 16;
        struct BrkEntry {
            uint16_t raw[6];
            uint16_t duty[6];   // x 10000, M1 a b c, M2 a b c
            uint8_t  cmp;       // comparator outputs, bit per phase (hw::comparatorOutputs)
        };
        BrkEntry          g_brk[BRK_RING];
        volatile uint32_t g_brk_n      = 0;       // entries written since the last unfreeze
        volatile bool     g_brk_frozen = false;

        // #DUMP: consecutive raw samples of all six channels (bring-up: is the sense noise periodic?).
        constexpr int     DUMP_MAX = 1024;
        uint16_t          g_dump[DUMP_MAX][6];
        volatile int      g_dump_n      = 0;
        int               g_dump_target = 0;
        volatile bool     g_dump_on     = false;

        Tuning g_tun = {
            CUR_KP, CUR_KI, POS_KP, POS_KD, POS_KI, POS_BW_MIN, POS_BW_FULL_RPM, OL_CUR_KI,
            HOLD_CURRENT_A, SPINUP_CURRENT_A, Z_MOVE_BOOST_A, IQ_LIMIT_A,
            PHASE_R_OHM, PHASE_L_H, FLUX_WB, SW_TRIP_A, DEADTIME_COMP_NS, DT_COMP_KNEE_A, true, FW_MAX_A, FW_TARGET,
        };

        void resetControl(Motor& m) {
            m.flux_a = m.flux_b = 0;
            m.ial_prev = m.ibe_prev = 0;
            m.obs_w = 0;
            m.obs_w_lock = 0;
            m.int_d = m.int_q = 0;
            m.vd = m.vq = m.valpha = m.vbeta = 0;
            m.va_prev = m.vb_prev = 0;
            m.pos_int = m.id_cl = m.iq_carry = 0;
            m.eps       = 0;
            m.eps_valid = false;
            m.over_count = 0;
            m.speed_err_frac = 0;
            m.fw_id = 0;
            m.u     = 0;
        }

        void outputsOff() {
            hw::disableOutputs(0);
            hw::disableOutputs(1);
            hw::writeDuty(0, 0, 0, 0);
            hw::writeDuty(1, 0, 0, 0);
            g_enabled = false;
            g_mode    = Mode::Off;
            g_align_volts = 0;
            g_req_closed = g_req_open = false;
        }

        void raiseFault(uint8_t f, int motor, int phase, float amps) {
            outputsOff();
            g_brk_frozen = true;   // keep the samples before it (#BRKDUMP)
            if (g_fault == FaultNone) {
                g_fault       = f;
                g_fault_motor = motor;
                g_fault_phase = phase;
                g_fault_amps  = amps;
            }
        }

        // Measured currents of motor k this period.
        void senseCurrents(int k, const uint16_t raw[3], bool rebuild) {
            Motor& m = mot[k];
            float  i[3];
            for (int p = 0; p < 3; p++) i[p] = SENSE_SIGN * ((float)raw[p] - m.off[p]) * SENSE_A_PER_LSB * m.gain[p];
            if (rebuild) {
                // The highest-duty phase has the shortest low-side time, so its sample sits closest to
                // the switching edge; rebuild it from the other two (they sum to zero).  ESC1 bench.
                int hi = 0;
                if (m.duty_prev[1] > m.duty_prev[hi]) hi = 1;
                if (m.duty_prev[2] > m.duty_prev[hi]) hi = 2;
                i[hi] = -(i[(hi + 1) % 3] + i[(hi + 2) % 3]);
            }
            m.ia  = i[0];
            m.ib  = i[1];
            m.ic  = i[2];
            m.ial = i[0];
            m.ibe = (i[1] - i[2]) * INV_SQRT3;
            float amax = fmaxf(fabsf(i[0]), fmaxf(fabsf(i[1]), fabsf(i[2])));
            if (amax > m.imax) m.imax = amax;
            m.i2sum += (i[0] * i[0] + i[1] * i[1] + i[2] * i[2]) * (1.0f / 1.5f);
            m.n++;
        }

        void observerUpdate(Motor& m) {
            const float R = g_tun.r, L = g_tun.l, F = g_tun.flux;
            m.flux_a = clampf(m.flux_a + (m.va_prev - R * m.ial) * DT - L * (m.ial - m.ial_prev), -F, F);
            m.flux_b = clampf(m.flux_b + (m.vb_prev - R * m.ibe) * DT - L * (m.ibe - m.ibe_prev), -F, F);
            m.ial_prev = m.ial;
            m.ibe_prev = m.ibe;
            float a  = fastAtan2(m.flux_b, m.flux_a);
            float da = wrapPi(a - m.obs_angle);
            m.obs_angle = a;
            m.obs_w += (da / DT - m.obs_w) * fminf(DT / OBS_SPEED_TAU_S, 1.0f);
            m.obs_w_lock += (m.obs_w - m.obs_w_lock) * (DT / OBS_LOCK_TAU_S);
            m.obs_da = da;
        }

        // The current loops' voltage limit (V, magnitude of vd/vq): the modulator's linear range - the
        // three duties fit in 0..DUTY_MAX (modulate), less the top phase's dead-time compensation - times
        // MAX_MODULATION.  ~20.5 V on a 36.4 V bus.
        inline float voltageLimit() {
            float vdt = fmaxf(g_tun.deadtime_ns, 0.0f) * 1e-9f / PWM_PERIOD_S;
            return MAX_MODULATION * (DUTY_MAX - vdt) * g_vbus * INV_SQRT3;
        }

        // dq current loops for one motor at angle m.theta, electrical speed we.
        //   soft = false (closed loop): PI current loops, ~400 Hz (ESC1 gains), decoupling + EMF feed-forward.
        //   soft = true  (open loop): a voltage drive - R*I, L and back-EMF feed-forward - trimmed by a
        //   slow integrator onto the target current.  The rotor's swings about the current vector then
        //   see the motor's own back-EMF through R, the electrical damping the ESP32's voltage drive
        //   had; a stiff current loop would cancel it and leave an undamped spring (test/sim).
        void currentLoop(int k, float we, bool emf_ff, bool soft) {
            Motor&      m    = mot[k];
            const float vmax = voltageLimit();
            float s, c;
            fastSinCos(m.theta, &s, &c);
            m.id = m.ial * c + m.ibe * s;
            m.iq = -m.ial * s + m.ibe * c;

            const float L = g_tun.l;
            float ed = m.id_ref - m.id, eq = m.iq_ref - m.iq;
            float vd, vq, int_d, int_q;
            if (soft) {
                float vd_ff = g_tun.r * m.id_ref - we * L * m.iq_ref;
                float vq_ff = g_tun.r * m.iq_ref + we * L * m.id_ref + (emf_ff ? we * g_tun.flux : 0.0f);
                int_d = clampf(m.int_d + g_tun.ol_ki * ed * DT, -vmax, vmax);
                int_q = clampf(m.int_q + g_tun.ol_ki * eq * DT, -vmax, vmax);
                vd = int_d + vd_ff;
                vq = int_q + vq_ff;
            } else {
                float vd_ff = -we * L * m.iq;
                float vq_ff = we * L * m.id + (emf_ff ? we * g_tun.flux : 0.0f);
                int_d = clampf(m.int_d + g_tun.cur_ki * ed * DT, -vmax, vmax);
                int_q = clampf(m.int_q + g_tun.cur_ki * eq * DT, -vmax, vmax);
                vd = g_tun.cur_kp * ed + int_d + vd_ff;
                vq = g_tun.cur_kp * eq + int_q + vq_ff;
            }
            float mag = sqrtf(vd * vd + vq * vq);
            m.u       = mag / fmaxf(vmax, 1e-3f);
            if (m.u > m.u_max) m.u_max = m.u;
            m.u_n++;
            if (mag > vmax) {
                float k2 = vmax / mag;
                vd *= k2;
                vq *= k2;
                m.sat_n++;
                // Saturated: hold the integrators where they were (no wind-up).
            } else {
                m.int_d = int_d;
                m.int_q = int_q;
            }
            // Field weakening (closed loop, #FW): the demand's excess over fw_target of the limit winds
            // up a negative d current (the caller subtracts fw_id from id_ref); below it, it unwinds.
            if (!soft) m.fw_id = clampf(m.fw_id + FW_KI * (m.u - g_tun.fw_target) * DT, 0.0f, g_tun.fw_max_a);
            m.vd = vd;
            m.vq = vq;
            // Rotate back at the angle the rotor will have while this voltage is applied.
            float th = m.theta + we * PWM_DELAY_PERIODS * DT;
            float s2, c2;
            fastSinCos(th, &s2, &c2);
            m.va_prev = m.valpha;
            m.vb_prev = m.vbeta;
            m.valpha  = vd * c2 - vq * s2;
            m.vbeta   = vd * s2 + vq * c2;
        }

        void modulate(int k, bool dt_comp) {
            Motor& m  = mot[k];
            float  va = m.valpha;
            float  vb = -0.5f * m.valpha + 0.5f * SQRT3 * m.vbeta;
            float  vc = -0.5f * m.valpha - 0.5f * SQRT3 * m.vbeta;
            float  vcm = 0.5f * (fmaxf(va, fmaxf(vb, vc)) + fminf(va, fminf(vb, vc)));
            float  inv = 1.0f / fmaxf(g_vbus, 5.0f);
            // Dead-time compensation: during the dead time a leg follows its current's body diode,
            // which costs dt/T * Vbus against the current.  Add it back, in proportion to the current up
            // to the knee (config.h: a steep knee makes it a negative resistance larger than the
            // winding's).  The observer reads the commanded voltage, so it needs the applied one to
            // match it.
            float  comp[3] = { 0, 0, 0 };
            if (dt_comp) {
                float vdt = g_tun.deadtime_ns * 1e-9f / PWM_PERIOD_S;   // as a duty
                const float i[3] = { m.ia, m.ib, m.ic };
                const float inv_knee = 1.0f / fmaxf(g_tun.dt_knee_a, 0.05f);
                for (int x = 0; x < 3; x++) comp[x] = vdt * clampf(i[x] * inv_knee, -1.0f, 1.0f);
            }
            float d[3] = { 0.5f + (va - vcm) * inv + comp[0], 0.5f + (vb - vcm) * inv + comp[1],
                           0.5f + (vc - vcm) * inv + comp[2] };
            // Centred (min-max SVPWM) while the duties fit under DUTY_MAX.  Above that all three move
            // down together - the line voltages do not change - so the top one sits at DUTY_MAX and the
            // bottom one may reach 0: its low side then stays on all period, with no switching and so no
            // dead time to make up.  That uses the whole 0..DUTY_MAX range, DUTY_MAX / sqrt(3) * Vbus less
            // the compensation (~20.5 V on 36.4 V), where centred duties stopped at (2 DUTY_MAX - 1) /
            // sqrt(3) * Vbus (~20.2 V).  The low-side sampling only gains: the zero vector sampled
            // (all low sides on) grows.
            float hi = fmaxf(d[0], fmaxf(d[1], d[2]));
            if (hi > DUTY_MAX) {
                float sh = hi - DUTY_MAX;
                d[0] -= sh;
                d[1] -= sh;
                d[2] -= sh;
            }
            for (int x = 0; x < 3; x++) {
                m.duty_prev[x] = m.duty[x];
                m.duty[x]      = clampf(d[x], 0.0f, DUTY_MAX);
            }
            hw::writeDuty(k, (uint16_t)(m.duty[0] * PWM_ARR), (uint16_t)(m.duty[1] * PWM_ARR),
                          (uint16_t)(m.duty[2] * PWM_ARR));
            // The interrupt starts just after the peak, counting down; the writes must land before the next
            // peak's update.  A write while the counter counts down again (past the next peak) would load two
            // periods late (bring-up: takeVolt().late counts writes made after the valley, still in time).
            if (!((k ? TIM8 : TIM1)->CR1 & TIM_CR1_DIR)) m.late_n++;
        }

        // Rotate the last applied voltage into the frame at angle th, for loop integrators.
        void voltageIn(const Motor& m, float th, float* vd, float* vq) {
            float s, c;
            fastSinCos(th, &s, &c);
            *vd = m.valpha * c + m.vbeta * s;
            *vq = -m.valpha * s + m.vbeta * c;
        }

        // Closed-loop angle PID gains at the present speed.  Below pos_bw_full_rpm the loop's bandwidth
        // b falls linearly to pos_bw_min at the hand-back speed; Kp*b^2, Kd*b, Ki*b^3 keep its damping.
        // The observer is weakest at low back-EMF, and at full gains the loop limit-cycled there in the
        // simulation, which made the hand-back a gamble (config.h POS_BW_MIN).
        void posGains(float* kp, float* kd, float* ki) {
            float rpm  = g_omega_cmd * (1.0f / RPM_TO_RAD);
            float span = fmaxf(g_tun.pos_bw_full_rpm - CL_EXIT_RPM, 1.0f);
            float bmin = g_tun.pos_bw_min;
            float b    = clampf(bmin + (1.0f - bmin) * (rpm - CL_EXIT_RPM) / span, bmin, 1.0f);
            *kp = g_tun.pos_kp * b * b;
            *kd = g_tun.pos_kd * b;
            *ki = g_tun.pos_ki * b * b * b;
        }

        void handoverToClosed() {
            float kp, kd, ki;
            posGains(&kp, &kd, &ki);
            for (int k = 0; k < 2; k++) {
                Motor& m = mot[k];
                // The rotor lags the open-loop vector by its load angle: that is the starting error,
                // so the closed loop pulls the rotor onto the same reference and the Z does not move.
                if (!m.eps_valid) m.eps = wrapPi(m.ref - m.obs_angle);
                m.eps_valid = true;
                // The current already flowing, as the open loop's own current vector (its references:
                // the slow trim holds the average on them) turned into the observer's frame by the load
                // angle - not this instant's measured current, which on board 1's motor 1 carries phase
                // B's sense noise (bursts to ~10 A) and kicked the angle loop at the hand-over.
                float sr, cr;
                fastSinCos(m.eps, &sr, &cr);
                float id = m.id_ref * cr - m.iq_ref * sr;
                float iq = m.id_ref * sr + m.iq_ref * cr;
                float vd, vq;
                voltageIn(m, m.obs_angle, &vd, &vq);
                float we = m.obs_w;
                // Start every loop from the voltage and current already there: no step at the hand-over.
                m.int_d = vd - (-we * g_tun.l * iq);
                m.int_q = vq - (we * g_tun.l * id + we * g_tun.flux);
                float wref = m.dir * g_omega_cmd + g_phi_rate;
                m.pos_int = clampf(iq - kp * m.eps - kd * (wref - m.obs_w), -g_tun.iq_limit, g_tun.iq_limit);
                m.id_cl   = id;   // fades to 0
                m.fw_id   = 0;    // field weakening starts from none (it is a high-speed matter)
            }
            g_mode = Mode::Closed;
        }

        void handbackToOpen() {
            // Open loop drives the same reference angle (the rotor is on it, within eps), while the
            // closed loop's torque current fades out (iq_carry).  The trims start from the closed loop's own
            // state - its current references and its integrators plus feed-forward at those references,
            // turned from its frame (the observed angle) into the reference frame (ref = observed + eps) -
            // not from this instant's measured current and last commanded voltage, which carry the sense
            // noise (Kp passes it straight into the voltage).  On board 1 that once seeded motor 2 ~2 A short
            // of the spin current: it could not hold the rotor and the slip check stopped the spin 70 ms
            // after the hand-back (2026-11-20).  The hand-over seeds the same way (handoverToClosed).
            for (int k = 0; k < 2; k++) {
                Motor& m     = mot[k];
                float  we_cl = m.obs_w;   // the speed the closed loop fed forward
                float  vdc   = m.int_d - we_cl * g_tun.l * m.iq_ref;
                float  vqc   = m.int_q + we_cl * g_tun.l * m.id_ref + we_cl * g_tun.flux;
                float  se, ce;
                fastSinCos(m.eps, &se, &ce);
                float id = m.id_ref * ce + m.iq_ref * se;
                float iq = -m.id_ref * se + m.iq_ref * ce;
                float vd = vdc * ce + vqc * se;
                float vq = -vdc * se + vqc * ce;
                // The soft loop applies trim + feed-forward (currentLoop): start the trims from that voltage
                // LESS the feed-forward for the current already flowing, so the feed-forward is not counted
                // twice (that was a whole back-EMF, ~1.2 V on q at 1500 RPM, as a step).  The one step left
                // is the intended one, below.
                float we   = m.dir * g_omega_cmd + g_phi_rate;   // the speed the open loop feeds forward
                m.int_d    = vd - (g_tun.r * id - we * g_tun.l * iq);
                m.int_q    = vq - (g_tun.r * iq + we * g_tun.l * id + we * g_tun.flux);
                // Carry the angle loop's integral - the steady load - not the iq of this instant,
                // which may be the peak of a swing that the open loop's d current cannot hold.
                m.iq_carry = clampf(m.pos_int, -g_tun.spin_a, g_tun.spin_a);
            }
            // Full spin current at once: the d current is what holds each rotor to its reference
            // in open loop (the closed loop's id was ~0).  The soft loop's R*I feed-forward makes the
            // step a voltage step, so the current follows within L/R (~0.2 ms).
            g_ol_current = g_tun.spin_a;
            g_mode       = Mode::Open;
        }

        void step(const uint16_t raw[2][3]) {
            // Hardware break (comparator over-current) on either bridge: both off.
            if ((TIM1->SR & TIM_SR_BIF) || (TIM8->SR & TIM_SR_BIF)) {
                if (g_enabled) raiseFault(FaultBreak, (TIM1->SR & TIM_SR_BIF) ? 0 : 1, -1, 0);
            }

            bool live = g_enabled && g_precharge == 0;
            for (int k = 0; k < 2; k++) senseCurrents(k, raw[k], live && g_align_volts == 0);

            // Software over-current: any phase over the limit for SW_TRIP_COUNT periods in a row.
            if (g_enabled) {
                for (int k = 0; k < 2; k++) {
                    Motor& m = mot[k];
                    float  i[3] = { m.ia, m.ib, m.ic };
                    int    worst = 0;
                    for (int p = 1; p < 3; p++)
                        if (fabsf(i[p]) > fabsf(i[worst])) worst = p;
                    if (fabsf(i[worst]) > g_tun.sw_trip_a) {
                        if (++m.over_count >= SW_TRIP_COUNT) raiseFault(FaultOverCurrent, k, worst, i[worst]);
                    } else {
                        m.over_count = 0;
                    }
                }
            }

            if (!g_enabled) {
                for (int k = 0; k < 2; k++) {
                    mot[k].flux_a = mot[k].flux_b = 0;
                    mot[k].obs_w  = 0;
                    mot[k].obs_w_lock = 0;
                    mot[k].valpha = mot[k].vbeta = 0;
                    mot[k].va_prev = mot[k].vb_prev = 0;
                }
                return;
            }

            if (g_precharge) {
                // Low sides on (duty 0) for a few periods to charge the DRV8300 bootstrap capacitors.
                hw::writeDuty(0, 0, 0, 0);
                hw::writeDuty(1, 0, 0, 0);
                for (int k = 0; k < 2; k++)
                    for (int x = 0; x < 3; x++) mot[k].duty[x] = mot[k].duty_prev[x] = 0;
                g_precharge--;
                return;
            }

            // Voltage-mode align test (bring-up): a fixed voltage vector (phase A axis unless #GCAL turns
            // it), no current loop.
            if (g_align_volts > 0) {
                for (int k = 0; k < 2; k++) {
                    mot[k].valpha = g_align_va;
                    mot[k].vbeta  = g_align_vb;
                    modulate(k, false);   // raw: the align test reads the sense, not the drive
                }
                return;
            }

            // Speed ramp.  In open loop the ramp waits at the hand-over speed until the observers lock.
            float target = g_omega_target;
            if (g_mode == Mode::Open && g_tun.closed_loop_allowed) target = fminf(target, CL_ENTER_RPM * RPM_TO_RAD);
            float rate = g_omega_cmd >= SPIN_RAMP_SLOW_ABOVE_RPM * RPM_TO_RAD ? SPIN_RAMP_HIGH_RAD_S2 : SPIN_RAMP_RAD_S2;
            if (g_mode == Mode::Open) rate = fminf(rate, SPIN_RAMP_OPEN_RAD_S2);
            g_omega_cmd = slew(g_omega_cmd, target, rate * DT);
            g_alpha     = wrapPi(g_alpha + g_omega_cmd * DT);

            // Phase offset (Z) ramp: trapezoidal, so a big move (homing, tool load) does not start or
            // stop with a speed step - that rings the rotors on their current vectors.  The XY board's
            // own streamed Z is far gentler than this limit.
            {
                float err   = g_phi_target - g_phi;
                float vstop = sqrtf(2.0f * PHASE_OFFSET_ACCEL_RAD_S2 * fabsf(err));
                float vwant = copysignf(fminf(PHASE_OFFSET_RAMP_RAD_S, vstop), err);
                g_phi_rate  = slew(g_phi_rate, vwant, PHASE_OFFSET_ACCEL_RAD_S2 * DT);
                float stepd = g_phi_rate * DT;
                if (fabsf(err) <= 1e-5f || (stepd * err > 0 && fabsf(stepd) >= fabsf(err))) {
                    g_phi      = g_phi_target;   // arrived (or would pass it this period)
                    g_phi_rate = fabsf(err) <= 1e-5f ? 0.0f : err / DT;
                    if (fabsf(err) <= 1e-5f) g_phi_rate = 0;
                } else {
                    g_phi += stepd;
                }
            }

            // Open-loop current: hold, spin, plus the Z-move boost.
            float ol_target = (g_omega_cmd > 0.0f || g_omega_target > 0.0f) ? g_tun.spin_a : g_tun.hold_a;
            if (g_z_moving) ol_target += g_tun.boost_a;
            g_ol_current = slew(g_ol_current, ol_target, OL_CURRENT_SLEW_A_S * DT);

            // Each rotor's angle against its reference: always in closed loop; in open loop once the
            // observer is trusted (above OBS_TRUST_RPM and tracking), so an open-loop slip is seen too.
            bool fast = g_omega_cmd > OBS_TRUST_RPM * RPM_TO_RAD;
            for (int k = 0; k < 2; k++) {
                Motor& m = mot[k];
                observerUpdate(m);
                float ref  = wrapPi(m.dir * g_alpha + g_phi + m.c);
                float dref = wrapPi(ref - m.ref);
                m.ref      = ref;
                if (m.eps_valid) {
                    m.eps += dref - m.obs_da;
                    if (g_mode == Mode::Open && !fast) m.eps_valid = false;
                } else if (g_mode == Mode::Open && fast &&
                           fabsf(m.obs_w - m.dir * g_omega_cmd) < OBS_TRUST_TOL * g_omega_cmd) {
                    m.eps       = wrapPi(m.ref - m.obs_angle);   // the load angle, now followed unwrapped
                    m.eps_valid = true;
                }
                if (!m.eps_valid) m.eps = 0;
            }

            if (g_req_closed) {
                g_req_closed = false;
                if (g_mode == Mode::Open) handoverToClosed();
            }
            if (g_req_open) {
                g_req_open = false;
                if (g_mode == Mode::Closed) handbackToOpen();
            }

            g_zerr          = 0.5f * (mot[0].eps + mot[1].eps);
            g_angle_err_max = fmaxf(fabsf(mot[0].eps), fabsf(mot[1].eps));

            for (int k = 0; k < 2; k++) {
                Motor& m = mot[k];
                float wref = m.dir * g_omega_cmd + g_phi_rate;   // the reference angle's own speed
                if (g_mode == Mode::Open) {
                    // The rotor runs (nearly) synchronous with the vector, so its back-EMF is fed
                    // forward on q; the slow trim absorbs the load angle's share and the dead time.
                    m.iq_carry = slew(m.iq_carry, 0.0f, OL_CARRY_FADE_A_S * DT);
                    m.theta  = m.ref;
                    m.id_ref = g_ol_current;
                    m.iq_ref = m.iq_carry;
                    currentLoop(k, wref, true, true);
                } else {
                    // Closed loop: PID on the rotor's angle against the reference, D on the observer's
                    // speed.  The angle (not just the speed) is held, so each rotor keeps its place and
                    // the Z - the two rotors' relative angle - stays where the phase offset puts it.
                    float werr = wref - m.obs_w;
                    float kp, kd, ki;
                    posGains(&kp, &kd, &ki);
                    m.pos_int  = clampf(m.pos_int + ki * m.eps * DT, -g_tun.iq_limit, g_tun.iq_limit);
                    m.iq_ref   = clampf(kp * m.eps + kd * werr + m.pos_int, -g_tun.iq_limit, g_tun.iq_limit);
                    m.id_cl    = slew(m.id_cl, 0.0f, CL_ID_RAMP_A_S * DT);
                    m.id_ref   = m.id_cl - m.fw_id;
                    m.theta    = m.obs_angle;
                    m.speed_err_frac = fabsf(werr) / fmaxf(g_omega_cmd, 1.0f);
                    currentLoop(k, m.obs_w, true, false);
                }
                modulate(k, true);
            }
        }
    }

    // ==================================================================== interrupt

    extern "C" void ADC5_IRQHandler(void) {
        uint32_t t0 = DWT->CYCCNT;
        TAMP->BKP2R = 1;   // inside the interrupt (reset-surviving diagnostics, see link.h)
        ADC5->ISR   = ADC_ISR_JEOS | ADC_ISR_JEOC;
        uint16_t raw[2][3] = {
            { (uint16_t)ADC1->JDR1, (uint16_t)ADC2->JDR1, (uint16_t)ADC3->JDR1 },
            { (uint16_t)ADC5->JDR1, (uint16_t)ADC5->JDR2, (uint16_t)ADC4->JDR1 },
        };
        hw::slowKickFromIsr();   // after the current samples, so it ends long before the next trigger
        if (!g_brk_frozen) {
            BrkEntry& e = g_brk[g_brk_n % BRK_RING];
            e.cmp       = hw::comparatorOutputs();
            for (int k = 0; k < 2; k++)
                for (int p = 0; p < 3; p++) {
                    e.raw[k * 3 + p]  = raw[k][p];
                    e.duty[k * 3 + p] = (uint16_t)(mot[k].duty_prev[p] * 10000.0f);
                }
            g_brk_n++;
        }
        {
            uint32_t isr = USART2->ISR;   // sampled, not cleared: the UART interrupt clears them (a lower bound)
            if (isr & USART_ISR_ORE) g_uart_err[0]++;
            if (isr & USART_ISR_FE) g_uart_err[1]++;
            if (isr & USART_ISR_NE) g_uart_err[2]++;
        }
        if (g_noise_on) {
            for (int k = 0; k < 2; k++)
                for (int p = 0; p < 3; p++) {
                    uint32_t v = raw[k][p];
                    g_noise_sum[k][p] += v;
                    g_noise_sq[k][p] += (uint64_t)(v * v);
                    if (v < g_noise_min[k][p]) g_noise_min[k][p] = v;
                    if (v > g_noise_max[k][p]) g_noise_max[k][p] = v;
                }
            for (int k = 0; k < 2; k++) {
                const Motor& m = mot[k];
                float ps = m.gain[0] * ((float)raw[k][0] - m.off[0]) + m.gain[1] * ((float)raw[k][1] - m.off[1]) +
                           m.gain[2] * ((float)raw[k][2] - m.off[2]);
                int32_t v = (int32_t)(ps * 16.0f);
                g_noise_ps_sum[k] += v;
                g_noise_ps_sq[k] += (int64_t)v * v;
                if (v < g_noise_ps_min[k]) g_noise_ps_min[k] = v;
                if (v > g_noise_ps_max[k]) g_noise_ps_max[k] = v;
            }
            {
                if (++g_rip_i >= 2 * RIP_N) g_rip_i = 0;
                const int    k = g_rip_i >= RIP_N ? 1 : 0, j = g_rip_i - k * RIP_N;
                const Motor& m = mot[k];
                float        x;
                switch (j) {
                    case 0: x = m.eps * 10000.0f; break;
                    case 1: x = m.obs_w * 10.0f; break;
                    case 2: x = m.iq_ref * 1000.0f; break;
                    case 3: x = m.iq * 1000.0f; break;
                    default: x = m.id * 1000.0f; break;
                }
                int32_t v = (int32_t)x;
                Rip&    r = g_rip[k][j];
                r.sum += v;
                r.sq += (int64_t)v * v;
                if (v < r.mn) r.mn = v;
                if (v > r.mx) r.mx = v;
                r.n++;
            }
            if (++g_noise_n >= g_noise_target) g_noise_on = false;
        }
        if (g_dump_on) {
            uint16_t* row = g_dump[g_dump_n];
            for (int k = 0; k < 2; k++)
                for (int p = 0; p < 3; p++) row[k * 3 + p] = raw[k][p];
            if (++g_dump_n >= g_dump_target) g_dump_on = false;
        }
        if (g_calibrating) {
            for (int k = 0; k < 2; k++)
                for (int p = 0; p < 3; p++) g_cal_sum[k][p] += raw[k][p];
            g_cal_n++;
        } else {
            step(raw);
        }
        if (g_scope_on && ++g_scope_skip >= g_scope_decim) {
            // Half a row per interrupt (the closed loop leaves ~4 us of the period): channels 0-3, then 4-7.
            g_scope_skip   = 0;
            const Motor& m = mot[g_scope_motor];
            int16_t*     r = g_scope[g_scope_n];
            const float  kdeg = 5729.578f;
            if (!g_scope_half) {
                const float kf = 10000.0f / g_tun.flux;
                r[0] = (int16_t)clampf(m.flux_a * kf, -32000.0f, 32000.0f);
                r[1] = (int16_t)clampf(m.flux_b * kf, -32000.0f, 32000.0f);
                r[2] = (int16_t)clampf(m.eps * kdeg, -32000.0f, 32000.0f);
                r[3] = (int16_t)clampf(m.obs_w * 9.549297f, -32000.0f, 32000.0f);
                g_scope_half = true;
            } else {
                r[4] = (int16_t)clampf(m.iq_ref * 1000.0f, -32000.0f, 32000.0f);
                r[5] = (int16_t)clampf(m.iq * 1000.0f, -32000.0f, 32000.0f);
                r[6] = (int16_t)clampf(m.id * 1000.0f, -32000.0f, 32000.0f);
                r[7] = (int16_t)clampf(m.ref * kdeg, -32000.0f, 32000.0f);
                g_scope_half = false;
                if (++g_scope_n >= SCOPE_MAX) g_scope_on = false;
            }
        }
        if (g_iqacc_on) {
            for (int k = 0; k < 2; k++) g_iqacc_sum[k] += mot[k].dir * mot[k].iq;
            g_iqacc_n++;
        }
        if (g_fake_load_cycles) {
            while (DWT->CYCCNT - t0 < g_fake_load_cycles) {}
        }
        uint32_t dt = DWT->CYCCNT - t0;
        if (dt > g_isr_max_cycles) g_isr_max_cycles = dt;
        if (dt > TAMP->BKP1R) TAMP->BKP1R = dt;   // longest since boot, kept across a reset
        // The next sample already waiting means this interrupt overran its period.  In a row, the main
        // loop gets no time at all (no link, no watchdog kick): turn the motors off so it shortens again.
        if (ADC5->ISR & ADC_ISR_JEOS) {
            g_overruns++;
            if (++g_overrun_run >= OVERRUN_TRIP && g_enabled)
                raiseFault(FaultOverrun, -1, -1, (float)dt / (SystemCoreClock / 1000000));
        } else {
            g_overrun_run = 0;
        }
        TAMP->BKP2R = 0;
        g_isr_count++;
    }

    // ==================================================================== main-loop API

    void init() {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        mot[0].dir = +1 * SPINDLE_DIRECTION;
        mot[1].dir = -1 * SPINDLE_DIRECTION;
        for (int k = 0; k < 2; k++) {
            for (int p = 0; p < 3; p++) mot[k].off[p] = 2010.0f;   // ~1.62 V; replaced by calibrateOffsets
            for (int p = 0; p < 3; p++) mot[k].gain[p] = 1.0f;     // nominal until #GCAL
            mot[k].c = 0;
            resetControl(mot[k]);
        }
    }

    bool calibrateOffsets(uint32_t samples, const char** why) {
        if (g_enabled) { *why = "motors enabled"; return false; }
        __disable_irq();
        for (int k = 0; k < 2; k++)
            for (int p = 0; p < 3; p++) g_cal_sum[k][p] = 0;
        g_cal_n       = 0;
        g_calibrating = true;
        __enable_irq();
        uint32_t t0 = millis();
        while (g_cal_n < samples) {
            if (millis() - t0 > samples / 10 + 200) {
                g_calibrating = false;
                *why = "no current samples (ADC interrupt not running)";
                return false;
            }
            hw::watchdogKick();
        }
        g_calibrating = false;
        for (int k = 0; k < 2; k++) {
            for (int p = 0; p < 3; p++) {
                float off = g_cal_sum[k][p] / (float)g_cal_n;
                // Expect ~1.62 V = 2010 counts; more than ~0.25 V away means a sense fault.
                if (fabsf(off - 2010.0f) > 310.0f) {
                    *why = "current-sense zero out of range";
                    mot[k].off[p] = off;
                    return false;
                }
                mot[k].off[p] = off;
            }
        }
        return true;
    }

    const char* g_enable_fail = "";

    const char* enableFailReason() {
        return g_enable_fail;
    }

    bool brkDump(int i, float amps[6], float duty[6], uint8_t* cmp) {
        // i = 0 is the newest entry (the interrupt that saw the break), BRK_RING - 1 the oldest.
        if (!g_brk_frozen || i < 0 || i >= BRK_RING || (uint32_t)i >= g_brk_n) return false;
        const BrkEntry& e = g_brk[(g_brk_n - 1 - i) % BRK_RING];
        for (int k = 0; k < 2; k++)
            for (int p = 0; p < 3; p++) {
                amps[k * 3 + p] = SENSE_SIGN * ((float)e.raw[k * 3 + p] - mot[k].off[p]) * SENSE_A_PER_LSB * mot[k].gain[p];
                duty[k * 3 + p] = e.duty[k * 3 + p] / 10000.0f;
            }
        *cmp = e.cmp;
        return true;
    }

    bool enable() {
        if (g_enabled) return true;
        g_enable_fail = "";
        if (g_fault != FaultNone) { g_enable_fail = "a fault is latched"; return false; }
        if (hw::breakLatched(0) || hw::breakLatched(1)) { g_enable_fail = "a timer break is latched"; return false; }
        g_brk_n       = 0;       // a new run: the last fault's post-mortem is dropped
        g_brk_frozen  = false;
        __disable_irq();
        for (int k = 0; k < 2; k++) resetControl(mot[k]);
        g_mode       = Mode::Open;
        g_ol_current = 0;   // slews up from zero
        g_precharge  = PRECHARGE_PERIODS;
        g_omega_cmd  = 0;
        hw::writeDuty(0, 0, 0, 0);
        hw::writeDuty(1, 0, 0, 0);
        bool ok1 = hw::enableOutputs(0);
        bool ok  = ok1 && hw::enableOutputs(1);
        if (ok) g_enabled = true;
        else {
            g_enable_fail = ok1 ? "TIM8 MOE would not set" : "TIM1 MOE would not set";
            outputsOff();
        }
        __enable_irq();
        return ok;
    }

    void disable() {
        __disable_irq();
        outputsOff();
        g_omega_cmd = 0;
        __enable_irq();
    }

    bool enabled() { return g_enabled; }
    Mode mode() { return g_mode; }

    void setSpeedTarget(float rpm) {
        if (rpm < 0) rpm = 0;
        if (rpm > MAX_COMMAND_RPM) rpm = MAX_COMMAND_RPM;
        g_omega_target = rpm * RPM_TO_RAD;
    }
    float speedCommandRpm() { return g_omega_cmd / RPM_TO_RAD; }
    float speedTargetRpm() { return g_omega_target / RPM_TO_RAD; }

    void setPhaseTarget(float rad) { g_phi_target = rad; }
    float phaseCurrent() { return g_phi; }
    float phaseTarget() { return g_phi_target; }

    void zeroPhase() {
        __disable_irq();
        float phi = g_phi;
        // ref = dir*alpha + phi + c: fold phi into c so nothing moves.
        for (int k = 0; k < 2; k++) mot[k].c = wrapPi(mot[k].c + phi);
        g_phi = g_phi_target = 0;
        g_phi_rate = 0;
        __enable_irq();
    }

    void setZMoving(bool moving) { g_z_moving = moving; }
    void requestClosedLoop() { g_req_closed = true; }
    void requestOpenLoop() { g_req_open = true; }

    bool observersTracking(float tol) {
        float w = g_omega_cmd;
        if (w <= 0) return false;
        for (int k = 0; k < 2; k++) {
            const Motor& m = mot[k];
            if (fabsf(m.obs_w_lock - m.dir * w) > tol * w) return false;
            float fl = sqrtf(m.flux_a * m.flux_a + m.flux_b * m.flux_b) / g_tun.flux;
            if (fl < 0.7f) return false;
        }
        return true;
    }

    float zErrorRad() { return g_zerr; }
    float maxAngleErrorRad() { return g_angle_err_max; }

    float maxSpeedErrorFraction() {
        return fmaxf(mot[0].speed_err_frac, mot[1].speed_err_frac);
    }

    void setFakeLoad(uint32_t us) {
        g_fake_load_cycles = us ? us * (SystemCoreClock / 1000000) : 0;
    }

    void iqAccStart() {
        __disable_irq();
        g_iqacc_sum[0] = g_iqacc_sum[1] = 0;
        g_iqacc_n                       = 0;
        g_iqacc_on                      = true;
        __enable_irq();
    }

    uint32_t iqAccStop(float mean[2]) {
        __disable_irq();
        g_iqacc_on = false;
        uint32_t n = g_iqacc_n;
        for (int k = 0; k < 2; k++) mean[k] = n ? g_iqacc_sum[k] / (float)n : 0.0f;
        __enable_irq();
        return n;
    }

    uint32_t overruns() {
        return g_overruns;
    }

    uint32_t uartErrorsSeen(int which) {
        return (which >= 0 && which < 3) ? g_uart_err[which] : 0;
    }

    uint8_t faultCode() {
        return g_fault;
    }

    uint8_t takeFault(char* detail, size_t n) {
        __disable_irq();
        uint8_t f     = g_fault;
        int     motor = g_fault_motor, phase = g_fault_phase;
        float   amps  = g_fault_amps;
        g_fault       = FaultNone;
        __enable_irq();
        if (f == FaultOverCurrent) {
            snprintf(detail, n, "over-current on M%d phase %c (%.1f A, %d periods over %.1f A)", motor + 1,
                     'A' + phase, amps, SW_TRIP_COUNT, g_tun.sw_trip_a);
        } else if (f == FaultBreak) {
            snprintf(detail, n, "hardware over-current (comparator break) on M%d", motor + 1);
        } else if (f == FaultOverrun) {
            snprintf(detail, n, "control interrupt overran its %lu us period %lu times in a row (last %.0f us) - motors off",
                     (unsigned long)(1000000 / PWM_FREQUENCY_HZ), (unsigned long)OVERRUN_TRIP, amps);
        } else if (n) {
            detail[0] = 0;
        }
        return f;
    }

    void startNoise(uint32_t samples) {
        __disable_irq();
        for (int k = 0; k < 2; k++)
            for (int p = 0; p < 3; p++) {
                g_noise_sum[k][p] = 0;
                g_noise_sq[k][p]  = 0;
                g_noise_min[k][p] = 0xFFFF;
                g_noise_max[k][p] = 0;
            }
        for (int k = 0; k < 2; k++) {
            g_noise_ps_sum[k] = 0;
            g_noise_ps_sq[k]  = 0;
            g_noise_ps_min[k] = INT32_MAX;
            g_noise_ps_max[k] = INT32_MIN;
            for (int j = 0; j < RIP_N; j++) g_rip[k][j] = { 0, 0, INT32_MAX, INT32_MIN, 0 };
        }
        g_noise_n      = 0;
        g_noise_target = samples ? samples : 1;
        g_noise_on     = true;
        __enable_irq();
    }

    int startDump(int samples) {
        if (samples < 1) samples = 1;
        if (samples > DUMP_MAX) samples = DUMP_MAX;
        __disable_irq();
        g_dump_n      = 0;
        g_dump_target = samples;
        g_dump_on     = true;
        __enable_irq();
        return samples;
    }

    bool dumpDone() {
        return !g_dump_on && g_dump_n > 0;
    }

    void dumpRow(int i, int out[6]) {
        for (int c = 0; c < 6; c++) out[c] = (int)g_dump[i][c] - (int)lroundf(mot[c / 3].off[c % 3]);
    }

    bool noiseDone() {
        return !g_noise_on && g_noise_n > 0;
    }

    void noiseResult(int k, int p, NoiseStats* out) {
        float n     = (float)g_noise_n;
        float mean  = g_noise_sum[k][p] / n;
        float var   = (float)((double)g_noise_sq[k][p] / n - (double)mean * mean);
        out->mean   = mean;
        out->offset = mot[k].off[p];
        out->sd     = var > 0 ? sqrtf(var) : 0.0f;
        out->min    = g_noise_min[k][p];
        out->max    = g_noise_max[k][p];
        out->n      = g_noise_n;
    }

    void noisePhaseSum(int k, float* mean_a, float* sd_a, float* min_a, float* max_a) {
        const double scale = SENSE_A_PER_LSB / 16.0;
        double       n     = (double)g_noise_n;
        double       mean  = (double)g_noise_ps_sum[k] / n;
        double       var   = (double)g_noise_ps_sq[k] / n - mean * mean;
        *mean_a = (float)(mean * scale);
        *sd_a   = var > 0 ? (float)(sqrt(var) * scale) : 0.0f;
        *min_a  = (float)(g_noise_ps_min[k] * scale);
        *max_a  = (float)(g_noise_ps_max[k] * scale);
    }

    void noiseRipple(int k, int sig, float* mean, float* sd, float* mn, float* mx) {
        // eps in degrees, the observer speed in RPM, the three currents in A
        static const double scale[RIP_N] = { 1e-4 * 57.29578, 0.1 * 9.549297, 1e-3, 1e-3, 1e-3 };
        const Rip& r = g_rip[k][sig];
        double     n = r.n ? (double)r.n : 1.0, mu = (double)r.sum / n, var = (double)r.sq / n - mu * mu;
        *mean = (float)(mu * scale[sig]);
        *sd   = var > 0 ? (float)(sqrt(var) * scale[sig]) : 0.0f;
        *mn   = (float)(r.mn * scale[sig]);
        *mx   = (float)(r.mx * scale[sig]);
    }

    void startScope(int motor, int decim) {
        __disable_irq();
        g_scope_motor = motor ? 1 : 0;
        g_scope_decim = decim < 1 ? 1 : decim;
        g_scope_skip  = 0;
        g_scope_half  = false;
        g_scope_n     = 0;
        g_scope_on    = true;
        __enable_irq();
    }

    bool scopeDone() {
        return !g_scope_on && g_scope_n > 0;
    }

    int16_t scopeValue(int row, int ch) {
        return g_scope[row][ch];
    }

    MotorTelemetry take(int k) {
        MotorTelemetry t;
        __disable_irq();
        Motor& m   = mot[k];
        t.ia       = m.ia;
        t.ib       = m.ib;
        t.ic       = m.ic;
        t.id       = m.id;
        t.iq       = m.iq;
        t.vd       = m.vd;
        t.vq       = m.vq;
        t.obs_rpm  = m.obs_w / RPM_TO_RAD;
        t.obs_flux = sqrtf(m.flux_a * m.flux_a + m.flux_b * m.flux_b) / g_tun.flux;
        t.imax     = m.imax;
        t.i2_sum   = m.i2sum;
        t.n        = m.n;
        m.imax     = 0;
        m.i2sum    = 0;
        m.n        = 0;
        __enable_irq();
        return t;
    }

    VoltStats takeVolt(int k) {
        VoltStats v;
        __disable_irq();
        Motor& m  = mot[k];
        v.vmax    = voltageLimit();
        v.u       = m.u;
        v.u_max   = m.u_max;
        v.sat     = m.sat_n;
        v.n       = m.u_n;
        v.fw_id   = m.fw_id;
        v.late    = m.late_n;
        v.id      = m.id;
        v.iq      = m.iq;
        v.vd      = m.vd;
        v.vq      = m.vq;
        v.obs_rpm = m.obs_w / RPM_TO_RAD;
        m.u_max   = 0;
        m.sat_n = m.u_n = m.late_n = 0;
        __enable_irq();
        return v;
    }

    void takeIsrStats(uint32_t* max_us, uint32_t* count) {
        __disable_irq();
        *max_us = g_isr_max_cycles / (SystemCoreClock / 1000000);
        *count  = g_isr_count;
        g_isr_max_cycles = 0;
        g_isr_count      = 0;
        __enable_irq();
    }

    Tuning& tuning() { return g_tun; }
    void setVbus(float v) { g_vbus = v; }

    bool startAlignTest(float volts) {
        if (volts <= 0 || volts > 3.0f) { g_enable_fail = "align voltage outside 0-3 V"; return false; }
        if (!enable()) return false;
        __disable_irq();
        g_align_va    = volts;
        g_align_vb    = 0;
        g_align_volts = volts;
        __enable_irq();
        return true;
    }

    void setAlignVector(float volts, float angle_rad) {
        float s, c;
        fastSinCos(angle_rad, &s, &c);
        __disable_irq();
        g_align_va = volts * c;
        g_align_vb = volts * s;
        __enable_irq();
    }

    void setPhaseGains(int k, const float g[3]) {
        __disable_irq();
        for (int p = 0; p < 3; p++) mot[k].gain[p] = g[p];
        __enable_irq();
    }

    void phaseGains(int k, float g[3]) {
        for (int p = 0; p < 3; p++) g[p] = mot[k].gain[p];
    }
}
