// Copyright (c) 2026 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#include "PayOutTest.h"
#include "TensionTest.h"
#include "Maslow.h"
#include "../Logging.h"
#include "../System.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

static constexpr float MAX_TARGET_ADC = 2800;  // leaves room under the cap
static constexpr float ADC_CAP        = 3000;  // halt above this, as $TCT's hard cap

static constexpr unsigned long TICK_MS = 20;
static constexpr unsigned long LOG_MS  = 100;  // sample lines at ~10 Hz

// Current loop, as in $TCT: slow rise, fast fall, no rise while the belt reels in
static constexpr double GAIN        = 0.04;
static constexpr double PWM_RISE    = 2;
static constexpr double PWM_FALL    = 20;
static constexpr double PWM_MAX     = 1023;
static constexpr double MOVING_MM_S = 2.0;
static constexpr double SPEED_ALPHA = 0.3;
static constexpr double BRAKE_GAIN  = 5;  // extra PWM per mm/s over max speed, per tick

// Current mode starts once the current has been on target with the belt still for STILL_MS
// (at least SETTLE_MIN_MS in), or after SETTLE_MAX_MS
static constexpr unsigned long STILL_MS      = 500;
static constexpr unsigned long SETTLE_MIN_MS = 1000;
static constexpr unsigned long SETTLE_MAX_MS = 15000;

static constexpr unsigned long COUNTDOWN_MS = 3000;  // drive mode: holding before paying out
static constexpr unsigned long RAMP_MS      = 2000;  // ramping the hold down at the end
static constexpr unsigned long END_WAIT_MS  = 3000;  // drive mode: setpoint reached but belt short

static constexpr double POS_P        = 300;  // the position gain in MotorUnit.cpp
static constexpr double BREAKAWAY_MM = 0.3;  // first movement out
static constexpr double JUMP_START   = 1.0;  // mm/s: a grab-slip jump starts
static constexpr double JUMP_STOP    = 0.3;  // mm/s: and ends
static constexpr double STEADY_MIN   = 0.5;  // mm/s: counted as moving for the moving stats

static constexpr double        OVERRUN_MM = 5;   // halt this far past max distance
static constexpr double        REEL_IN_MM = 10;  // halt if the belt reels in this far
static constexpr double        DEAD_PWM   = 300;
static constexpr double        DEAD_ADC   = 25;
static constexpr unsigned long DEAD_MS    = 1000;
static constexpr unsigned long ENC_MS     = 300;
static constexpr int           OVER_TICKS = 3;

bool PayOutTest::parse(const char* value, Params& p, std::string& why) {
    std::vector<std::string> toks;
    std::string              s(value ? value : "");
    size_t                   pos = 0;
    while (pos <= s.size() && !s.empty()) {
        size_t      comma = s.find(',', pos);
        std::string tok   = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        tok.erase(0, tok.find_first_not_of(' '));
        tok.erase(tok.find_last_not_of(' ') + 1);
        toks.push_back(tok);
        pos = comma == std::string::npos ? s.size() + 1 : comma + 1;
    }
    if (toks.size() < 4) {
        why = "use $TPO=<arm>,<target ADC>,<max speed mm/s>,<max dist mm>[,hold=s][,time=s]";
        return false;
    }
    p.arm = TensionTest::armFromName(toks[0]);
    if (p.arm < 0) {
        why = "unknown arm '" + toks[0] + "', use TL, TR, BL or BR";
        return false;
    }
    float* positional[] = { &p.targetAdc, &p.speed, &p.dist };
    for (size_t i = 1; i < toks.size(); i++) {
        std::string key, val = toks[i];
        size_t      eq = val.find('=');
        if (eq != std::string::npos) {
            key = val.substr(0, eq);
            val = val.substr(eq + 1);
        } else if (i > 3) {
            why = "expected key=value after the first four values, got '" + toks[i] + "'";
            return false;
        }
        char* end = nullptr;
        float v   = strtof(val.c_str(), &end);
        if (val.empty() || *end != '\0' || !std::isfinite(v)) {
            why = "bad number '" + val + "'";
            return false;
        }
        if (key.empty()) {
            *positional[i - 1] = v;
        } else if (key == "hold") {
            p.holdS = v;
        } else if (key == "time") {
            p.maxS = v;
        } else {
            why = "unknown key '" + key + "' (hold, time)";
            return false;
        }
    }
    if (p.targetAdc != 0 && (p.targetAdc < 100 || p.targetAdc > MAX_TARGET_ADC)) {
        why = "target must be 0 (drive mode) or 100-" + std::to_string(int(MAX_TARGET_ADC));
        return false;
    }
    if (p.speed < 0.5 || p.speed > 50 || p.dist < 1 || p.dist > 100) {
        why = "max speed must be 0.5-50 mm/s and max dist 1-100 mm";
        return false;
    }
    if (p.holdS < 1 || p.holdS > 30 || p.maxS < 10 || p.maxS > 600) {
        why = "hold must be 1-30 s and time 10-600 s";
        return false;
    }
    return true;
}

