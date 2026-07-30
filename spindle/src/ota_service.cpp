#include "ota_service.h"
#include "config.h"
#include "motor_controller.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

// The two BLDC motor controllers live in main.cpp.  They are disabled before an OTA
// image is written so the machine cannot move while the firmware is being replaced.
extern MotorController mc1;
extern MotorController mc2;

volatile bool g_ota_flashing = false;
volatile bool g_ota_active   = false;
volatile bool g_motor_in_ota_standby = false;

static TaskHandle_t s_ota_task = nullptr;
static char         s_ota_ssid[33];
static char         s_ota_pass[65];

static void otaTask(void* arg) {
    (void)arg;

    // Shed the motor-driver current load BEFORE the WiFi radio powers up.  The radio's
    // start-up surge briefly loads the 3.3V rail, and with the two DRV8316 drivers still
    // energised the rail can sag far enough to hang the chip (the brownout detector is
    // deliberately disabled, so it hangs instead of resetting).  The motor control loop
    // (core 1) watches g_ota_active and disables both drivers when it is set, so raise
    // it here and give that loop a moment to act before bringing WiFi up.  The machine
    // must not move during an update anyway.
    g_ota_active = true;

    // Wait until the motor loop confirms it has actually reached that standby branch
    // before touching WiFi.  This is essential, not just courteous: the motor loop reads
    // the phase currents with analogRead(), and WiFi seizes the ADC the instant its radio
    // starts.  If a conversion were in flight at that moment it would never complete and
    // core 1 would spin forever, tripping the interrupt watchdog.  The handshake guarantees
    // the loop is parked in its ADC-free standby branch first.
    uint32_t park_start = millis();
    while (!g_motor_in_ota_standby && (millis() - park_start) < 1000) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // Join the XY board's WiFi network.  Don't persist credentials to NVS - they are
    // supplied fresh over the link each time OTA is requested.
    Serial.println("[OTA] starting WiFi");
    Serial.flush();
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(OTA_HOSTNAME);
    WiFi.begin(s_ota_ssid, s_ota_pass);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < OTA_WIFI_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (WiFi.status() != WL_CONNECTED) {
        // Report the failure to the XY board and return to normal radio-off operation.
        Serial.println("[OTA] WiFi connect failed");
        Serial.flush();
        Serial1.print("OTA_ERR:wifi\n");
        WiFi.mode(WIFI_OFF);
        g_ota_active   = false;  // let the motor loop resume normal operation
        g_ota_flashing = false;
        s_ota_task     = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    Serial.printf("[OTA] WiFi connected ip=%s\n", WiFi.localIP().toString().c_str());
    Serial.flush();

    // Disable WiFi modem power-save.  With the default WIFI_PS_MIN_MODEM the radio sleeps
    // between beacons, which adds latency and drops packets during the OTA TCP transfer and
    // makes espota fail with "Error Uploading" at 0%.  The XY board (FluidNC) does the same.
    WiFi.setSleep(false);

    // Use a moderate transmit power.  The access point is physically close, so full power
    // buys no range but does add to the current draw this power-marginal board must sustain
    // while it is also erasing/writing flash during the transfer.
    WiFi.setTxPower(WIFI_POWER_13dBm);

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    // The relayed SoftAP path occasionally stalls for more than the 1s default.  Give the
    // receiver a generous window so a brief gap doesn't abort the flash with a receive error.
    ArduinoOTA.setTimeout(OTA_RECEIVE_TIMEOUT_MS);
    ArduinoOTA
        .onStart([]() {
            // Mark the flash in progress so the idle-timeout teardown below can't fire
            // mid-update.  The drivers are already disabled via g_ota_active.
            g_ota_flashing = true;
        })
        .onError([](ota_error_t) {
            // Flash failed; clear the in-progress mark so the idle timeout can tear the
            // radio back down.  Drivers stay disabled (g_ota_active) until it does.
            g_ota_flashing = false;
        });
    ArduinoOTA.begin();

    // Tell the XY board where to send the update.  The board is also discoverable via
    // mDNS as OTA_HOSTNAME ".local".
    Serial1.printf("OTA_IP:%s\n", WiFi.localIP().toString().c_str());

    uint32_t idle_start = millis();
    for (;;) {
        ArduinoOTA.handle();

        // If no update arrives within the idle window, tear WiFi/OTA back down so the
        // board returns to its normal radio-off, real-time state.
        if (!g_ota_flashing && (millis() - idle_start) >= OTA_IDLE_TIMEOUT_MS) {
            ArduinoOTA.end();
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            Serial1.print("OTA_END:timeout\n");
            g_ota_active   = false;  // restore normal motor operation
            g_ota_flashing = false;
            s_ota_task     = nullptr;
            vTaskDelete(nullptr);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void requestSpindleOTA(const char* ssid, const char* pass) {
    if (s_ota_task) {
        return;  // already bringing up / running OTA
    }

    strncpy(s_ota_ssid, ssid ? ssid : "", sizeof(s_ota_ssid) - 1);
    s_ota_ssid[sizeof(s_ota_ssid) - 1] = '\0';
    strncpy(s_ota_pass, pass ? pass : "", sizeof(s_ota_pass) - 1);
    s_ota_pass[sizeof(s_ota_pass) - 1] = '\0';

    // Run WiFi + OTA on core 0, clear of the real-time motor control task (core 1).
    xTaskCreatePinnedToCore(otaTask, "otaService", 8192, nullptr, 1, &s_ota_task, 0);
}
