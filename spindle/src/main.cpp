#include <Arduino.h>
#include <SimpleFOC.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "soc/rtc_cntl_reg.h"  // RTC_CNTL_BROWN_OUT_REG (disable brownout detector)
#include "driver/periph_ctrl.h"  // periph_module_reset (recover a wedged SAR-ADC)
#include "pins.h"
#include "config.h"
#include "motor_controller.h"
#include "calibration.h"
#include "serial_commands.h"
#include "ota_service.h"

// ------------------- Hardware Objects -------------------

SPIClass drvSPI(FSPI);

BLDCMotor motor1_hw(POLE_PAIRS);
DRV8316Driver6PWM driver1_hw(INHA, INLA, INHB, INLB, INHC, INLC,
                              SPI_CS_PIN, false, EN_GATE, NFAULT);

BLDCMotor motor2_hw(POLE_PAIRS);
DRV8316Driver6PWM driver2_hw(INHA2, INLA2, INHB2, INLB2, INHC2, INLC2,
                              SPI_CS_PIN2, false, EN_GATE, NFAULT);

// ------------------- Motor Controllers -------------------
// direction: +1 for motor 1 (forward), -1 for motor 2 (opposite)

MotorController mc1(motor1_hw, driver1_hw, CURA, CURB, CURC, +1);
MotorController mc2(motor2_hw, driver2_hw, CURA2, CURB2, CURC2, -1);

Calibration calibration;
static TaskHandle_t motor_control_task_handle = nullptr;

// ------------------- Calibration LUT Default Data -------------------
// Full auto-calibration sweep (100-14000 RPM in 100 RPM steps, tuned to I_phase_rms = 3.5 A).
// These are the per-motor open-loop voltages measured on the bench; the drive reaches its
// 16 V ceiling around 13300 RPM (M1) / 13900 RPM (M2), which is well above the 10000 RPM
// machine max.  Loaded as the seed LUT at boot (a stored NVS calibration, if present,
// overrides them), so the spindle has a correct rising high-RPM voltage profile out of the
// box instead of stalling/over-currenting above 6000 RPM.

static const float MC1_DEFAULT_LUT[] = {
    1.638f, 1.637f, 1.672f, 1.696f, 1.689f, 1.724f, 1.780f, 1.822f, 1.876f, 1.914f,   // 100-1000
    1.946f, 2.020f, 2.061f, 2.122f, 2.177f, 2.243f, 2.319f, 2.361f, 2.444f, 2.503f,   // 1100-2000
    2.591f, 2.646f, 2.722f, 2.786f, 2.851f, 2.937f, 3.007f, 3.087f, 3.154f, 3.239f,   // 2100-3000
    3.307f, 3.374f, 3.460f, 3.541f, 3.611f, 3.691f, 3.758f, 3.839f, 3.928f, 4.004f,   // 3100-4000
    4.085f, 4.160f, 4.237f, 4.319f, 4.394f, 4.469f, 4.556f, 4.642f, 4.723f, 4.794f,   // 4100-5000
    4.882f, 4.968f, 5.052f, 5.128f, 5.203f, 5.303f, 5.369f, 5.457f, 5.540f, 5.619f,   // 5100-6000
    5.705f, 5.791f, 5.872f, 5.963f, 6.047f, 6.131f, 6.206f, 6.297f, 6.376f, 6.464f,   // 6100-7000
    6.547f, 6.629f, 6.715f, 6.800f, 6.876f, 6.976f, 7.058f, 7.126f, 7.223f, 7.313f,   // 7100-8000
    7.396f, 7.478f, 7.566f, 7.662f, 7.731f, 7.818f, 7.897f, 7.985f, 8.041f, 8.076f,   // 8100-9000
    8.163f, 8.192f, 8.245f, 8.318f, 8.387f, 8.454f, 8.520f, 8.614f, 8.680f, 8.741f,   // 9100-10000
    8.814f, 8.884f, 8.951f, 9.025f, 9.111f, 9.184f, 9.280f, 9.363f, 9.430f, 9.551f,   // 10100-11000
    9.643f, 9.756f, 9.876f, 9.988f, 10.143f, 10.300f, 10.422f, 10.618f, 10.786f, 11.015f,  // 11100-12000
    11.236f, 11.459f, 11.746f, 12.026f, 12.278f, 12.623f, 13.018f, 13.406f, 13.758f, 14.365f, // 12100-13000
    14.894f, 15.414f, 16.000f, 16.000f, 16.000f, 16.000f, 16.000f, 16.000f, 16.000f, 16.000f, // 13100-14000
};

static const float MC2_DEFAULT_LUT[] = {
    2.107f, 2.157f, 2.158f, 2.161f, 2.174f, 2.172f, 2.243f, 2.255f, 2.277f, 2.278f,   // 100-1000
    2.283f, 2.330f, 2.472f, 2.468f, 2.497f, 2.598f, 2.617f, 2.669f, 2.669f, 2.749f,   // 1100-2000
    2.832f, 2.885f, 2.914f, 2.956f, 3.056f, 3.103f, 3.103f, 3.271f, 3.307f, 3.376f,   // 2100-3000
    3.438f, 3.472f, 3.615f, 3.680f, 3.710f, 3.744f, 3.840f, 3.920f, 3.922f, 4.078f,   // 3100-4000
    4.118f, 4.241f, 4.319f, 4.348f, 4.408f, 4.513f, 4.588f, 4.664f, 4.730f, 4.817f,   // 4100-5000
    4.884f, 4.957f, 5.025f, 5.111f, 5.222f, 5.250f, 5.337f, 5.420f, 5.501f, 5.561f,   // 5100-6000
    5.625f, 5.732f, 5.782f, 5.901f, 5.955f, 6.041f, 6.105f, 6.175f, 6.276f, 6.341f,   // 6100-7000
    6.418f, 6.518f, 6.572f, 6.656f, 6.740f, 6.828f, 6.889f, 6.990f, 7.081f, 7.159f,   // 7100-8000
    7.241f, 7.305f, 7.402f, 7.484f, 7.559f, 7.643f, 7.701f, 7.784f, 7.854f, 7.919f,   // 8100-9000
    7.980f, 8.047f, 8.100f, 8.124f, 8.186f, 8.231f, 8.331f, 8.371f, 8.424f, 8.486f,   // 9100-10000
    8.561f, 8.632f, 8.709f, 8.787f, 8.845f, 8.917f, 8.988f, 9.063f, 9.137f, 9.191f,   // 10100-11000
    9.260f, 9.355f, 9.438f, 9.519f, 9.623f, 9.720f, 9.810f, 9.956f, 10.087f, 10.207f, // 11100-12000
    10.331f, 10.504f, 10.667f, 10.913f, 11.064f, 11.235f, 11.504f, 11.743f, 11.962f, 12.308f, // 12100-13000
    12.608f, 12.913f, 13.321f, 13.607f, 14.167f, 14.567f, 14.977f, 15.605f, 16.000f, 16.000f, // 13100-14000
};

