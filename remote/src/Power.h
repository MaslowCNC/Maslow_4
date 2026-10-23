// Screen-off and deep sleep.
//
// After the screen timeout the backlight goes off; the radio stays up, so the
// link and STOP keep working and the next touch turns the screen back on.
// After the sleep timeout, and only while the machine is not running a job or
// a Maslow belt operation, the remote deep-sleeps.  The XPT2046 touch
// controller pulls PENIRQ (GPIO 36) low on a touch, which wakes the ESP32;
// waking is a reboot, and the link reconnects in about a second.

#pragma once

#include <Arduino.h>

namespace Power {
    void begin();  // first thing in setup()
    void loop();

    void activity();  // a touch or something worth showing: restart the timers
    bool screenOff();
    void wakeScreen();
    bool wokeFromSleep();

    // Timeouts; 0 means never.
    uint16_t screenTimeoutSec();
    uint16_t sleepTimeoutMin();
    void     setScreenTimeoutSec(uint16_t s);
    void     setSleepTimeoutMin(uint16_t m);

    bool canSleep();  // false while a job or belt operation is running
    void sleepNow();
}
