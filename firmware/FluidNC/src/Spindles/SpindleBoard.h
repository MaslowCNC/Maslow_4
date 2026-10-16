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
                     "B<0|1>"  XY belt motors idle/active (vacuum cooling request)
                     "C<0-100>" dust collection fan speed (0 = off), from Maslow_Dust_Fan_Speed
                     "E"       emergency stop
                     "?"       request status
      board -> XY:   "T:<state>,P:<phase_deg>,R:<rpm>,F:<fault>"

    The XY firmware maps the planned Z position (mm) to a phase offset (degrees)
    using the configurable "phase_deg_per_mm" calibration constant.
*/

#include "Spindle.h"
#include "../Uart.h"
#include "../FileStream.h"

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
        // Z position (mm), whether the XY belt motors need vacuum cooling, and the dust
        // collection fan speed (0-100).  Streams Z (as a phase offset), flushes pending state
        // changes (so a new fan speed takes effect at once), and services the return link.
        void update(float zPositionMM, bool beltMotionActive, int fanSpeed);

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

        // Ask the spindle board to send its trip recording over the link again ("T"), or with
        // test=true to record a test one now ("T!").  Triggered by $Spindle/Trip [TEST].  The
        // recording is saved to the local filesystem as TRIP_FILE either way.
        void sendTripRequest(bool test);

        static constexpr const char* TRIP_FILE = "/spindle_trip.csv";

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
            // The UART can be configured either with the embedded "uart:" section above
            // (_uart set, _uart_num == -1) or an external "uart_num:" reference.  When
            // generating (saving) the config, only emit "uart_num" if it is actually in
            // use; otherwise the generator would write "uart_num: -1" alongside the
            // embedded "uart:" section, and reloading that file fails validation with
            // "conflicting UART configuration".
            if (handler.handlerType() != Configuration::HandlerType::Generator || _uart_num != -1) {
                handler.item("uart_num", _uart_num);
            }
            handler.item("phase_deg_per_mm", _phase_deg_per_mm, 0.0f, 100000.0f);
            handler.item("max_rpm", _max_rpm, 1, 100000);

            Spindle::group(handler);
        }

        const char* name() const override { return "SpindleBoard"; }

        virtual ~SpindleBoard() {}

    private:
        int   _uart_num         = -1;
        Uart* _uart             = nullptr;
        float _phase_deg_per_mm = 1.0f;
        int   _max_rpm          = 10000;

        // Last values sent so we only transmit on change
        float    _last_sent_deg  = 0.0f;
        bool     _deg_sent       = false;
        uint32_t _last_sent_rpm  = 0xFFFFFFFF;  // sentinel forces the first send
        int      _last_sent_fan  = -1;          // sentinel forces the first send
        int      _last_sent_belt = -1;          // sentinel forces the first send

        // Speed update requested from the ISR, flushed in update()
        volatile uint32_t _pending_rpm = 0;
        volatile bool     _rpm_dirty   = false;

        // Status return-link parsing
        char    _rx_buf[400];  // the trip recording's CSV header line is ~330 chars
        size_t  _rx_len     = 0;
        uint8_t _last_fault = 0;

        // Set when the spindle board reports a fault; cleared only by a deliberate M3/M4.
        // While set, speeds queued by the stepper ISR are dropped: the blocks still in the
        // planner when the fault arrived carry the old speed and would restart the spindle.
        bool _fault_latched = false;

        // Whether the Maslow loop reported belt motion on the latest update() call.
        bool _belt_motion_active = false;

        // Set once the spindle board has answered (handshake reply or status line),
        // so the runtime confirmation message is logged exactly once.
        bool _link_confirmed = false;

        // When the last line arrived from the spindle board.  It reports status every 50 ms,
        // so a gap longer than LINK_SILENCE_MS means it went away (typically a reboot or
        // reflash, which resets its fan speed to 100) and the fan speed must be re-sent.
        uint32_t _last_rx_ms = 0;

        static const uint32_t LINK_SILENCE_MS = 1500;

        // True once we have told the spindle board the machine is at rest so it can power
        // down the Z-axis drivers.  Reset as soon as the machine starts moving again so
        // the release is re-sent on the next rest, and refreshed every
        // HOLD_RELEASE_REPEAT_MS while at rest so a dropped or forgotten 'D' cannot leave
        // the drivers (and the vacuum fan) energized indefinitely.
        bool     _hold_released        = false;
        uint32_t _last_hold_release_ms = 0;

        static const uint32_t HOLD_RELEASE_REPEAT_MS = 2000;

        // Trip recording being received from the spindle board (see handleTripLine).
        FileStream* _trip_file    = nullptr;
        int         _trip_lines   = 0;
        uint32_t    _trip_last_ms = 0;

        // A recording arrived while the machine was moving and was not saved.  Ask for it
        // again once the machine is at rest and the first copy has finished arriving.
        bool _trip_refetch = false;

        static const uint32_t TRIP_LINE_TIMEOUT_MS = 3000;

        // Save one "[TRIP] ..." / "TRIPCSV,..." line from the spindle board to TRIP_FILE.
        void handleTripLine();
        void closeTripFile(const char* why);
        bool machineAtRest() const;

        void sendSpeed(uint32_t rpm);
        void sendPhase(float deg);
        void sendBeltCooling(bool active);
        void serviceStatus();

        // Tell the spindle board it may power down its Z-axis (phase-offset) drivers
        // once their move settles, because the machine is idle and the spindle is off.
        void sendHoldRelease();

        // Send a Z-hold release when the machine is idle and the spindle is stopped, so
        // the spindle board's BLDC drivers (and its cooling fan) do not stay energized.
        void updateHoldState();

        // Send the dust collection fan speed (0-100) to the spindle board (only on change).
        void sendFan(int level);

        // Boot-time handshake: ping the spindle board and log whether it answers.
        void handshake();
    };
}
