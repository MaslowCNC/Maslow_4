// Copyright (c) 2024 -	Maslow
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
    SpindleBoard talks to the separate Maslow spindle/Z controller board over a
    UART link.  The spindle board runs the router spindle and produces Z-axis
    motion by adjusting the relative phase of its two BLDC motors.

    Protocol (newline terminated ASCII):
      XY  -> board:  "S<rpm>"  set spindle speed (0 = stop)
                     "Z<deg>"  set absolute target phase offset (Z position)
                     "E"       emergency stop
                     "?"       request status
      board -> XY:   "T:<state>,P:<phase_deg>,R:<rpm>,F:<fault>"

    The XY firmware maps the planned Z position (mm) to a phase offset (degrees)
    using the configurable "phase_deg_per_mm" calibration constant.
*/

#include "Spindle.h"
#include "../Uart.h"

namespace Spindles {
    class SpindleBoard : public Spindle {
    public:
        SpindleBoard() = default;

        SpindleBoard(const SpindleBoard&)            = delete;
        SpindleBoard(SpindleBoard&&)                 = delete;
        SpindleBoard& operator=(const SpindleBoard&) = delete;
        SpindleBoard& operator=(SpindleBoard&&)      = delete;

        void init() override;
        void setState(SpindleState state, SpindleSpeed speed) override;
        void setSpeedfromISR(uint32_t dev_speed) override;
        void config_message() override;

        // Called periodically by the Maslow update loop with the latest planned
        // Z position (mm).  Streams Z (as a phase offset) to the spindle board,
        // flushes any pending speed change, and services the status return link.
        void update(float zPositionMM);

        // Push the XY board's own WiFi station credentials to the spindle board over the
        // link so it can join WiFi and start its ArduinoOTA server.  Triggered by the
        // $Spindle/EnableOTA command.  The spindle can then be reflashed wirelessly with
        // `pio run -e esp32-s3-devkitc-1-ota -t upload`.
        void sendEnableOTA();

        // Tell the spindle board to run its Z homing cycle (raise the Z until the
        // top-of-travel beam breaks).  Triggered by the $Spindle/Home command / web UI button.
        void sendHome();

        // Tell the spindle board to remove the loaded tool (raise the Z until the tool clears
        // the top-of-travel beam).  Triggered by the $Spindle/RemoveTool command / web UI button.
        void sendRemoveTool();

        // The single active instance, so the Maslow loop can stream Z to it
        // without depending on the spindle factory internals.
        static SpindleBoard* instance;

        // Configuration handlers:
        void validate() override {
            Spindle::validate();
            Assert(_uart != nullptr || _uart_num != -1, "SpindleBoard: missing UART configuration");
            Assert(!(_uart != nullptr && _uart_num != -1), "SpindleBoard: conflicting UART configuration");
        }

        void group(Configuration::HandlerBase& handler) override {
            // Hardware UART engine 2: UART0 is the console and UART1 drives the TMC
            // stepper drivers, so the inter-board link uses UART2.
            handler.section("uart", _uart, 2);
            handler.item("uart_num", _uart_num);
            handler.item("phase_deg_per_mm", _phase_deg_per_mm, 0.0f, 100000.0f);
            handler.item("max_rpm", _max_rpm, 1, 100000);
            handler.item("suction_power", _suction_power, 0, 100);

            Spindle::group(handler);
        }

        const char* name() const override { return "SpindleBoard"; }

        virtual ~SpindleBoard() {}

    private:
        int   _uart_num         = -1;
        Uart* _uart             = nullptr;
        float _phase_deg_per_mm = 1.0f;
        int   _max_rpm          = 10000;
        int   _suction_power    = 100;  // cooling/suction fan power 0-100, sent to the board

        // Last values sent so we only transmit on change
        float    _last_sent_deg  = 0.0f;
        bool     _deg_sent       = false;
        uint32_t _last_sent_rpm  = 0xFFFFFFFF;  // sentinel forces the first send
        int      _last_sent_fan  = -1;          // sentinel forces the first send

        // Speed update requested from the ISR, flushed in update()
        volatile uint32_t _pending_rpm = 0;
        volatile bool     _rpm_dirty   = false;

        // Status return-link parsing
        char    _rx_buf[160];
        size_t  _rx_len     = 0;
        uint8_t _last_fault = 0;

        // Set once the spindle board has answered (handshake reply or status line),
        // so the runtime confirmation message is logged exactly once.
        bool _link_confirmed = false;

        // True once we have told the spindle board the machine is idle so it can power
        // down the Z-axis drivers.  Reset as soon as the machine starts moving again so
        // the release is re-sent on the next idle.
        bool _hold_released = false;

        void sendSpeed(uint32_t rpm);
        void sendPhase(float deg);
        void serviceStatus();

        // Tell the spindle board it may power down its Z-axis (phase-offset) drivers
        // once their move settles, because the machine is idle and the spindle is off.
        void sendHoldRelease();

        // Send a Z-hold release when the machine is idle and the spindle is stopped, so
        // the spindle board's BLDC drivers (and its cooling fan) do not stay energized.
        void updateHoldState();

        // Send the configured suction/cooling fan power (0-100) to the spindle board.
        void sendFan(int level);

        // Boot-time handshake: ping the spindle board and log whether it answers.
        void handshake();
    };
}
