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

static TaskHandle_t s_ota_task = nullptr;
static char         s_ota_ssid[33];
static char         s_ota_pass[65];

static void otaTask(void* arg) {
    (void)arg;

    // Join the XY board's WiFi network.  Don't persist credentials to NVS - they are
    // supplied fresh over the link each time OTA is requested.
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
        Serial1.print("OTA_ERR:wifi\n");
        WiFi.mode(WIFI_OFF);
        s_ota_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA
        .onStart([]() {
            // The motor control loop (core 1) watches this flag and powers the drivers
            // down before the first flash write, so the machine stays put during the update.
            g_ota_flashing = true;
        })
        .onError([](ota_error_t) {
            // Flash failed; let the motor loop resume so the board is not left dead.
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
            s_ota_task = nullptr;
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
