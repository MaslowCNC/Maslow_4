#include "Link.h"
#include "Debug.h"
#include "ESPNowCrypto.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/queue.h>
#include <stddef.h>
#include <string.h>

// Wire format, identical to the machine side (ESPNowChannel.h / .cpp).
namespace {
    constexpr uint8_t PKT_DISCOVERY      = 0x01;
    constexpr uint8_t PKT_PAIR_CHALLENGE = 0x02;
    constexpr uint8_t PKT_DATA           = 0x03;
    constexpr uint8_t PKT_PAIR_CONFIRM   = 0x04;
    constexpr uint8_t PKT_REALTIME       = 0x05;
    constexpr uint8_t PKT_KEEPALIVE      = 0x06;
    constexpr uint8_t PKT_PAIR_RESULT    = 0x07;
    constexpr uint8_t PKT_PAIR_COMPLETE  = 0x08;

    constexpr uint8_t PAIRING_PROTO_V4  = 4;
    constexpr uint8_t PAIRING_MODE_PAIR = 1;
    constexpr size_t  HOSTNAME_SIZE     = 32;
    constexpr size_t  SESSION_ID_SIZE   = 16;
    constexpr size_t  PUBKEY_SIZE       = ESPNowCrypto::ECDH_PUBLIC_KEY_SIZE;

    constexpr size_t  MAX_PAYLOAD   = 250;
    constexpr size_t  TAG_SIZE      = 8;   // nonce(4) + counter(4)
    constexpr size_t  PAIR_TAG_SIZE = 16;
    constexpr size_t  FRAG_HDR      = 12;  // type + tag(8) + seq + idx + total
    constexpr size_t  FRAG_DATA     = MAX_PAYLOAD - FRAG_HDR;
    constexpr uint8_t MAX_FRAGS     = 8;

    constexpr size_t  AUTH_KEEPALIVE_SIZE = 1 + 4 + TAG_SIZE + 1;
    constexpr uint8_t KEEPALIVE_CONFIRMED = 0x01;

    struct __attribute__((packed)) DiscoveryPkt {
        uint8_t type, version, mode;
        uint8_t mac[6];
        uint8_t channel;
        uint8_t session_id[SESSION_ID_SIZE];
        uint8_t pubkey[PUBKEY_SIZE];
    };
    struct __attribute__((packed)) ChallengePkt {
        uint8_t type, version, mode;
        uint8_t mac[6];
        uint8_t channel;
        uint8_t dial_channel;
        uint8_t session_id[SESSION_ID_SIZE];
        uint8_t pubkey[PUBKEY_SIZE];
        char    hostname[HOSTNAME_SIZE];
    };
    struct __attribute__((packed)) ConfirmPkt {  // also PAIR_COMPLETE
        uint8_t type, version, mode;
        uint8_t mac[6];
        uint8_t session_id[SESSION_ID_SIZE];
        uint8_t auth_tag[PAIR_TAG_SIZE];
    };
    struct __attribute__((packed)) ResultPkt {
        uint8_t type, version, mode;
        uint8_t mac[6];
        uint8_t channel;
        uint8_t session_id[SESSION_ID_SIZE];
        char    hostname[HOSTNAME_SIZE];
        uint8_t auth_tag[PAIR_TAG_SIZE];
    };
    static_assert(sizeof(DiscoveryPkt) == 91, "discovery layout");
    static_assert(sizeof(ChallengePkt) == 124, "challenge layout");
    static_assert(sizeof(ConfirmPkt) == 41, "confirm layout");
    static_assert(sizeof(ResultPkt) == 74, "result layout");

    struct RxPacket {
        uint8_t  src[6];
        int8_t   rssi;
        uint8_t  len;
        uint8_t  data[MAX_PAYLOAD];
    };

    const uint8_t BROADCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    // Most routers sit on 1, 6 or 11, so try those first.
    const uint8_t SCAN_ORDER[] = { 1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13 };

    constexpr uint32_t PAIRING_TIMEOUT_MS   = 60000;
    constexpr uint32_t PAIR_DWELL_MS        = 700;   // per channel while discovering
    constexpr uint32_t PAIR_RESEND_MS       = 250;
    constexpr uint32_t PAIR_SETTLE_MS       = 1200;  // quiet time after the last RESULT
    constexpr uint32_t HELLO_INTERVAL_MS    = 400;
    constexpr uint32_t KEEPALIVE_MS         = 1000;
    constexpr uint32_t LINK_LOST_MS         = 3500;
    constexpr uint32_t SEARCH_BEFORE_SCAN_MS = 4000;
    constexpr uint32_t SCAN_DWELL_MS        = 500;

