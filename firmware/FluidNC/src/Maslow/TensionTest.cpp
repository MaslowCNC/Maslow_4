// Copyright (c) 2026 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#include "TensionTest.h"
#include "Maslow.h"
#include "../Logging.h"
#include "../System.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

// Hard limits that the command arguments can't raise
static constexpr float MAX_ADC_CAP  = 3000;  // the safety check warns at 4000
static constexpr float MAX_TIME_CAP = 900;
static constexpr float MAX_MOVE_CAP = 50;

static constexpr unsigned long TICK_MS     = 20;   // controller period
static constexpr unsigned long BASELINE_MS = 500;  // motor off, to record the zero-current reading
static constexpr double        PWM_MAX     = 1023;
static constexpr double        GAIN        = 0.04;  // PWM per ADC count of error, per tick
// The PWM rises slowly and not at all while the belt moves.  Pulling against a spring scale the
// belt has to reel in a few mm before the force builds; a moving motor draws less current, so a
// loop that kept raising the PWM then would overshoot hard when the spring took the load.
static constexpr double        PWM_RISE    = 2;     // most the PWM rises in one tick (100/s)
static constexpr double        PWM_FALL    = 20;    // most the PWM falls in one tick
static constexpr double        MOVING_MM_S = 2.0;   // belt speed above which the PWM isn't raised
static constexpr double        SPEED_ALPHA = 0.3;   // smoothing of the belt speed, per tick

// A level's hold starts once the settle time has passed and the current has been on target with
// the belt still for STILL_MS, or after SETTLE_EXTRA_S more if that never happens
static constexpr unsigned long STILL_MS       = 500;
static constexpr float         SETTLE_EXTRA_S = 15;

// A dead current sense (TL's on a bad board reads 0 always) must not ramp the motor to full power
static constexpr double        DEAD_PWM   = 300;
static constexpr double        DEAD_ADC   = 25;
static constexpr unsigned long DEAD_MS    = 1000;
static constexpr unsigned long SAT_MS     = 1000;  // at full PWM and still short of the target
static constexpr unsigned long ENC_MS     = 300;   // encoder not answering
static constexpr int           OVER_TICKS = 3;     // ticks over the current cap before halting

static const char* const ARM_NAMES[ARM_COUNT] = { "TL", "TR", "BL", "BR" };

// The UI's conversion (maslow.js): 3.3 V full scale, 1.5 V/A
static double adcToMa(double adc) {
    return 1000.0 * 3.3 * adc / 4095.0 / 1.5;
}

