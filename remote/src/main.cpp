#include <Arduino.h>
#include "Display.h"
#include "Debug.h"
#include "Link.h"

LGFX lcd;

void setup() {
    Debug::begin();
    lcd.init();
    lcd.setBrightness(200);
    lcd.fillScreen(TFT_BLACK);
    Link::begin();
    Link::onLine([](const char* l) { DLOG("RX %s\n", l); });
    Debug::setHandler([](const char* c) {
        if (!strcmp(c, "pair")) Link::startPairing();
        else if (!strcmp(c, "forget")) Link::forget();
        else if (!strncmp(c, "send ", 5)) DLOG("send %d\n", Link::sendLine(c + 5));
        else if (!strncmp(c, "rt ", 3)) DLOG("rt %d\n", Link::sendRealtime((uint8_t)strtol(c + 3, nullptr, 16)));
        else if (!strcmp(c, "link")) DLOG("LINK %s host=%s mac=%s ch=%u rssi=%d tx=%u rx=%u fail=%u pair=%s\n", Link::stateName(), Link::hostname(), Link::peerMac(), Link::channel(), Link::rssi(), Link::txPackets(), Link::rxPackets(), Link::txFailures(), Link::pairingStatus());
    });
}

void loop() {
    Debug::poll();
    Link::loop();
    delay(2);
}