    QueueHandle_t rxQueue;

    Link::State linkState = Link::State::Unpaired;

    // Saved pairing
    bool    havePairing = false;
    uint8_t peerMacBytes[6];
    uint8_t lmk[16];
    uint8_t savedChannel = 1;
    char    host[HOSTNAME_SIZE + 1] = "";

    uint8_t  ourMacBytes[6];
    uint8_t  curChannel = 1;
    int      lastRssi   = 0;
    uint32_t stats_tx = 0, stats_rx = 0, stats_txfail = 0;

    // Session
    uint32_t rxNonce     = 0;  // we advertise this; the machine stamps with it
    uint32_t peerNonce   = 0;  // the machine advertises this; we stamp with it
    bool     peerKnown   = false;
    uint32_t txCounter   = 0;
    uint32_t replayTop   = 0;
    uint64_t replayMask  = 0;
    bool     confirmed   = false;
    uint32_t lastAuthRx  = 0;
    uint32_t lastHello   = 0;
    uint32_t lastKeepalive = 0;
    int      unanswered    = 0;  // keepalives sent since the last authenticated packet
    uint32_t lastPrompt  = 0;
    uint32_t searchStart = 0;
    uint32_t scanStepAt  = 0;
    size_t   scanIndex   = 0;
    bool     scanning    = false;
    uint8_t  txSeq       = 0;

    struct {
        bool     active;
        uint8_t  seq, total, received;
        uint32_t start;
        uint16_t sizes[MAX_FRAGS];
        uint8_t  data[MAX_FRAGS][FRAG_DATA];
    } frag;

    char   lineBuf[1024];
    size_t lineLen = 0;
    std::function<void(const char*)> lineHandler;

    // Pairing
    enum class PairPhase : uint8_t { Discover, AwaitResult, Completing };
    struct {
        PairPhase phase;
        uint32_t  started;
        uint32_t  phaseAt;
        uint32_t  lastSend;
        uint32_t  hopAt;
        size_t    hopIndex;
        uint8_t   privateKey[ESPNowCrypto::ECDH_PRIVATE_KEY_SIZE];
        DiscoveryPkt discovery;
        uint8_t   lmk[16];
        uint8_t   mac[6];
        uint8_t   channel;
        char      host[HOSTNAME_SIZE + 1];
        char      status[48];
    } pair;