bool TensionTest::parse(const char* value, Params& p, std::string& why) {
    if (!value || !*value) {
        why = "give the arm: $TCT=TL, TR, BL or BR";
        return false;
    }
    std::string s(value);
    size_t      pos   = 0;
    bool        first = true;
    while (pos <= s.size()) {
        size_t      comma = s.find(',', pos);
        std::string tok   = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        pos               = comma == std::string::npos ? s.size() + 1 : comma + 1;
        while (!tok.empty() && tok.front() == ' ') {
            tok.erase(0, 1);
        }
        while (!tok.empty() && tok.back() == ' ') {
            tok.pop_back();
        }
        if (first) {
            first = false;
            for (int arm = 0; arm < ARM_COUNT; arm++) {
                if (strcasecmp(tok.c_str(), ARM_NAMES[arm]) == 0) {
                    p.arm = arm;
                }
            }
            if (p.arm < 0) {
                why = "unknown arm '" + tok + "', use TL, TR, BL or BR";
                return false;
            }
            continue;
        }
        if (tok.empty()) {
            continue;
        }
        size_t eq = tok.find('=');
        if (eq == std::string::npos) {
            why = "expected key=value, got '" + tok + "'";
            return false;
        }
        std::string key = tok.substr(0, eq);
        std::string val = tok.substr(eq + 1);
        char*       end = nullptr;
        float       v   = strtof(val.c_str(), &end);
        if (val.empty() || *end != '\0' || !std::isfinite(v)) {
            why = "bad number for " + key + ": '" + val + "'";
            return false;
        }
        if (key == "from") {
            p.from = v;
        } else if (key == "to") {
            p.to = v;
        } else if (key == "step") {
            p.step = v;
        } else if (key == "hold") {
            p.holdS = v;
        } else if (key == "settle") {
            p.settleS = v;
        } else if (key == "move") {
            p.moveMm = v;
        } else if (key == "max") {
            p.maxAdc = v;
        } else if (key == "time") {
            p.maxS = v;
        } else if (key == "down") {
            p.down = v != 0;
        } else {
            why = "unknown key '" + key + "' (from, to, step, hold, settle, move, max, time, down)";
            return false;
        }
    }

    if (p.maxAdc <= 0 || p.maxAdc > MAX_ADC_CAP) {
        why = "max must be 1-" + std::to_string(int(MAX_ADC_CAP));
        return false;
    }
    if (p.from < 50 || p.to < p.from || p.to > p.maxAdc) {
        why = "need 50 <= from <= to <= max (max=" + std::to_string(int(p.maxAdc)) + ")";
        return false;
    }
    if (p.step <= 0 || (p.to - p.from) / p.step > 31) {
        why = "step must be > 0 and give at most 32 levels";
        return false;
    }
    if (p.holdS < 0.5 || p.holdS > 60 || p.settleS < 0.5 || p.settleS > 30) {
        why = "hold must be 0.5-60 s and settle 0.5-30 s";
        return false;
    }
    if (p.moveMm < 1 || p.moveMm > MAX_MOVE_CAP) {
        why = "move must be 1-" + std::to_string(int(MAX_MOVE_CAP)) + " mm";
        return false;
    }
    if (p.maxS < 10 || p.maxS > MAX_TIME_CAP) {
        why = "time must be 10-" + std::to_string(int(MAX_TIME_CAP)) + " s";
        return false;
    }
    int   up      = int((p.to - p.from) / p.step + 1e-3) + 1;
    int   levels  = p.down ? 2 * up - 1 : up;
    float needed  = BASELINE_MS / 1000.0f + levels * (p.settleS + p.holdS);
    if (needed > p.maxS) {
        why = std::to_string(levels) + " levels need " + std::to_string(int(needed + 1)) + " s, more than time=" +
              std::to_string(int(p.maxS)) + "; raise time or use fewer levels";
        return false;
    }
    return true;
}

void TensionTest::start(const Params& p) {
    _p          = p;
    _levelCount = 0;
    int up      = int((p.to - p.from) / p.step + 1e-3) + 1;
    for (int i = 0; i < up; i++) {
        _levels[_levelCount++] = p.from + i * p.step;
    }
    if (p.down) {
        for (int i = up - 2; i >= 0; i--) {
            _levels[_levelCount++] = p.from + i * p.step;
        }
    }

    MotorUnit& axis = Maslow.axis[_p.arm];
    axis.stop();
    _startCalState    = Maslow.calibration.getCurrentState();
    _startPos         = axis.getPosition();
    _pwm              = 0;
    _level            = 0;
    _testStart        = millis();
    _phaseStart       = _testStart;
    _lastTick         = _testStart;
    _n                = 0;
    _sumI = _sumI2 = _sumPwm = 0;
    _deadSince = _saturatedSince = _encoderFailSince = 0;
    _overCount                                       = 0;
    _phase                                           = Phase::Baseline;

    char buf[220];
    snprintf(buf,
             sizeof(buf),
             "TCT start arm=%s from=%.0f to=%.0f step=%.0f levels=%d hold_s=%.1f settle_s=%.1f move_mm=%.1f max_adc=%.0f max_s=%.0f pos=%.3f",
             ARM_NAMES[_p.arm],
             _p.from,
             _p.to,
             _p.step,
             _levelCount,
             _p.holdS,
             _p.settleS,
             _p.moveMm,
             _p.maxAdc,
             _p.maxS,
             _startPos);
    log_info(buf);
}