static void loadDefaultLUT(MotorController& mc, int motor_idx,
                           const float* defaults, size_t default_count) {
    float expanded_defaults[CAL_LUT_SIZE];
    float tail_value = (default_count > 0) ? defaults[default_count - 1] : BASE_VOLTAGE;

    for (int i = 0; i < CAL_LUT_SIZE; i++) {
        expanded_defaults[i] = (i < (int)default_count) ? defaults[i] : tail_value;
    }

    // Guard against legacy seed tables that drop back to BASE_VOLTAGE at higher RPM.
    // Calibration expects a smooth/non-decreasing baseline so per-step voltage ceilings
    // do not collapse at the next checkpoint.
    for (int i = 1; i < CAL_LUT_SIZE; i++) {
        if (expanded_defaults[i] < expanded_defaults[i - 1]) {
            expanded_defaults[i] = expanded_defaults[i - 1];
        }
    }

    loadCalibrationLUT(mc, motor_idx, expanded_defaults);
}

// ------------------- Fault Monitoring -------------------

static int ocp_consec_count1 = 0;
static int ocp_consec_count2 = 0;
static int serious_consec_count1 = 0;
static int serious_consec_count2 = 0;
static uint32_t last_drv_check = 0;
static uint32_t last_spinup_telem = 0;
static int overcurrent_count = 0;

// --- Over-current auto-recovery state ---
static bool     recovery_pending = false;
static uint32_t recovery_resume_at = 0;
static float    recovery_target_v1 = 0.0f;
static float    recovery_target_v2 = 0.0f;
static int      recovery_attempts = 0;
static uint32_t recovery_last_fault_at = 0;  // millis() of the most recent over-current retry
static char     recovery_cause[80] = "";  // the fault that triggered the pending recovery

// Send a human-readable event to the XY board (which surfaces it in the ESP3D web console
// via log_warn / log_error / log_info) and to the local USB console.  level is one of
// "WARN", "ERR" or "MSG" - the XY board maps these to the matching log level.
static void reportEvent(const char* level, const char* fmt, ...) {
    char buf[100];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    // The motor FOC loop runs in THIS SAME task.  A blocking serial write (the UART TX buffer
    // filling, or the USB CDC host not draining fast enough) would stall the loop; at high RPM
    // even a few ms stall makes the open-loop electrical angle jump between FOC updates, which
    // applies a coarse voltage step that spikes phase current and trips the DRV8316 OCP (all six
    // phase comparators at once).  So write only when the whole line already fits in the TX
    // buffer; otherwise drop it - these link/USB messages are advisory and a fresh status or
    // telemetry line follows shortly.  This keeps inter-board serial traffic from disturbing FOC.
    char line[128];
    int n = snprintf(line, sizeof(line), "%s:%s\n", level, buf);
    if (n <= 0) return;
    if (n > (int)sizeof(line)) n = (int)sizeof(line);

    if (Serial1.availableForWrite() >= n) {
        Serial1.write(reinterpret_cast<const uint8_t*>(line), n);
    }
    if (Serial && Serial.availableForWrite() >= n) {
        Serial.write(reinterpret_cast<const uint8_t*>(line), n);
    }
}

// Tell the XY board that the spindle has (re)established its Z zero (phase offset 0) so it
// can reset its own Z-axis machine position to 0 and keep the two boards' Z origins aligned.
// Sent whenever homing, tool loading, or tool removal redefines the phase-offset zero.
static void notifyZHomed() {
    Serial1.println(F("ZHOMED"));
    if (Serial) Serial.println(F("[MSG] Z zero re-established -> notified XY board (ZHOMED)"));
}

// Begin auto-recovery from a transient over-current: remember the commanded speed, stop the
// motors for a brief cooldown, and schedule a resume.  Returns false (so the caller fails out)
// during calibration, or once over-currents keep recurring past FAULT_RECOVERY_MAX_ATTEMPTS
// within the window.
static bool beginFaultRecovery(const char* what) {
    // A calibration sweep drives the motors open-loop; retrying velocity mode would corrupt it,
    // so the caller aborts the sweep instead.
    if (calibration.isActive()) return false;

    uint32_t now = millis();
    // Reset the retry counter only after a sustained CLEAN run since the last over-current.
    // Each failed spin-up retry recurs quickly (cooldown + OCP re-detect ~1 s), which is far
    // shorter than the window, so consecutive fast retries must keep accumulating - otherwise
    // a wall-clock window that expires mid-chain would reset the count and loop forever instead
    // of failing out.  A genuine one-off transient during a long job clears the count because
    // the motor then runs fault-free for longer than FAULT_RECOVERY_WINDOW_MS.
    if (recovery_attempts > 0 && now - recovery_last_fault_at > FAULT_RECOVERY_WINDOW_MS) {
        recovery_attempts = 0;
    }
    recovery_last_fault_at = now;
    recovery_attempts++;
    if (recovery_attempts > FAULT_RECOVERY_MAX_ATTEMPTS) {
        return false;  // kept over-currenting -> let the caller fail out to 0 RPM
    }

    // Remember what was running so we can resume after the cooldown.
    recovery_target_v1 = mc1.velocity_mode ? mc1.target_velocity : 0.0f;
    recovery_target_v2 = mc2.velocity_mode ? mc2.target_velocity : 0.0f;

    mc1.emergencyStop();
    mc2.emergencyStop();

    recovery_pending = true;
    recovery_resume_at = now + FAULT_RECOVERY_COOLDOWN_MS;
    g_speed_command_flag = false;  // ignore our own resume; watch for a NEW operator command

    // Remember the cause so the later "retried" message can name what tripped it.
    strncpy(recovery_cause, what, sizeof(recovery_cause) - 1);
    recovery_cause[sizeof(recovery_cause) - 1] = '\0';

    reportEvent("WARN", "%s - pausing %lums to retry (%d/%d)", what,
                (unsigned long)FAULT_RECOVERY_COOLDOWN_MS, recovery_attempts, FAULT_RECOVERY_MAX_ATTEMPTS);
    return true;
}

// Complete a pending auto-recovery once the cooldown has elapsed, resuming the last commanded
// speed so a transient overload does not stop the job.
static void updateFaultRecovery() {
    if (!recovery_pending) return;

    // A fresh operator/XY speed command during the cooldown wins - drop the recovery so we do
    // not override it (e.g. the operator commanded a stop).
    if (g_speed_command_flag) {
        recovery_pending = false;
        return;
    }
    if ((int32_t)(millis() - recovery_resume_at) < 0) return;

    recovery_pending = false;

    bool resumed = false;
    if (fabsf(recovery_target_v1) > 0.1f) { mc1.target_velocity = recovery_target_v1; mc1.velocity_mode = true; resumed = true; }
    if (fabsf(recovery_target_v2) > 0.1f) { mc2.target_velocity = recovery_target_v2; mc2.velocity_mode = true; resumed = true; }

    if (resumed) {
        float v = (fabsf(recovery_target_v1) > 0.1f) ? recovery_target_v1 : recovery_target_v2;
        reportEvent("MSG", "retried %s - resuming %.0f RPM", recovery_cause,
                    fabsf(v) * 60.0f / (2.0f * PI));
    } else {
        reportEvent("MSG", "retried %s - motors idle", recovery_cause);
    }
}

