// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*
#include "../Channel.h"
#include <queue>
#include <list>
#include <utility>

#ifdef ENABLE_WIFI

#    include "../Settings.h"

#    include <WiFi.h>

class TelnetClient;

namespace WebUI {
    class TelnetServer {
        static const int DEFAULT_TELNET_STATE      = 1;
        static const int DEFAULT_TELNETSERVER_PORT = 23;

        static const int MAX_TELNET_PORT = 65001;
        static const int MIN_TELNET_PORT = 1;

        static const int MAX_TLNT_CLIENTS = 2;

        static const int FLUSHTIMEOUT = 500;

    public:
        TelnetServer();

        bool begin();
        void end();
        void handle();

        uint16_t port() { return _port; }

        std::queue<TelnetClient*> _disconnected;

        // Clients the board cut off for not reading wait here before being deleted: replies
        // already queued for the output task may still name them, and writing to a deleted client
        // crashes the board.  Their sockets are already released, so each holds only the small
        // client object.  Clients that disconnect normally are deleted at once, as before.
        static const uint32_t                      CLIENT_DELETE_DELAY_MS = 5000;
        std::list<std::pair<TelnetClient*, uint32_t>> _closing;

        ~TelnetServer();

    private:
        bool        _setupdone  = false;
        WiFiServer* _wifiServer = nullptr;
        uint16_t    _port       = 0;
    };

    extern TelnetServer telnetServer;
}

#endif