void TensionTest::update() {
    if (_phase == Phase::Off) {
        return;
    }
    unsigned long now = millis();
    if (now - _lastTick < TICK_MS) {
        return;
    }
    _lastTick = now;

    MotorUnit& axis = Maslow.axis[_p.arm];

    if (Maslow.calibration.getCurrentState() != _startCalState) {
        halt("state_changed");
        return;
    }
    if (now - _testStart > (unsigned long)(_p.maxS * 1000)) {
        halt("time_limit");
        return;
    }
    // Read this belt's encoder every tick so a dead encoder can't hide movement
    if (axis.updateEncoderPosition()) {
        _encoderFailSince = 0;
    } else if (_encoderFailSince == 0) {
        _encoderFailSince = now;
    } else if (now - _encoderFailSince > ENC_MS) {
        halt("encoder");
        return;
    }

    double current = axis.getMotorCurrent();
    double pos     = axis.getPosition();
    if (!checkSafety(current, pos)) {
        return;
    }

    if (_phase == Phase::Baseline) {
        axis.stop();
        // skip the first 100 ms so the averaging buffer holds only motor-off readings
        if (now - _phaseStart > 100) {
            _n++;
            _sumI += current;
        }
        if (now - _phaseStart >= BASELINE_MS) {
            _baseline = _n ? _sumI / _n : current;
            char buf[120];
            snprintf(buf, sizeof(buf), "TCT baseline arm=%s adc=%.1f mA=%.1f", ARM_NAMES[_p.arm], _baseline, adcToMa(_baseline));
            log_info(buf);
            _level   = 0;
            _lastPos = pos;
            _speed   = 0;
            beginLevel();
        }
        return;
    }

    // Integral control of the PWM on the measured current, pulling the belt in
    double target = _levels[_level];
    _speed        = SPEED_ALPHA * (pos - _lastPos) / (TICK_MS / 1000.0) + (1 - SPEED_ALPHA) * _speed;
    _lastPos      = pos;
    bool   moving = std::fabs(_speed) > MOVING_MM_S;
    double delta  = GAIN * (target - current);
    delta         = constrain(delta, -PWM_FALL, moving ? 0.0 : PWM_RISE);
    _pwm          = constrain(_pwm + delta, 0.0, PWM_MAX);
    axis.driveIn(int(_pwm + 0.5));

    if (!moving && onTarget(current, target)) {
        if (_stableSince == 0) {
            _stableSince = now;
        }
    } else {
        _stableSince = 0;
    }

    // Full PWM and still short of the target: a stuck sense, a bad driver, or a level too high
    if (_pwm >= PWM_MAX && current < 0.9 * target) {
        if (_saturatedSince == 0) {
            _saturatedSince = now;
        } else if (now - _saturatedSince > SAT_MS) {
            halt("target_unreachable");
            return;
        }
    } else {
        _saturatedSince = 0;
    }

    if (_phase == Phase::Settle) {
        unsigned long waited = now - _phaseStart;
        bool          ready  = waited >= (unsigned long)(_p.settleS * 1000) && _stableSince != 0 && now - _stableSince >= STILL_MS;
        if (ready || waited >= (unsigned long)((_p.settleS + SETTLE_EXTRA_S) * 1000)) {
            _phase      = Phase::Hold;
            _phaseStart = now;
            _n          = 0;
            _sumI = _sumI2 = _sumPwm = 0;
            _minI                    = current;
            _maxI                    = current;
            char buf[160];
            snprintf(buf,
                     sizeof(buf),
                     "TCT read arm=%s step=%d/%d dir=%s target_adc=%.0f target_mA=%.1f hold_s=%.1f settle_s=%.1f on_target=%d",
                     ARM_NAMES[_p.arm],
                     _level + 1,
                     _levelCount,
                     (_p.down && _level > 0 && _levels[_level] < _levels[_level - 1]) ? "down" : "up",
                     target,
                     adcToMa(target),
                     _p.holdS,
                     waited / 1000.0,
                     ready ? 1 : 0);
            log_info(buf);
        }
        return;
    }

    // Hold: average what the scale is seeing
    _n++;
    _sumI += current;
    _sumI2 += current * current;
    _sumPwm += _pwm;
    _minI = std::min(_minI, current);
    _maxI = std::max(_maxI, current);
    if (now - _phaseStart >= (unsigned long)(_p.holdS * 1000)) {
        logStep();
        _level++;
        if (_level >= _levelCount) {
            finish();
        } else {
            beginLevel();
        }
    }
}

