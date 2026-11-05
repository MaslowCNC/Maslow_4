// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstdint>
#include <cstring>
#include <atomic>
#include <list>
#include <map>
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "Channel.h"

namespace WebUI {
    class WSChannel : public Channel {
    public:
        WSChannel(AsyncWebSocket* server, objnum_t clientNum, std::string session);

        size_t write(uint8_t c) override;
        size_t write(const uint8_t* buffer, size_t size) override;
        void   flush() override;

        Error pollLine(char* line) override;

        bool sendTXT(std::string_view s);

        inline size_t write(const char* s) { return write((uint8_t*)s, ::strlen(s)); }

        objnum_t id() { return _clientNum; }

        int      rx_buffer_available() override { return std::max(0, 256 - int(queued_bytes())); }
        uint32_t clientNum() { return _clientNum; };

        operator bool() const;

        ~WSChannel();

        int read() override;
        int available() override { return queued_bytes() + (_rtchar > -1); }

        void        autoReport() override;
        void        active(bool is_active);
        std::string session() { return _session; };

        // Time (millis()) of the last inbound frame - data, ping or pong.  Set
        // from the AsyncWebSocket event task and read from the WebUI poll task,
        // hence atomic.  reapStaleChannels() uses it only as a debounce: a
        // channel is removed when it is BOTH silent this long AND already
        // disconnected, so this is not an idle timeout on live connections.
        void     noteActivity(uint32_t now) { _lastActivityMs.store(now, std::memory_order_relaxed); }
        uint32_t lastActivityMs() const { return _lastActivityMs.load(std::memory_order_relaxed); }

    private:
        AsyncWebSocket* _server;
        objnum_t        _clientNum;
        std::string     _session;

        // WebSocket output is coalesced here and emitted as one frame per
        // ~WS_OUT_FLUSH_LEN bytes (or when idle - see pollLine()/flush()).
        // Sending each small line as its own frame costs a full-MSS oversized
        // pbuf per frame (lwip TCP_OVERSIZE == MSS), which shreds the largest
        // free heap block during the WebUI load burst.
        static constexpr size_t   WS_OUT_FLUSH_LEN = 1400;
        static constexpr uint32_t WS_OUT_IDLE_MS   = 8;
        std::string               _output_line;
        uint32_t                  _output_pending_since = 0;
        // Maslow: guards _output_line and _output_pending_since.  write() runs on the output
        // task, while pollLine() flushes from the polling task; without a lock, output appended
        // between a flush's send and its clear was lost (whole replies, "ok" included).
        SemaphoreHandle_t         _output_mutex    = xSemaphoreCreateMutex();
        unsigned long             _last_queue_full = 0;
        std::atomic<uint32_t>     _lastActivityMs { 0 };

        // may_block=false: if the client send queue is full, give up rather than
        // spin (used from pollLine(), which runs under AllChannels' poll mutex).
        bool send_frame(const uint8_t* out, size_t outlen, bool may_block);
        void flush_output(bool may_block = true);
        void flush_output_locked(bool may_block);  // caller holds _output_mutex

        // Instead of queueing realtime characters, we put them here
        // so they can be processed immediately during operations like
        // homing where GCode handling is blocked.
        int32_t _rtchar = -1;
    };

    // Upper bound on concurrent WebSocket clients. Each client carries a WSChannel
    // (line-assembly std::string) plus an AsyncWebSocket send queue of up to
    // WS_MAX_QUEUED_MESSAGES malloc'd frames, so this directly bounds WS heap use.
    // AsyncWebSocket's own default (DEFAULT_MAX_WS_CLIENTS) is 8; a CNC realistically
    // needs a browser plus a pendant or phone. Over the limit, the oldest is closed
    // (WSChannels::evictOldest, with a periodic cleanupClients() as a backstop).
#ifndef WEBUI_MAX_WS_CLIENTS
#    define WEBUI_MAX_WS_CLIENTS 4
#endif

    class WSChannels {
    private:
        static std::vector<WSChannel*> _wsChannels;  // List of channels by client ID
        static AsyncWebSocket*         _server;

        static WSChannel* _lastWSChannel;
        static WSChannel* getWSChannel(objnum_t pageid, std::string session);

        static std::vector<std::pair<pinnum_t, InputPin*>> _pins;

    public:
        static void removeChannel(WSChannel* channel);
        static void removeChannel(objnum_t num);

        static bool runGCode(uint32_t pageid, std::string_view cmd, std::string session);
        static bool sendError(uint32_t pageid, std::string error, std::string session);
        // Maslow: WebSocket close codes for the closes the firmware makes on purpose. The UI
        // (socket.js) shows a notice for these instead of reconnecting, so tabs don't fight.
        static constexpr uint16_t CLOSE_REPLACED = 4001;  // a newer socket from the same browser
        static constexpr uint16_t CLOSE_TOO_MANY = 4002;  // over WEBUI_MAX_WS_CLIENTS, oldest closed

        static void closeSessionChannels(const std::string& session, objnum_t exceptId = 0);
        // Maslow: close the oldest sockets (never keepId) until at most WEBUI_MAX_WS_CLIENTS remain
        static void evictOldest(AsyncWebSocket* server, objnum_t keepId);
        static void sendPing();

        // Close and remove any channel with no inbound traffic for stale_ms.
        // Safe to call from the WebUI poll task alongside sendPing().
        static void reapStaleChannels(AsyncWebSocket* server, uint32_t stale_ms);
        static void handleEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);

        static void showChannels();

        static void registerEvent(pinnum_t index, InputPin* obj);
    };
}
