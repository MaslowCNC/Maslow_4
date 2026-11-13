// Copyright (c) 2026 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#pragma once

#include <Arduino.h>
#include <string>

// $TCT: the belt tension / motor current test.  One arm's belt is hooked to a scale and the motor
// pulls in against it, held at a series of current levels (raw ADC, closed loop on the measured
// current) so the scale reading at each level gives the current -> force relation.  The levels go
// up and then back down: the difference between the two at the same current is the gearbox
// friction band.
//
// Runs in the Homing state, called from Maslow_::update() instead of Calibration::home().  It halts
// (motor off, "TCT halt" logged) when the belt moves more than the move limit, the current goes
// over the cap, the current sense reads nothing at real PWM, the target can't be reached at full
// PWM, the encoder stops answering, the time cap is hit, or the machine leaves Homing ($STOP, 0x18).
class TensionTest {
public:
    struct Params {
        int   arm      = -1;
        float from     = 200;   // first current level, raw ADC
        float to       = 1600;  // highest current level, raw ADC
        float step     = 200;   // step between levels, raw ADC
        float holdS    = 5;     // seconds each level is held and averaged (read the scale then)
        float settleS  = 2;     // least seconds before the hold; it also waits (up to 15 s more) for the current to be on target with the belt still
        float moveMm   = 15;    // halt if the belt moves further than this from where it started
        float maxAdc   = 2000;  // halt if the measured current goes above this
        float maxS     = 300;   // halt after this many seconds in all
        bool  down     = true;  // after the top level, step back down to `from`
    };

    // Shared with $TPO (PayOutTest)
    static const char* armName(int arm);
    static int         armFromName(const std::string& name);  // -1 if not TL, TR, BL or BR
    static double      adcToMa(double adc);                   // the UI's conversion

    // Parses "$TCT=<arm>[,key=value...]".  On failure returns false with the reason in `why`.
    static bool parse(const char* value, Params& p, std::string& why);

    // Starts the test.  The caller has checked the machine state and set State::Homing.
    void start(const Params& p);
    void update();
    // Halts the test if it is running: motor off and "TCT halt reason=<reason>" logged.
    void abort(const char* reason);
    bool active() const { return _phase != Phase::Off; }

private:
    enum class Phase { Off, Baseline, Settle, Hold };

    void halt(const char* reason);
    void finish();
    void beginLevel();
    static bool onTarget(double current, double target);
    void logStep();
    bool checkSafety(double current, double pos);

    Params        _p;
    Phase         _phase = Phase::Off;
    int           _startCalState = 0;
    float         _levels[64];
    int           _levelCount = 0;
    int           _level      = 0;
    unsigned long _testStart  = 0;
    unsigned long _phaseStart = 0;
    unsigned long _lastTick   = 0;
    double        _pwm        = 0;
    double        _startPos   = 0;
    double        _baseline   = 0;
    double        _lastPos    = 0;
    double        _speed      = 0;  // belt speed, mm/s, smoothed
    unsigned long _stableSince = 0;  // millis() the current came on target with the belt still

    // Sums over the current phase (baseline or hold)
    int    _n      = 0;
    double _sumI   = 0;
    double _sumI2  = 0;
    double _sumPwm = 0;
    double _minI   = 0;
    double _maxI   = 0;

    // Fault timers (millis() the condition started, 0 when it isn't present)
    unsigned long _deadSince        = 0;
    unsigned long _saturatedSince   = 0;
    unsigned long _encoderFailSince = 0;
    int           _overCount        = 0;
};
