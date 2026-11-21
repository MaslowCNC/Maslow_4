// Host simulation of the G474 spindle FOC (src/foc.cpp, compiled unchanged) driving two PMSM models.
//
//   c++ -std=c++17 -O2 -I. -I../../src sim_main.cpp ../../src/foc.cpp -o sim
//   ./sim [J_kgm2] [flux_factor] [R_factor]     defaults 1e-5 1 1 (the firmware's own constants)
//
// flux_factor / R_factor scale the MODEL's motor against what the firmware assumes, e.g.
// "./sim 1e-5 0.93" is a motor whose flux is the ESP32 VSWEEP's 7.28 mWb while the firmware uses 7.84.
//
// The hardware is replaced by an average-value model: the ISR runs at each PWM peak with the
// phase currents it would sample, the duties it writes take effect half a period later (the
// valley), and each motor is R-L-back-EMF per phase on a rigid rotor with viscous friction.  The
// supervisor's hand-over rules from main.cpp are repeated here in a few lines.
//
// What it checks (exit status 1 if any check fails): the sense/modulation/observer sign
// conventions agree, the open-loop current vector holds and moves the Z, the observers lock and
// the closed-loop hand-over happens, speed tracks, the Z (sum of the rotor angles) follows the
// phase offset in both modes and across both hand-overs, and a load step is ridden through.

#include "Arduino.h"
#include "../../src/board.h"
#include "../../src/config.h"
#include "../../src/foc.h"
#include "../../src/hw.h"
#include <cstdlib>
#include <random>
#include <vector>

TIM_TypeDef    sim_tim1, sim_tim8;
ADC_TypeDef    sim_adc[5];
DWT_Type       sim_dwt;
TAMP_TypeDef   sim_tamp;
USART_TypeDef  sim_usart2;
CoreDebug_Type sim_cd;
uint32_t       SystemCoreClock = 170000000;
static double  g_t             = 0;   // s
uint32_t       millis() { return (uint32_t)(g_t * 1000.0); }
extern "C" void ADC5_IRQHandler(void);

// ---- hw stubs: the duty registers are the model's input ----
static uint16_t ccr_pending[2][3], ccr_active[2][3];
static bool     moe[2];
namespace hw {
    const char* g_update_phase = "sim";
    bool        enableOutputs(int m) { moe[m] = true; return true; }
    void        disableOutputs(int m) { moe[m] = false; }
    bool        outputsEnabled(int m) { return moe[m]; }
    void        writeDuty(int m, uint16_t a, uint16_t b, uint16_t c) {
        ccr_pending[m][0] = a;
        ccr_pending[m][1] = b;
        ccr_pending[m][2] = c;
    }
    bool     breakLatched(int) { return false; }
    bool     clearBreak(int) { return true; }
    uint8_t  comparatorOutputs() { return 0; }
    void     setHardwareTripAmps(float) {}
    uint16_t rawCurrent(int, int) { return 0; }
    void     serviceSlowAnalog() {}
    void     slowKickFromIsr() {}
    float    vbus() { return 36.0f; }
    float    temperature(int) { return 25.0f; }
    void     setFanDuty(uint8_t) {}
    void     watchdogStart() {}
    void     watchdogKick() {}
}

// ---- motor model ----
struct Pmsm {
    double R = PHASE_R_OHM, L = PHASE_L_H, psi = FLUX_WB;
    double vdt = 0.3e-6 / PWM_PERIOD_S * 36.0;   // dead time (DRV8300 200 ns + timer 100 ns) as a voltage error
    double J = 1.0e-5;    // kg m^2 (guess)
    double B = 6.4e-6;    // N m s: ~8 mNm at 12k RPM, the ESC1's 0.68 A no-load
    double ia = 0, ib = 0;
    double th = 0, w = 0; // electrical = mechanical (1 pole pair); th unwrapped
    double load = 0;      // N m, opposing motion
    void step(const double u[3], double dt) {
        double ic = -ia - ib;
        double e[3], k[3] = { 0, 2 * M_PI / 3, -2 * M_PI / 3 }, i[3] = { ia, ib, ic };
        for (int x = 0; x < 3; x++) e[x] = -psi * w * sin(th - k[x]);
        ia += (u[0] - R * ia - e[0]) / L * dt;
        ib += (u[1] - R * ib - e[1]) / L * dt;
        double T = 0;
        for (int x = 0; x < 3; x++) T += -psi * i[x] * sin(th - k[x]);
        double fr = B * w + (w > 0 ? load : w < 0 ? -load : 0);
        w += (T - fr) / J * dt;
        th += w * dt;
    }
    double ic() const { return -ia - ib; }
};