// Periodic spin-up telemetry to the web console so the exact RPM / applied voltage / measured
// phase current at which a fault occurs can be seen.  Throttled, and only active while actually
// ramping a spindle spin command (not during calibration or steady running).
static void reportSpinupTelemetry() {
    if (calibration.isActive()) return;
    if (!mc1.velocity_mode && !mc2.velocity_mode) return;
    bool ramping = (fabsf(mc1.target_velocity - mc1.current_velocity) > 1.0f) ||
                   (fabsf(mc2.target_velocity - mc2.current_velocity) > 1.0f);
    if (!ramping) return;

    uint32_t now = millis();
    if (now - last_spinup_telem < 200) return;
    last_spinup_telem = now;

    const float k = 60.0f / (2.0f * PI);  // rad/s -> RPM
    reportEvent("MSG", "spinup v=%.0f/%.0f Uq=%.2f/%.2f I=%.2f/%.2f",
                mc1.current_velocity * k, mc2.current_velocity * k,
                mc1.motor.voltage_limit, mc2.motor.voltage_limit,
                mc1.protection_current, mc2.protection_current);
}

static void checkDRV8316Faults() {
    if (!mc1.enabled && !mc2.enabled) return;
    if (millis() - last_drv_check < 100) return;
    last_drv_check = millis();

    DRV8316Status st1 = mc1.driver.getStatus();
    DRV8316Status st2 = mc2.driver.getStatus();

    bool ocp1 = st1.isFault() && st1.isOverCurrent();
    bool ocp2 = st2.isFault() && st2.isOverCurrent();
    bool serious1 = st1.isFault() && (st1.isOverTemperature() || st1.isOverVoltage());
    bool serious2 = st2.isFault() && (st2.isOverTemperature() || st2.isOverVoltage());

    ocp_consec_count1 = ocp1 ? (ocp_consec_count1 + 1) : 0;
    ocp_consec_count2 = ocp2 ? (ocp_consec_count2 + 1) : 0;

    // Debounce over-temp / over-voltage: real thermal/voltage faults are sustained, so require
    // the OT/OVP bit to persist across SERIOUS_CONSEC_LIMIT reads before treating it as a
    // genuine (latching) serious fault.  A hard over-current burst (e.g. the open-loop spin-up
    // current transient) can momentarily co-assert OT/OVP for a single read; without this
    // debounce that single co-assertion latches a hard fault and alarms the XY board even
    // though the real event is an over-current.  The DRV8316 hardware OTP/OVP still protects
    // instantly regardless of this debounce.
    serious_consec_count1 = serious1 ? (serious_consec_count1 + 1) : 0;
    serious_consec_count2 = serious2 ? (serious_consec_count2 + 1) : 0;

    bool persistent_ocp = (ocp_consec_count1 >= OCP_CONSEC_LIMIT) ||
                          (ocp_consec_count2 >= OCP_CONSEC_LIMIT);
    bool serious = (serious_consec_count1 >= SERIOUS_CONSEC_LIMIT) ||
                   (serious_consec_count2 >= SERIOUS_CONSEC_LIMIT);

    // Ride through any not-yet-confirmed fault (a transient OCP, or an OT/OVP flicker that has
    // not persisted long enough): clear the driver latch so the next read reflects reality and
    // a one-shot glitch resets its counter.  The consecutive counters above keep accumulating
    // across reads, so a genuinely persistent OCP or sustained OT/OVP still escalates below.
    if (!serious && !persistent_ocp) {
        if (st1.isFault()) { mc1.driver.clearFault(); delayMicroseconds(1); }
        if (st2.isFault()) { mc2.driver.clearFault(); delayMicroseconds(1); }
        if ((ocp1 || ocp2) && (ocp_consec_count1 == 1 || ocp_consec_count2 == 1)) {
            Serial.printf("DRV8316 OCP transient (CBC auto-clear): M1=%d M2=%d\n", ocp1, ocp2);
        }
        return;
    }

    if (st1.isFault()) {
        Serial.printf("DRV8316 HARDWARE FAULT (Motor 1): OCP=%d OT=%d OVP=%d\n",
                      st1.isOverCurrent(), st1.isOverTemperature(), st1.isOverVoltage());
        mc1.driver.clearFault(); delayMicroseconds(1);
    }
    if (st2.isFault()) {
        Serial.printf("DRV8316 HARDWARE FAULT (Motor 2): OCP=%d OT=%d OVP=%d\n",
                      st2.isOverCurrent(), st2.isOverTemperature(), st2.isOverVoltage());
        mc2.driver.clearFault(); delayMicroseconds(1);
    }
    if (persistent_ocp) {
        Serial.println(F("  (persistent OCP - fault present for 500+ ms)"));
    }

    ocp_consec_count1 = 0;
    ocp_consec_count2 = 0;

    // Over-current only (no CONFIRMED over-temperature / over-voltage): pause and retry like the
    // software monitor.  Sustained over-temp / over-voltage is genuinely dangerous, so it latches.
    if (!serious && persistent_ocp) {
        if (beginFaultRecovery("DRV8316 over-current (persistent OCP)")) {
            return;
        }
        // Retries exhausted (or a calibration sweep is running) -> fail out without latching.
        mc1.emergencyStop();
        mc2.emergencyStop();
        if (calibration.isActive()) {
            reportEvent("ERR", "DRV8316 over-current during calibration - aborting");
            calibration.abort(mc1, mc2, "DRV8316 over-current");
        } else {
            reportEvent("WARN", "DRV8316 over-current persisted after %d tries - spindle stopped (0 RPM); send a new speed to restart",
                        FAULT_RECOVERY_MAX_ATTEMPTS);
        }
        return;
    }

    // Confirmed serious fault (sustained over-temp / over-voltage): latch and alarm the XY board.
    // Include the exact status bits so the operator can see which one tripped in the ESP3D web
    // console.  Kept compact so the whole line fits the XY board's link receive buffer.
    serious_consec_count1 = 0;
    serious_consec_count2 = 0;

    // Diagnostic dump: the summary OT bit is set by EITHER an over-temp WARNING (OTW, ~20C
    // below shutdown) OR an over-temp SHUTDOWN (OTS); distinguish them, and also surface the
    // per-phase OCP bits and SPI/charge-pump errors, plus the raw register bytes for off-line
    // decoding.  This tells us whether "OT" is a real thermal shutdown, a mere warning, or a
    // glitched SPI read (which would also set the SPI/parity bits).
    reportEvent("MSG", "DRVraw M1[IC%02X S1%02X S2%02X] M2[IC%02X S1%02X S2%02X]",
                st1.status.reg, st1.status1.reg, st1.status2.reg,
                st2.status.reg, st2.status1.reg, st2.status2.reg);
    reportEvent("MSG", "M1 OTS%d OTW%d OVP%d SPI%d VCPuv%d OCP[HA%d LA%d HB%d LB%d HC%d LC%d]",
                st1.isOverTemperatureShutdown(), st1.isOverTemperatureWarning(), st1.isOverVoltage(),
                st1.isSPIError(), st1.isChargePumpUnderVoltage(),
                st1.isOverCurrent_Ah(), st1.isOverCurrent_Al(), st1.isOverCurrent_Bh(),
                st1.isOverCurrent_Bl(), st1.isOverCurrent_Ch(), st1.isOverCurrent_Cl());
    reportEvent("MSG", "M2 OTS%d OTW%d OVP%d SPI%d VCPuv%d OCP[HA%d LA%d HB%d LB%d HC%d LC%d]",
                st2.isOverTemperatureShutdown(), st2.isOverTemperatureWarning(), st2.isOverVoltage(),
                st2.isSPIError(), st2.isChargePumpUnderVoltage(),
                st2.isOverCurrent_Ah(), st2.isOverCurrent_Al(), st2.isOverCurrent_Bh(),
                st2.isOverCurrent_Bl(), st2.isOverCurrent_Ch(), st2.isOverCurrent_Cl());

    reportEvent("ERR", "DRV8316 fault M1[OC%d OT%d OV%d] M2[OC%d OT%d OV%d]",
                st1.isOverCurrent(), st1.isOverTemperature(), st1.isOverVoltage(),
                st2.isOverCurrent(), st2.isOverTemperature(), st2.isOverVoltage());

    g_fault_code = 1;  // DRV8316 hardware fault
    mc1.emergencyStop();
    mc2.emergencyStop();

    calibration.abort(mc1, mc2, "DRV8316 hardware fault");
}

