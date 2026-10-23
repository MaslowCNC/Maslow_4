// Maslow 4.1 ESP-NOW remote: firmware for the ESP32-2432S028R display.

#include <Arduino.h>

#include "Debug.h"
#include "Display.h"
#include "Link.h"
#include "Machine.h"
#include "Ui.h"
#include "Views.h"

LGFX lcd;

// RGB LED (active low): green connected, blue pairing, red otherwise.
static constexpr int LED_R = 4, LED_G = 16, LED_B = 17;

static void setLed(bool r, bool g, bool b) {
    digitalWrite(LED_R, !r);
    digitalWrite(LED_G, !g);
    digitalWrite(LED_B, !b);
}

static void debugCommand(const char* c) {
    if (!strcmp(c, "pair")) {
        Link::startPairing();
    } else if (!strcmp(c, "forget")) {
        Link::forget();
    } else if (!strncmp(c, "send ", 5)) {
        Machine::send(c + 5);
    } else if (!strncmp(c, "rt ", 3)) {
        Machine::realtime((uint8_t)strtol(c + 3, nullptr, 16));
    } else if (!strncmp(c, "tab ", 4)) {
        Ui::showTab(atoi(c + 4));
    } else if (!strcmp(c, "redraw")) {
        Ui::redrawAll();
    } else if (!strcmp(c, "link")) {
        DLOG("LINK %s host=%s mac=%s ch=%u rssi=%d tx=%u rx=%u fail=%u\n", Link::stateName(), Link::hostname(),
             Link::peerMac(), Link::channel(), Link::rssi(), (unsigned)Link::txPackets(), (unsigned)Link::rxPackets(),
             (unsigned)Link::txFailures());
    } else if (!strcmp(c, "state")) {
        DLOG("STATE %s maslow=%d inches=%d mpos=%.3f,%.3f,%.3f wco=%d %.3f,%.3f,%.3f sd=%.1f ovr=%d,%d,%d\n",
             Machine::st.state, Machine::maslowState, Machine::inches, Machine::st.mpos[0], Machine::st.mpos[1],
             Machine::st.mpos[2], Machine::st.haveWco, Machine::st.wco[0], Machine::st.wco[1], Machine::st.wco[2],
             Machine::st.sdPercent, Machine::st.ovr[0], Machine::st.ovr[1], Machine::st.ovr[2]);
    } else {
        DLOG("? %s\n", c);
    }
}

void setup() {
    pinMode(LED_R, OUTPUT);
    pinMode(LED_G, OUTPUT);
    pinMode(LED_B, OUTPUT);
    setLed(false, false, false);
    Debug::begin();
    Debug::setHandler(debugCommand);
    Ui::begin();
    Link::begin();
    Machine::begin();
    if (Link::state() == Link::State::Unpaired) {
        showPairing();
    }
    DLOG("READY\n");
}

void loop() {
    Debug::poll();
    Link::loop();
    Machine::loop();
    Ui::loop();

    static uint32_t ledAt = 0;
    if (millis() - ledAt > 250) {
        ledAt = millis();
        switch (Link::state()) {
            case Link::State::Connected:
                setLed(Machine::stateIs("Alarm"), !Machine::stateIs("Alarm"), false);
                break;
            case Link::State::Pairing:
                setLed(false, false, (millis() / 250) & 1);
                break;
            default:
                setLed((millis() / 500) & 1, false, false);
                break;
        }
    }
    delay(1);
}
