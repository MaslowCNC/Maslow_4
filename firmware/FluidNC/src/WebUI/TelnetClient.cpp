// Copyright 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// #include "../Machine/MachineConfig.h"
#include "TelnetClient.h"
#include "TelnetServer.h"

#ifdef ENABLE_WIFI

#    include "WifiServices.h"

#    include <WiFi.h>
#    include <lwip/sockets.h>
#    include <algorithm>
#    include <cerrno>

namespace WebUI {
    TelnetClient::TelnetClient(WiFiClient* wifiClient) : Channel("telnet"), _wifiClient(wifiClient) {
        _connectionTime = millis();
    }

    // Output is queued in _tx by write() and sent from here, in the polling task, with
    // non-blocking sends.  WiFiClient::write() waits up to 10 x 1 s for each chunk a peer does not
    // read; write() runs in the output task, so a stuck client used to stall every log line, and
    // whoever logs - usually the main loop - blocked in send_line() until the task watchdog reset
    // the board.
    void TelnetClient::handle() {
        if (_state == -1 || _outputClosed) {
            return;
        }
        int fd = _wifiClient->fd();
        while (true) {
            size_t start, chunk;
            portENTER_CRITICAL(&_txLock);
            start = _txHead;
            chunk = std::min(_txLen, TX_BUFFER_SIZE - start);
            portEXIT_CRITICAL(&_txLock);
            if (chunk == 0) {
                return;
            }
            // write() only fills the free part of the ring, so this region is stable while unlocked
            int res = fd < 0 ? -1 : send(fd, _tx + start, chunk, MSG_DONTWAIT);
            if (res <= 0) {
                if (res < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    _outputClosed = true;  // read() closes the socket
                }
                return;  // Socket buffer full; try again on the next poll
            }
            portENTER_CRITICAL(&_txLock);
            _txHead = (_txHead + res) % TX_BUFFER_SIZE;
            _txLen -= res;
            portEXIT_CRITICAL(&_txLock);
        }
    }

    void TelnetClient::closeOnDisconnect() {
        if (_state != -1 && !_wifiClient->connected()) {
            _state = -1;
            telnetServer._disconnected.push(this);
        }
    }

    void TelnetClient::flushRx() {
        Channel::flushRx();
    }

    size_t TelnetClient::write(uint8_t data) {
        return write(&data, 1);
    }

    // Never blocks: it may run in the output task.  Nothing here may log, or the output task would
    // wait on its own queue.
    size_t TelnetClient::write(const uint8_t* buffer, size_t length) {
        if (_state == -1 || _outputClosed) {
            return length;  // Closing; output is dropped
        }
        portENTER_CRITICAL(&_txLock);
        for (size_t i = 0; i < length; i++) {
            uint8_t c = buffer[i];
            // Replace \n with \r\n
            size_t needed = (c == '\n' && _lastTx != '\r') ? 2 : 1;
            if (TX_BUFFER_SIZE - _txLen < needed) {
                // The client has stopped reading: the socket buffer and _tx are both full.
                _stalled      = true;
                _outputClosed = true;  // read() closes the socket
                break;
            }
            if (needed == 2) {
                _tx[(_txHead + _txLen++) % TX_BUFFER_SIZE] = '\r';
            }
            _tx[(_txHead + _txLen++) % TX_BUFFER_SIZE] = c;
            _lastTx                                     = c;
        }
        portEXIT_CRITICAL(&_txLock);
        return length;
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
            // write() or handle() gave up on this client; close it here, in the task that reads the socket.
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