static Pmsm      mot[2];
static const double VBUS = 36.0;
static std::mt19937 rng(1);

static void simulateHalf(double half) {
    const int    N  = 25;
    const double dt = half / N;
    for (int k = 0; k < 2; k++) {
        double u[3];
        if (moe[k]) {
            double v[3], vn = 0;
            for (int x = 0; x < 3; x++) {
                v[x] = (double)ccr_active[k][x] / PWM_ARR * VBUS;
                vn += v[x] / 3;
            }
            // Dead time: during it the phase follows its current's diode, which costs vdt against the current.
            const double i[3] = { mot[k].ia, mot[k].ib, mot[k].ic() };
            for (int x = 0; x < 3; x++) v[x] -= (i[x] > 0.05 ? 1 : i[x] < -0.05 ? -1 : i[x] / 0.05) * mot[k].vdt;
            vn = (v[0] + v[1] + v[2]) / 3;
            for (int x = 0; x < 3; x++) u[x] = v[x] - vn;
            for (int n = 0; n < N; n++) mot[k].step(u, dt);
        } else {
            // Bridge off: no current path (back-EMF stays below the bus here); the rotor coasts.
            mot[k].ia = mot[k].ib = 0;
            u[0] = u[1] = u[2] = 0;
            for (int n = 0; n < N; n++) {
                mot[k].w -= (mot[k].B * mot[k].w) / mot[k].J * dt;
                mot[k].th += mot[k].w * dt;
            }
        }
    }
}

static uint16_t adcFor(double amps) {
    std::normal_distribution<double> noise(0.0, 1.5);   // LSB, as the ESP32 bench's zero-current noise
    double raw = 2010.0 + amps / (SENSE_SIGN * SENSE_A_PER_LSB) + noise(rng);
    if (raw < 0) raw = 0;
    if (raw > 4095) raw = 4095;
    return (uint16_t)lround(raw);
}

// One PWM period: sample at the peak, run the ISR, new duties from the valley.
static void period() {
    sim_adc[0].JDR1 = adcFor(mot[0].ia);
    sim_adc[1].JDR1 = adcFor(mot[0].ib);
    sim_adc[2].JDR1 = adcFor(mot[0].ic());
    sim_adc[4].JDR1 = adcFor(mot[1].ia);
    sim_adc[4].JDR2 = adcFor(mot[1].ib);
    sim_adc[3].JDR1 = adcFor(mot[1].ic());
    ADC5_IRQHandler();
    simulateHalf(PWM_PERIOD_S / 2);
    memcpy(ccr_active, ccr_pending, sizeof(ccr_active));
    simulateHalf(PWM_PERIOD_S / 2);
    g_t += PWM_PERIOD_S;
}

// ---- the supervisor's hand-over rules (main.cpp superviseSpin), condensed ----
static double lock_since = -1;
static void supervise() {
    if (foc::enabled() && foc::mode() == foc::Mode::Open && foc::speedTargetRpm() >= CL_ENTER_RPM &&
        foc::speedCommandRpm() >= CL_ENTER_RPM - 1) {
        if (foc::observersTracking(CL_LOCK_TOL)) {
            if (lock_since < 0) lock_since = g_t;
            else if (g_t - lock_since >= CL_LOCK_MS / 1000.0) {
                foc::requestClosedLoop();
                printf("%7.3f s  closed loop requested at %.0f RPM\n", g_t, foc::speedCommandRpm());
                lock_since = -1;
            }
        } else {
            lock_since = -1;
        }
    }
    if (foc::enabled() && foc::mode() == foc::Mode::Closed && foc::speedTargetRpm() < CL_EXIT_RPM &&
        foc::speedCommandRpm() <= CL_EXIT_RPM)
        foc::requestOpenLoop();
}

static double zReal();
static double zErrDeg();
static double pk_z = 0, pk_i = 0, pk_ang = 0;   // peaks since the last report
static bool   faulted = false;

