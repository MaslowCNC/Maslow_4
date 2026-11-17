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
        constexpr float DUTY_MAX  = 0.97f;   // leaves the low side on >= 1.5 us per period (bootstrap)

        inline float wrapPi(float a) {
            while (a > PI_F) a -= TWO_PI_F;
            while (a < -PI_F) a += TWO_PI_F;
            return a;
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
            float ia, ib, ic;             // A
            float ial, ibe;               // A, stationary frame
            float duty[3];                // duties in force during this sample
            // MXLEMMING flux observer (SimpleFOCDrivers' MXLEMMINGObserverSensor, as on the ESC1)
            float flux_a, flux_b;
            float ial_prev, ibe_prev;
            float obs_angle, obs_w;       // rad, rad/s (electrical = mechanical, 1 pole pair)
            // control
            float theta;
            float id, iq, id_ref, iq_ref;
            float int_d, int_q;
            float vd, vq;
            float valpha, vbeta;          // last commanded voltage (the observer's input next period)
            float vel_int, iq_cmd, id_cl;
            float c;                      // open-loop angle constant
            float speed_err_frac;
            int   over_count;
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
        float          g_sum          = 0;   // running sum of both observers' angle increments
        float          g_zref         = 0;
        volatile float g_zerr         = 0;
        volatile float g_vbus         = 36.0f;
        uint32_t       g_precharge    = 0;
        volatile bool  g_req_closed   = false;
        volatile bool  g_req_open     = false;
        volatile float g_align_volts  = 0;   // > 0: voltage-mode align test at angle 0

        volatile uint8_t g_fault       = FaultNone;
        volatile int8_t  g_fault_motor = -1;
        volatile int8_t  g_fault_phase = -1;
        volatile float   g_fault_amps  = 0;

        volatile bool  g_calibrating = false;
        volatile uint32_t g_cal_n    = 0;
        float          g_cal_sum[2][3];

        volatile uint32_t g_isr_max_cycles = 0;
        volatile uint32_t g_isr_count      = 0;

        Tuning g_tun = {
            CUR_KP, CUR_KI, VEL_KP, VEL_KI, Z_KP,
            HOLD_CURRENT_A, SPINUP_CURRENT_A, Z_MOVE_BOOST_A, IQ_LIMIT_A,
            PHASE_R_OHM, PHASE_L_H, FLUX_WB, SW_TRIP_A, true,
        };

        void resetControl(Motor& m) {
            m.flux_a = m.flux_b = 0;
            m.ial_prev = m.ibe_prev = 0;
            m.obs_w = 0;
            m.int_d = m.int_q = 0;
            m.vd = m.vq = m.valpha = m.vbeta = 0;
            m.vel_int = m.iq_cmd = m.id_cl = 0;
            m.over_count = 0;
            m.speed_err_frac = 0;
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
            for (int p = 0; p < 3; p++) i[p] = SENSE_SIGN * ((float)raw[p] - m.off[p]) * SENSE_A_PER_LSB;
            if (rebuild) {
                // The highest-duty phase has the shortest low-side time, so its sample sits closest to
                // the switching edge; rebuild it from the other two (they sum to zero).  ESC1 bench.
                int hi = 0;
                if (m.duty[1] > m.duty[hi]) hi = 1;
                if (m.duty[2] > m.duty[hi]) hi = 2;
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
            m.flux_a = clampf(m.flux_a + (m.valpha - R * m.ial) * DT - L * (m.ial - m.ial_prev), -F, F);
            m.flux_b = clampf(m.flux_b + (m.vbeta - R * m.ibe) * DT - L * (m.ibe - m.ibe_prev), -F, F);
            m.ial_prev = m.ial;
            m.ibe_prev = m.ibe;
            float a  = atan2f(m.flux_b, m.flux_a);
            float da = wrapPi(a - m.obs_angle);
            m.obs_angle = a;
            m.obs_w += (da / DT - m.obs_w) * fminf(DT / OBS_SPEED_TAU_S, 1.0f);
            g_sum += da;
        }

        // dq current loops + SVPWM for one motor at angle m.theta, electrical speed we.
        void currentLoop(int k, float we, bool emf_ff) {
            Motor&      m    = mot[k];
            const float vmax = MAX_MODULATION * g_vbus * INV_SQRT3;
            float       s = sinf(m.theta), c = cosf(m.theta);
            m.id = m.ial * c + m.ibe * s;
            m.iq = -m.ial * s + m.ibe * c;

            const float L = g_tun.l;
            float vd_ff = -we * L * m.iq;
            float vq_ff = we * L * m.id + (emf_ff ? we * g_tun.flux : 0.0f);
            float ed = m.id_ref - m.id, eq = m.iq_ref - m.iq;
            float int_d = clampf(m.int_d + g_tun.cur_ki * ed * DT, -vmax, vmax);
            float int_q = clampf(m.int_q + g_tun.cur_ki * eq * DT, -vmax, vmax);
            float vd = g_tun.cur_kp * ed + int_d + vd_ff;
            float vq = g_tun.cur_kp * eq + int_q + vq_ff;
            float mag = sqrtf(vd * vd + vq * vq);
            if (mag > vmax) {
                float k2 = vmax / mag;
                vd *= k2;
                vq *= k2;
                // Saturated: hold the integrators where they were (no wind-up).
            } else {
                m.int_d = int_d;
                m.int_q = int_q;
            }
            m.vd = vd;
            m.vq = vq;
            m.valpha = vd * c - vq * s;
            m.vbeta  = vd * s + vq * c;
        }

        void modulate(int k) {
            Motor& m  = mot[k];
            float  va = m.valpha;
            float  vb = -0.5f * m.valpha + 0.5f * SQRT3 * m.vbeta;
            float  vc = -0.5f * m.valpha - 0.5f * SQRT3 * m.vbeta;
            float  vcm = 0.5f * (fmaxf(va, fmaxf(vb, vc)) + fminf(va, fminf(vb, vc)));
            float  inv = 1.0f / fmaxf(g_vbus, 5.0f);
            m.duty[0] = clampf(0.5f + (va - vcm) * inv, 0.0f, DUTY_MAX);
            m.duty[1] = clampf(0.5f + (vb - vcm) * inv, 0.0f, DUTY_MAX);
            m.duty[2] = clampf(0.5f + (vc - vcm) * inv, 0.0f, DUTY_MAX);
            hw::writeDuty(k, (uint16_t)(m.duty[0] * PWM_ARR), (uint16_t)(m.duty[1] * PWM_ARR),
                          (uint16_t)(m.duty[2] * PWM_ARR));
        }

        void handoverToClosed() {
            for (int k = 0; k < 2; k++) {
                Motor& m  = mot[k];
                float  s  = sinf(m.obs_angle), c = cosf(m.obs_angle);
                float  id = m.ial * c + m.ibe * s;
                float  iq = -m.ial * s + m.ibe * c;
                float  vd = m.valpha * c + m.vbeta * s;
                float  vq = -m.valpha * s + m.vbeta * c;
                float  we = m.obs_w;
                // Start the loops from the voltage and current already there: no step at the hand-over.
                m.int_d   = vd - (-we * g_tun.l * iq);
                m.int_q   = vq - (we * g_tun.l * id + we * g_tun.flux);
                m.vel_int = iq;
                m.iq_cmd  = iq;
                m.id_cl   = id;   // fades to 0
            }
            g_zref = g_sum - 2.0f * g_phi;
            g_mode = Mode::Closed;
        }

        void handbackToOpen() {
            g_alpha    = 0;
            float imag = 0;
            for (int k = 0; k < 2; k++) {
                Motor& m   = mot[k];
                float  mag = sqrtf(m.ial * m.ial + m.ibe * m.ibe);
                // Put the open-loop current vector where the current already is.
                float ang = mag > 0.3f ? atan2f(m.ibe, m.ial) : m.obs_angle;
                m.c       = wrapPi(ang - g_phi);
                float s = sinf(ang), c = cosf(ang);
                m.int_d = m.valpha * c + m.vbeta * s;
                m.int_q = -m.valpha * s + m.vbeta * c;
                if (mag > imag) imag = mag;
            }
            g_ol_current = fmaxf(imag, 0.5f);
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
                    mot[k].valpha = mot[k].vbeta = 0;
                }
                return;
            }

            if (g_precharge) {
                // Low sides on (duty 0) for a few periods to charge the DRV8300 bootstrap capacitors.
                hw::writeDuty(0, 0, 0, 0);
                hw::writeDuty(1, 0, 0, 0);
                for (int k = 0; k < 2; k++) mot[k].duty[0] = mot[k].duty[1] = mot[k].duty[2] = 0;
                g_precharge--;
                return;
            }

            // Voltage-mode align test (bring-up): a fixed voltage on the phase A axis, no current loop.
            if (g_align_volts > 0) {
                for (int k = 0; k < 2; k++) {
                    mot[k].valpha = g_align_volts;
                    mot[k].vbeta  = 0;
                    modulate(k);
                }
                return;
            }

            // Speed ramp.  In open loop the ramp waits at the hand-over speed until the observers lock.
            float target = g_omega_target;
            if (g_mode == Mode::Open && g_tun.closed_loop_allowed) target = fminf(target, CL_ENTER_RPM * RPM_TO_RAD);
            float rate = g_omega_cmd >= SPIN_RAMP_SLOW_ABOVE_RPM * RPM_TO_RAD ? SPIN_RAMP_HIGH_RAD_S2 : SPIN_RAMP_RAD_S2;
            g_omega_cmd = slew(g_omega_cmd, target, rate * DT);
            g_alpha     = wrapPi(g_alpha + g_omega_cmd * DT);

            // Phase offset (Z) ramp.
            float phi    = g_phi;
            float newphi = slew(phi, g_phi_target, PHASE_OFFSET_RAMP_RAD_S * DT);
            g_phi_rate   = (newphi - phi) / DT;
            g_phi        = newphi;

            // Open-loop current: hold, spin, plus the Z-move boost.
            float ol_target = (g_omega_cmd > 0.0f || g_omega_target > 0.0f) ? g_tun.spin_a : g_tun.hold_a;
            if (g_z_moving) ol_target += g_tun.boost_a;
            g_ol_current = slew(g_ol_current, ol_target, OL_CURRENT_SLEW_A_S * DT);

            if (g_req_closed) {
                g_req_closed = false;
                if (g_mode == Mode::Open) handoverToClosed();
            }
            if (g_req_open) {
                g_req_open = false;
                if (g_mode == Mode::Closed) handbackToOpen();
            }

            for (int k = 0; k < 2; k++) observerUpdate(mot[k]);

            if (g_mode == Mode::Closed) g_zerr = g_sum - (g_zref + 2.0f * g_phi);
            else g_zerr = 0;

            for (int k = 0; k < 2; k++) {
                Motor& m = mot[k];
                if (g_mode == Mode::Open) {
                    m.theta  = wrapPi(m.dir * g_alpha + g_phi + m.c);
                    m.id_ref = g_ol_current;
                    m.iq_ref = 0;
                    currentLoop(k, m.dir * g_omega_cmd + g_phi_rate, false);
                } else {
                    // Closed loop: speed reference = the spin, the Z ramp's own rate, and the Z loop's
                    // correction shared between the motors (each turns the sum by its own angle).
                    float wref = m.dir * g_omega_cmd + g_phi_rate - 0.5f * g_tun.z_kp * g_zerr;
                    float verr = wref - m.obs_w;
                    m.vel_int  = clampf(m.vel_int + g_tun.vel_ki * verr * DT, -g_tun.iq_limit, g_tun.iq_limit);
                    float iq   = clampf(g_tun.vel_kp * verr + m.vel_int, -g_tun.iq_limit, g_tun.iq_limit);
                    m.iq_cmd   = slew(m.iq_cmd, iq, VEL_IQ_RAMP_A_S * DT);
                    m.id_cl    = slew(m.id_cl, 0.0f, CL_ID_RAMP_A_S * DT);
                    m.iq_ref   = m.iq_cmd;
                    m.id_ref   = m.id_cl;
                    m.theta    = m.obs_angle;
                    m.speed_err_frac = fabsf(verr) / fmaxf(g_omega_cmd, 1.0f);
                    currentLoop(k, m.obs_w, true);
                }
                modulate(k);
            }
        }
    }

    // ==================================================================== interrupt

    extern "C" void ADC5_IRQHandler(void) {
        uint32_t t0 = DWT->CYCCNT;
        ADC5->ISR   = ADC_ISR_JEOS | ADC_ISR_JEOC;
        uint16_t raw[2][3] = {
            { (uint16_t)ADC1->JDR1, (uint16_t)ADC2->JDR1, (uint16_t)ADC3->JDR1 },
            { (uint16_t)ADC5->JDR1, (uint16_t)ADC5->JDR2, (uint16_t)ADC4->JDR1 },
        };
        if (g_calibrating) {
            for (int k = 0; k < 2; k++)
                for (int p = 0; p < 3; p++) g_cal_sum[k][p] += raw[k][p];
            g_cal_n++;
        } else {
            step(raw);
        }
        uint32_t dt = DWT->CYCCNT - t0;
        if (dt > g_isr_max_cycles) g_isr_max_cycles = dt;
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

    bool enable() {
        if (g_enabled) return true;
        if (g_fault != FaultNone) return false;
        if (hw::breakLatched(0) || hw::breakLatched(1)) return false;
        __disable_irq();
        for (int k = 0; k < 2; k++) resetControl(mot[k]);
        g_mode       = Mode::Open;
        g_ol_current = 0;   // slews up from zero
        g_precharge  = PRECHARGE_PERIODS;
        g_omega_cmd  = 0;
        hw::writeDuty(0, 0, 0, 0);
        hw::writeDuty(1, 0, 0, 0);
        bool ok = hw::enableOutputs(0) && hw::enableOutputs(1);
        if (ok) g_enabled = true;
        else outputsOff();
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
        // theta = dir*alpha + phi + c: fold phi into c so nothing moves.
        for (int k = 0; k < 2; k++) mot[k].c = wrapPi(mot[k].c + phi);
        g_zref += 2.0f * phi;
        g_phi = g_phi_target = 0;
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
            if (fabsf(m.obs_w - m.dir * w) > tol * w) return false;
            float fl = sqrtf(m.flux_a * m.flux_a + m.flux_b * m.flux_b) / g_tun.flux;
            if (fl < 0.7f) return false;
        }
        return true;
    }

    float zErrorRad() { return g_zerr; }

    float maxSpeedErrorFraction() {
        return fmaxf(mot[0].speed_err_frac, mot[1].speed_err_frac);
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
        } else if (n) {
            detail[0] = 0;
        }
        return f;
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
        if (volts <= 0 || volts > 3.0f) return false;
        if (!enable()) return false;
        g_align_volts = volts;
        return true;
    }
}
