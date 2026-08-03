// Copyright (c) 2024 -	Maslow
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "SpindleBoard.h"

#include "../Machine/MachineConfig.h"  // config->_uarts
#include "../System.h"                 // sys
#include "../Protocol.h"               // rtAlarm, ExecAlarm
#include "../GCode.h"                  // gc_sync_position
#include "../Planner.h"                // plan_sync_position
#include "../WebUI/WifiConfig.h"       // wifi_sta_ssid / wifi_sta_password (ENABLE_WIFI)

#include <cstdio>   // snprintf
#include <cstdlib>  // strtol
#include <cstring>  // strncmp
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

        // Confirm the spindle/Z controller board is connected and talking.
        handshake();

        // Push the configured suction/cooling fan power down to the spindle board.
        sendFan(_suction_power);
    }

    void SpindleBoard::handshake() {
        if (!_uart) {
            return;
        }

        log_info(name() << ": handshaking with spindle board over the inter-board link...");

        _uart->flushRx();

        char line[80];
        // Try for ~2 seconds (10 x 200 ms).  The spindle board needs a moment after
        // power-on before its control task starts answering, so a few misses are normal.
        for (int attempt = 1; attempt <= 10; attempt++) {
            _uart->write((const uint8_t*)"H\n", 2);

            uint32_t start   = millis();
            size_t   idx     = 0;
            bool     gotLine = false;
            while (millis() - start < 200) {
                while (_uart->available()) {
                    char c = (char)_uart->read();
                    if (c == '\r') {
                        continue;
                    }
                    if (c == '\n') {
                        line[idx] = '\0';
                        gotLine   = true;
                        break;
                    }
                    if (idx < sizeof(line) - 1) {
                        line[idx++] = c;
                    }
                }
                if (gotLine) {
                    break;
                }
                delay(5);
            }

            // An "I:" identity reply or a "T:" status line both prove the link is up.
            if (gotLine && (strncmp(line, "I:", 2) == 0 || strncmp(line, "T:", 2) == 0)) {
                _link_confirmed = true;
                log_info(name() << ": link ESTABLISHED with spindle board on attempt " << attempt << " -> " << line);
                return;
            }
        }

        log_warn(name() << ": NO response from spindle board during boot. Check the link wiring "
                           "(XY TX gpio.15 -> spindle RX gpio.39, XY RX gpio.16 <- spindle TX gpio.38), "
                           "a shared ground between the boards, and 115200 baud. "
                           "Will confirm automatically if the spindle board responds later.");
    }

    void SpindleBoard::config_message() {
        log_info(name() << " Spindle on " << (_uart_num != -1 ? "uart" : "embedded uart")
                        << " phase_deg_per_mm:" << _phase_deg_per_mm << " max_rpm:" << _max_rpm
                        << " suction_power:" << _suction_power);
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

    void SpindleBoard::sendFan(int level) {
        if (!_uart) {
            return;
        }
        if (level < 0) {
            level = 0;
        } else if (level > 100) {
            level = 100;
        }
        if (level == _last_sent_fan) {
            return;
        }
        _last_sent_fan = level;
        char buf[16];
        int  n = snprintf(buf, sizeof(buf), "C%d\n", level);
        if (n > 0) {
            _uart->write((const uint8_t*)buf, (size_t)n);
        }
    }

    void SpindleBoard::sendHoldRelease() {
        if (!_uart) {
            return;
        }
        _uart->write((const uint8_t*)"D\n", 2);
    }

    void SpindleBoard::sendHome() {
        if (!_uart) {
            log_error(name() << ": cannot home spindle Z - no link to the spindle board");
            return;
        }
        _uart->write((const uint8_t*)"G\n", 2);
        log_info(name() << ": sent Z homing request to the spindle board");
    }

    void SpindleBoard::sendRemoveTool() {
        if (!_uart) {
            log_error(name() << ": cannot remove tool - no link to the spindle board");
            return;
        }
        _uart->write((const uint8_t*)"R\n", 2);
        log_info(name() << ": sent tool removal request to the spindle board");
    }

    void SpindleBoard::sendEnableOTA() {
        if (!_uart) {
            log_error(name() << ": cannot enable spindle OTA - no link to the spindle board");
            return;
        }
#ifdef ENABLE_WIFI
        const char* ssid = WebUI::wifi_sta_ssid->get();
        const char* pass = WebUI::wifi_sta_password->get();
        if (!ssid || ssid[0] == '\0') {
            log_error(name() << ": cannot enable spindle OTA - the XY board has no station SSID configured");
            return;
        }
        // "W<ssid>\t<password>" - the spindle joins this network and starts ArduinoOTA.
        char buf[128];
        int  n = snprintf(buf, sizeof(buf), "W%s\t%s\n", ssid, pass ? pass : "");
        if (n <= 0 || n >= (int)sizeof(buf)) {
            log_error(name() << ": SSID/password too long to send to the spindle board");
            return;
        }
        _uart->write((const uint8_t*)buf, (size_t)n);
        log_info(name() << ": told spindle board to join '" << ssid
                        << "' and start OTA. When it reports its IP, run: "
                           "pio run -e esp32-s3-devkitc-1-ota -t upload");
#else
        log_error(name() << ": cannot enable spindle OTA - this XY firmware was built without WiFi");
#endif
    }

    void SpindleBoard::updateHoldState() {
        // The Z axis is driven by the spindle board's two BLDC motors.  While the
        // machine is idle and the spindle is stopped, those drivers would otherwise sit
        // energized just holding the Z position, keeping the drivers hot and the cooling
        // fan running.  Tell the board it may power them down once its move has settled.
        // As soon as the machine starts moving again, the streamed Z target re-energizes
        // the drivers, so we simply re-arm and re-send the release on the next idle.
        bool idle = (sys.state() == State::Idle || sys.state() == State::Sleep) && _current_state == SpindleState::Disable;

        if (idle == _hold_released) {
            return;  // already in the desired state; nothing to send
        }
        if (idle) {
            sendHoldRelease();
        }
        _hold_released = idle;
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
        updateHoldState();
        serviceStatus();
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
                // Any complete line from the board proves the link is alive.  Log the
                // first confirmation if the boot-time handshake did not already do so.
                if (!_link_confirmed && _rx_len > 0) {
                    _link_confirmed = true;
                    log_info(name() << ": link to spindle board confirmed -> " << _rx_buf);
                    // The board may have just (re)booted, so re-push the configured
                    // suction power in case it came up with a different default.
                    _last_sent_fan = -1;
                    sendFan(_suction_power);
                }
                // OTA lifecycle messages from the spindle board (in response to
                // $Spindle/EnableOTA).  Surface the address so the update can be pushed.
                if (strncmp(_rx_buf, "OTA_IP:", 7) == 0) {
                    log_info(name() << ": spindle board is on WiFi at " << (_rx_buf + 7)
                                    << " (also maslow-spindle.local). Reflash it with: "
                                       "pio run -e esp32-s3-devkitc-1-ota -t upload");
                } else if (strncmp(_rx_buf, "OTA_ERR:", 8) == 0) {
                    log_error(name() << ": spindle board could not start OTA (" << (_rx_buf + 8) << ")");
                } else if (strncmp(_rx_buf, "OTA_END:", 8) == 0) {
                    log_info(name() << ": spindle board left OTA mode (" << (_rx_buf + 8) << ")");
                } else if (strncmp(_rx_buf, "ERR:", 4) == 0) {
                    // Latched spindle fault detail (the F: field below raises the alarm).
                    log_error(name() << ": " << (_rx_buf + 4));
                } else if (strncmp(_rx_buf, "WARN:", 5) == 0) {
                    // Recoverable event, e.g. a transient over-current the board is auto-recovering.
                    log_warn(name() << ": " << (_rx_buf + 5));
                } else if (strncmp(_rx_buf, "MSG:", 4) == 0) {
                    // Informational event, e.g. the board has recovered and resumed.
                    log_info(name() << ": " << (_rx_buf + 4));
                }
                // The spindle board redefined its Z zero (homing / tool load / tool removal
                // finished).  Reset the XY board's Z-axis machine position to 0 so the two
                // boards' Z origins stay aligned; otherwise the next streamed Z target (based
                // on the XY board's own Z) would differ from where the spindle parked and
                // drive the Z straight back off home.  Only accept this while the machine is
                // idle/alarm/asleep so it can never disturb an active move.
                if (strncmp(_rx_buf, "ZHOMED", 6) == 0) {
                    State st = sys.state();
                    if (st == State::Idle || st == State::Alarm || st == State::Sleep) {
                        set_motor_steps(4, mpos_to_steps(0.0f, 4));  // Z is axis 4
                        gc_sync_position();                          // push into the gcode engine
                        plan_sync_position();                        // sync the planner so the first Z move plans from 0
                        // Also clear any lingering Z work offset so both machine and work Z read 0
                        // after homing; otherwise a prior "Set Z Home" (G10 L20) offset persists and
                        // the work Z keeps reporting the old value instead of 0.
                        char clear_g92[] = "G92.1";
                        if (gc_execute_line(clear_g92) != Error::Ok) {
                            log_warn(name() << ": could not clear G92 offset after Z home");
                        }
                        char zero_wcs[] = "G10 L20 P0 Z0";
                        if (gc_execute_line(zero_wcs) != Error::Ok) {
                            log_warn(name() << ": could not zero Z work coordinate after Z home");
                        }
                        _deg_sent = false;                           // re-send the next phase from the new zero
                        log_info(name() << ": spindle re-homed Z - reset XY Z position to 0");
                    } else {
                        log_warn(name() << ": ignoring spindle ZHOMED while machine is busy (state " << (int)st << ")");
                    }
                }
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

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<SpindleBoard> registration("SpindleBoard");
    }
}
