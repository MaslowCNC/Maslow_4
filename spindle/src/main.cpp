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
#include "trip_recorder.h"
#include "load_sense.h"
#include "usb_console.h"
#include "esp32s3/rom/rtc.h"
#include "esp_task_wdt.h"
#include "soc/usb_serial_jtag_struct.h"   // rtc_get_reset_reason: the chip-level reset cause

static void reportResetReasonOnce();  // defined with setup() below
static void reportLoadWarnings();     // defined with the fault monitoring below
static void watchUsbOutput();         // defined with setup() below

// ------------------- Hardware Objects -------------------

// MP6541A: independent HS/LS inputs, driven as plain 6-PWM.  The drivers have no enable pin
// of their own - the shared nSLEEP line is managed by MotorController::enable()/disable().
BLDCMotor motor1_hw(POLE_PAIRS);
BLDCDriver6PWM driver1_hw(INHA, INLA, INHB, INLB, INHC, INLC);

BLDCMotor motor2_hw(POLE_PAIRS);
BLDCDriver6PWM driver2_hw(INHA2, INLA2, INHB2, INLB2, INHC2, INLC2);

// ------------------- Motor Controllers -------------------
// direction: the two motors always run in opposite senses; SPINDLE_DIRECTION (config.h) flips
// both together to reverse the spindle.

MotorController mc1(motor1_hw, driver1_hw, CURA, CURB, CURC, +1 * SPINDLE_DIRECTION);
MotorController mc2(motor2_hw, driver2_hw, CURA2, CURB2, CURC2, -1 * SPINDLE_DIRECTION);

Calibration calibration;
static TaskHandle_t motor_control_task_handle = nullptr;
static TaskHandle_t housekeeping_task_handle  = nullptr;

// ------------------- Calibration LUT Default Data -------------------
// Per-motor open-loop voltages from the MP6541A board's auto-calibration (CAL) sweep, 100-18000 RPM
// in 100 RPM steps, hunted to a MEASURED I_phase_rms of CAL_TARGET_CURRENT (2.625 A).  Copied from
// the board's stored NVS calibration on 2026-10-05.  Loaded as the seed LUT at boot; a stored NVS
// calibration, if present, overrides them.
//
// Note the top of both tables: M1 reaches the 24 V bus by ~17000 RPM and M2 is at 23.3 V by 18000,
// i.e. deep into SinePWM over-modulation.  A trip recording at ~17700 RPM showed M1 tripping the
// driver's hardware over-current there.  These entries were measured with SinePWM and a 24 V
// ceiling; applyVoltageLimit() now clamps them to MAX_VOLTAGE (13.86 V, the SpaceVectorPWM limit).

static const float MC1_DEFAULT_LUT[] = {
    2.386f, 2.589f, 2.622f, 2.635f, 2.643f, 2.649f, 2.712f, 2.718f, 2.725f, 2.809f,   // 100-1000
    2.820f, 2.906f, 2.948f, 3.013f, 3.030f, 3.098f, 3.092f, 3.179f, 3.230f, 3.307f,   // 1100-2000
    3.345f, 3.442f, 3.488f, 3.542f, 3.581f, 3.653f, 3.738f, 3.741f, 3.819f, 3.894f,   // 2100-3000
    4.040f, 4.073f, 4.138f, 4.185f, 4.263f, 4.312f, 4.412f, 4.472f, 4.551f, 4.623f,   // 3100-4000
    4.688f, 4.780f, 4.831f, 4.893f, 5.020f, 5.059f, 5.112f, 5.240f, 5.301f, 5.302f,   // 4100-5000
    5.435f, 5.501f, 5.599f, 5.626f, 5.754f, 5.819f, 5.924f, 5.958f, 6.024f, 6.085f,   // 5100-6000
    6.191f, 6.308f, 6.365f, 6.428f, 6.511f, 6.597f, 6.697f, 6.742f, 6.818f, 6.930f,   // 6100-7000
    7.009f, 7.092f, 7.099f, 7.240f, 7.273f, 7.387f, 7.444f, 7.489f, 7.618f, 7.717f,   // 7100-8000
    7.789f, 7.863f, 7.952f, 8.031f, 8.087f, 8.194f, 8.234f, 8.370f, 8.367f, 8.441f,   // 8100-9000
    8.586f, 8.656f, 8.661f, 8.795f, 8.901f, 8.961f, 9.027f, 9.020f, 9.190f, 9.401f,   // 9100-10000
    9.380f, 9.392f, 9.479f, 9.642f, 9.656f, 9.818f, 9.835f, 9.951f, 10.018f, 10.098f,   // 10100-11000
    10.145f, 10.221f, 10.311f, 10.449f, 10.506f, 10.587f, 10.637f, 10.724f, 10.833f, 10.937f,   // 11100-12000
    10.961f, 11.058f, 11.107f, 11.201f, 11.316f, 11.377f, 11.442f, 11.521f, 11.613f, 11.683f,   // 12100-13000
    11.737f, 11.787f, 11.931f, 11.993f, 12.112f, 12.215f, 12.314f, 12.427f, 12.504f, 12.657f,   // 13100-14000
    12.770f, 12.920f, 13.045f, 13.197f, 13.418f, 13.465f, 13.547f, 13.836f, 14.109f, 14.250f,   // 14100-15000
    14.552f, 14.620f, 15.014f, 15.445f, 15.592f, 15.711f, 16.249f, 16.584f, 17.112f, 17.459f,   // 15100-16000
    17.855f, 18.389f, 18.719f, 19.370f, 19.991f, 20.690f, 21.564f, 22.213f, 23.076f, 23.976f,   // 16100-17000
    24.000f, 24.000f, 24.000f, 24.000f, 24.000f, 24.000f, 24.000f, 24.000f, 24.000f, 24.000f,   // 17100-18000
};

