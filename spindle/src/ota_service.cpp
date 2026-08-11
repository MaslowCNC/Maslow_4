#include "ota_service.h"
#include "config.h"
#include "motor_controller.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <esp_wifi.h>
#include <Preferences.h>

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

static void otaTask(void* arg);  // core-0 WiFi + ArduinoOTA task (defined below)

// NVS-persisted OTA request.  The WiFi radio's power-up surge can reset this power-marginal
// board mid-bring-up, and a runtime 'W' link command would then be lost; so the request is
// stored in flash and the board reboots to service it from a clean boot (the state that
// reliably comes up).  If an attempt resets the board, the flag survives and setup() retries.
static const char* const OTA_NVS_NS = "ota";

// Forget any persisted OTA request so the board boots normally next time.
static void clearPendingOTA() {
    Preferences p;
    if (p.begin(OTA_NVS_NS, false)) {
        p.putBool("pending", false);
        p.putUChar("attempts", 0);
        p.end();
    }
}

// Capture the credentials and spin up the core-0 OTA task.  A no-op if OTA is already active.
static void startOtaTask(const char* ssid, const char* pass) {
    if (s_ota_task) {
        return;  // already bringing up / running OTA
    }

    strncpy(s_ota_ssid, ssid ? ssid : "", sizeof(s_ota_ssid) - 1);
    s_ota_ssid[sizeof(s_ota_ssid) - 1] = '\0';
    strncpy(s_ota_pass, pass ? pass : "", sizeof(s_ota_pass) - 1);
    s_ota_pass[sizeof(s_ota_pass) - 1] = '\0';

    // Run WiFi + OTA on core 0, clear of the real-time motor control task (core 1).  The
    // stack must be generous: bringing the WiFi stack and ArduinoOTA up from within this task
    // uses far more stack than steady-state, and an overflow here would look exactly like the
    // power-up crash we are guarding against.
    xTaskCreatePinnedToCore(otaTask, "otaService", 16384, nullptr, 1, &s_ota_task, 0);
}

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

    // Give the drivers a beat to actually stop sinking current after the FOC task has
    // disabled them, so the rail is at its quietest before the radio's surge hits.
    vTaskDelay(pdMS_TO_TICKS(50));

    // Drop to a low CPU clock for the duration of the radio bring-up.  Powering the WiFi
    // PHY up and running its RF calibration draws a large current pulse; on this
    // power-marginal board that pulse (on top of a 240 MHz core) sags the 3.3V rail far
    // enough to reset the chip the instant WiFi.mode(WIFI_STA) starts the radio - the exact
    // failure that made on-demand OTA unusable.  Running the core at 80 MHz through the
    // bring-up cuts the baseline draw so the surge has headroom; it is restored to full
    // speed once associated (the machine is parked during OTA, so a slow core is harmless).
    setCpuFrequencyMhz(80);

    // Join the XY board's WiFi network.  Don't persist credentials to NVS - they are
    // supplied fresh over the link each time OTA is requested.
    // NOTE: every USB-Serial write here is gated on (bool)Serial (true only when a host is
    // attached and draining the CDC FIFO).  With no host, an unguarded Serial.flush() blocks
    // forever because nothing empties the FIFO - which is exactly what made OTA require USB.
    if (Serial) {
        Serial.println("[OTA] starting WiFi");
        Serial.flush();
    }
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    // Cap transmit power immediately, before association keys the PA for the first time, to
    // hold the radio's peak current down on this power-marginal board.  The access point is
    // physically close so the reduced range costs nothing.
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.setHostname(OTA_HOSTNAME);
    WiFi.begin(s_ota_ssid, s_ota_pass);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < OTA_WIFI_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (WiFi.status() != WL_CONNECTED) {
        // Report the failure to the XY board and reboot to retry from a clean boot.  The
        // per-boot attempt counter (resumePendingOTA) bounds this so an unreachable network
        // can't loop forever.
        setCpuFrequencyMhz(240);
        if (Serial) {
            Serial.println("[OTA] WiFi connect failed - rebooting to retry");
            Serial.flush();
        }
        Serial1.print("OTA_ERR:wifi\n");
        Serial1.flush();
        WiFi.mode(WIFI_OFF);
        delay(100);
        ESP.restart();
        return;
    }

    // Association survived the surge - restore full CPU speed for a brisk OTA transfer.
    setCpuFrequencyMhz(240);
    // Let the APB clock change settle before reconfiguring the radio below.
    vTaskDelay(pdMS_TO_TICKS(100));

    if (Serial) {
        Serial.printf("[OTA] WiFi connected ip=%s\n", WiFi.localIP().toString().c_str());
        Serial.flush();
    }

    // Report the IP to the XY board as the very first thing after associating, before any
    // further radio configuration, so the operator always learns where to send the update
    // even if a later step misbehaves.  The board is also discoverable via mDNS as
    // OTA_HOSTNAME ".local".
    Serial1.printf("OTA_IP:%s\n", WiFi.localIP().toString().c_str());

    // Disable WiFi modem power-save.  With the default WIFI_PS_MIN_MODEM the radio sleeps
    // between beacons, which adds hundreds of milliseconds of latency and drops packets during
    // the OTA TCP transfer, making espota fail with "Error Uploading" partway through.  The
    // Arduino WiFi.setSleep(false) wrapper does not reliably stick on this build, so also pin
    // WIFI_PS_NONE directly through the IDF API.  (The XY board does the equivalent.)
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    vTaskDelay(pdMS_TO_TICKS(50));  // let the power-save change apply before continuing

    // Keep transmit power at 13 dBm for the transfer.  Full power (19.5 dBm) draws a current
    // surge that resets this power-marginal board, but dropping below 13 dBm weakens the link
    // enough that the access point stops ACKing and the transfer stalls in the first few
    // percent - 13 dBm is the sweet spot that both survives the rail and holds the link.
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
        .onEnd([]() {
            // Update succeeded.  Forget the pending request so the reboot into the new
            // firmware boots normally instead of re-entering OTA.
            clearPendingOTA();
        })
        .onError([](ota_error_t) {
            // A transfer failed.  ArduinoOTA does NOT reliably accept a retry on the same boot
            // (it wedges, rejecting the next espota at the very first block), so reboot to retry
            // from a fresh WiFi/OTA state.  The persisted pending flag makes setup() resume and
            // the bounded per-boot attempt counter stops it looping forever.
            g_ota_flashing = false;
            Serial1.print("OTA_ERR:transfer failed - rebooting to retry\n");
            Serial1.flush();
            delay(200);
            ESP.restart();
        });
    ArduinoOTA.begin();

    // NOTE: the pending flag is deliberately NOT cleared here.  It is cleared on success
    // (onEnd) and on idle timeout (below); a failed transfer instead reboots (onError) and
    // resumes so each retry gets a clean state.  A reset during bring-up therefore also retries.

    uint32_t idle_start = millis();
    for (;;) {
        ArduinoOTA.handle();

        // If no update arrives within the idle window, tear WiFi/OTA back down so the
        // board returns to its normal radio-off, real-time state.
        if (!g_ota_flashing && (millis() - idle_start) >= OTA_IDLE_TIMEOUT_MS) {
            clearPendingOTA();  // no upload arrived; don't re-enter OTA on the next boot
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
    // Persist the request + credentials, then reboot to service OTA from a clean, freshly
    // booted state.  Bringing WiFi up from a long-running state - or surviving the radio's
    // power-up current surge on 24 V-only power - is unreliable on this board; a fresh boot is
    // the state that comes up cleanly, and if the surge resets us mid-bring-up the persisted
    // flag makes setup() retry automatically (see resumePendingOTA).
    Preferences p;
    if (p.begin(OTA_NVS_NS, false)) {
        p.putString("ssid", ssid ? ssid : "");
        p.putString("pass", pass ? pass : "");
        p.putUChar("attempts", 0);
        p.putBool("pending", true);
        p.end();
    }
    // Gate on (bool)Serial: an unguarded Serial.flush() with no USB host blocks forever, so
    // ESP.restart() below would never run and OTA would silently require USB to start.
    if (Serial) {
        Serial.println("[OTA] request stored - rebooting to start OTA from a clean boot");
        Serial.flush();
    }
    delay(100);
    ESP.restart();
}

void resumePendingOTA() {
    Preferences p;
    if (!p.begin(OTA_NVS_NS, false)) {
        return;
    }
    if (!p.getBool("pending", false)) {
        p.end();
        return;  // no OTA requested - normal boot
    }

    uint8_t attempts = p.getUChar("attempts", 0);
    if (attempts >= OTA_BOOT_MAX_ATTEMPTS) {
        // Too many failed bring-ups in a row (network unreachable, or the rail can't support
        // the radio).  Give up and boot normally so the board stays usable.
        p.putBool("pending", false);
        p.putUChar("attempts", 0);
        p.end();
        Serial1.printf("OTA_ERR:gave up after %u tries\n", (unsigned)attempts);
        return;
    }

    // Count this attempt BEFORE starting WiFi and commit it to flash, so even an instant
    // surge-reset still makes forward progress and can't boot-loop.
    p.putUChar("attempts", (uint8_t)(attempts + 1));
    String ssid = p.getString("ssid", "");
    String pass = p.getString("pass", "");
    p.end();

    if (Serial) {
        Serial.printf("[OTA] resuming pending OTA from NVS (attempt %u/%u)\n",
                      (unsigned)(attempts + 1), (unsigned)OTA_BOOT_MAX_ATTEMPTS);
    }
    startOtaTask(ssid.c_str(), pass.c_str());
}
