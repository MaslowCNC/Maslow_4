// Copyright 2024 Maslow CNC
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "maslow_watchdog.h"
#include "src/Config.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>

// Forward declarations for external functions/classes
extern void protocol_disable_steppers();

// Forward declaration of Maslow class
class Maslow_;
extern Maslow_& Maslow;

namespace MaslowWatchdog {

    // Constants
    static const uint32_t WATCHDOG_PING_INTERVAL_MS   = 200;    // Expected ping interval from main thread
    static const uint32_t WATCHDOG_TIMEOUT_MS         = 4000;   // Timeout before triggering watchdog action
    static const uint32_t WATCHDOG_DISARM_DURATION_MS = 60000;  // Duration for which watchdog is disarmed
    static const char*    NVS_NAMESPACE               = "maslow";
    static const char*    NVS_WATCHDOG_FLAG_KEY       = "wdg_reset";

    // State variables
    static volatile uint32_t last_ping_time_ms    = 0;
    static volatile uint32_t disarm_until_ms      = 0;
    static volatile bool     watchdog_armed       = false;
    static TaskHandle_t      watchdog_task_handle = nullptr;

    // Forward declarations
    static void watchdog_task(void* parameter);
    static void trigger_watchdog_action();

    void init() {
        // Initialize NVS if not already done
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            // NVS partition was truncated and needs to be erased
            // This shouldn't happen in normal operation
        }

        last_ping_time_ms = millis();
        disarm_until_ms   = 0;
        watchdog_armed    = false;
    }

    void start() {
        if (watchdog_task_handle != nullptr) {
            return;  // Already started
        }

        watchdog_armed    = true;
        last_ping_time_ms = millis();

        // Create watchdog task on Core 0 with higher priority than other tasks
        xTaskCreatePinnedToCore(watchdog_task,          // task function
                                "watchdog",             // name
                                4096,                   // stack size
                                nullptr,                // parameters
                                2,                      // priority (higher than other Core 0 tasks)
                                &watchdog_task_handle,  // task handle
                                SUPPORT_TASK_CORE       // Core 0
        );
    }

    void ping() {
        uint32_t current_time = millis();

        // If we were disarmed and now we get a ping, re-arm the watchdog
        if (current_time >= disarm_until_ms && !watchdog_armed) {
            watchdog_armed = true;
        }

        // Update last ping time
        last_ping_time_ms = current_time;
    }

    void disarm() {
        uint32_t current_time = millis();
        disarm_until_ms       = current_time + WATCHDOG_DISARM_DURATION_MS;
        watchdog_armed        = false;
    }

    bool was_watchdog_reset() {
        nvs_handle_t handle;
        esp_err_t    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
        if (ret != ESP_OK) {
            return false;
        }

        int32_t flag = 0;
        ret          = nvs_get_i32(handle, NVS_WATCHDOG_FLAG_KEY, &flag);
        nvs_close(handle);

        return (ret == ESP_OK && flag == 1);
    }

    void clear_watchdog_flag() {
        nvs_handle_t handle;
        esp_err_t    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (ret != ESP_OK) {
            return;
        }

        nvs_set_i32(handle, NVS_WATCHDOG_FLAG_KEY, 0);
        nvs_commit(handle);
        nvs_close(handle);
    }

    static void set_watchdog_flag() {
        nvs_handle_t handle;
        esp_err_t    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (ret != ESP_OK) {
            return;
        }

        nvs_set_i32(handle, NVS_WATCHDOG_FLAG_KEY, 1);
        nvs_commit(handle);
        nvs_close(handle);
    }

    static void trigger_watchdog_action() {
        // Set the flag in NVS so we know on reboot that watchdog triggered
        set_watchdog_flag();

        // Halt the A B C D axis motors (TL, TR, BL, BR)
        // We need to call Maslow.stopMotors() but we can't include Maslow.h here due to circular dependencies
        // So we'll just disable steppers which stops all motion
        protocol_disable_steppers();

        // Small delay to ensure everything is stopped
        vTaskDelay(100 / portTICK_PERIOD_MS);

        // Restart the system
        esp_restart();
    }

    static void watchdog_task(void* parameter) {
        while (true) {
            vTaskDelay(100 / portTICK_PERIOD_MS);  // Check every 100ms

            uint32_t current_time = millis();

            // Check if watchdog is armed
            if (!watchdog_armed) {
                // Check if we should re-arm
                if (current_time >= disarm_until_ms) {
                    watchdog_armed = true;
                }
                continue;
            }

            // Calculate time since last ping
            uint32_t time_since_ping = current_time - last_ping_time_ms;

            // Check if we've exceeded the timeout
            if (time_since_ping > WATCHDOG_TIMEOUT_MS) {
                // Watchdog timeout! Trigger action
                trigger_watchdog_action();
            }
        }
    }

}  // namespace MaslowWatchdog