const char* PayOutTest::phaseName() const {
    switch (_phase) {
        case Phase::Settle:
            return "settle";
        case Phase::Ready:
            return "hold";
        case Phase::Moving:
            return _braking ? "brake" : "move";
        case Phase::EndHold:
            return "end";
        case Phase::RampDown:
            return "ramp";
        default:
            return "off";
    }
}

void PayOutTest::setPhase(Phase phase) {
    _phase      = phase;
    _phaseStart = millis();
}

void PayOutTest::start(const Params& p) {
    _p              = p;
    MotorUnit& axis = Maslow.axis[_p.arm];
    axis.stop();
    _startCalState = Maslow.calibration.getCurrentState();
    _initialPos = _startPos = _setpoint = _lastPos = axis.getPosition();
    _speed = _pwm = 0;
    _braking = _brokeAway = _inJump = false;
    _breakAdc = _breakPwm = _breakT = 0;
    _jumps = _movN = _endN = 0;
    _movSumI = _movSumI2 = _movSumV = _endSumI = _endSumPwm = 0;
    _deadSince = _encoderFailSince = _stableSince = 0;
    _overCount                                    = 0;
    _testStart = _lastTick = _lastLog = millis();

    char buf[200];
    snprintf(buf,
             sizeof(buf),
             "TPO start arm=%s mode=%s target_adc=%.0f target_mA=%.1f speed=%.1f dist=%.1f hold_s=%.1f max_s=%.0f pos=%.3f",
             TensionTest::armName(_p.arm),
             driveMode() ? "drive" : "current",
             _p.targetAdc,
             TensionTest::adcToMa(_p.targetAdc),
             _p.speed,
             _p.dist,
             _p.holdS,
             _p.maxS,
             _startPos);
    log_info(buf);

    if (driveMode()) {
        axis.setTarget(_setpoint);
        axis.resetPID();
        setPhase(Phase::Ready);
        log_info("TPO ready arm=" << TensionTest::armName(_p.arm) << " drive starts in 3 s, keep the scale taut");
    } else {
        setPhase(Phase::Settle);
    }
}

// The $TCT current loop, pulling in, with a brake on outward speed
void PayOutTest::currentLoop(double current, bool reelingIn) {
    double delta = GAIN * (_p.targetAdc - current);
    delta        = constrain(delta, -PWM_FALL, reelingIn ? 0.0 : PWM_RISE);
    _braking     = _speed > _p.speed;
    if (_braking) {
        delta = std::max(delta, BRAKE_GAIN * (_speed - _p.speed));
    }
    _pwm = constrain(_pwm + delta, 0.0, PWM_MAX);
    Maslow.axis[_p.arm].driveIn(int(_pwm + 0.5));
}

