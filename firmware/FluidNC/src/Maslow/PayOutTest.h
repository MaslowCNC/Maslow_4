// Copyright (c) 2026 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#pragma once

#include <Arduino.h>
#include <string>

// $TPO: the belt pay-out test, the companion to $TCT.  It measures the force it takes to pull a
// belt out against its motor, through an inline scale held by hand.
//
// Current mode (target > 0): the motor pulls in at the target current (raw ADC, closed loop as in
// $TCT).  Once it is on target the user pulls the belt out; past max speed the motor adds pull to
// brake.  At max distance it holds position (the machine's own P loop), then ramps down and ends.
//
// Drive mode (target 0): the realistic machine case.  After a 3 s countdown the motor pays the
// belt out at max speed under the machine's position control while the user keeps the scale
// taut, then holds at max distance, ramps down and ends.  The log shows the current it took.
//
// Both log a sample line about every 100 ms and a summary (breakaway, steady moving, end hold).
// Runs in the Homing state like $TCT, and halts the same ways.
class PayOutTest {
public:
    struct Params {
        int   arm       = -1;
        float targetAdc = 0;   // 0 = drive mode
        float speed     = 5;   // mm/s: brake above it (current mode), pay-out speed (drive mode)
        float dist      = 20;  // mm out from the start position
        float holdS     = 5;   // seconds holding at max distance
        float maxS      = 120;
    };

    // Parses "$TPO=<arm>,<target ADC>,<max speed>,<max dist>[,hold=s][,time=s]".
    static bool parse(const char* value, Params& p, std::string& why);

    // Starts the test.  The caller has checked the machine state and set State::Homing.
    void start(const Params& p);
    void update();
    // Halts the test if it is running: motor off, "TPO halt reason=<reason>" and the summary logged.
    void abort(const char* reason);
    bool active() const { return _phase != Phase::Off; }

private:
    enum class Phase { Off, Settle, Ready, Moving, EndHold, RampDown };

    bool        driveMode() const { return _p.targetAdc <= 0; }
    const char* phaseName() const;
    void        setPhase(Phase phase);
    void        currentLoop(double current, bool moving);
    bool        checkSafety(double current, double pos);
    void        logSample(double current, double pos);
    void        logSummary();
    void        finish();
    void        halt(const char* reason);

    Params        _p;
    Phase         _phase         = Phase::Off;
    int           _startCalState = 0;
    unsigned long _testStart     = 0;
    unsigned long _phaseStart    = 0;
    unsigned long _lastTick      = 0;
    unsigned long _lastLog       = 0;
    unsigned long _stableSince   = 0;
    double        _initialPos    = 0;  // where the test started (reel-in limit)
    double        _startPos      = 0;  // where pay-out is measured from
    double        _setpoint      = 0;  // position target in drive mode and at the end
    double        _lastPos       = 0;
    double        _speed         = 0;  // mm/s, + is out, smoothed
    double        _pwm           = 0;  // current mode: PWM pulling in (0-1023)
    bool          _braking       = false;

    // Results
    bool          _brokeAway     = false;
    double        _breakAdc      = 0;
    double        _breakPwm      = 0;
    double        _breakT        = 0;
    bool          _inJump        = false;
    int           _jumps         = 0;
    int           _movN          = 0;
    double        _movSumI       = 0;
    double        _movSumI2      = 0;
    double        _movSumV       = 0;
    int           _endN          = 0;
    double        _endSumI       = 0;
    double        _endSumPwm     = 0;

    // Fault timers (millis() the condition started, 0 when it isn't present)
    unsigned long _deadSince        = 0;
    unsigned long _encoderFailSince = 0;
    int           _overCount        = 0;
};