static void checkOvercurrent() {
    static int last_cal_motor_idx = -1;

    // Arm protection 500ms after motor was enabled
    uint32_t now_ms = millis();
    if (mc1.enabled && !mc1.reached_speed && (now_ms - mc1.start_time) > 500) mc1.reached_speed = true;
    if (mc2.enabled && !mc2.reached_speed && (now_ms - mc2.start_time) > 500) mc2.reached_speed = true;

    if (calibration.isActive()) {
        if (last_cal_motor_idx != calibration.active_motor_idx) {
            overcurrent_count = 0;
            last_cal_motor_idx = calibration.active_motor_idx;
        }
    } else {
        last_cal_motor_idx = -1;
    }

    float oc_threshold = OVERCURRENT_THRESHOLD;

    bool m1_trip = mc1.enabled && mc1.reached_speed && (mc1.protection_current > oc_threshold);
    bool m2_trip = mc2.enabled && mc2.reached_speed && (mc2.protection_current > oc_threshold);

    // During calibration, only the active motor should participate in OC trip decisions.
    if (calibration.isActive()) {
        if (calibration.active_motor_idx == 0) {
            m2_trip = false;
        } else {
            m1_trip = false;
        }
    }

    if (m1_trip || m2_trip) {
        overcurrent_count++;
    } else {
        overcurrent_count = 0;
    }

    if (overcurrent_count < OVERCURRENT_CONSECUTIVE_TRIPS) return;
    overcurrent_count = 0;

    // Describe which motor(s) tripped and at what current for the console.
    char detail[80];
    if (m1_trip && m2_trip)
        snprintf(detail, sizeof(detail), "over-current on M1 (%.1fA) & M2 (%.1fA)",
                 mc1.protection_current, mc2.protection_current);
    else if (m1_trip)
        snprintf(detail, sizeof(detail), "over-current on M1 (%.1fA)", mc1.protection_current);
    else
        snprintf(detail, sizeof(detail), "over-current on M2 (%.1fA)", mc2.protection_current);

    Serial.printf("OVERCURRENT: %s\n", detail);

    // Transient over-current: pause and retry the commanded speed without latching.
    if (beginFaultRecovery(detail)) {
        return;
    }

    // Retries exhausted (or a calibration sweep is running).
    mc1.emergencyStop();
    mc2.emergencyStop();
    if (calibration.isActive()) {
        reportEvent("ERR", "%s during calibration - aborting", detail);
        calibration.abort(mc1, mc2, "overcurrent fault");
    } else {
        // Fail out: come to rest at 0 RPM and wait for a fresh speed command (no fault/alarm).
        reportEvent("WARN", "%s persisted after %d tries - spindle stopped (0 RPM); send a new speed to restart",
                    detail, FAULT_RECOVERY_MAX_ATTEMPTS);
    }
}

// ------------------- Phase Offset -------------------

static void updatePhaseOffset(float dt) {
    if (fabsf(phase_offset.target - phase_offset.current) < 0.0001f) return;

    float max_step = PHASE_OFFSET_RAMP_RATE * dt;
    float err = phase_offset.target - phase_offset.current;
    float delta;
    if (fabsf(err) <= max_step) {
        delta = err;
    } else {
        delta = (err > 0) ? max_step : -max_step;
    }
    phase_offset.current += delta;

    // Apply delta to each motor's position
    MotorController* motors[] = { &mc1, &mc2 };
    for (int i = 0; i < 2; i++) {
        if (!motors[i]->enabled) continue;
        if (motors[i]->velocity_mode) {
            motors[i]->motor.shaft_angle += delta;
        } else {
            motors[i]->target_angle += delta;
            motors[i]->motor.target = motors[i]->target_angle;
        }
    }
}

// Redefine the current resting position as the phase-offset zero.  It is NOT enough to
// zero phase_offset alone: while raising/lowering to home the motors' open-loop angle
// target (target_angle / motor.target) accumulates the whole travel, and that is what the
// FOC actually drives to.  If we leave it stale, the next streamed Z target reconciles the
// motors against that large angle and the Z lurches far past the commanded move.  Zeroing
// the phase offset AND the motor angles together (with the drivers already powered down so
// there is no live jerk) keeps the logical Z zero and the motors' target phase in sync.
static void zeroPhaseReference() {
    phase_offset.current = 0.0f;
    phase_offset.target  = 0.0f;
    MotorController* motors[] = { &mc1, &mc2 };
    for (int i = 0; i < 2; i++) {
        motors[i]->target_angle      = 0.0f;
        motors[i]->motor.target      = 0.0f;
        motors[i]->motor.shaft_angle = 0.0f;
    }
}

// ------------------- Telemetry -------------------

static void printTelemetry() {
    static uint32_t last_print_time = 0;
    static bool header_printed = false;
    uint32_t now = millis();

    if (now - last_print_time < 500) return;
    last_print_time = now;

    if (!header_printed) {
        Serial.println(F("  M1 RPM  M1 Lim(V)  M1 Curr(A)    M2 RPM  M2 Lim(V)  M2 Curr(A)"));
        header_printed = true;
    }

    float cmd_rpm1 = mc1.enabled ? mc1.current_velocity * 60.0f / (2.0f * PI) : 0.0f;
    float cmd_rpm2 = mc2.enabled ? mc2.current_velocity * 60.0f / (2.0f * PI) : 0.0f;

    Serial.printf("%7.0f  %9.2f  %10.3f    %7.0f  %9.2f  %10.3f\n",
                  cmd_rpm1, mc1.motor.voltage_limit, mc1.filtered_current,
                  cmd_rpm2, mc2.motor.voltage_limit, mc2.filtered_current);
}

// ------------------- Motor Timeout -------------------

static void checkMotorTimeouts() {
    if (mc1.enabled && (millis() - mc1.start_time) >= MOTOR_RUN_DURATION) {
        mc1.disable();
        mc1.continuous_rotation = false;
        Serial.println(F("Motor 1 powered down after timeout"));
    }
    if (mc2.enabled && (millis() - mc2.start_time) >= MOTOR_RUN_DURATION) {
        mc2.disable();
        mc2.continuous_rotation = false;
        Serial.println(F("Motor 2 powered down after timeout"));
    }
}

// ------------------- Z Hold Power-Down -------------------

