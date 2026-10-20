// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*
#include "../Channel.h"

#ifdef ENABLE_WIFI
#    include <WiFi.h>

namespace WebUI {
    class TelnetClient : public Channel {
        WiFiClient*  _wifiClient;
        volatile int _fd;  // The socket, for sendAll(); -1 once releaseSocket() closed it

        // The default value of the rx buffer in WiFiClient.cpp is 1436 which is
        // related to the network frame size minus TCP/IP header sizes.
        // The WiFiClient API has no way to override or query it.
        // We use a smaller value for safety.  There is little advantage
        // to sending too many GCode lines at once, especially since the
        // common serial communication case is typically limited to 128 bytes.
        static const int WIFI_CLIENT_READ_BUFFER_SIZE = 1200;

        static const int DISCONNECT_CHECK_COUNTS = 1000;

        int _state = 0;

        // Telnet protocol constants
        static const uint8_t IAC  = 0xFF;  // Interpret As Command
        static const uint8_t WILL = 0xFB;
        static const uint8_t WONT = 0xFC;
        static const uint8_t DO   = 0xFD;
        static const uint8_t DONT = 0xFE;
        static const uint8_t SB   = 0xFA;  // Subnegotiation Begin
        static const uint8_t SE   = 0xF0;  // Subnegotiation End

        // Grace period to ignore realtime commands during telnet negotiation (milliseconds)
        static const uint32_t NEGOTIATION_GRACE_PERIOD_MS = 2000;

        uint32_t _connectionTime = 0;

        // A client whose socket buffer stays full for this long is closed (see sendAll()).  It
        // bounds how long one stuck client can hold up all output - and, once the output queue
        // fills, the main loop - while leaving room for WiFi retransmits to a client that reads.
        static constexpr uint32_t STALL_TIMEOUT_MS = 200;

        bool sendAll(const uint8_t* buffer, size_t length);
        
        enum TelnetState {
            TELNET_NORMAL,
            TELNET_IAC_RECEIVED,
            TELNET_NEGOTIATION,
            TELNET_SUBNEGOTIATION
        };
        
        TelnetState _telnetState = TELNET_NORMAL;

    public:
        TelnetClient(WiFiClient* wifiClient);

        int    rx_buffer_available() override;
        size_t write(uint8_t data) override;
        size_t write(const uint8_t* buffer, size_t size) override;
        int    read(void) override;
        int    peek(void) override;
        int    available() override;
        void   flush() override {}
        void   flushRx() override;
        
        // Override realtimeOkay to block realtime commands during negotiation
        bool realtimeOkay(char c) override;

        void closeOnDisconnect();
        void releaseSocket();  // After closeOnDisconnect(): closes the socket and frees its buffers

        volatile bool _stalled      = false;  // Closed because it stopped reading output
        volatile bool _outputClosed = false;  // No more output; read() then closes the socket

        void handle() override;

        ~TelnetClient();
    };
}
#endif