    const char* macStr(const uint8_t* m) {
        static char buf[2][18];
        static int  which = 0;
        which ^= 1;
        snprintf(buf[which], sizeof(buf[which]), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
        return buf[which];
    }

    void setChannel(uint8_t ch) {
        if (ch < 1 || ch > 13) {
            return;
        }
        curChannel = ch;
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    }

    bool addPeer(const uint8_t* mac, const uint8_t* key) {
        esp_now_del_peer(mac);
        esp_now_peer_info_t p = {};
        memcpy(p.peer_addr, mac, 6);
        p.channel = 0;  // whatever channel we are on
        p.ifidx   = WIFI_IF_STA;
        p.encrypt = key != nullptr;
        if (key) {
            memcpy(p.lmk, key, 16);
        }
        return esp_now_add_peer(&p) == ESP_OK;
    }

    void send(const uint8_t* mac, const void* data, size_t len) {
        ++stats_tx;
        if (esp_now_send(mac, (const uint8_t*)data, len) != ESP_OK) {
            ++stats_txfail;
        }
    }

    void copyHost(char* out, const char* in) {
        memcpy(out, in, HOSTNAME_SIZE);
        out[HOSTNAME_SIZE] = 0;
    }

    void loadPairing() {
        Preferences p;
        p.begin("link", true);
        havePairing = p.getBytes("mac", peerMacBytes, 6) == 6 && p.getBytes("lmk", lmk, 16) == 16;
        savedChannel = p.getUChar("ch", 1);
        String h     = p.getString("host", "");
        strlcpy(host, h.c_str(), sizeof(host));
        p.end();
    }

    void savePairing() {
        Preferences p;
        p.begin("link", false);
        p.putBytes("mac", peerMacBytes, 6);
        p.putBytes("lmk", lmk, 16);
        p.putUChar("ch", savedChannel);
        p.putString("host", host);
        p.end();
    }

    void saveChannel() {
        Preferences p;
        p.begin("link", false);
        p.putUChar("ch", savedChannel);
        p.end();
    }

    // ---- Session -------------------------------------------------------

    void resetSession() {
        peerKnown  = false;
        confirmed  = false;
        replayTop  = 0;
        replayMask = 0;
        frag.active = false;
        lineLen    = 0;
    }

    void startSearching() {
        linkState   = Link::State::Searching;
        searchStart = millis();
        scanning    = false;
        lastHello   = 0;
        resetSession();
        setChannel(savedChannel);
    }

    bool acceptReplay(uint32_t nonce, uint32_t counter) {
        if (nonce != rxNonce || counter == 0) {
            return false;
        }
        if (counter > replayTop) {
            uint32_t shift = counter - replayTop;
            replayMask     = shift >= 64 ? 0 : replayMask << shift;
            replayMask |= 1;
            replayTop = counter;
            return true;
        }
        uint32_t diff = replayTop - counter;
        if (diff >= 64 || (replayMask & (1ULL << diff))) {
            return false;
        }
        replayMask |= 1ULL << diff;
        return true;
    }

    void stamp(uint8_t* tag) {
        uint32_t c = ++txCounter;
        memcpy(tag, &peerNonce, 4);
        memcpy(tag + 4, &c, 4);
    }

    void sendHello() {
        uint8_t pkt[5] = { PKT_KEEPALIVE };
        memcpy(pkt + 1, &rxNonce, 4);
        send(peerMacBytes, pkt, sizeof(pkt));
    }

    void sendAuthKeepalive() {
        if (!peerKnown) {
            return;
        }
        uint8_t pkt[AUTH_KEEPALIVE_SIZE] = { PKT_KEEPALIVE };
        memcpy(pkt + 1, &rxNonce, 4);
        stamp(pkt + 5);
        pkt[1 + 4 + TAG_SIZE] = 0;
        send(peerMacBytes, pkt, sizeof(pkt));
        lastKeepalive = millis();
        ++unanswered;
    }

    void noteAuthenticated(int8_t rssi) {
        lastAuthRx = millis();
        unanswered = 0;
        lastRssi   = rssi;
        if (savedChannel != curChannel) {
            savedChannel = curChannel;
            saveChannel();
            DLOG("LINK machine found on channel %u\n", curChannel);
        }
        scanning = false;
    }

    void deliverBytes(const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            char c = (char)p[i];
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                lineBuf[lineLen] = 0;
                if (lineLen && lineHandler) {
                    lineHandler(lineBuf);
                }
                lineLen = 0;
            } else if (lineLen + 1 < sizeof(lineBuf)) {
                lineBuf[lineLen++] = c;
            }
        }
    }

    void handleKeepalive(const RxPacket& pk) {
        if (pk.len == 5) {
            memcpy(&peerNonce, pk.data + 1, 4);
            peerKnown = peerNonce != 0;
            return;
        }
        if (pk.len != AUTH_KEEPALIVE_SIZE) {
            return;
        }
        uint32_t adv, nonce, counter;
        memcpy(&adv, pk.data + 1, 4);
        memcpy(&nonce, pk.data + 5, 4);
        memcpy(&counter, pk.data + 9, 4);
        if (adv == 0 || !acceptReplay(nonce, counter)) {
            return;
        }
        peerNonce = adv;
        peerKnown = true;
        noteAuthenticated(pk.rssi);
        bool wasConfirmed = confirmed;
        confirmed = pk.data[1 + 4 + TAG_SIZE] & KEEPALIVE_CONFIRMED;
        if (!confirmed) {
            // The machine knows our nonce but has not authenticated us yet.
            if ((uint32_t)(millis() - lastPrompt) > 100) {
                lastPrompt = millis();
                sendAuthKeepalive();
            }
        } else if (!wasConfirmed || linkState != Link::State::Connected) {
            linkState = Link::State::Connected;
            DLOG("LINK connected to %s (%s) ch %u\n", host, macStr(peerMacBytes), curChannel);
        }
    }

    void handleData(const RxPacket& pk) {
        if (pk.len < FRAG_HDR) {
            return;
        }
        uint32_t nonce, counter;
        memcpy(&nonce, pk.data + 1, 4);
        memcpy(&counter, pk.data + 5, 4);
        uint8_t seq = pk.data[9], idx = pk.data[10], total = pk.data[11];
        size_t  n   = pk.len - FRAG_HDR;
        if (total == 0 || total > MAX_FRAGS || idx >= total || !acceptReplay(nonce, counter)) {
            return;
        }
        noteAuthenticated(pk.rssi);

        if (total == 1) {
            frag.active = false;
            deliverBytes(pk.data + FRAG_HDR, n);
            return;
        }
        if (frag.active && (frag.seq != seq || frag.total != total || (uint32_t)(millis() - frag.start) > 3000)) {
            frag.active = false;
        }
        if (!frag.active) {
            frag.active   = true;
            frag.seq      = seq;
            frag.total    = total;
            frag.received = 0;
            frag.start    = millis();
        }
        memcpy(frag.data[idx], pk.data + FRAG_HDR, n);
        frag.sizes[idx] = n;
        frag.received |= 1u << idx;
        uint8_t full = (uint8_t)((1u << total) - 1u);
        if ((frag.received & full) == full) {
            for (uint8_t i = 0; i < total; ++i) {
                deliverBytes(frag.data[i], frag.sizes[i]);
            }
            frag.active = false;
        }
    }

    void sessionTick() {
        uint32_t now = millis();
        if (linkState == Link::State::Connected) {
            // Silence alone is not enough: if this loop stalled (a long redraw)
            // we sent nothing to answer.  Require unanswered keepalives too.
            if ((uint32_t)(now - lastAuthRx) > LINK_LOST_MS && unanswered >= 3) {
                DLOG("LINK lost (no reply for %u ms)\n", (unsigned)(now - lastAuthRx));
                startSearching();
                return;
            }
            if ((uint32_t)(now - lastKeepalive) >= KEEPALIVE_MS) {
                sendAuthKeepalive();
            }
            return;
        }
        // Searching
        if ((uint32_t)(now - lastHello) >= HELLO_INTERVAL_MS) {
            lastHello = now;
            sendHello();
            sendAuthKeepalive();
        }
        bool heardRecently = lastAuthRx && (uint32_t)(now - lastAuthRx) < LINK_LOST_MS;
        if (!scanning && !heardRecently && (uint32_t)(now - searchStart) > SEARCH_BEFORE_SCAN_MS) {
            scanning   = true;
            scanIndex  = 0;
            scanStepAt = 0;
            DLOG("LINK no answer on channel %u, scanning\n", curChannel);
        }
        if (scanning && (uint32_t)(now - scanStepAt) >= SCAN_DWELL_MS) {
            scanStepAt = now;
            setChannel(SCAN_ORDER[scanIndex++ % sizeof(SCAN_ORDER)]);
            sendHello();
            lastHello = now;
        }
    }

    // ---- Pairing -------------------------------------------------------

    void pairStatus(const char* s) {
        strlcpy(pair.status, s, sizeof(pair.status));
        DLOG("PAIR %s\n", s);
    }

    void pairFail(const char* why) {
        pairStatus(why);
        esp_now_del_peer(BROADCAST);
        if (pair.mac[0] | pair.mac[1] | pair.mac[2] | pair.mac[3] | pair.mac[4] | pair.mac[5]) {
            esp_now_del_peer(pair.mac);
        }
        ESPNowCrypto::secureZero(pair.privateKey, sizeof(pair.privateKey));
        ESPNowCrypto::secureZero(pair.lmk, sizeof(pair.lmk));
        if (havePairing) {
            addPeer(peerMacBytes, lmk);
            startSearching();
        } else {
            linkState = Link::State::Unpaired;
        }
    }

    void sendDiscovery() {
        pair.discovery.channel = curChannel;
        send(BROADCAST, &pair.discovery, sizeof(pair.discovery));
        pair.lastSend = millis();
    }

    void sendConfirmLike(uint8_t type) {
        ConfirmPkt c = {};
        c.type    = type;
        c.version = PAIRING_PROTO_V4;
        c.mode    = PAIRING_MODE_PAIR;
        memcpy(c.mac, ourMacBytes, 6);
        memcpy(c.session_id, pair.discovery.session_id, SESSION_ID_SIZE);
        ESPNowCrypto::pairingAuthTag(pair.lmk, (const uint8_t*)&c, offsetof(ConfirmPkt, auth_tag), c.auth_tag);
        send(pair.mac, &c, sizeof(c));
        pair.lastSend = millis();
    }

    void handleChallenge(const RxPacket& pk) {
        if (pair.phase != PairPhase::Discover || pk.len != sizeof(ChallengePkt)) {
            return;
        }
        ChallengePkt c;
        memcpy(&c, pk.data, sizeof(c));
        if (c.version != PAIRING_PROTO_V4 || c.mode != PAIRING_MODE_PAIR || memcmp(c.mac, pk.src, 6) != 0 ||
            memcmp(c.session_id, pair.discovery.session_id, SESSION_ID_SIZE) != 0 || c.pubkey[0] != 0x04) {
            return;
        }
        // The machine hashed the discovery packet it received, which carried
        // the channel we were on at the time: the one it echoes as dial_channel.
        DiscoveryPkt transcript = pair.discovery;
        transcript.channel      = c.dial_channel;

        uint8_t secret[ESPNowCrypto::ECDH_SHARED_SECRET_SIZE];
        uint8_t windowLmk[16];
        if (!ESPNowCrypto::deriveEcdhSharedSecret(pair.privateKey, c.pubkey, secret)) {
            pairFail("Key exchange failed");
            return;
        }
        ESPNowCrypto::derivePairingWindowLmk(windowLmk);
        ESPNowCrypto::derivePairingSessionLmk(
            windowLmk, secret, (const uint8_t*)&transcript, sizeof(transcript), (const uint8_t*)&c, sizeof(c), pair.lmk);
        ESPNowCrypto::secureZero(secret, sizeof(secret));
        ESPNowCrypto::secureZero(windowLmk, sizeof(windowLmk));
        ESPNowCrypto::secureZero(pair.privateKey, sizeof(pair.privateKey));

        memcpy(pair.mac, c.mac, 6);
        pair.channel = c.channel;
        copyHost(pair.host, c.hostname);
        if (c.channel != curChannel) {
            setChannel(c.channel);
        }
        if (!addPeer(pair.mac, nullptr)) {
            pairFail("Could not add peer");
            return;
        }
        pair.phase   = PairPhase::AwaitResult;
        pair.phaseAt = millis();
        char s[48];
        snprintf(s, sizeof(s), "Found %s, confirming", pair.host);
        pairStatus(s);
        sendConfirmLike(PKT_PAIR_CONFIRM);
    }

    void handleResult(const RxPacket& pk) {
        if (pair.phase == PairPhase::Discover || pk.len != sizeof(ResultPkt) || memcmp(pk.src, pair.mac, 6) != 0) {
            return;
        }
        ResultPkt r;
        memcpy(&r, pk.data, sizeof(r));
        if (r.version != PAIRING_PROTO_V4 || r.mode != PAIRING_MODE_PAIR || memcmp(r.mac, pair.mac, 6) != 0 ||
            memcmp(r.session_id, pair.discovery.session_id, SESSION_ID_SIZE) != 0 ||
            !ESPNowCrypto::verifyPairingAuthTag(pair.lmk, (const uint8_t*)&r, offsetof(ResultPkt, auth_tag), r.auth_tag)) {
            return;
        }
        if (pair.phase == PairPhase::AwaitResult) {
            pair.phase = PairPhase::Completing;
            pairStatus("Completing");
        }
        pair.channel = r.channel;
        pair.phaseAt = millis();  // settle timer restarts on every RESULT
        sendConfirmLike(PKT_PAIR_COMPLETE);
    }

    void finishPairing() {
        esp_now_del_peer(BROADCAST);
        if (havePairing && memcmp(peerMacBytes, pair.mac, 6) != 0) {
            esp_now_del_peer(peerMacBytes);
        }
        memcpy(peerMacBytes, pair.mac, 6);
        memcpy(lmk, pair.lmk, 16);
        ESPNowCrypto::secureZero(pair.lmk, sizeof(pair.lmk));
        strlcpy(host, pair.host, sizeof(host));
        savedChannel = pair.channel ? pair.channel : curChannel;
        havePairing  = true;
        savePairing();
        if (!addPeer(peerMacBytes, lmk)) {
            DLOG("PAIR failed to add encrypted peer\n");
        }
        char s[48];
        snprintf(s, sizeof(s), "Paired with %s", host);
        pairStatus(s);
        startSearching();
    }

    void pairingTick() {
        uint32_t now = millis();
        if ((uint32_t)(now - pair.started) > PAIRING_TIMEOUT_MS) {
            pairFail("No machine answered");
            return;
        }
        switch (pair.phase) {
            case PairPhase::Discover:
                if ((uint32_t)(now - pair.hopAt) >= PAIR_DWELL_MS) {
                    pair.hopAt = now;
                    setChannel(SCAN_ORDER[pair.hopIndex++ % sizeof(SCAN_ORDER)]);
                    char s[48];
                    snprintf(s, sizeof(s), "Searching channel %u", curChannel);
                    strlcpy(pair.status, s, sizeof(pair.status));
                    sendDiscovery();
                } else if ((uint32_t)(now - pair.lastSend) >= PAIR_RESEND_MS) {
                    sendDiscovery();
                }
                break;
            case PairPhase::AwaitResult:
                if ((uint32_t)(now - pair.phaseAt) > 10000) {
                    pairFail("Machine did not confirm");
                } else if ((uint32_t)(now - pair.lastSend) >= 300) {
                    sendConfirmLike(PKT_PAIR_CONFIRM);
                }
                break;
            case PairPhase::Completing:
                if ((uint32_t)(now - pair.phaseAt) >= PAIR_SETTLE_MS) {
                    finishPairing();
                }
                break;
        }
    }

    // ---- ESP-NOW callbacks (WiFi task) ---------------------------------

