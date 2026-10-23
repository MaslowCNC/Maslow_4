#include "Power.h"
#include "Debug.h"
#include "Display.h"
#include "Machine.h"
#include "Ui.h"

#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

namespace {
    constexpr gpio_num_t PIN_PENIRQ = GPIO_NUM_36;
    constexpr gpio_num_t PIN_BL     = GPIO_NUM_21;
    // RGB LED, active low
    constexpr gpio_num_t LEDS[] = { GPIO_NUM_4, GPIO_NUM_16, GPIO_NUM_17 };
    // XPT2046 bus (bit-banged for the power-down command)
    constexpr int T_CLK = 25, T_MOSI = 32, T_MISO = 39, T_CS = 33;

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

    // A conversion command with PD1:PD0 = 00 leaves the XPT2046 powered down
    // between conversions with PENIRQ enabled, which is what wakes us.
    void touchPowerDownWithIrq() {
        pinMode(T_CS, OUTPUT);
        pinMode(T_CLK, OUTPUT);
        pinMode(T_MOSI, OUTPUT);
        pinMode(T_MISO, INPUT);
        digitalWrite(T_CLK, LOW);
        digitalWrite(T_CS, LOW);
        uint8_t cmd = 0xD0;  // start, X position, 12 bit, differential, PD=00
        for (int i = 7; i >= 0; --i) {
            digitalWrite(T_MOSI, (cmd >> i) & 1);
            delayMicroseconds(2);
            digitalWrite(T_CLK, HIGH);
            delayMicroseconds(2);
            digitalWrite(T_CLK, LOW);
        }
        digitalWrite(T_MOSI, LOW);
        for (int i = 0; i < 16; ++i) {  // clock out the result
            delayMicroseconds(2);
            digitalWrite(T_CLK, HIGH);
            delayMicroseconds(2);
            digitalWrite(T_CLK, LOW);
        }
        digitalWrite(T_CS, HIGH);
    }

    bool touched() { return gpio_get_level(PIN_PENIRQ) == 0; }
}

namespace Power {
    void begin() {
        // Pins held through the last deep sleep must be released before use.
        gpio_hold_dis(PIN_BL);
        for (auto p : LEDS) gpio_hold_dis(p);
        gpio_deep_sleep_hold_dis();

        Preferences p;
        p.begin("ui", true);
        screenSec = p.getUShort("scr", 60);
        sleepMin  = p.getUShort("slp", 10);
        p.end();

        woke = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
        if (woke) {
            // The waking touch must not land on a button: wait for the release.
            pinMode((int)PIN_PENIRQ, INPUT);
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
        touchPowerDownWithIrq();

        // Keep the backlight off and the LED dark while asleep.
        gpio_set_level(PIN_BL, 0);
        gpio_hold_en(PIN_BL);
        for (auto p : LEDS) {
            pinMode((int)p, OUTPUT);
            digitalWrite((int)p, HIGH);
            gpio_hold_en(p);
        }
        gpio_deep_sleep_hold_en();

        esp_sleep_enable_ext0_wakeup(PIN_PENIRQ, 0);
        esp_deep_sleep_start();
    }
}
