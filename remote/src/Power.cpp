#include "Power.h"
#include "Debug.h"
#include "Display.h"
#include "Machine.h"
#include "Ui.h"

#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

namespace {
    constexpr gpio_num_t PIN_TOUCH_INT = (gpio_num_t)PIN_TOUCH_IRQ;
    constexpr gpio_num_t PIN_BL     = (gpio_num_t)PIN_BACKLIGHT;

    uint16_t screenSec  = 60;
    uint16_t sleepMin   = 10;
    uint32_t lastActive = 0;
    bool     off        = false;
    bool     woke       = false;

    void savePrefs() {
        Preferences p;
        p.begin("ui", false);
        p.putUShort("scr", screenSec);
        p.putUShort("slp", sleepMin);
        p.end();
    }

    bool touched() { return gpio_get_level(PIN_TOUCH_INT) == 0; }
}

namespace Power {
    void begin() {
        // Pins held through the last deep sleep must be released before use.
        gpio_hold_dis(PIN_BL);
        gpio_deep_sleep_hold_dis();

        Preferences p;
        p.begin("ui", true);
        screenSec = p.getUShort("scr", 60);
        sleepMin  = p.getUShort("slp", 10);
        p.end();

        woke = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
        if (woke) {
            // The waking touch must not land on a button: wait for the release.
            pinMode((int)PIN_TOUCH_INT, INPUT);
            uint32_t t0 = millis();
            while (touched() && millis() - t0 < 3000) delay(10);
            delay(50);
        }
        lastActive = millis();
    }

    bool wokeFromSleep() { return woke; }

    void activity() {
        lastActive = millis();
        if (off) wakeScreen();
    }

    bool screenOff() { return off; }

    void wakeScreen() {
        lastActive = millis();
        if (!off) return;
        off = false;
        Ui::setBrightness(Ui::brightness());
        DLOG("POWER screen on\n");
    }

    uint16_t screenTimeoutSec() { return screenSec; }
    uint16_t sleepTimeoutMin() { return sleepMin; }
    void     setScreenTimeoutSec(uint16_t s) {
        screenSec = s;
        savePrefs();
    }
    void setSleepTimeoutMin(uint16_t m) {
        sleepMin = m;
        savePrefs();
    }

    bool canSleep() {
        if (!Machine::connected()) return true;
        using namespace Machine;
        if (st.sdPercent >= 0) return false;
        for (const char* s : { "Run", "Hold", "Jog", "Home", "Door" }) {
            if (stateIs(s)) return false;
        }
        switch (maslowState) {
            case MS_RETRACTING:
            case MS_EXTENDING:
            case MS_TAKING_SLACK:
            case MS_FINDING_ANCHORS:
            case MS_RELEASE_TENSION:
            case MS_ANCHORS_COMPUTING:
                return false;
            default:
                return true;
        }
    }

    void loop() {
        uint32_t idle = millis() - lastActive;
        if (!off && screenSec && idle > screenSec * 1000UL) {
            off = true;
            lcd.setBrightness(0);
            DLOG("POWER screen off\n");
        }
        if (sleepMin && idle > sleepMin * 60000UL && canSleep() && !touched()) {
            sleepNow();
        }
    }

    void sleepNow() {
        DLOG("POWER deep sleep\n");
        Serial.flush();
        lcd.setBrightness(0);
        lcd.sleep();
        // The FT6336U keeps signalling touches on INT in its monitor mode.

        // Keep the backlight off and the LED dark while asleep.
        gpio_set_level(PIN_BL, 0);
        gpio_hold_en(PIN_BL);
        boardLed(false, false, false);
        gpio_deep_sleep_hold_en();

        esp_sleep_enable_ext0_wakeup(PIN_TOUCH_INT, 0);
        esp_deep_sleep_start();
    }
}