void PayOutTest::update() {
    if (_phase == Phase::Off) {
        return;
    }
    unsigned long now = millis();
    if (now - _lastTick < TICK_MS) {
        return;
    }
    double dt = (now - _lastTick) / 1000.0;
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
    _speed         = SPEED_ALPHA * (pos - _lastPos) / dt + (1 - SPEED_ALPHA) * _speed;
    _lastPos       = pos;
    if (!checkSafety(current, pos)) {
        return;
    }
    double rel = pos - _startPos;

    // Breakaway and grab-slip jumps, once the belt is free to go out
    if (_phase == Phase::Ready || _phase == Phase::Moving) {
        if (!_brokeAway && rel > BREAKAWAY_MM) {
            _brokeAway = true;
            _breakAdc  = current;
            _breakPwm  = axis.getMotorPower();
            _breakT    = (now - _testStart) / 1000.0;
            char buf[160];
            snprintf(buf,
                     sizeof(buf),
                     "TPO breakaway arm=%s t=%.2f adc=%.0f mA=%.1f pwm=%.0f pos=%.3f",
                     TensionTest::armName(_p.arm),
                     _breakT,
                     _breakAdc,
                     TensionTest::adcToMa(_breakAdc),
                     _breakPwm,
                     rel);
            log_info(buf);
            setPhase(Phase::Moving);
        }
        if (!_inJump && _speed > JUMP_START) {
            _inJump = true;
            _jumps++;
        } else if (_inJump && _speed < JUMP_STOP) {
            _inJump = false;
        }
        if (_phase == Phase::Moving && _speed > STEADY_MIN && !_braking) {
            _movN++;
            _movSumI += current;
            _movSumI2 += current * current;
            _movSumV += _speed;
        }
    }

    switch (_phase) {
        case Phase::Settle: {
            currentLoop(current, _speed < -MOVING_MM_S);
            bool still = std::fabs(_speed) < MOVING_MM_S && std::fabs(current - _p.targetAdc) <= std::max(10.0, 0.05 * _p.targetAdc);
            if (!still) {
                _stableSince = 0;
            } else if (_stableSince == 0) {
                _stableSince = now;
            }
            unsigned long waited = now - _phaseStart;
            bool          ready  = waited >= SETTLE_MIN_MS && _stableSince != 0 && now - _stableSince >= STILL_MS;
            if (ready || waited >= SETTLE_MAX_MS) {
                _startPos = pos;
                rel       = 0;
                setPhase(Phase::Ready);
                log_info("TPO ready arm=" << TensionTest::armName(_p.arm) << " on_target=" << (ready ? 1 : 0)
                                          << " pull the belt out now");
            }
            break;
        }
        case Phase::Ready:
        case Phase::Moving:
            if (driveMode()) {
                if (_phase == Phase::Ready && now - _phaseStart < COUNTDOWN_MS) {
                    _setpoint = _startPos;
                } else {
                    if (_phase == Phase::Ready) {
                        setPhase(Phase::Moving);  // no breakaway yet: the belt may not move at once
                    }
                    _setpoint = std::min(_setpoint + _p.speed * dt, _startPos + _p.dist);
                }
                axis.setTarget(_setpoint);
                axis.recomputePID();
                bool atEnd = _setpoint >= _startPos + _p.dist;
                if (atEnd && (rel >= _p.dist - 0.2 || now - _phaseStart > (unsigned long)(_p.dist / _p.speed * 1000) + END_WAIT_MS)) {
                    setPhase(Phase::EndHold);
                }
            } else {
                currentLoop(current, _speed < -MOVING_MM_S);
                if (rel >= _p.dist) {
                    _setpoint = _startPos + _p.dist;
                    axis.setTarget(_setpoint);
                    axis.resetPID();
                    _braking = false;
                    setPhase(Phase::EndHold);
                }
            }
            if (_phase == Phase::EndHold) {
                log_info("TPO end arm=" << TensionTest::armName(_p.arm) << " holding at max distance");
            }
            break;
        case Phase::EndHold:
            axis.setTarget(_setpoint);
            axis.recomputePID();
            _endN++;
            _endSumI += current;
            _endSumPwm += axis.getMotorPower();
            if (now - _phaseStart >= (unsigned long)(_p.holdS * 1000)) {
                setPhase(Phase::RampDown);
            }
            break;
        case Phase::RampDown: {
            double scale = 1.0 - double(now - _phaseStart) / RAMP_MS;
            if (scale <= 0) {
                finish();
                return;
            }
            double out = constrain(POS_P * (_setpoint - pos), -PWM_MAX, PWM_MAX);
            axis.runAtPWM(out * scale);
            break;
        }
        default:
            break;
    }

    if (now - _lastLog >= LOG_MS) {
        _lastLog = now;
        logSample(current, pos);
    }
}