#if ESP_IDF_VERSION_MAJOR >= 5
    void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
        const uint8_t* src  = info ? info->src_addr : nullptr;
        int8_t         rssi = (info && info->rx_ctrl) ? info->rx_ctrl->rssi : 0;
#else
    void onRecv(const uint8_t* src, const uint8_t* data, int len) {
        int8_t rssi = 0;
#endif
        if (!src || !data || len < 1 || len > (int)MAX_PAYLOAD) {
            return;
        }
        RxPacket p;
        memcpy(p.src, src, 6);
        p.rssi = rssi;
        p.len  = (uint8_t)len;
        memcpy(p.data, data, len);
        xQueueSend(rxQueue, &p, 0);
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
    void onSent(const esp_now_send_info_t*, esp_now_send_status_t status) {
#else
    void onSent(const uint8_t*, esp_now_send_status_t status) {
#endif
        if (status != ESP_NOW_SEND_SUCCESS) {
            ++stats_txfail;
        }
    }

    void process(const RxPacket& pk) {
        ++stats_rx;
        uint8_t type = pk.data[0];
        if (linkState == Link::State::Pairing) {
            if (type == PKT_PAIR_CHALLENGE) {
                handleChallenge(pk);
            } else if (type == PKT_PAIR_RESULT) {
                handleResult(pk);
            }
            return;
        }
        if (!havePairing || memcmp(pk.src, peerMacBytes, 6) != 0) {
            return;
        }
        if (type == PKT_KEEPALIVE) {
            handleKeepalive(pk);
        } else if (type == PKT_DATA) {
            handleData(pk);
        }
    }
}

namespace Link {
    void begin() {
        rxQueue = xQueueCreate(24, sizeof(RxPacket));
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(false, false);
        esp_wifi_set_ps(WIFI_PS_NONE);
        esp_wifi_get_mac(WIFI_IF_STA, ourMacBytes);
        if (esp_now_init() != ESP_OK) {
            DLOG("LINK esp_now_init failed\n");
            return;
        }
        uint8_t pmk[16];
        ESPNowCrypto::derivePmk(pmk);
        esp_now_set_pmk(pmk);
        esp_now_register_recv_cb(onRecv);
        esp_now_register_send_cb(onSent);

        do {
            rxNonce = esp_random();
        } while (rxNonce == 0);

        loadPairing();
        DLOG("LINK our MAC %s, %s\n", macStr(ourMacBytes), havePairing ? "paired" : "not paired");
        if (havePairing) {
            DLOG("LINK machine %s (%s) channel %u\n", host, macStr(peerMacBytes), savedChannel);
            addPeer(peerMacBytes, lmk);
            startSearching();
        } else {
            linkState = State::Unpaired;
        }
    }

    void loop() {
        RxPacket pk;
        while (xQueueReceive(rxQueue, &pk, 0) == pdTRUE) {
            process(pk);
        }
        switch (linkState) {
            case State::Pairing:
                pairingTick();
                break;
            case State::Searching:
            case State::Connected:
                sessionTick();
                break;
            default:
                break;
        }
    }

    State state() { return linkState; }

    const char* stateName() {
        switch (linkState) {
            case State::Unpaired:
                return "Not paired";
            case State::Pairing:
                return "Pairing";
            case State::Searching:
                return "Searching";
            case State::Connected:
                return "Connected";
        }
        return "?";
    }

    const char* hostname() { return host; }
    const char* peerMac() { return havePairing ? macStr(peerMacBytes) : ""; }
    const char* ourMac() { return macStr(ourMacBytes); }
    uint8_t     channel() { return curChannel; }
    int         rssi() { return lastRssi; }
    uint32_t    lastRxMs() { return lastAuthRx; }
    const char* pairingStatus() { return pair.status; }

    void startPairing() {
        memset(&pair, 0, sizeof(pair));
        pair.phase   = PairPhase::Discover;
        pair.started = millis();
        pair.hopAt   = 0;
        pairStatus("Generating keys");
        if (!ESPNowCrypto::generateEcdhKeypair(pair.privateKey, pair.discovery.pubkey)) {
            pairFail("Key generation failed");
            return;
        }
        pair.discovery.type    = PKT_DISCOVERY;
        pair.discovery.version = PAIRING_PROTO_V4;
        pair.discovery.mode    = PAIRING_MODE_PAIR;
        memcpy(pair.discovery.mac, ourMacBytes, 6);
        do {
            esp_fill_random(pair.discovery.session_id, SESSION_ID_SIZE);
        } while (!(pair.discovery.session_id[0] | pair.discovery.session_id[1]));
        addPeer(BROADCAST, nullptr);
        linkState = State::Pairing;
        pairStatus("Searching");
    }

    void cancelPairing() {
        if (linkState == State::Pairing) {
            pairFail("Cancelled");
        }
    }

    void forget() {
        if (havePairing) {
            esp_now_del_peer(peerMacBytes);
        }
        havePairing = false;
        memset(peerMacBytes, 0, 6);
        ESPNowCrypto::secureZero(lmk, sizeof(lmk));
        host[0] = 0;
        Preferences p;
        p.begin("link", false);
        p.clear();
        p.end();
        resetSession();
        linkState = State::Unpaired;
    }

    bool sendLine(const char* line) {
        if (linkState != State::Connected || !peerKnown) {
            return false;
        }
        size_t len = strlen(line);
        bool   nl  = len && line[len - 1] == '\n';
        size_t total_len = len + (nl ? 0 : 1);
        uint8_t frags = (uint8_t)((total_len + FRAG_DATA - 1) / FRAG_DATA);
        if (frags == 0 || frags > MAX_FRAGS) {
            return false;
        }
        uint8_t seq = txSeq++;
        uint8_t pkt[MAX_PAYLOAD];
        size_t  off = 0;
        for (uint8_t i = 0; i < frags; ++i) {
            size_t chunk = total_len - off;
            if (chunk > FRAG_DATA) {
                chunk = FRAG_DATA;
            }
            pkt[0] = PKT_DATA;
            stamp(pkt + 1);
            pkt[9]  = seq;
            pkt[10] = i;
            pkt[11] = frags;
            for (size_t j = 0; j < chunk; ++j) {
                size_t k        = off + j;
                pkt[FRAG_HDR + j] = k < len ? (uint8_t)line[k] : '\n';
            }
            send(peerMacBytes, pkt, FRAG_HDR + chunk);
            off += chunk;
        }
        return true;
    }

    bool sendRealtime(uint8_t cmd) {
        if (linkState != State::Connected || !peerKnown) {
            return false;
        }
        uint8_t pkt[1 + TAG_SIZE + 1] = { PKT_REALTIME };
        stamp(pkt + 1);
        pkt[1 + TAG_SIZE] = cmd;
        send(peerMacBytes, pkt, sizeof(pkt));
        return true;
    }

    void onLine(std::function<void(const char*)> handler) { lineHandler = handler; }

    uint32_t txPackets() { return stats_tx; }
    uint32_t rxPackets() { return stats_rx; }
    uint32_t txFailures() { return stats_txfail; }
}
