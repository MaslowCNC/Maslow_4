// Copyright (c) 2024 -	Maslow
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "SpindleBoard.h"

#include "../Machine/MachineConfig.h"  // config->_uarts
#include "../System.h"                 // sys
#include "../Protocol.h"               // rtAlarm, ExecAlarm

#include <cstdlib>  // strtol
#include <cmath>    // fabsf

namespace Spindles {
    SpindleBoard* SpindleBoard::instance = nullptr;

    void SpindleBoard::init() {
        // Either an embedded "uart:" section or an external "uart_num:" reference.
        if (_uart) {
            _uart->begin();
        } else {
            _uart = config->_uarts[_uart_num];
            if (!_uart) {
                log_error("SpindleBoard: missing uart" << _uart_num << " section");
                return;
            }
        }

        is_reversable = false;

        if (_speeds.size() == 0) {
            // Default linear map so that the device speed equals the requested RPM.
            linearSpeeds(_max_rpm, 100.0f);
        }
        setupSpeeds(maxSpeed());

        _current_state = SpindleState::Disable;
        instance       = this;

        // Make sure the board is stopped at startup.
        sendSpeed(0);

        config_message();
    }

    void SpindleBoard::config_message() {
        log_info(name() << " Spindle on " << (_uart_num != -1 ? "uart" : "embedded uart")
                        << " phase_deg_per_mm:" << _phase_deg_per_mm << " max_rpm:" << _max_rpm);
    }

    void SpindleBoard::setState(SpindleState state, SpindleSpeed speed) {
        if (sys.abort()) {
            return;  // Block during abort.
        }

        // Always call mapSpeed() with the unmodified input so sys.spindle_speed is set.
        uint32_t dev_speed = mapSpeed(speed);
        if (state == SpindleState::Disable) {
            dev_speed = offSpeed();
        }

        sendSpeed(dev_speed);

        _current_state = state;
        spindleDelay(state, speed);
    }

    void IRAM_ATTR SpindleBoard::setSpeedfromISR(uint32_t dev_speed) {
        // UART transmission is unsafe from an ISR, so just record the request and
        // let update() flush it from task context.
        _pending_rpm = dev_speed;
        _rpm_dirty   = true;
    }

    void SpindleBoard::sendSpeed(uint32_t rpm) {
        if (!_uart || rpm == _last_sent_rpm) {
            return;
        }
        _last_sent_rpm = rpm;
        char    buf[24];
        int     n = snprintf(buf, sizeof(buf), "S%lu\n", (unsigned long)rpm);
        if (n > 0) {
            _uart->write((const uint8_t*)buf, (size_t)n);
        }
    }

    void SpindleBoard::sendPhase(float deg) {
        if (!_uart) {
            return;
        }
        if (_deg_sent && fabsf(deg - _last_sent_deg) < 0.05f) {
            return;
        }
        _deg_sent      = true;
        _last_sent_deg = deg;
        char buf[24];
        int  n = snprintf(buf, sizeof(buf), "Z%.2f\n", deg);
        if (n > 0) {
            _uart->write((const uint8_t*)buf, (size_t)n);
        }
    }

    void SpindleBoard::serviceStatus() {
        if (!_uart) {
            return;
        }
        while (_uart->available()) {
            char c = (char)_uart->read();
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                _rx_buf[_rx_len] = '\0';
                // Expected: "T:<state>,P:<deg>,R:<rpm>,F:<code>"
                const char* f = strstr(_rx_buf, "F:");
                if (f) {
                    uint8_t fault = (uint8_t)strtol(f + 2, nullptr, 10);
                    if (fault != 0 && _last_fault == 0) {
                        log_error("SpindleBoard reported fault " << (int)fault);
                        rtAlarm = ExecAlarm::SpindleControl;
                    }
                    _last_fault = fault;
                }
                _rx_len = 0;
            } else if (_rx_len < sizeof(_rx_buf) - 1) {
                _rx_buf[_rx_len++] = c;
            } else {
                _rx_len = 0;  // overflow: drop the malformed line
            }
        }
    }

    void SpindleBoard::update(float zPositionMM) {
        if (!_uart) {
            return;
        }
        if (_rpm_dirty) {
            _rpm_dirty = false;
            sendSpeed(_pending_rpm);
        }
        sendPhase(zPositionMM * _phase_deg_per_mm);
        serviceStatus();
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<SpindleBoard> registration("SpindleBoard");
    }
}