static double trace_from = -1, trace_to = -1;   // TRACE=<from>,<to> (s): a line every 5 ms

static void run(double seconds) {
    long n = lround(seconds / PWM_PERIOD_S);
    for (long i = 0; i < n; i++) {
        period();
        if (g_t >= trace_from && g_t <= trace_to && i % 100 == 0) {
            foc::MotorTelemetry a = foc::take(0), b = foc::take(1);
            printf("TRACE %7.3f mode %d cmd %6.0f  M1 %7.1f M2 %7.1f rpm  zerr %+6.2f  M1 dq %+5.2f %+5.2f  M2 dq %+5.2f %+5.2f  "
                   "angerr %5.1f\n",
                   g_t, (int)foc::mode(), foc::speedCommandRpm(), mot[0].w * 60 / (2 * M_PI), mot[1].w * 60 / (2 * M_PI),
                   zErrDeg(), a.id, a.iq, b.id, b.iq, foc::maxAngleErrorRad() * 180 / M_PI);
        }
        pk_z   = fmax(pk_z, fabs(zErrDeg()));
        pk_ang = fmax(pk_ang, foc::maxAngleErrorRad() * 180 / M_PI);
        for (int k = 0; k < 2; k++) pk_i = fmax(pk_i, fmax(fabs(mot[k].ia), fmax(fabs(mot[k].ib), fabs(mot[k].ic()))));
        if (i % 20 == 0) supervise();   // main loop ~1 kHz
        char why[96];
        if (foc::takeFault(why, sizeof(why))) {
            printf("%7.3f s  FAULT: %s\n", g_t, why);
            faulted = true;
        }
        if (foc::enabled() && foc::mode() != foc::Mode::Off &&
            foc::maxAngleErrorRad() > (foc::mode() == foc::Mode::Closed ? CL_ANGLE_TRIP_RAD : OL_SLIP_TRIP_RAD)) {
            if (!faulted) printf("%7.3f s  FAULT (main.cpp would stop): rotor %.0f deg off its reference\n", g_t,
                                 foc::maxAngleErrorRad() * 180 / M_PI);
            faulted = true;
        }
    }
}

// The physical Z: half the sum of the rotor angles (rad), and its reference at the start.
static double zReal() { return 0.5 * (mot[0].th + mot[1].th); }
static double z0 = 0;
static double zErrDeg() { return (zReal() - z0 - foc::phaseCurrent()) * 180 / M_PI; }

