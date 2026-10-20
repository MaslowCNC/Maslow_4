// Copyright 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// #include "../Machine/MachineConfig.h"
#include "TelnetClient.h"
#include "TelnetServer.h"

#ifdef ENABLE_WIFI

#    include "WifiServices.h"

#    include <WiFi.h>
#    include <lwip/sockets.h>
#    include <cerrno>

namespace WebUI {
    TelnetClient::TelnetClient(WiFiClient* wifiClient) : Channel("telnet"), _wifiClient(wifiClient), _fd(wifiClient->fd()) {
        _connectionTime = millis();
    }

    void TelnetClient::handle() {}

    void TelnetClient::closeOnDisconnect() {
        if (_state != -1 && !_wifiClient->connected()) {
            _state = -1;
            telnetServer._disconnected.push(this);
        }
    }

    void TelnetClient::releaseSocket() {
        // sendAll() only uses _fd, so it stops sending once this is -1; read() is no longer called
        _fd = -1;
        _wifiClient->stop();
    }

    void TelnetClient::flushRx() {
        Channel::flushRx();
    }

    size_t TelnetClient::write(uint8_t data) {
        return write(&data, 1);
    }

    size_t TelnetClient::write(const uint8_t* buffer, size_t length) {
        if (_state == -1 || _outputClosed) {
            return length;  // Closing; output is dropped
        }
        // Replace \n with \r\n
        size_t  rem      = length;
        uint8_t lastchar = '\0';
        size_t  j        = 0;
        while (rem) {
            const int bufsize = 128;
            uint8_t   modbuf[bufsize];
            // bufsize-1 in case the last character is \n
            size_t k = 0;
            while (rem && k < (bufsize - 1)) {
                uint8_t c = buffer[j++];
                if (c == '\n' && lastchar != '\r') {
                    modbuf[k++] = '\r';
                }
                lastchar    = c;
                modbuf[k++] = c;
                --rem;
            }
            if (k && !sendAll(modbuf, k)) {
                break;
            }
        }
        return length;
    }

    // Send without WiFiClient::write(), which waits up to 10 x 1 s for every chunk a peer does
    // not read.  This runs in the output task: while it waits, log lines back up and whoever
    // logs - usually the main loop, which feeds the 5 s task watchdog - blocks in send_line().
    // The socket's own send buffer absorbs normal bursts; a client that drains none of it for
    // STALL_TIMEOUT_MS gets no more output, and read() - which owns the socket - closes it.
    // Nothing here may log: the output task would wait on its own queue.
    bool TelnetClient::sendAll(const uint8_t* buffer, size_t length) {
        uint32_t lastProgress = millis();
        while (length) {
            int fd = _fd;
            if (fd < 0) {
                _outputClosed = true;
                return false;
            }
            int res = send(fd, buffer, length, MSG_DONTWAIT);
            if (res > 0) {
                buffer += res;
                length -= res;
                lastProgress = millis();
                continue;
            }
            if (res < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                _outputClosed = true;
                return false;
            }
            if (millis() - lastProgress >= STALL_TIMEOUT_MS) {
                _stalled      = true;
                _outputClosed = true;
                return false;
            }
            vTaskDelay(1);
        }
        return true;
    }

    int TelnetClient::peek(void) {
        return _wifiClient->peek();
    }

    int TelnetClient::available() {
        return _wifiClient->available();
    }

    int TelnetClient::rx_buffer_available() {
        return WIFI_CLIENT_READ_BUFFER_SIZE - available();
    }

    int TelnetClient::read(void) {
        if (_state == -1) {
            return -1;
        }
        if (_outputClosed) {
            // sendAll() gave up on this client; close it here, in the task that reads the socket.
            _wifiClient->stop();
            closeOnDisconnect();
            return -1;
        }
        auto ret = _wifiClient->read();
        if (ret < 0) {
            // calling _wifiClient->connected() is expensive when the client is
            // connected because it calls recv() to double check, so we check
            // infrequently, only after quite a few reads have returned no data
            if (++_state >= DISCONNECT_CHECK_COUNTS) {
                _state = 0;
                closeOnDisconnect();  // sets _state to -1 if disconnected
            }
        } else {
            // Reset the counter if we have data
            _state = 0;
            
            // Process telnet protocol sequences
            uint8_t byte = static_cast<uint8_t>(ret);
            
            switch (_telnetState) {
                case TELNET_NORMAL:
                    if (byte == IAC) {
                        _telnetState = TELNET_IAC_RECEIVED;
                        return read();  // Read next byte
                    }
                    break;
                    
                case TELNET_IAC_RECEIVED:
                    if (byte == IAC) {
                        // Escaped IAC (0xFF 0xFF means literal 0xFF)
                        _telnetState = TELNET_NORMAL;
                        return IAC;
                    } else if (byte == WILL || byte == WONT || byte == DO || byte == DONT) {
                        _telnetState = TELNET_NEGOTIATION;
                        return read();  // Read option byte
                    } else if (byte == SB) {
                        _telnetState = TELNET_SUBNEGOTIATION;
                        return read();  // Start consuming subnegotiation
                    } else {
                        // Unknown command, return to normal
                        _telnetState = TELNET_NORMAL;
                        return read();
                    }
                    break;
                    
                case TELNET_NEGOTIATION:
                    // Consume the option byte and return to normal
                    _telnetState = TELNET_NORMAL;
                    return read();  // Read next byte after negotiation
                    
                case TELNET_SUBNEGOTIATION:
                    if (byte == IAC) {
                        // Might be end of subnegotiation
                        _telnetState = TELNET_IAC_RECEIVED;
                    }
                    // Keep consuming bytes in subnegotiation
                    return read();
            }
        }
        return ret;
    }
    
    bool TelnetClient::realtimeOkay(char c) {
        // Block realtime commands during the grace period after connection
        // This prevents telnet negotiation bytes from triggering system resets
        uint32_t elapsedTime = millis() - _connectionTime;
        if (elapsedTime < NEGOTIATION_GRACE_PERIOD_MS) {
            return false;
        }
        return true;
    }

    TelnetClient::~TelnetClient() {
        delete _wifiClient;
    }
}

#endif