bool TensionTest::checkSafety(double current, double pos) {
    unsigned long now = millis();
    if (std::fabs(pos - _startPos) > _p.moveMm) {
        halt("move_limit");
        return false;
    }
    if (current > _p.maxAdc) {
        if (++_overCount >= OVER_TICKS) {
            halt("over_current");
            return false;
        }
    } else {
        _overCount = 0;
    }
    if (_pwm >= DEAD_PWM && current < DEAD_ADC) {
        if (_deadSince == 0) {
            _deadSince = now;
        } else if (now - _deadSince > DEAD_MS) {
            halt("dead_current_sense");
            return false;
        }
    } else {
        _deadSince = 0;
    }
    return true;
}

void TensionTest::beginLevel() {
    _phase       = Phase::Settle;
    _phaseStart  = millis();
    _stableSince = 0;
}

bool TensionTest::onTarget(double current, double target) {
    return std::fabs(current - target) <= std::max(10.0, 0.05 * target);
}

void TensionTest::logStep() {
    MotorUnit& axis = Maslow.axis[_p.arm];
    double     mean = _n ? _sumI / _n : 0;
    double     var  = _n ? _sumI2 / _n - mean * mean : 0;
    double     sd   = var > 0 ? std::sqrt(var) : 0;
    double     pwm  = _n ? _sumPwm / _n : _pwm;
    double     pos  = axis.getPosition();
    double     tgt  = _levels[_level];
    bool       dn   = _p.down && _level > 0 && _levels[_level] < _levels[_level - 1];
    bool       ok   = onTarget(mean, tgt);
    char       buf[300];
    snprintf(buf,
             sizeof(buf),
             "TCT step arm=%s step=%d dir=%s target_adc=%.0f adc=%.1f adc_sd=%.1f adc_min=%.0f adc_max=%.0f mA=%.1f "
             "baseline_adc=%.1f pwm=%.1f pos=%.3f move_mm=%.3f settled=%d t_s=%.1f",
             ARM_NAMES[_p.arm],
             _level + 1,
             dn ? "down" : "up",
             tgt,
             mean,
             sd,
             _minI,
             _maxI,
             adcToMa(mean),
             _baseline,
             pwm,
             pos,
             pos - _startPos,
             ok ? 1 : 0,
             (millis() - _testStart) / 1000.0);
    log_info(buf);
}

void TensionTest::finish() {
    Maslow.axis[_p.arm].stop();
    _phase = Phase::Off;
    char buf[120];
    snprintf(buf,
             sizeof(buf),
             "TCT done arm=%s steps=%d move_mm=%.3f t_s=%.1f",
             ARM_NAMES[_p.arm],
             _levelCount,
             Maslow.axis[_p.arm].getPosition() - _startPos,
             (millis() - _testStart) / 1000.0);
    log_info(buf);
    sys.set_state(State::Idle);
}

void TensionTest::halt(const char* reason) {
    MotorUnit& axis = Maslow.axis[_p.arm];
    axis.stop();
    _phase = Phase::Off;
    char buf[220];
    snprintf(buf,
             sizeof(buf),
             "TCT halt arm=%s reason=%s step=%d adc=%.1f pwm=%.0f move_mm=%.3f t_s=%.1f",
             ARM_NAMES[_p.arm],
             reason,
             _level + 1,
             axis.getMotorCurrent(),
             _pwm,
             axis.getPosition() - _startPos,
             (millis() - _testStart) / 1000.0);
    log_warn(buf);
    if (sys.state() == State::Homing) {
        sys.set_state(State::Idle);
    }
}

void TensionTest::abort(const char* reason) {
    if (_phase != Phase::Off) {
        halt(reason);
    }
}