static const float MC2_DEFAULT_LUT[] = {
    2.685f, 2.750f, 2.745f, 2.815f, 2.822f, 2.867f, 2.914f, 2.926f, 2.918f, 2.939f,   // 100-1000
    3.080f, 3.077f, 3.175f, 3.204f, 3.233f, 3.261f, 3.267f, 3.355f, 3.396f, 3.471f,   // 1100-2000
    3.472f, 3.544f, 3.586f, 3.650f, 3.661f, 3.783f, 3.803f, 3.866f, 3.918f, 3.967f,   // 2100-3000
    4.027f, 4.109f, 4.109f, 4.187f, 4.289f, 4.297f, 4.369f, 4.454f, 4.544f, 4.568f,   // 3100-4000
    4.654f, 4.730f, 4.797f, 4.797f, 4.914f, 4.954f, 5.039f, 5.139f, 5.195f, 5.243f,   // 4100-5000
    5.294f, 5.366f, 5.479f, 5.514f, 5.643f, 5.647f, 5.751f, 5.838f, 5.878f, 5.911f,   // 5100-6000
    6.037f, 6.110f, 6.222f, 6.260f, 6.299f, 6.381f, 6.486f, 6.505f, 6.583f, 6.646f,   // 6100-7000
    6.718f, 6.804f, 6.902f, 6.962f, 7.002f, 7.115f, 7.167f, 7.249f, 7.348f, 7.397f,   // 7100-8000
    7.488f, 7.507f, 7.635f, 7.731f, 7.762f, 7.808f, 7.922f, 7.960f, 8.069f, 8.107f,   // 8100-9000
    8.229f, 8.282f, 8.312f, 8.433f, 8.486f, 8.574f, 8.667f, 8.664f, 8.813f, 8.831f,   // 9100-10000
    8.925f, 9.028f, 9.054f, 9.166f, 9.226f, 9.292f, 9.391f, 9.388f, 9.509f, 9.609f,   // 10100-11000
    9.679f, 9.716f, 9.835f, 9.889f, 9.984f, 10.013f, 10.103f, 10.228f, 10.260f, 10.394f,   // 11100-12000
    10.416f, 10.471f, 10.576f, 10.642f, 10.710f, 10.754f, 10.872f, 10.913f, 10.980f, 11.087f,   // 12100-13000
    11.146f, 11.239f, 11.275f, 11.329f, 11.445f, 11.534f, 11.611f, 11.695f, 11.777f, 11.816f,   // 13100-14000
    11.863f, 11.971f, 11.996f, 12.191f, 12.195f, 12.417f, 12.439f, 12.532f, 12.580f, 12.739f,   // 14100-15000
    12.751f, 13.003f, 13.005f, 13.326f, 13.396f, 13.565f, 13.744f, 13.971f, 14.173f, 14.395f,   // 15100-16000
    14.625f, 14.789f, 15.066f, 15.323f, 15.705f, 15.997f, 16.403f, 16.645f, 17.052f, 17.323f,   // 16100-17000
    17.625f, 18.341f, 18.689f, 19.485f, 20.190f, 20.750f, 21.379f, 22.279f, 22.436f, 23.336f,   // 17100-18000
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
static int overcurrent_count = 0;

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
    // applies a coarse voltage step that spikes phase current and trips the driver's hardware
    // over-current protection.  So write only when the whole line already fits in the TX
    // buffer; otherwise drop it - these link/USB messages are advisory and a fresh status or
    // telemetry line follows shortly.  This keeps inter-board serial traffic from disturbing FOC.
    char line[128];
    int n = snprintf(line, sizeof(line), "%s:%s\n", level, buf);
    if (n <= 0) return;
    if (n > (int)sizeof(line)) n = (int)sizeof(line);

    if (Serial1.availableForWrite() >= n) {
        Serial1.write(reinterpret_cast<const uint8_t*>(line), n);
    }
    usbWriteIfRoom(line, n);
}