static int fails = 0;
static void check(bool ok, const char* what) {
    printf("%7.3f s  %s  %s\n", g_t, ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

static void report(const char* label) {
    printf("%7.3f s  %-18s mode %d cmd %6.0f  M1 %7.0f M2 %7.0f rpm  zerr %+6.2f deg  peak: zerr %5.1f deg, rotor %5.1f deg, "
           "I %5.1f A\n",
           g_t, label, (int)foc::mode(), foc::speedCommandRpm(), mot[0].w * 60 / (2 * M_PI), mot[1].w * 60 / (2 * M_PI),
           zErrDeg(), pk_z, pk_ang, pk_i);
    pk_z = pk_ang = pk_i = 0;
}

int main(int argc, char** argv) {
    double J = argc > 1 ? atof(argv[1]) : 1e-5, kf = argc > 2 ? atof(argv[2]) : 1, kr = argc > 3 ? atof(argv[3]) : 1;
    for (int k = 0; k < 2; k++) {
        mot[k].J = J;
        mot[k].psi *= kf;
        mot[k].R *= kr;
    }
    printf("model: J %.1e kg m^2, flux %.2f mWb, R %.3f ohm (firmware assumes %.2f mWb, %.3f ohm)\n", J,
           mot[0].psi * 1e3, mot[0].R, FLUX_WB * 1e3, PHASE_R_OHM);
    if (const char* tr = getenv("TRACE")) sscanf(tr, "%lf,%lf", &trace_from, &trace_to);
    foc::init();
    // Tuning overrides for experiments: OLKI=<V/(A s)>, POS=<kp>,<kd>,<ki>
    if (const char* d = getenv("OLKI")) foc::tuning().ol_ki = atof(d);
    if (const char* p = getenv("POS")) sscanf(p, "%f,%f,%f", &foc::tuning().pos_kp, &foc::tuning().pos_kd, &foc::tuning().pos_ki);
    run(0.01);   // a few idle periods

    // 1. Enable: open-loop hold.  The rotors pull into the current vectors.
    check(foc::enable(), "enable");
    run(0.3);
    z0 = zReal() - foc::phaseCurrent();
    report("hold");
    check(fabs(mot[0].w) < 0.5 && fabs(mot[1].w) < 0.5, "rotors at rest in the hold");
    {
        foc::MotorTelemetry t = foc::take(0);
        check(fabs(t.id - HOLD_CURRENT_A) < 0.3 && fabs(t.iq) < 0.3, "hold current on the d axis");
    }

    // 2. Z move at standstill: +90 deg of phase (2 mm).
    foc::setPhaseTarget(90 * M_PI / 180);
    foc::setZMoving(true);
    run(0.5);
    foc::setZMoving(false);
    // The model has no Z mechanism friction: loose rotors on a current vector ring at ~7-10 Hz with
    // only the back-EMF damping the ESP32's voltage drive also had (zeta ~0.1-0.2).  Let it settle.
    run(0.6);
    report("Z +90 at rest");
    check(fabs(foc::phaseCurrent() - M_PI / 2) < 1e-3, "phase ramp reached its target");
    check(fabs(zErrDeg()) < 3, "rotors followed the standstill Z move");

    // 3. Spin up to 6000 RPM: open loop to 2000, hand-over, closed loop on.
    foc::setSpeedTarget(6000);
    run(0.6);
    report("spin-up");
    run(2.0);
    report("6000 RPM");
    check(foc::mode() == foc::Mode::Closed, "closed loop engaged");
    check(fabs(mot[0].w * 60 / (2 * M_PI) - (+1 * SPINDLE_DIRECTION) * 6000) < 120 &&
              fabs(mot[1].w * 60 / (2 * M_PI) - (-1 * SPINDLE_DIRECTION) * 6000) < 120,
          "both motors at 6000 RPM in opposite senses");
    check(fabs(zErrDeg()) < 5, "Z unchanged through spin-up and the hand-over");

    // 4. Z move while spinning: -45 deg (1 mm down) and back.
    foc::setPhaseTarget(45 * M_PI / 180);
    run(0.5);
    report("Z -45 at 6000");
    check(fabs(zErrDeg()) < 5, "Z followed the move while spinning");

    // 5. Load steps on motor 1: 15 mNm, then 36 mNm (the ESC1 hand-load peak, ~3 A).
    run(0.2);
    pk_z = pk_ang = pk_i = 0;
    mot[0].load = 0.015;
    run(0.5);
    double pz = pk_z;
    report("M1 +15 mNm");
    check(fabs(mot[0].w * 60 / (2 * M_PI) - (+1 * SPINDLE_DIRECTION) * 6000) < 300, "M1 holds speed under load");
    check(pz < 10, "15 mNm step: Z peak error < 10 deg of phase (0.22 mm)");
    mot[0].load = 0.036;
    run(0.5);
    pz = pk_z;
    report("M1 +36 mNm");
    check(pz < 20, "36 mNm step: Z peak error < 20 deg of phase (0.44 mm)");
    mot[0].load = 0;
    run(0.5);
    report("load off");

    // 6. Up to 18000 RPM.
    foc::setSpeedTarget(18000);
    run(12.0);
    report("18000 RPM");
    check(fabs(fabs(mot[1].w * 60 / (2 * M_PI)) - 18000) < 400, "18000 RPM reached on 36 V");
    check(fabs(zErrDeg()) < 5, "Z held at 18000 RPM");

    // 7. Spin down: hand back to open loop, ramp to rest.
    foc::setSpeedTarget(0);
    run(14.0);
    report("spun down");
    check(foc::mode() == foc::Mode::Open, "back in open loop");
    check(fabs(mot[0].w) < 1 && fabs(mot[1].w) < 1, "rotors at rest");
    check(fabs(zErrDeg()) < 5, "Z unchanged through the hand-back and stop");

    check(!faulted, "no fault or slip stop during the run");
    foc::disable();
    printf("\n%d check(s) failed\n", fails);
    return fails ? 1 : 0;
}
