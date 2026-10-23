// ESP-NOW link to a Maslow (FluidNC ESPNowChannel), peripheral side.
//
// Pairing (protocol v4, mirrors firmware/FluidNC/esp32/ESPNow/ESPNowChannel.cpp):
//   remote  -> broadcast DISCOVERY  (session id, ECDH public key, our channel)
//   machine -> PAIR_CHALLENGE       (its MAC, WiFi channel, public key, hostname)
//   remote  -> PAIR_CONFIRM         (HMAC with the derived LMK)
//   machine -> PAIR_RESULT          (HMAC)
//   remote  -> PAIR_COMPLETE        (HMAC); both ends then use an encrypted
//                                    ESP-NOW peer keyed with the LMK.
// The machine only answers while its pairing window is open ($EP / $ESPNow/Pair).
//
// Session: each side advertises a random nonce in KEEPALIVE packets and stamps
// everything it sends to the other side with (that side's nonce, counter).
// The machine drops a peripheral it has not heard from for 10 s, so we send an
// authenticated keepalive every second.

#pragma once

#include <Arduino.h>
#include <functional>

namespace Link {
    enum class State : uint8_t {
        Unpaired,   // nothing saved
        Pairing,    // looking for a machine with an open pairing window
        Searching,  // paired, no answer yet (scanning channels if it takes long)
        Connected,  // session confirmed by the machine
    };

    void begin();
    void loop();

    State       state();
    const char* stateName();
    const char* hostname();  // of the paired machine
    const char* peerMac();   // "aa:bb:cc:dd:ee:ff"
    const char* ourMac();
    uint8_t     channel();
    int         rssi();          // of the last packet from the machine
    uint32_t    lastRxMs();      // millis() of the last authenticated packet
    const char* pairingStatus(); // progress text while pairing

    void startPairing();
    void cancelPairing();
    void forget();

    // Text to the machine.  A '\n' is appended if missing.  False when not
    // connected or the line is too long for one message (8 x 238 bytes).
    bool sendLine(const char* line);
    // A single realtime command byte ('?', '!', '~', 0x90 ...).
    bool sendRealtime(uint8_t cmd);

    // Called from loop() for every complete line the machine sends.
    void onLine(std::function<void(const char*)> handler);

    // Statistics for the info screen.
    uint32_t txPackets();
    uint32_t rxPackets();
    uint32_t txFailures();
}