// Tell the XY board that the spindle has (re)established its Z zero (phase offset 0) so it
// can reset its own Z-axis machine position to 0 and keep the two boards' Z origins aligned.
// Sent whenever homing, tool loading, or tool removal redefines the phase-offset zero.
static void notifyZHomed() {
    Serial1.println(F("ZHOMED"));
    if (Serial) Serial.println(F("[MSG] Z zero re-established -> notified XY board (ZHOMED)"));
}

// Defined with the tool state machine below: drop the Z reference and park the sub-machine in
// NeedsHoming, so nothing moves the Z until a homing cycle re-establishes the zero.
static void requireRehome();

// Stop for an over-current.  The motors run OPEN-LOOP, so an over-current means the rotor has
// already lost synchronisation with the commanded field - resuming the commanded speed cannot
// recover that, it just re-trips.  So come to rest and wait for the operator.
//
// Two things follow from the slip:
//   - fault code 2 is latched so the XY board raises its alarm and a running job stops.  It must
//     STAY latched: the XY board alarms only on a 0 -> non-zero transition.  A fresh speed
//     command (setSpindleSpeed) is the deliberate restart that clears it.
//   - the Z position IS the relative phase between the two motors, so a slip invalidates it.
//     Z targets are refused until a homing cycle re-establishes the zero.
static void stopForOverCurrent(const char* detail) {
    tripRecorderTrigger(detail);
    mc1.emergencyStop();
    mc2.emergencyStop();

    requireRehome();

    if (calibration.isActive()) {
        // A trip at the top of motor 1's auto sweep just marks the end of its usable range: the
        // sweep moves on to motor 2, so do not latch a fault (which would also alarm the XY board).
        if (calibration.handleMotorTrip(mc1, mc2, "over-current")) {
            reportEvent("WARN", "%s during calibration - motor 1 sweep ended, continuing with motor 2", detail);
            return;
        }
        g_fault_code = 2;
        reportEvent("ERR", "%s during calibration - aborting", detail);
        return;
    }

    g_fault_code = 2;
    reportEvent("WARN", "%s - spindle stopped; re-home the Z, then send a new speed", detail);
}

// --- nFAULT monitoring ---
// The MP6541A has no status registers.  Each driver has one open-drain nFAULT pin, and the two
// faults it reports are told apart by their shape:
//   over-current  - outputs off, automatic retry after ~2ms, so the pin produces a BURST of
//                   falling edges for as long as the overload lasts.  A 100ms poll would miss
//                   those 2ms pulses entirely, so the edges are counted in an ISR.
//   over-temp     - outputs off and the pin held LOW continuously until the die cools ~25C.
// Over-voltage and UVLO do NOT pull nFAULT low on this part, so they can no longer be seen
// from firmware (the driver still protects itself).
static volatile uint32_t nfault_edges1 = 0;
static volatile uint32_t nfault_edges2 = 0;

// Running totals for the trip recorder, which takes its own per-sample differences (the counters
// above are zeroed by checkDriverFaults every 100ms).
static volatile uint32_t nfault_total1 = 0;
static volatile uint32_t nfault_total2 = 0;

static void IRAM_ATTR onNFault1() { nfault_edges1++; nfault_total1++; }
static void IRAM_ATTR onNFault2() { nfault_edges2++; nfault_total2++; }

static void initFaultPins() {
    pinMode(DRV_NFAULT1, INPUT);   // 5.1k pull-up on the board
    pinMode(DRV_NFAULT2, INPUT);
    attachInterrupt(digitalPinToInterrupt(DRV_NFAULT1), onNFault1, FALLING);
    attachInterrupt(digitalPinToInterrupt(DRV_NFAULT2), onNFault2, FALLING);
}

// True while a driver is holding nFAULT low with no retry activity - i.e. a thermal shutdown.
// Only meaningful while the drivers are awake; a sleeping MP6541A releases the pin.
static bool drv_over_temp1 = false;
static bool drv_over_temp2 = false;

bool driverOverTemp(int motor_idx) {
    return (motor_idx == 0) ? drv_over_temp1 : drv_over_temp2;
}