// When the XY board reports the machine is idle (the 'D' command sets
// g_hold_release_requested), power the Z-axis BLDC drivers down once their phase move
// has actually settled.  Holding a Z position in angle mode keeps drawing current,
// which heats the DRV8316s and keeps the cooling fan running even though nothing is
// moving.  We wait for the phase ramp to reach its target here (rather than powering
// down the instant the XY board goes idle) so the Z axis is not released before it has
// finished its move.  A new spindle-speed or Z-target command clears the request.
static void checkPhaseHoldPowerdown() {
    if (!g_hold_release_requested) {
        return;
    }
    // Never release while the spindle is spinning, free-running, or calibrating.
    if (mc1.velocity_mode || mc2.velocity_mode || mc1.continuous_rotation || mc2.continuous_rotation ||
        calibration.isActive()) {
        return;
    }
    // Wait until the phase ramp has reached its target (the Z move is complete).
    if (fabsf(phase_offset.target - phase_offset.current) >= PHASE_MOVE_COMPLETE_EPS_RAD) {
        return;
    }
    if (mc1.enabled || mc2.enabled) {
        mc1.disable();
        mc2.disable();
        Serial.println(F("Z move complete - motor drivers powered down"));
    }
    g_hold_release_requested = false;
}

// ------------------- Machine State Machine -------------------

// The spindle board runs a single, explicit state machine.  Every transition is printed to
// the ESP3D web console (and the local USB console) by setMachineState(), so the operator can
// always see what the board is doing and why a move started or stopped.
//
// The Z axis is moved by advancing the inter-motor phase offset (the same mechanism the XY
// board's 'Z' command uses); the top-of-travel beam detector tells us where a tool is.
//
//   Booting            - just powered up (or a re-home was requested); decide the next state
//   Homing             - raising the Z to find the top-of-travel beam
//   IdleToolLoaded     - homed with a tool; the Z follows the XY board, spindle off
//   IdleToolUnloaded   - homed with no tool; watching the beam for the operator to insert one
//   LoadingTool        - a tool broke the beam; lowering the Z until the beam clears (seated)
//   UnloadingTool      - raising the Z to lift the tool up and out through the beam (ejected)
//   SpindleRunning     - the spindle is spinning
//   Calibrating        - the auto-calibration sweep is running
//   Fault              - a latched fault stopped the motors
//   OtaUpdate          - a WiFi/OTA firmware update is in progress
//
// The Homing / Idle* / Loading / Unloading states are the "tool" sub-machine driven by
// updateToolStateMachine() and tracked in g_tool_state.  SpindleRunning / Calibrating /
// Fault / OtaUpdate are overlays: updateReportedState() layers them on top each loop so the
// single reported state (g_machine_state) always reflects the board's real activity.
enum class MachineState : uint8_t {
    Booting,
    Homing,
    IdleToolLoaded,
    IdleToolUnloaded,
    LoadingTool,
    UnloadingTool,
    SpindleRunning,
    Calibrating,
    Fault,
    OtaUpdate,
};

static MachineState g_tool_state    = MachineState::Booting;  // Z/tool sub-machine state
static MachineState g_machine_state = MachineState::Booting;  // reported state (logged on change)

// Human-readable name for the console.  Kept close to the operator's mental model
// ("Loading Tool", "Idle - Tool Loaded", "Spindle On", ...).
static const char* machineStateName(MachineState s) {
    switch (s) {
        case MachineState::Booting:          return "Booting";
        case MachineState::Homing:           return "Homing";
        case MachineState::IdleToolLoaded:   return "Idle - Tool Loaded";
        case MachineState::IdleToolUnloaded: return "Idle - Tool Unloaded";
        case MachineState::LoadingTool:      return "Loading Tool";
        case MachineState::UnloadingTool:    return "Unloading Tool";
        case MachineState::SpindleRunning:   return "Spindle On";
        case MachineState::Calibrating:      return "Calibrating";
        case MachineState::Fault:            return "Fault";
        case MachineState::OtaUpdate:        return "OTA Update";
    }
    return "?";
}

// Change the reported state, printing "state: <old> -> <new>" to the ESP3D console (and USB)
// on every transition.  A no-op when the state is unchanged, so it is safe to call each loop.
static void setMachineState(MachineState s) {
    if (s == g_machine_state) {
        return;
    }
    MachineState prev = g_machine_state;
    g_machine_state = s;
    reportEvent("MSG", "state: %s -> %s", machineStateName(prev), machineStateName(s));
}

static float        g_z_homing_start_phase = 0.0f;
static float        g_z_load_start_phase   = 0.0f;  // phase at the start of a tool-load/remove move
static bool         g_beam_prev_blocked    = false; // previous beam state, for edge detection
static bool         g_tool_load_confirming = false; // beam has cleared; timing the load confirm window
static uint32_t     g_tool_load_clear_since = 0;  // millis() when the beam last became clear (loading)
static bool         g_tool_remove_confirming = false; // beam has cleared; timing the confirm window
static uint32_t     g_tool_remove_clear_since = 0;  // millis() when the beam last became clear
static bool         g_tool_remove_seen_blocked = false; // beam has been interrupted during this removal
static bool         g_tool_loaded       = false;  // set by homing: true once the beam confirms a tool
static bool         g_z_homed           = false;  // true once power-up homing has completed

// The detector output is active-low for a clear path: it reads 1 when the beam is
// interrupted (BLOCKED) and 0 when the beam reaches the detector (CLEAR).
static inline bool beamBlocked() {
    return digitalRead(BEAM_DETECT_PIN) != 0;
}

