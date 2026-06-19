#include <Arduino.h>
#include <SimpleFOC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "pins.h"
#include "config.h"
#include "motor_controller.h"
#include "calibration.h"
#include "serial_commands.h"

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
// These are pre-measured voltage-vs-RPM values from prior calibration runs.

static const float MC1_DEFAULT_LUT[] = {
    1.615f, 1.688f, 1.687f, 1.725f, 1.761f, 1.767f, 1.809f, 1.849f, 1.922f, 1.957f,  // 100-1000
    2.008f, 2.060f, 2.127f, 2.199f, 2.247f, 2.287f, 2.348f, 2.433f, 2.518f, 2.570f,  // 1100-2000
    2.618f, 2.715f, 2.777f, 2.830f, 2.901f, 2.972f, 3.029f, 3.151f, 3.222f, 3.267f,  // 2100-3000
    3.329f, 3.423f, 3.498f, 3.572f, 3.645f, 3.735f, 3.787f, 3.862f, 3.946f, 4.000f,  // 3100-4000
    4.097f, 4.173f, 4.255f, 4.333f, 4.412f, 4.486f, 4.540f, 4.610f, 4.695f, 4.772f,  // 4100-5000
    4.857f, 4.939f, 5.023f, 5.085f, 5.197f, 5.251f, 5.330f, 5.415f, 5.488f, 5.566f,  // 5100-6000
    1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f,                      // 6100-7000
    1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f,                      // 7100-8000
    1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f,                      // 8100-9000
    1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f,                      // 9100-10000
};