static void checkDriverFaults() {
    if (!mc1.enabled && !mc2.enabled) {
        drv_over_temp1 = drv_over_temp2 = false;   // nFAULT says nothing with the motors off
        return;
    }
    if (millis() - last_drv_check < 100) return;
    last_drv_check = millis();

    // nFAULT only means anything once the drivers are awake and past their ~1ms start-up.
    if (!driversAwake() || (millis() - driversAwakeSince()) < 5) {
        nfault_edges1 = nfault_edges2 = 0;
        drv_over_temp1 = drv_over_temp2 = false;
        return;
    }

    uint32_t edges1 = nfault_edges1; nfault_edges1 = 0;
    uint32_t edges2 = nfault_edges2; nfault_edges2 = 0;
    bool low1 = (digitalRead(DRV_NFAULT1) == LOW);
    bool low2 = (digitalRead(DRV_NFAULT2) == LOW);

    // Retrying over-current: edges in this window (a pin found low with an edge is the same
    // event caught mid-retry).  Sustained low with no edges at all: thermal shutdown.
    bool ocp1 = (edges1 > 0);
    bool ocp2 = (edges2 > 0);
    drv_over_temp1 = low1 && (edges1 == 0);
    drv_over_temp2 = low2 && (edges2 == 0);

    ocp_consec_count1 = ocp1 ? (ocp_consec_count1 + 1) : 0;
    ocp_consec_count2 = ocp2 ? (ocp_consec_count2 + 1) : 0;

    serious_consec_count1 = drv_over_temp1 ? (serious_consec_count1 + 1) : 0;
    serious_consec_count2 = drv_over_temp2 ? (serious_consec_count2 + 1) : 0;

    bool persistent_ocp = (ocp_consec_count1 >= OCP_CONSEC_LIMIT) ||
                          (ocp_consec_count2 >= OCP_CONSEC_LIMIT);
    // While a calibration sweep is running, a thermal shutdown is the sweep's own business
    // (it coasts to cool and carries on) rather than a latching fault.
    bool serious = !calibration.isActive() &&
                   ((serious_consec_count1 >= SERIOUS_CONSEC_LIMIT) ||
                    (serious_consec_count2 >= SERIOUS_CONSEC_LIMIT));

    // Ride through a fault that has not yet persisted: the MP6541A clears and retries on its
    // own, and the consecutive counters above keep accumulating, so a genuinely persistent
    // over-current or sustained over-temperature still escalates below.
    if (!serious && !persistent_ocp) {
        if ((ocp1 || ocp2) && (ocp_consec_count1 == 1 || ocp_consec_count2 == 1)) {
            Serial.printf("nFAULT over-current transient (auto-retry): M1=%lu M2=%lu edges\n",
                          (unsigned long)edges1, (unsigned long)edges2);
        }
        return;
    }

    if (persistent_ocp) {
        Serial.printf("DRIVER OVER-CURRENT: M1=%lu M2=%lu retries in the last 100ms (500+ ms persistent)\n",
                      (unsigned long)edges1, (unsigned long)edges2);
    }
    if (serious) {
        Serial.printf("DRIVER THERMAL SHUTDOWN: nFAULT held low M1=%d M2=%d\n",
                      (int)drv_over_temp1, (int)drv_over_temp2);
    }

    ocp_consec_count1 = 0;
    ocp_consec_count2 = 0;

    // Over-current: the rotor has slipped, so stop rather than retry.  A sustained thermal
    // shutdown falls through to the latching path below.
    if (!serious && persistent_ocp) {
        stopForOverCurrent("driver over-current (nFAULT retries)");
        return;
    }

    // Confirmed thermal shutdown: latch and alarm the XY board.
    serious_consec_count1 = 0;
    serious_consec_count2 = 0;

    reportEvent("ERR", "driver over-temperature (nFAULT low) M1[%d] M2[%d]",
                (int)drv_over_temp1, (int)drv_over_temp2);

    tripRecorderTrigger("driver over-temperature (nFAULT held low)");
    g_fault_code = 1;  // driver hardware fault
    mc1.emergencyStop();
    mc2.emergencyStop();

    // A thermal shutdown cuts the outputs mid-rotation, so the open-loop rotor slips just as it
    // does on an over-current: the Z reference is no longer trustworthy either.
    requireRehome();

    calibration.abort(mc1, mc2, "driver hardware fault");
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

    stopForOverCurrent(detail);
}

// Advisory stall warning from the load-sense fit (load_sense.h): announce each change to the XY
// board (WARN:/MSG: lines appear in its console) and the USB console.  No protective action.
static void reportLoadWarnings() {
    static uint8_t prev = 0;
    uint8_t        mask = loadWarnMask();
    if (mask == prev) return;
    MotorController* motors[2] = { &mc1, &mc2 };
    for (int m = 0; m < 2; m++) {
        uint8_t bit = 1u << m;
        if ((mask & bit) && !(prev & bit)) {
            const LoadEstimate& e = loadEstimate(m);
            reportEvent("WARN", "load warning M%d: current phase %+.0f deg off no-load at %.0f RPM - stall risk",
                        m + 1, e.lag_dev, fabsf(motors[m]->current_velocity) * 60.0f / (2.0f * PI));
        } else if (!(mask & bit) && (prev & bit)) {
            reportEvent("MSG", "load warning M%d cleared", m + 1);
        }
    }
    prev = mask;
}