// Enable both Z motors in open-loop angle mode (if not already enabled) so a phase-offset
// move can drive them.  Used by every state that starts a Z move.
static void enableZMotors() {
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

// Finish a Z move: power the drivers down so the motors are not left energized (locked in
// position with the cooling fan running) waiting for the XY board to report idle - which does
// not happen while it sits in its boot alarm.  Redefine the stopping point as the phase-offset
// zero so it matches the XY board's Z home (setting current and target together applies no
// motor delta - see updatePhaseOffset), arm beam edge detection at the current level, and tell
// the XY board to reset its Z machine position to match this new zero.
static void finishZMove() {
    mc1.disable();
    mc2.disable();
    zeroPhaseReference();                    // coherent phase + motor-angle zero
    g_beam_prev_blocked = beamBlocked();     // arm edge detection at the current level
    notifyZHomed();                          // XY board resets its Z position to match this zero
}

// Begin the power-up / re-home decision.  Entered from the Booting state once OTA,
// calibration and faults are all clear.
static void beginHoming() {
    // Already at the top of travel (beam interrupted): we are home and a tool is loaded, so
    // complete without moving.
    if (beamBlocked()) {
        g_tool_loaded = true;
        g_z_homed     = true;
        g_tool_state  = MachineState::IdleToolLoaded;
        reportEvent("MSG", "Z homed: top-of-travel beam already interrupted, tool loaded");
        finishZMove();
        return;
    }
    // Begin raising: command the phase offset all the way to the travel limit.  The global
    // phase ramp moves us there at PHASE_OFFSET_RAMP_RATE; we freeze the instant the beam
    // breaks.
    g_z_homing_start_phase = phase_offset.current;
    phase_offset.target    = phase_offset.current + Z_HOMING_PHASE_DIR * Z_HOMING_MAX_RAD;
    enableZMotors();
    g_tool_state = MachineState::Homing;
    reportEvent("MSG", "Z homing: raising to find the top-of-travel beam");
}

// Begin a tool removal (the XY board's 'R' command / web UI "Remove Tool" button): raise the
// Z to eject the tool.  A loaded tool usually sits at the top interrupting the beam, but it
// may have been left below the beam (undetected), so the beam state alone cannot tell us
// whether a tool is present.  Always raise: if the beam is interrupted and then clears, the
// tool has been ejected; if the full travel passes without the beam ever breaking, there was
// no tool to remove.
static void beginToolRemoval() {
    g_z_load_start_phase       = phase_offset.current;
    phase_offset.target        = phase_offset.current + Z_HOMING_PHASE_DIR * Z_TOOL_REMOVE_MAX_RAD;
    g_beam_prev_blocked        = beamBlocked();
    g_tool_remove_confirming   = false;          // no clearance confirmation window open yet
    g_tool_remove_seen_blocked = beamBlocked();  // has the tool interrupted the beam yet?
    enableZMotors();
    g_tool_state = MachineState::UnloadingTool;
    reportEvent("MSG", "Tool removal: raising Z to eject any loaded tool");
}

// The tool sub-machine: Homing, Idle (loaded / unloaded), Loading and Unloading.  Runs once
// per control loop.  Each transition here changes g_tool_state; updateReportedState() (called
// later in the loop) turns that into the single reported state and logs it to the console.
static void updateToolStateMachine() {
    // A re-home request (the XY board's 'G' command, e.g. from the web UI test button)
    // restarts the cycle from the top.
    if (g_home_requested) {
        g_home_requested = false;
        g_tool_state = MachineState::Booting;  // the homing decision runs below, after the guards
        reportEvent("MSG", "Z home requested (ota=%d cal=%d fault=%d beam=%d)",
                    (int)g_ota_active, (int)calibration.isActive(), (int)g_fault_code, (int)beamBlocked());
    }

    // Never home or move a tool while OTA, calibration, or a latched fault is in progress.
    if (g_ota_active || calibration.isActive() || g_fault_code != 0) {
        return;
    }

    // A tool removal request preempts whatever idle/homing state we are in.
    if (g_remove_tool_requested) {
        g_remove_tool_requested = false;
        beginToolRemoval();
        return;
    }

    switch (g_tool_state) {
        case MachineState::Booting:
            // Give the shared 24V rail and the other boards time to come up before the homing
            // raise energizes the motors.  Only the initial boot homing is delayed; an operator
            // re-home happens long after boot so millis() is already past the threshold.
            if (millis() < BOOT_HOMING_DELAY_MS) {
                break;
            }
            beginHoming();
            break;

        case MachineState::Homing: {
            if (beamBlocked()) {
                // Reached the top: stop here.  This is home and a tool is loaded.
                phase_offset.target = phase_offset.current;
                g_tool_loaded = true;
                g_z_homed     = true;
                g_tool_state  = MachineState::IdleToolLoaded;
                reportEvent("MSG", "Z homed: top-of-travel beam interrupted, tool loaded");
                finishZMove();
            } else if (fabsf(phase_offset.current - g_z_homing_start_phase) >= Z_HOMING_MAX_RAD) {
                // Travelled the full limit without breaking the beam: no tool is loaded.  Drop
                // into beam monitoring (Idle - Tool Unloaded) so a tool can be loaded on demand.
                phase_offset.target = phase_offset.current;
                g_tool_loaded = false;
                g_z_homed     = true;
                g_tool_state  = MachineState::IdleToolUnloaded;
                reportEvent("WARN", "Z homing: no beam within %.0fmm - no tool loaded", Z_HOMING_MAX_MM);
                finishZMove();
            }
            break;
        }

        case MachineState::IdleToolUnloaded: {
            // No tool is loaded.  Watch the top-of-travel beam; when the operator inserts a
            // tool it interrupts the beam.  Trigger the loading move only on a clear->blocked
            // edge so a beam that is still blocked after an aborted attempt does not drive the
            // Z down again on its own.
            bool blocked = beamBlocked();
            if (blocked && !g_beam_prev_blocked) {
                // Begin lowering (the reverse of homing): command the phase offset toward the
                // travel limit in the opposite direction, and freeze when the beam clears.
                g_z_load_start_phase = phase_offset.current;
                phase_offset.target  = phase_offset.current - Z_HOMING_PHASE_DIR * Z_TOOL_LOAD_MAX_RAD;
                g_tool_load_confirming = false;  // no clearance confirmation window open yet
                enableZMotors();
                g_tool_state = MachineState::LoadingTool;
                reportEvent("MSG", "Tool loading: beam interrupted, lowering Z until the beam clears");
            }
            g_beam_prev_blocked = blocked;
            break;
        }

        case MachineState::LoadingTool: {
            // Keep lowering until the beam has stayed clear for Z_TOOL_LOAD_CONFIRM_MS.  As the
            // operator inserts the tool the beam can flicker; completing on the first momentary
            // clear would report the tool loaded before the motors have actually pulled it down
            // and seated it, so any re-interruption of the beam restarts the confirm window.
            bool blocked   = beamBlocked();
            bool hit_limit = fabsf(phase_offset.current - g_z_load_start_phase) >= Z_TOOL_LOAD_MAX_RAD;

            if (blocked) {
                // Tool still interrupting the beam (or flickering as it is inserted): keep
                // lowering and cancel any pending clearance-confirmation window.
                g_tool_load_confirming = false;
                if (hit_limit) {
                    // Lowered the full limit without the beam clearing: abort and go back to
                    // monitoring.  Edge detection (armed by finishZMove) keeps the Z from
                    // immediately lowering again while the beam stays blocked.
                    phase_offset.target = phase_offset.current;
                    g_tool_loaded = false;
                    g_tool_state  = MachineState::IdleToolUnloaded;
                    reportEvent("WARN", "Tool loading: beam still blocked after %.0fmm - aborting", Z_TOOL_LOAD_MAX_MM);
                    finishZMove();
                }
            } else {
                // Beam clear: the tool may have seated.  Keep lowering while we confirm the
                // beam stays clear for the full window before declaring the tool loaded.
                if (!g_tool_load_confirming) {
                    g_tool_load_confirming  = true;
                    g_tool_load_clear_since = millis();
                }
                if ((millis() - g_tool_load_clear_since) >= Z_TOOL_LOAD_CONFIRM_MS || hit_limit) {
                    // Beam stayed clear for the full window (or we ran out of travel while
                    // clear): the tool has seated.  Stop here; a tool is now loaded.
                    phase_offset.target = phase_offset.current;
                    g_tool_loaded = true;
                    g_tool_state  = MachineState::IdleToolLoaded;
                    reportEvent("MSG", "Tool loaded: beam clear for %ums", (unsigned)Z_TOOL_LOAD_CONFIRM_MS);
                    finishZMove();
                }
            }
            break;
        }

        case MachineState::UnloadingTool: {
            // Raising to eject the loaded tool.  We keep raising until the beam has stayed
            // clear for Z_TOOL_REMOVE_CONFIRM_MS; a tool sitting right on the edge of the beam
            // can flicker, so any re-break of the beam restarts that confirmation window.
            bool blocked   = beamBlocked();
            bool hit_limit = fabsf(phase_offset.current - g_z_load_start_phase) >= Z_TOOL_REMOVE_MAX_RAD;

            if (blocked) {
                // Tool still interrupting the beam (or flickering on its edge): keep raising
                // and reset the clearance-confirmation window.
                g_tool_remove_seen_blocked = true;   // a tool has entered the beam
                g_tool_remove_confirming   = false;
                if (hit_limit) {
                    // Raised the full limit with the beam still blocked: give up and stop.
                    // The tool is still considered loaded since it never cleared the beam.
                    phase_offset.target = phase_offset.current;
                    g_tool_state = MachineState::IdleToolLoaded;
                    reportEvent("WARN", "Tool removal: beam still blocked after %.0fmm - stopping", Z_TOOL_REMOVE_MAX_MM);
                    finishZMove();
                }
            } else if (!g_tool_remove_seen_blocked) {
                // Beam still clear and never interrupted during this removal.  Either the tool
                // is below the beam (keep raising it up and out) or there is no tool at all.
                // Only conclude "no tool" once the full travel is exhausted.
                if (hit_limit) {
                    phase_offset.target = phase_offset.current;
                    g_tool_loaded = false;
                    g_tool_state  = MachineState::IdleToolUnloaded;
                    reportEvent("MSG", "Tool removal: no tool detected within %.0fmm - already unloaded", Z_TOOL_REMOVE_MAX_MM);
                    finishZMove();
                }
                // else keep raising, still searching for the tool
            } else {
                // The tool has already interrupted the beam and the beam is now clear: open
                // (or continue) the confirmation window while still raising.
                if (!g_tool_remove_confirming) {
                    g_tool_remove_confirming  = true;
                    g_tool_remove_clear_since = millis();
                }
                if ((millis() - g_tool_remove_clear_since) >= Z_TOOL_REMOVE_CONFIRM_MS || hit_limit) {
                    // Beam stayed clear for the full window (or we ran out of travel while
                    // clear): the tool has been removed.  Return to monitoring.
                    phase_offset.target = phase_offset.current;
                    g_tool_loaded = false;
                    g_tool_state  = MachineState::IdleToolUnloaded;
                    reportEvent("MSG", "Tool removed: beam clear for %ums", (unsigned)Z_TOOL_REMOVE_CONFIRM_MS);
                    finishZMove();
                }
            }
            break;
        }

        case MachineState::IdleToolLoaded:
        default:
            // Homed with a tool loaded: nothing to do.  The Z follows the XY board.
            break;
    }
}

// True while the spindle motors are spinning (or spinning down).  velocity_mode /
// continuous_rotation stay set until the ramp reaches 0 rpm, so this reliably tracks
// "Spindle On" for the reported state.
static inline bool spindleSpinning() {
    return mc1.velocity_mode || mc2.velocity_mode ||
           mc1.continuous_rotation || mc2.continuous_rotation;
}

// Reconcile the single reported state each loop.  The tool sub-machine (g_tool_state) is the
// baseline; the higher-priority overlays (OTA > Fault > Calibrating > Spindle On) are layered
// on top.  setMachineState() logs "state: <old> -> <new>" whenever the reported state changes.
static void updateReportedState() {
    MachineState reported;
    if (g_ota_active) {
        reported = MachineState::OtaUpdate;
    } else if (g_fault_code != 0) {
        reported = MachineState::Fault;
    } else if (calibration.isActive()) {
        reported = MachineState::Calibrating;
    } else if (spindleSpinning()) {
        reported = MachineState::SpindleRunning;
    } else {
        reported = g_tool_state;
    }
    setMachineState(reported);
}

static void motorControlTask(void* arg) {
    (void)arg;
    uint32_t last_ramp_time = millis();

    for (;;) {
        // While OTA is active (WiFi/OTA runs on core 0), keep the motor drivers off and
        // stand the control loop down.  This starts as soon as OTA is requested - before the
        // WiFi radio powers up - so the drivers' current draw can't sag the rail during the
        // radio's start-up surge.  The board reboots into the new firmware when an update
        // completes, so we never resume from here in that case.
        if (g_ota_active) {
            setMachineState(MachineState::OtaUpdate);  // announce the transition before we park
            mc1.disable();
            mc2.disable();
            // Signal otaTask that we are parked here and no longer touching the ADC, so it
            // is now safe to bring the WiFi radio up (see the handshake in ota_service.cpp).
            g_motor_in_ota_standby = true;
            last_ramp_time         = millis();
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        g_motor_in_ota_standby = false;

        uint32_t current_time = millis();
        float dt = (current_time - last_ramp_time) / 1000.0f;
        last_ramp_time = current_time;

        checkMotorTimeouts();
        checkPhaseHoldPowerdown();
        updateFaultRecovery();
        checkDRV8316Faults();
        checkOvercurrent();
        reportSpinupTelemetry();
        calibration.update(mc1, mc2);

        // Tool state machine: power-up homing, tool loading/unloading via the top-of-travel
        // beam.  Runs before the phase ramp so its commanded phase target is applied this
        // iteration.  The reported state (with the spindle/fault/cal/OTA overlays) is
        // reconciled and logged at the end of the loop by updateReportedState().
        updateToolStateMachine();

        bool cal_active = calibration.isActive() &&
                          calibration.state != CAL_RAMP_DOWN &&
                          calibration.state != MCAL_RAMP_DOWN;
        // While the relative phase is changing (Z axis moving), give the motors an extra
        // volt of headroom to overcome the Z drive's mechanical resistance.
        float z_move_boost = (fabsf(phase_offset.target - phase_offset.current) > PHASE_MOVE_COMPLETE_EPS_RAD)
                                 ? Z_MOVE_VOLTAGE_BOOST
                                 : 0.0f;
        mc1.applyVoltageLimit(cal_active && calibration.active_motor_idx == 0, calibration.hunt_voltage, z_move_boost);
        mc2.applyVoltageLimit(cal_active && calibration.active_motor_idx == 1, calibration.hunt_voltage, z_move_boost);

        // Spindle spin-up/down uses a fast ramp; calibration keeps its slower, settled
        // ramp so per-checkpoint current measurements stay accurate.  Outside calibration the
        // low-speed region is accelerated gently (SPINDLE_RAMP_LOWSPEED_RATE) so the open-loop
        // rotor stays synchronized and does not spike phase current into the DRV8316 OCP; once
        // above SPINDLE_RAMP_LOWSPEED_RAD back-EMF holds it in sync and the faster rate is used.
        bool cal = calibration.isActive();
        float ramp_fast = cal ? VELOCITY_RAMP_RATE : SPINDLE_RAMP_RATE;
        float rr1 = (!cal && fabsf(mc1.current_velocity) < SPINDLE_RAMP_LOWSPEED_RAD)
                        ? SPINDLE_RAMP_LOWSPEED_RATE : ramp_fast;
        float rr2 = (!cal && fabsf(mc2.current_velocity) < SPINDLE_RAMP_LOWSPEED_RAD)
                        ? SPINDLE_RAMP_LOWSPEED_RATE : ramp_fast;
        mc1.rampVelocity(dt, rr1);
        mc2.rampVelocity(dt, rr2);
        applyFanForMotorState(mc1.enabled || mc2.enabled);
        updateFanControl(dt);
        updatePhaseOffset(dt);

        mc1.updateControlMode();
        mc2.updateControlMode();

        mc1.runMotorLoop();
        mc2.runMotorLoop();

        mc1.updateCurrent();
        mc2.updateCurrent();

        calibration.accumulateCurrentSample(mc1.last_instantaneous_current, 0);
        calibration.accumulateCurrentSample(mc2.last_instantaneous_current, 1);

        handleSerialCommands(mc1, mc2, calibration);

        // Reconcile the single reported state now that this iteration's tool state, faults,
        // calibration and spindle commands have all been applied, logging any transition.
        updateReportedState();

        // Report status to the XY board over the inter-board link
        static uint32_t last_status_time = 0;
        // Only send when the whole status line already fits the TX buffer, so this write (which
        // shares the task with loopFOC) can never block and stall the FOC loop.  If the buffer is
        // momentarily full we skip and retry next iteration - status is periodic and advisory.
        if (current_time - last_status_time >= LINK_STATUS_INTERVAL_MS &&
            Serial1.availableForWrite() >= 40) {
            last_status_time = current_time;
            sendStatus(Serial1, mc1, mc2);
        }

        taskYIELD();
    }
}

// ------------------- Setup & Loop -------------------

void setup() {
    // Disable the hardware brownout detector as the very first thing the app does. If the
    // board's 3.3V rail dips briefly when 24V is applied (inrush charging the 24V bulk caps
    // and the DRV8316 drivers), a brownout reset can put the chip into a reset loop so the
    // application never runs unless USB's stiff 5V holds the rail up. Clearing this register
    // stops brownout-triggered resets. NOTE: this only helps if the chip actually reaches
    // this line; a sag during the first few ms of power-on (before the app starts) is a
    // pure hardware problem this cannot fix.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // Reset the SAR-ADC peripheral before anything reads it, as boot hygiene.  If a
    // WiFi/OTA session was interrupted it can leave the ADC controller mid-conversion;
    // because adc1_get_raw() polls for a "done" flag inside a critical section, a wedged
    // controller makes the first analogRead() spin forever with interrupts disabled and
    // trips the interrupt watchdog.  This resets the digital ADC controller (the RTC-domain
    // portion only fully clears on a physical power cycle, so a deep wedge still needs one).
    periph_module_reset(PERIPH_SARADC_MODULE);

    // DIAGNOSTIC + safety: force the fan OFF as the very first action, before Serial, the
    // boot delay, or any other init. The fan output is hardware-default ON, so this line
    // is a visible "is setup() running?" indicator: if the board boots WITHOUT USB and the
    // fan goes off immediately, setup() is executing (the fault is later); if it stays on,
    // the chip never reaches setup().
    pinMode(FAN_PWM_PIN, OUTPUT);
    digitalWrite(FAN_PWM_PIN, LOW);

    // Z-axis homing beam break: drive the IR LED emitter on and read the detector.
    // The LED is held on continuously so the beam is active; the power-up homing routine
    // in motorControlTask (updateToolStateMachine) samples the detector while it raises the
    // Z to find the top-of-travel beam.
    pinMode(BEAM_LED_PIN, OUTPUT);
    digitalWrite(BEAM_LED_PIN, HIGH);
    pinMode(BEAM_DETECT_PIN, INPUT);

    Serial.begin(115200);
    // Serial here is the USB Serial/JTAG CDC (ARDUINO_USB_MODE=1, ARDUINO_USB_CDC_ON_BOOT=1).
    // With no USB host attached its TX FIFO never drains, so writing to it during boot can
    // stall the board. We therefore (1) keep a 0 ms TX timeout as a backstop, (2) do ALL
    // functional bring-up before producing any console output, and (3) gate every console
    // write on (bool)Serial, which is true only when a host is actually connected. The result
    // is that the board boots and runs identically whether or not USB is connected at boot.
    Serial.setTxTimeoutMs(0);

    // Record why we last reset, so unexpected reboots (e.g. a supply sag when the motors
    // surge against mechanical resistance) can be distinguished from panics/watchdogs.
    // NOTE: the hardware brownout detector was disabled above (RTC_CNTL_BROWN_OUT_REG=0),
    // so a genuine rail sag on THIS board shows up as "power-on" rather than "brownout";
    // the XY board (detector still enabled) is the one that will report a true brownout.
    // Printed unconditionally (like the SimpleFOC MOT lines) so it is captured over USB.
    esp_reset_reason_t reset_reason = esp_reset_reason();
    const char*        reset_str;
    switch (reset_reason) {
        case ESP_RST_POWERON:   reset_str = "power-on"; break;
        case ESP_RST_EXT:       reset_str = "external pin"; break;
        case ESP_RST_SW:        reset_str = "software"; break;
        case ESP_RST_PANIC:     reset_str = "panic/exception"; break;
        case ESP_RST_INT_WDT:   reset_str = "interrupt watchdog"; break;
        case ESP_RST_TASK_WDT:  reset_str = "task watchdog"; break;
        case ESP_RST_WDT:       reset_str = "other watchdog"; break;
        case ESP_RST_BROWNOUT:  reset_str = "brownout (supply sag)"; break;
        case ESP_RST_DEEPSLEEP: reset_str = "deep-sleep wake"; break;
        default:                reset_str = "unknown"; break;
    }
    Serial.printf("RESET REASON: %s (%d)\n", reset_str, (int)reset_reason);

    // Bring up the inter-board link to the FluidNC XY board first (RX=GPIO39, TX=GPIO38)
    // so it is listening as early as possible, before the XY board finishes booting and
    // sends its handshake.
    // Enlarge the TX ring buffer so status/telemetry lines are always accepted without the
    // write blocking (the FOC loop shares this task, see reportEvent/sendStatus).  Must be
    // called before begin().
    Serial1.setTxBufferSize(512);
    Serial1.begin(LINK_BAUD, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);

    // Report the reset reason to the XY board so it appears in the ESP3D web console too.
    reportEvent("MSG", "spindle reset reason: %s", reset_str);

    // --- Functional bring-up: none of this depends on a USB host being present ---
    delay(3000);   // let the 24V supply / gate drivers settle before enabling them

    initFanControl();   // drives the fan OFF (its hardware default is ON)

    // Initialize SPI bus
    drvSPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SPI_CS_PIN);

    // Initialize both motor drivers and motors
    mc1.initDriver(&drvSPI);
    mc2.initDriver(&drvSPI);
    mc1.initMotor();
    mc2.initMotor();

    // Load pre-measured calibration LUT data
    loadDefaultLUT(mc1, 0, MC1_DEFAULT_LUT, sizeof(MC1_DEFAULT_LUT) / sizeof(MC1_DEFAULT_LUT[0]));
    loadDefaultLUT(mc2, 1, MC2_DEFAULT_LUT, sizeof(MC2_DEFAULT_LUT) / sizeof(MC2_DEFAULT_LUT[0]));

    // Start the real-time control task. Once created it runs independently on core 1, so
    // nothing after this point can affect motor / Z / fan control.
    xTaskCreatePinnedToCore(motorControlTask, "motorControl", 8192, nullptr, 3, &motor_control_task_handle, 1);

    // --- Console banner LAST, and only when a USB host is actually attached. ---
    if (Serial) {
        Serial.println(F("\n=== ESP32-S3 + DRV8316 (SPI + 6-PWM) + Hall + SimpleFOC ==="));
        Serial.printf("Inter-board link ready on Serial1 (RX=GPIO%d, TX=GPIO%d, %d baud, 8N1)\n",
                      LINK_RX_PIN, LINK_TX_PIN, LINK_BAUD);
        Serial.println(F("Waiting for handshake ('H') from XY board over the link..."));
        mc1.printFaultStatus();
        Serial.println(F("Motor 1 initialized for open-loop control"));
        Serial.println(F("Motor 2 initialized for open-loop control (opposite direction)"));
        printCommandHelp();
        Serial.println(F("Motors ready. Send a velocity command to start."));
        Serial.println(F("Active motor: 1 (use 'q', 'w', or 'e' to switch)"));
    }
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

