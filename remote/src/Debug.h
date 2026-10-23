// Serial debug console: screenshots, injected touches, raw link traffic.
// tools/remote_cli.py is the host side.

#pragma once

#include <Arduino.h>
#include <functional>

namespace Debug {
    void begin();
    void poll();

    // Injected touch (from the serial "tap"/"hold" commands); the UI reads it
    // in place of the touch panel while it is active.
    bool injectedTouch(int& x, int& y);

    void dumpScreen();

    // Lines the console itself does not know go here.
    void setHandler(std::function<void(const char*)> handler);
}

#define DLOG(...) Serial.printf(__VA_ARGS__)