// Announce the load boost switching on and off (it holds and ramps out after warnings clear).
static void reportLoadBoost() {
    static bool was_on = false;
    bool        on     = loadBoostVolts() > 0.0f;
    if (on == was_on) return;
    was_on = on;
    if (on) reportEvent("MSG", "load boost on: +%.1f V on both motors", LOAD_BOOST_V);
    else    reportEvent("MSG", "load boost off");
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
// which heats the drivers and keeps the cooling fan running even though nothing is
// moving.  We wait for the phase ramp to reach its target here (rather than powering
// down the instant the XY board goes idle) so the Z axis is not released before it has
// finished its move.  A new spindle-speed or Z-target command clears the request.
static void checkPhaseHoldPowerdown() {
    if (!g_hold_release_requested) {
        return;
    }
    // Never release while the spindle is spinning, free-running, or calibrating.
    if (mc1.velocity_mode || mc2.velocity_mode || mc1.continuous_rotation || mc2.continuous_rotation ||
        calibration.isActive() || loadSenseBusy()) {
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
//   NeedsHoming        - an over-current slipped the rotor: the Z zero is no longer trusted
//                        and Z moves are refused until the operator runs a homing cycle
//   Fault              - a latched fault stopped the motors
//   OtaUpdate          - a WiFi/OTA firmware update is in progress
//
// The Homing / Idle* / Loading / Unloading states are the "tool" sub-machine driven by
// updateToolStateMachine() and tracked in g_tool_state.  SpindleRunning / Calibrating /
// Fault / OtaUpdate are overlays: updateReportedState() layers them on top each loop so the
// single reported state (g_machine_state) always reflects the board's real activity.
enum class MachineState : uint8_t {
    Booting,
    NeedsHoming,
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
        case MachineState::NeedsHoming:      return "Needs Homing";
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
    g_z_reference_valid = true;              // the phase offset means a real Z position again
    notifyZHomed();                          // XY board resets its Z position to match this zero
}

// Drop the Z reference after a slip.  Deliberately parks in NeedsHoming rather than Booting:
// Booting auto-starts a homing cycle after BOOT_HOMING_DELAY_MS, and the Z must not move on its
// own immediately after a fault - the operator (or the XY board's 'G') asks for it.
static void requireRehome() {
    g_z_reference_valid = false;
    g_tool_state        = MachineState::NeedsHoming;
}

// Begin the power-up / re-home decision.  Entered from the Booting state once OTA,
// calibration and faults are all clear.
static void beginHoming() {
    // If the beam starts interrupted, lower until it clears instead of driving farther up.
    // Reuse the loading state's clearance-confirmation window so beam flicker cannot stop the
    // move before the tool has moved fully below the detector.
    if (beamBlocked()) {
        g_z_load_start_phase   = phase_offset.current;
        phase_offset.target    = phase_offset.current - Z_HOMING_PHASE_DIR * Z_TOOL_LOAD_MAX_RAD;
        g_tool_load_confirming = false;
        enableZMotors();
        g_tool_state = MachineState::LoadingTool;
        reportEvent("MSG", "Z homing: beam already interrupted, lowering Z until the beam clears");
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

    // Never home or move a tool while OTA, calibration, or a HARD driver fault is in progress.
    // An over-current stop (code 2) deliberately does NOT block homing: re-homing is exactly how
    // the operator recovers the Z reference it invalidated.
    if (g_ota_active || calibration.isActive() || g_fault_code == 1) {
        return;
    }

    // A tool removal request preempts whatever idle/homing state we are in.
    if (g_remove_tool_requested) {
        g_remove_tool_requested = false;
        beginToolRemoval();
        return;
    }

    switch (g_tool_state) {
        case MachineState::NeedsHoming:
            // An over-current invalidated the Z zero.  Sit still - no beam watching, no tool
            // moves - until the operator runs a homing cycle ('G'), which re-enters Booting
            // above and re-establishes the reference.
            break;

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

// FOC loop timing for the trip recorder.  Written by the FOC task (core 1), read and reset by the
// housekeeping task (core 0) each sample.  The period is measured start-to-start, so it includes
// anything that held core 1 off between iterations (ISRs, preemption, an enable()'s tPUD wait).
static volatile uint32_t g_foc_loops      = 0;
static volatile uint32_t g_foc_max_period_us = 0;

// Ramp rate for one motor this FOC iteration (rad/s per second).
static float spinRampRate(const MotorController& mc) {
    if (calibration.isActive()) return VELOCITY_RAMP_RATE;
    return (fabsf(mc.current_velocity) >= SPINDLE_RAMP_SLOW_ABOVE_RAD) ? SPINDLE_RAMP_RATE_HIGH
                                                                       : SPINDLE_RAMP_RATE;
}

static void motorControlTask(void* arg) {
    (void)arg;
    uint32_t last_ramp_time = millis();
    uint32_t last_iter_us   = micros();

    for (;;) {
        // During OTA keep the drivers off and stand down.  The housekeeping task (which owns the
        // ADC) performs the WiFi-safe standby handshake; here we only need to stop driving so the
        // drivers' current draw can't sag the rail during the radio's start-up surge.  The board
        // reboots into the new firmware when an update completes, so we never resume from here.
        if (g_ota_active) {
            mc1.disable();
            mc2.disable();
            last_ramp_time = millis();
            vTaskDelay(pdMS_TO_TICKS(10));
            last_iter_us = micros();
            continue;
        }

        uint32_t iter_us = micros();
        uint32_t period  = iter_us - last_iter_us;
        last_iter_us     = iter_us;
        if (period > g_foc_max_period_us) g_foc_max_period_us = period;
        g_foc_loops++;

        uint32_t current_time  = millis();
        float    dt            = (current_time - last_ramp_time) / 1000.0f;
        last_ramp_time         = current_time;

        // ---- FOC hot path: the ONLY work on core 1, run every iteration for the smoothest,
        // most uniform open-loop commutation.  Current sensing, housekeeping, calibration and all
        // serial I/O live on the core-0 housekeeping task so nothing here can ever lengthen a
        // commutation step. ----
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

        // Spindle spin-up/down slows near the top of the range, where the drive is close to its
        // voltage ceiling; calibration keeps its slower, settled rate so per-checkpoint current
        // measurements stay accurate.
        mc1.rampVelocity(dt, spinRampRate(mc1));
        mc2.rampVelocity(dt, spinRampRate(mc2));
        updatePhaseOffset(dt);

        mc1.updateControlMode();
        mc2.updateControlMode();

        mc1.runMotorLoop();
        mc2.runMotorLoop();

        taskYIELD();
    }
}

// Take one trip-recorder sample of both motors.  Called right after the current update so the
// per-phase currents are fresh; the fault checks later in the same pass may then trigger it.
static void recordTripSample(uint32_t now_ms) {
    static uint32_t prev_nf_total[2] = { 0, 0 };

    TripSample s;
    s.t_ms = now_ms;
    // Take-and-reset the FOC counters.  The exchange is atomic, so no iteration is lost between
    // the read and the reset.
    uint32_t loops  = __atomic_exchange_n(&g_foc_loops, 0, __ATOMIC_RELAXED);
    uint32_t max_us = __atomic_exchange_n(&g_foc_max_period_us, 0, __ATOMIC_RELAXED);
    s.foc_loops  = (uint16_t)min<uint32_t>(loops, 65535);
    s.foc_max_us = (uint16_t)min<uint32_t>(max_us, 65535);
    s.phase_cur  = phase_offset.current;
    s.phase_tgt  = phase_offset.target;

    const MotorController* motors[2]   = { &mc1, &mc2 };
    const uint32_t         nf_total[2] = { nfault_total1, nfault_total2 };
    const int              nf_pin[2]   = { DRV_NFAULT1, DRV_NFAULT2 };
    for (int i = 0; i < 2; i++) {
        const MotorController& mc = *motors[i];
        TripMotorSample&       m  = s.m[i];
        m.rpm      = mc.current_velocity * 60.0f / (2.0f * PI);
        m.vlim     = mc.motor.voltage_limit;
        m.ia       = mc.last_current_a;
        m.ib       = mc.last_current_b;
        m.ic       = mc.last_current_c;
        m.prot     = mc.protection_current;
        m.enabled  = mc.enabled ? 1 : 0;
        m.nf_edges = (uint8_t)min<uint32_t>(nf_total[i] - prev_nf_total[i], 255);
        m.nf_low   = (digitalRead(nf_pin[i]) == LOW) ? 1 : 0;
        const LoadEstimate& le = loadEstimate(i);
        m.ip       = le.ip;
        m.iq       = le.iq;
        m.lag      = le.lag_deg;
        prev_nf_total[i] = nf_total[i];
    }
    tripRecorderPush(s);
}

// Housekeeping task (core 0).  Owns everything that is not time-critical for commutation:
// current sensing (the six analogReads), the calibration sweep, serial command dispatch, fault
// monitoring, the Z tool state machine, fan control, telemetry and status reporting.  Keeping all
// of this off core 1 lets the FOC loop run at a steady, high rate with no periodic hiccups.
// Because this task performs the ADC reads, it also owns the OTA standby handshake.
static void housekeepingTask(void* arg) {
    (void)arg;
    uint32_t last_status_time = 0;
    uint32_t last_hk_time     = millis();

    // Watch this task (see HOUSEKEEPING_WDT_TIMEOUT_S).  esp_task_wdt_init on an already-running
    // TWDT just updates its timeout; panic stays on so a hang reboots and leaves a coredump.
    esp_task_wdt_init(HOUSEKEEPING_WDT_TIMEOUT_S, true);
    esp_task_wdt_add(nullptr);

    for (;;) {
        esp_task_wdt_reset();
        // OTA: stop touching the ADC and signal otaTask that it is now safe to bring the WiFi
        // radio up (see the handshake in ota_service.cpp).  The FOC task independently disables
        // the drivers.  The board reboots into the new firmware when an update completes.
        if (g_ota_active) {
            setMachineState(MachineState::OtaUpdate);  // announce the transition before we park
            g_motor_in_ota_standby = true;
            last_hk_time           = millis();
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        g_motor_in_ota_standby = false;

        uint32_t now_ms = millis();
        float    hk_dt  = (now_ms - last_hk_time) / 1000.0f;
        last_hk_time    = now_ms;

        // Current sensing (six analogReads, ~0.5 ms) - feeds protection, telemetry and cal.
        // Dithered: the always-on load-sense fit needs reads at random points in the PWM cycle.
        mc1.updateCurrent(true);
        mc2.updateCurrent(true);
        calibration.accumulateCurrentSample(mc1.last_instantaneous_current, 0);
        calibration.accumulateCurrentSample(mc2.last_instantaneous_current, 1);
        // Load-sense fit over the reads above (no extra ADC reads) and the advisory stall warning.
        // Calibration hunts its own voltages, which the no-load baseline does not describe.
        loadSenseUpdate(mc1, mc2, hk_dt, !calibration.isActive());
        reportLoadWarnings();
        reportLoadBoost();
        recordTripSample(now_ms);

        // Calibration sweep and serial command handling (both set targets the FOC task actuates).
        calibration.update(mc1, mc2);
        handleSerialCommands(mc1, mc2, calibration);

        checkMotorTimeouts();
        checkPhaseHoldPowerdown();
        checkDriverFaults();
        checkOvercurrent();
        // Tool state machine (power-up homing, tool load/unload via the top-of-travel beam).
        // Its commanded phase target is picked up by updatePhaseOffset in the FOC task.
        updateToolStateMachine();
        applyFanForMotorState(mc1.enabled || mc2.enabled);
        updateFanControl(hk_dt);
        // Reconcile the single reported state now that this pass's tool state, faults,
        // calibration and spindle commands have all been applied, logging any transition.
        updateReportedState();
        tripRecorderService();  // stream a captured trip recording to USB, a few lines per pass
        reportResetReasonOnce();
        watchUsbOutput();

        // Report status to the XY board over the inter-board link.  Only send when the whole
        // line already fits the TX buffer so this write can never block; if the buffer is
        // momentarily full we skip and retry next pass.
        if (now_ms - last_status_time >= LINK_STATUS_INTERVAL_MS &&
            Serial1.availableForWrite() >= 48) {   // longest status line is ~36 chars
            last_status_time = now_ms;
            sendStatus(Serial1, mc1, mc2);
        }

        // Fixed housekeeping period.  All the sub-tasks above have their own longer internal
        // throttles; this base period sets the current-sampling and command-handling cadence.
        vTaskDelay(pdMS_TO_TICKS(FOC_HOUSEKEEPING_INTERVAL_MS));
    }
}

// Why the board last reset, for the USB console.  The RESET REASON line above is printed before a
// USB host has usually reconnected, so it is easily lost; this is repeated once a host is attached
// and on demand with the WHY command.
char g_reset_reason[80] = "unknown";

static void reportResetReasonOnce() {
    static bool done = false;
    if (done || !Serial || millis() < 2000) return;
    char line[120];
    int  n = snprintf(line, sizeof(line), "[BOOT] last reset: %s, %lu s ago\n", g_reset_reason,
                      (unsigned long)(millis() / 1000));
    done = usbWriteIfRoom(line, n);
}

// USB console output watchdog.  On 2026-10-06 the board's USB output died right after a spindle
// start while everything else (including USB input - 'x' stopped the spindle) kept working, and it
// stayed dead even across a host reconnect.  If the USB TX buffer stays nearly full for a second,
// report the peripheral's state over the XY-board link (which still works), then keep kicking
// the TX path (flush the hardware FIFO + re-enable the TX-empty interrupt) every 500 ms, and
// report when output drains again.  It also fires, harmlessly, when no USB host is reading.
static void watchUsbOutput() {
    static uint32_t low_since = 0, last_kick = 0;
    static bool     reported  = false;
    static int      kicks     = 0;
    if (!Serial) return;
    uint32_t now  = millis();
    int      room = Serial.availableForWrite();
    if (room >= 128) {
        if (reported) reportEvent("MSG", "USB console output draining again after %d kick(s)", kicks);
        low_since = 0;
        reported  = false;
        kicks     = 0;
        return;
    }
    if (!low_since) {
        low_since = now ? now : 1;
        return;
    }
    if (now - low_since < 1000) return;
    if (!reported) {
        reported = true;
        reportEvent("MSG", "USB console not draining (no host, or USB stall): room=%d fifo=%u ena=%lx raw=%lx ep=%u/%u/%u",
                    room, (unsigned)USB_SERIAL_JTAG.ep1_conf.serial_in_ep_data_free,
                    (unsigned long)USB_SERIAL_JTAG.int_ena.val, (unsigned long)USB_SERIAL_JTAG.int_raw.val,
                    (unsigned)USB_SERIAL_JTAG.in_ep1_st.in_ep1_state,
                    (unsigned)USB_SERIAL_JTAG.in_ep1_st.in_ep1_wr_addr,
                    (unsigned)USB_SERIAL_JTAG.in_ep1_st.in_ep1_rd_addr);
    }
    if (now - last_kick >= 500) {
        last_kick = now;
        kicks++;
        usb_serial_jtag_ll_txfifo_flush();
        usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
    }
}

// ------------------- Setup & Loop -------------------

void setup() {
    // Disable the hardware brownout detector as the very first thing the app does. If the
    // board's 3.3V rail dips briefly when 24V is applied (inrush charging the 24V bulk caps
    // and the motor drivers), a brownout reset can put the chip into a reset loop so the
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
    const char*        reset_str = "unknown";
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
    // The chip-level cause is more specific than esp_reset_reason() (which calls a reset from the
    // USB flasher "unknown").  With the brownout detector disabled, a supply sag shows up as
    // power-on (1), glitch (19/23) or brownout (15) here.
    int         rom_reason = (int)rtc_get_reset_reason(0);
    const char* rom_str;
    switch (rom_reason) {
        case 1:  rom_str = "power-on"; break;
        case 3:  rom_str = "software"; break;
        case 7: case 8: case 12: case 17: rom_str = "timer watchdog"; break;
        case 9: case 14: case 16: rom_str = "RTC watchdog"; break;
        case 15: rom_str = "brownout"; break;
        case 18: rom_str = "super watchdog"; break;
        case 19: rom_str = "clock glitch"; break;
        case 21: rom_str = "USB-UART"; break;
        case 22: rom_str = "USB-JTAG (flasher/host)"; break;
        case 23: rom_str = "power glitch"; break;
        default: rom_str = "other"; break;
    }
    snprintf(g_reset_reason, sizeof(g_reset_reason), "%s (%d); chip: %s (%d)", reset_str,
             (int)reset_reason, rom_str, rom_reason);

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

    // Both MP6541As sleep until a motor is enabled, and their nFAULT pins are monitored by
    // interrupt (the over-current retry pulses are only ~2ms wide).
    initDriverSleepPin();
    initFaultPins();

    // Initialize both motor drivers and motors
    mc1.initDriver();
    mc2.initDriver();
    mc1.initMotor();
    mc2.initMotor();

    // Load pre-measured calibration LUT data
    loadDefaultLUT(mc1, 0, MC1_DEFAULT_LUT, sizeof(MC1_DEFAULT_LUT) / sizeof(MC1_DEFAULT_LUT[0]));
    loadDefaultLUT(mc2, 1, MC2_DEFAULT_LUT, sizeof(MC2_DEFAULT_LUT) / sizeof(MC2_DEFAULT_LUT[0]));
    loadSenseInit();  // no-load baseline for the load-sense diagnostic (NVS)

    // Start the housekeeping task on core 0: current sensing, calibration, serial commands, fault
    // monitoring, the Z tool state machine, fan, telemetry and status reporting - everything that
    // is not time-critical for commutation, kept off the FOC core.  Created FIRST: the FOC task
    // below runs at a higher priority on core 1 and never blocks, so once it exists it starves
    // this setup context (loopTask, core 1) and no later line here would execute.
    xTaskCreatePinnedToCore(housekeepingTask, "housekeeping", 8192, nullptr, 2, &housekeeping_task_handle, 0);

    // If an OTA update was requested before the last reboot, resume it now - from this clean,
    // freshly booted state (the one that reliably survives the WiFi radio's power-up surge on
    // 24 V-only power).  Done AFTER the housekeeping task exists (it owns the ADC standby
    // handshake the OTA task waits on) and BEFORE the FOC task below (which permanently
    // preempts this context).  A no-op on a normal boot.
    resumePendingOTA();

    // Start the real-time FOC control task on core 1.  It runs ONLY the open-loop commutation
    // hot path, so nothing else can lengthen a commutation step.  Created LAST because it
    // preempts this context permanently.
    xTaskCreatePinnedToCore(motorControlTask, "motorControl", 8192, nullptr, 3, &motor_control_task_handle, 1);

    // --- Console banner LAST, and only when a USB host is actually attached. ---
    if (Serial) {
        Serial.println(F("\n=== ESP32-S3 + MP6541A (6-PWM) + SimpleFOC ==="));
        Serial.printf("Inter-board link ready on Serial1 (RX=GPIO%d, TX=GPIO%d, %d baud, 8N1)\n",
                      LINK_RX_PIN, LINK_TX_PIN, LINK_BAUD);
        Serial.println(F("Waiting for handshake ('H') from XY board over the link..."));
        Serial.printf("Driver sense zero (V): M1 %.3f/%.3f/%.3f  M2 %.3f/%.3f/%.3f\n",
                      mc1.cur_zero_a, mc1.cur_zero_b, mc1.cur_zero_c,
                      mc2.cur_zero_a, mc2.cur_zero_b, mc2.cur_zero_c);
        Serial.println(F("Motor 1 initialized for open-loop control"));
        Serial.println(F("Motor 2 initialized for open-loop control (opposite direction)"));
        printCommandHelp();
        Serial.println(F("Motors ready. Send a velocity command to start."));
        Serial.println(F("Active motor: BOTH (use 'q', 'w', or 'e' to switch)"));
    }
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

