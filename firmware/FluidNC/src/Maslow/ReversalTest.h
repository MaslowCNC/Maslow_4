// Copyright (c) 2026 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#pragma once

#include <Arduino.h>
#include <string>

// $TRV: the belt reversal test.  One arm follows a setpoint profile under the machine's position
// loop (P = 300 as in MotorUnit.cpp, driven through DCMotor::runAtPWM with its 75-PWM floor, or
// without the floor for comparison): hold, pay out `out` mm, hold, reel back to the start, hold,
// repeated `cycles` times.  Meant for a belt preloaded against a spring scale with $TCT, where the
// encoder position also gives the force.
//
// The encoder reads the belt after the spool, so a belt with no tension outside stops registering
// pay-out while the spool keeps unwinding.  If the encoder stops following the pay-out for `slack`
// seconds the test logs it and starts the return at once, which bounds that internal slack.
//
// Samples (setpoint, position, error, PID output before and after the floor, current) are kept in
// RAM at up to 50 Hz and printed after the run, so logging can't slow the loop.  Each phase also
// logs a one-line summary as it ends.  Runs in the Homing state like $TCT and halts the same ways.
class ReversalTest {
public:
    struct Params {
        int   arm    = -1;
        float outMm  = 1;     // pay-out distance from the start position
        float speed  = 5;     // mm/s for the setpoint moves
        float holdS  = 5;     // seconds for each hold
        int   cycles = 1;
        bool  floor  = true;  // drive through runAtPWM's 75-PWM floor, as the machine does
        float slackS = 1;     // encoder not following pay-out this long: log and turn back
        float pmax   = 1023;  // cap on the PID output
        float maxS   = 120;
    };

    // Parses "$TRV=<arm>,<out mm>,<speed mm/s>[,key=value...]".
    static bool parse(const char* value, Params& p, std::string& why);

    // Starts the test.  The caller has checked the machine state and set State::Homing.  Returns
    // false (nothing started) if the sample buffer can't be allocated.
    bool start(const Params& p);
    void update();
    // Prints the samples after the run, a line every 5 ms.  Called on every Maslow update, whatever
    // the state, so a run ended by $STOP or 0x18 still prints what it recorded.
    void serviceDump();
    // Halts the test if it is running: motor off, "TRV halt reason=<reason>" logged, then the
    // samples.  Does nothing once the run is over and only the printout is left.
    void abort(const char* reason);
    bool active() const { return _phase != Phase::Off; }

private:
    enum class Phase { Off, Hold, MoveOut, MoveBack, Dump };

    struct Sample {
        uint32_t t;      // ms since start
        int16_t  sp;     // setpoint, 0.01 mm from the start position
        int16_t  pos;    // encoder, 0.01 mm from the start position
        int16_t  pid;    // P output before the floor
        int16_t  out;    // PWM actually commanded (+ is out)
        uint16_t adc;
    };
    static constexpr int MAX_SAMPLES = 1500;  // 24 KB: the heap has ~70 KB free with WiFi up
    static constexpr int MAX_STEPS   = 1 + 4 * 5;

    struct Stats {
        int           n;
        double        errSum, errMin, errMax, adcSum, adcMax;
        int           flips;  // sign changes of the PID output
        int           lastSign;
        unsigned long deadMs;  // moves: until the encoder moved 0.1 mm the commanded way
        bool          moved;
    };

    void        beginStep(int step);
    void        endStep();
    const char* stepName(int step) const;
    double      stepTarget(int step) const;  // where the setpoint ends, mm from the start
    void        drive(double out);
    bool        checkSafety(double current, double pos, double err, double out);
    void        stopAndDump(bool halted);  // motor off, Idle, then the printout
    void        freeSamples();

    Params        _p;
    Phase         _phase         = Phase::Off;
    int           _startCalState = 0;
    int           _step          = 0;
    int           _stepCount     = 0;
    unsigned long _testStart     = 0;
    unsigned long _stepStart     = 0;
    unsigned long _lastSample    = 0;
    unsigned long _lastCheck     = 0;
    unsigned long _lastDumpLine  = 0;
    unsigned long _sampleMs      = 20;
    double        _startPos      = 0;
    double        _sp            = 0;  // mm from the start
    double        _stepPos0      = 0;  // encoder at the start of the step, mm from the start
    unsigned long _lastCtl       = 0;
    double        _lastAdvancePos = 0;
    unsigned long _lastAdvanceT  = 0;
    bool          _slackHandled  = false;
    Stats         _st;

    Sample* _samples = nullptr;
    int     _n       = 0;
    int     _dumpIdx = 0;

    unsigned long _encoderFailSince = 0;
    unsigned long _deadSince        = 0;
    int           _overCount        = 0;
};