static const float MC2_DEFAULT_LUT[] = {
    1.892f, 2.169f, 2.162f, 2.164f, 2.273f, 2.277f, 2.272f, 2.304f, 2.380f, 2.424f,  // 100-1000
    2.520f, 2.521f, 2.555f, 2.641f, 2.639f, 2.698f, 2.733f, 2.764f, 2.871f, 2.870f,  // 1100-2000
    2.934f, 2.938f, 3.023f, 3.135f, 3.137f, 3.205f, 3.391f, 3.420f, 3.413f, 3.524f,  // 2100-3000
    3.600f, 3.648f, 3.676f, 3.785f, 3.897f, 3.894f, 4.032f, 4.104f, 4.113f, 4.178f,  // 3100-4000
    4.233f, 4.329f, 4.471f, 4.476f, 4.556f, 4.593f, 4.648f, 4.759f, 4.837f, 4.899f,  // 4100-5000
    4.967f, 5.057f, 5.107f, 5.190f, 5.252f, 5.325f, 5.485f, 5.486f, 5.566f, 5.684f,  // 5100-6000
    1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f,                      // 6100-7000
    1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f,                      // 7100-8000
    1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f,                      // 8100-9000
    1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f, 1.3f,                      // 9100-10000
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
static uint32_t last_drv_check = 0;
static int overcurrent_count = 0;

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

    bool persistent_ocp = (ocp_consec_count1 >= OCP_CONSEC_LIMIT) ||
                          (ocp_consec_count2 >= OCP_CONSEC_LIMIT);

    // Transient OCP: just clear and log
    if ((ocp1 || ocp2) && !serious1 && !serious2 && !persistent_ocp) {
        if (ocp1) { mc1.driver.clearFault(); delayMicroseconds(1); }
        if (ocp2) { mc2.driver.clearFault(); delayMicroseconds(1); }
        if (ocp_consec_count1 == 1 || ocp_consec_count2 == 1) {
            Serial.printf("DRV8316 OCP transient (CBC auto-clear): M1=%d M2=%d\n", ocp1, ocp2);
        }
        return;
    }

    // Serious fault or persistent OCP -> full shutdown
    if (!serious1 && !serious2 && !persistent_ocp) return;

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

    g_fault_code = 1;  // DRV8316 hardware fault
    mc1.emergencyStop();
    mc2.emergencyStop();
    Serial.println(F("  -> Both motors disabled. Send a new velocity command to restart."));

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

    float oc_threshold = calibration.isActive() ? OVERCURRENT_THRESHOLD_CAL : OVERCURRENT_THRESHOLD;

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

    Serial.printf("OVERCURRENT FAULT: M1=%.3f A  M2=%.3f A (phase RMS) -- both motors disabled.\n",
                  mc1.protection_current, mc2.protection_current);

    g_fault_code = 2;  // overcurrent fault
    mc1.emergencyStop();
    mc2.emergencyStop();

    calibration.abort(mc1, mc2, "overcurrent fault");
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

static void motorControlTask(void* arg) {
    (void)arg;
    uint32_t last_ramp_time = millis();

    for (;;) {
        uint32_t current_time = millis();
        float dt = (current_time - last_ramp_time) / 1000.0f;
        last_ramp_time = current_time;

        checkMotorTimeouts();
        checkDRV8316Faults();
        checkOvercurrent();
        calibration.update(mc1, mc2);

        bool cal_active = calibration.isActive() &&
                          calibration.state != CAL_RAMP_DOWN &&
                          calibration.state != MCAL_RAMP_DOWN;
        mc1.applyVoltageLimit(cal_active && calibration.active_motor_idx == 0, calibration.hunt_voltage);
        mc2.applyVoltageLimit(cal_active && calibration.active_motor_idx == 1, calibration.hunt_voltage);

        mc1.rampVelocity(dt);
        mc2.rampVelocity(dt);
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

        // Report status to the XY board over the inter-board link
        static uint32_t last_status_time = 0;
        if (current_time - last_status_time >= LINK_STATUS_INTERVAL_MS) {
            last_status_time = current_time;
            sendStatus(Serial1, mc1, mc2);
        }

        taskYIELD();
    }
}

// ------------------- Setup & Loop -------------------

void setup() {
    Serial.begin(115200);

    // Bring up the inter-board link to the FluidNC XY board first (RX=GPIO39, TX=GPIO38)
    // so it is listening as early as possible, before the XY board finishes booting and
    // sends its handshake.
    Serial1.begin(LINK_BAUD, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);

    delay(3000);
    Serial.println(F("\n=== ESP32-S3 + DRV8316 (SPI + 6-PWM) + Hall + SimpleFOC ==="));
    Serial.printf("Inter-board link ready on Serial1 (RX=GPIO%d, TX=GPIO%d, %d baud, 8N1)\n",
                  LINK_RX_PIN, LINK_TX_PIN, LINK_BAUD);
    Serial.println(F("Waiting for handshake ('H') from XY board over the link..."));

    initFanControl();

    // Initialize SPI bus
    drvSPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SPI_CS_PIN);

    // Initialize both motor drivers and motors
    mc1.initDriver(&drvSPI);
    mc2.initDriver(&drvSPI);

    mc1.printFaultStatus();

    mc1.initMotor();
    mc2.initMotor();

    // Load pre-measured calibration LUT data
    loadDefaultLUT(mc1, 0, MC1_DEFAULT_LUT, sizeof(MC1_DEFAULT_LUT) / sizeof(MC1_DEFAULT_LUT[0]));
    loadDefaultLUT(mc2, 1, MC2_DEFAULT_LUT, sizeof(MC2_DEFAULT_LUT) / sizeof(MC2_DEFAULT_LUT[0]));

    xTaskCreatePinnedToCore(motorControlTask, "motorControl", 8192, nullptr, 3, &motor_control_task_handle, 1);

    Serial.println(F("Motor 1 initialized for open-loop control"));
    Serial.println(F("Motor 2 initialized for open-loop control (opposite direction)"));

    printCommandHelp();

    Serial.println(F("Motors ready. Send a velocity command to start."));
    Serial.println(F("Active motor: 1 (use 'q', 'w', or 'e' to switch)"));
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