bool PayOutTest::checkSafety(double current, double pos) {
    unsigned long now = millis();
    if (pos - _startPos > _p.dist + OVERRUN_MM) {
        halt("overrun");
        return false;
    }
    if (_initialPos - pos > REEL_IN_MM) {
        halt("reeled_in");
        return false;
    }
    if (current > ADC_CAP) {
        if (++_overCount >= OVER_TICKS) {
            halt("over_current");
            return false;
        }
    } else {
        _overCount = 0;
    }
    // Only while the belt is still: a stalled motor at real PWM must draw current
    double pwm = std::fabs(Maslow.axis[_p.arm].getMotorPower());
    if (pwm >= DEAD_PWM && current < DEAD_ADC && std::fabs(_speed) < STEADY_MIN) {
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

void PayOutTest::logSample(double current, double pos) {
    char buf[140];
    snprintf(buf,
             sizeof(buf),
             "TPO s t=%.2f ph=%s pos=%.3f v=%.2f adc=%.0f pwm=%.0f",
             (millis() - _testStart) / 1000.0,
             phaseName(),
             pos - _startPos,
             _speed,
             current,
             Maslow.axis[_p.arm].getMotorPower());
    log_info(buf);
}

void PayOutTest::logSummary() {
    double movMean = _movN ? _movSumI / _movN : 0;
    double movVar  = _movN ? _movSumI2 / _movN - movMean * movMean : 0;
    char   buf[340];
    snprintf(buf,
             sizeof(buf),
             "TPO summary arm=%s mode=%s target_adc=%.0f speed=%.1f dist=%.1f breakaway=%d breakaway_adc=%.0f "
             "breakaway_pwm=%.0f breakaway_t=%.2f moving_adc=%.1f moving_adc_sd=%.1f moving_v=%.2f moving_n=%d "
             "jumps=%d end_adc=%.1f end_pwm=%.1f pos=%.3f t_s=%.1f",
             TensionTest::armName(_p.arm),
             driveMode() ? "drive" : "current",
             _p.targetAdc,
             _p.speed,
             _p.dist,
             _brokeAway ? 1 : 0,
             _breakAdc,
             _breakPwm,
             _breakT,
             movMean,
             movVar > 0 ? std::sqrt(movVar) : 0.0,
             _movN ? _movSumV / _movN : 0.0,
             _movN,
             _jumps,
             _endN ? _endSumI / _endN : 0.0,
             _endN ? _endSumPwm / _endN : 0.0,
             Maslow.axis[_p.arm].getPosition() - _startPos,
             (millis() - _testStart) / 1000.0);
    log_info(buf);
}

void PayOutTest::finish() {
    Maslow.axis[_p.arm].stop();
    _phase = Phase::Off;
    logSummary();
    log_info("TPO done arm=" << TensionTest::armName(_p.arm));
    sys.set_state(State::Idle);
}

void PayOutTest::halt(const char* reason) {
    MotorUnit& axis = Maslow.axis[_p.arm];
    axis.stop();
    char buf[200];
    snprintf(buf,
             sizeof(buf),
             "TPO halt arm=%s reason=%s phase=%s adc=%.1f pos=%.3f t_s=%.1f",
             TensionTest::armName(_p.arm),
             reason,
             phaseName(),
             axis.getMotorCurrent(),
             axis.getPosition() - _startPos,
             (millis() - _testStart) / 1000.0);
    log_warn(buf);
    _phase = Phase::Off;
    logSummary();
    if (sys.state() == State::Homing) {
        sys.set_state(State::Idle);
    }
}

void PayOutTest::abort(const char* reason) {
    if (_phase != Phase::Off) {
        halt(reason);
    }
}
