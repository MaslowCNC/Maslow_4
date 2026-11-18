// Copyright (c) 2026 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#include "ReversalTest.h"
#include "TensionTest.h"
#include "Maslow.h"
#include "../Logging.h"
#include "../System.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

static constexpr double POS_P     = 300;   // the position gain in MotorUnit.cpp
static constexpr int    PWM_FLOOR = 75;    // DCMotor::runAtPWM's motorStartsToMovePWM
static constexpr int    PWM_MAX   = 1023;

static constexpr unsigned long CHECK_MS   = 20;  // encoder read and safety checks
static constexpr unsigned long DUMP_MS    = 5;   // one sample line per 5 ms after the run
static constexpr double        MOVED_MM   = 0.1;
static constexpr double        LAG_MM     = 0.5;   // pay-out lag that counts as the encoder not following
static constexpr double        ADVANCE_MM = 0.05;  // encoder progress that counts as following

static constexpr double        OVERRUN_MM = 5;   // past the pay-out distance
static constexpr double        REEL_IN_MM = 10;  // inside the start position
static constexpr double        ADC_CAP    = 3000;
static constexpr int           OVER_TICKS = 3;
static constexpr double        DEAD_PWM   = 300;
static constexpr double        DEAD_ADC   = 25;
static constexpr unsigned long DEAD_MS    = 1000;
static constexpr unsigned long ENC_MS     = 300;

bool ReversalTest::parse(const char* value, Params& p, std::string& why) {
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
    if (toks.size() < 3) {
        why = "use $TRV=<arm>,<out mm>,<speed mm/s>[,hold=s][,cycles=n][,floor=0|1][,slack=s][,pmax=n][,time=s]";
        return false;
    }
    p.arm = TensionTest::armFromName(toks[0]);
    if (p.arm < 0) {
        why = "unknown arm '" + toks[0] + "', use TL, TR, BL or BR";
        return false;
    }
    for (size_t i = 1; i < toks.size(); i++) {
        std::string key, val = toks[i];
        size_t      eq = val.find('=');
        if (eq != std::string::npos) {
            key = val.substr(0, eq);
            val = val.substr(eq + 1);
        } else if (i > 2) {
            why = "expected key=value after the first three values, got '" + toks[i] + "'";
            return false;
        }
        char* end = nullptr;
        float v   = strtof(val.c_str(), &end);
        if (val.empty() || *end != '\0' || !std::isfinite(v)) {
            why = "bad number '" + val + "'";
            return false;
        }
        if (key.empty()) {
            (i == 1 ? p.outMm : p.speed) = v;
        } else if (key == "hold") {
            p.holdS = v;
        } else if (key == "cycles") {
            p.cycles = int(v);
        } else if (key == "floor") {
            p.floor = v != 0;
        } else if (key == "slack") {
            p.slackS = v;
        } else if (key == "pmax") {
            p.pmax = v;
        } else if (key == "time") {
            p.maxS = v;
        } else {
            why = "unknown key '" + key + "' (hold, cycles, floor, slack, pmax, time)";
            return false;
        }
    }
    if (p.outMm < 0.2 || p.outMm > 20 || p.speed < 0.5 || p.speed > 50) {
        why = "out must be 0.2-20 mm and speed 0.5-50 mm/s";
        return false;
    }
    if (p.holdS < 0.5 || p.holdS > 30 || p.cycles < 1 || p.cycles > 5) {
        why = "hold must be 0.5-30 s and cycles 1-5";
        return false;
    }
    if (p.slackS < 0.2 || p.slackS > 5 || p.pmax < 100 || p.pmax > PWM_MAX || p.maxS < 10 || p.maxS > 300) {
        why = "slack must be 0.2-5 s, pmax 100-1023 and time 10-300 s";
        return false;
    }
    return true;
}

// Steps: 0 holds at the start, then per cycle: out, hold_out, back, hold_back
const char* ReversalTest::stepName(int step) const {
    if (step == 0) {
        return "hold0";
    }
    static const char* const names[] = { "out", "hold_out", "back", "hold_back" };
    return names[(step - 1) % 4];
}

double ReversalTest::stepTarget(int step) const {
    if (step == 0) {
        return 0;
    }
    int k = (step - 1) % 4;
    return (k == 0 || k == 1) ? _p.outMm : 0;
}

bool ReversalTest::start(const Params& p) {
    freeSamples();
    _samples = (Sample*)malloc(sizeof(Sample) * MAX_SAMPLES);
    if (!_samples) {
        log_error("TRV refused: no memory for the sample buffer");
        return false;
    }
    _p              = p;
    MotorUnit& axis = Maslow.axis[_p.arm];
    axis.stop();
    _startCalState = Maslow.calibration.getCurrentState();
    _startPos      = axis.getPosition();
    _sp            = 0;
    _n = _dumpIdx = 0;
    _stepCount    = 1 + 4 * _p.cycles;
    _encoderFailSince = _deadSince = 0;
    _overCount                     = 0;

    // Sample at 50 Hz, slower if the whole profile wouldn't fit in the buffer
    double expectS = _p.holdS * (1 + 2 * _p.cycles) + 2 * _p.cycles * _p.outMm / _p.speed + 2;
    _sampleMs      = std::max<unsigned long>(20, (unsigned long)std::ceil(expectS * 1000 / MAX_SAMPLES));

    _testStart = _lastSample = _lastCheck = _lastCtl = millis();
    _phase                                = Phase::Hold;

    char buf[220];
    snprintf(buf,
             sizeof(buf),
             "TRV start arm=%s out=%.2f speed=%.1f hold_s=%.1f cycles=%d floor=%d slack_s=%.1f pmax=%.0f sample_ms=%lu pos=%.3f",
             TensionTest::armName(_p.arm),
             _p.outMm,
             _p.speed,
             _p.holdS,
             _p.cycles,
             _p.floor ? 1 : 0,
             _p.slackS,
             _p.pmax,
             _sampleMs,
             _startPos);
    log_info(buf);
    beginStep(0);
    return true;
}

void ReversalTest::beginStep(int step) {
    _step       = step;
    _stepStart  = millis();
    _st         = Stats {};
    _st.errMin  = 1e9;
    _st.errMax  = -1e9;
    _stepPos0   = Maslow.axis[_p.arm].getPosition() - _startPos;
    int k       = step == 0 ? 3 : (step - 1) % 4;
    _phase      = k == 0 ? Phase::MoveOut : k == 2 ? Phase::MoveBack : Phase::Hold;
    if (k == 0) {
        _lastAdvancePos = Maslow.axis[_p.arm].getPosition() - _startPos;
        _lastAdvanceT   = _stepStart;
        _slackHandled   = false;
    }
}

void ReversalTest::endStep() {
    double posEnd = Maslow.axis[_p.arm].getPosition() - _startPos;
    char   buf[260];
    snprintf(buf,
             sizeof(buf),
             "TRV phase arm=%s step=%d kind=%s sp=%.3f err_mean=%.3f err_min=%.3f err_max=%.3f flips=%d adc_mean=%.0f adc_max=%.0f "
             "dead_ms=%ld pos_end=%.3f t_s=%.2f",
             TensionTest::armName(_p.arm),
             _step,
             stepName(_step),
             _sp,
             _st.n ? _st.errSum / _st.n : 0.0,
             _st.n ? _st.errMin : 0.0,
             _st.n ? _st.errMax : 0.0,
             _st.flips,
             _st.n ? _st.adcSum / _st.n : 0.0,
             _st.n ? _st.adcMax : 0.0,
             (_phase == Phase::MoveOut || _phase == Phase::MoveBack) ? (_st.moved ? long(_st.deadMs) : -1L) : -1L,
             posEnd,
             (millis() - _testStart) / 1000.0);
    log_info(buf);
}

// The machine's drive: P output through runAtPWM (with its floor), or raw without the floor
void ReversalTest::drive(double out) {
    MotorUnit& axis = Maslow.axis[_p.arm];
    if (_p.floor) {
        axis.runAtPWM(out);
    } else if (out >= 0) {
        axis.driveOut(int(out + 0.5));
    } else {
        axis.driveIn(int(-out + 0.5));
    }
}

void ReversalTest::serviceDump() {
    if (_phase != Phase::Dump) {
        return;
    }
    unsigned long now = millis();
    if (now - _lastDumpLine < DUMP_MS) {
        return;
    }
    _lastDumpLine = now;
    if (_dumpIdx >= _n) {
        log_info("TRV done arm=" << TensionTest::armName(_p.arm) << " samples=" << _n);
        freeSamples();
        _phase = Phase::Off;
        return;
    }
    const Sample& s = _samples[_dumpIdx++];
    char          buf[120];
    snprintf(buf,
             sizeof(buf),
             "TRV s t=%.3f sp=%.2f pos=%.2f err=%.2f pid=%d out=%d adc=%u",
             s.t / 1000.0,
             s.sp / 100.0,
             s.pos / 100.0,
             (s.pos - s.sp) / 100.0,
             s.pid,
             s.out,
             s.adc);
    log_info(buf);
}

void ReversalTest::update() {
    if (_phase == Phase::Off) {
        return;
    }
    unsigned long now = millis();

    if (_phase == Phase::Dump) {
        return;  // serviceDump() prints the samples
    }

    MotorUnit& axis = Maslow.axis[_p.arm];

    if (now - _lastCheck >= CHECK_MS) {
        _lastCheck = now;
        if (Maslow.calibration.getCurrentState() != _startCalState) {
            abort("state_changed");
            return;
        }
        if (now - _testStart > (unsigned long)(_p.maxS * 1000)) {
            abort("time_limit");
            return;
        }
        if (axis.updateEncoderPosition()) {
            _encoderFailSince = 0;
        } else if (_encoderFailSince == 0) {
            _encoderFailSince = now;
        } else if (now - _encoderFailSince > ENC_MS) {
            abort("encoder");
            return;
        }
    }

    double current = axis.getMotorCurrent();
    double pos     = axis.getPosition() - _startPos;

    // Setpoint
    double dt = (now - _lastCtl) / 1000.0;
    _lastCtl  = now;
    if (dt > 0.1) {
        dt = 0.1;  // first call, or a stall: don't jump the setpoint
    }
    double target = stepTarget(_step);
    bool   stepDone;
    if (_phase == Phase::MoveOut || _phase == Phase::MoveBack) {
        double dir = target > _sp ? 1 : -1;
        _sp += dir * _p.speed * dt;
        if ((dir > 0 && _sp >= target) || (dir < 0 && _sp <= target)) {
            _sp = target;
        }
        stepDone = _sp == target;
    } else {
        stepDone = now - _stepStart >= (unsigned long)(_p.holdS * 1000);
    }

    double pid = constrain(POS_P * (_sp - pos), -double(_p.pmax), double(_p.pmax));
    double err = pos - _sp;  // the sign BE uses
    if (!checkSafety(current, pos, err, pid)) {
        return;
    }
    drive(pid);

    // Stats for this step
    _st.n++;
    _st.errSum += err;
    _st.errMin = std::min(_st.errMin, err);
    _st.errMax = std::max(_st.errMax, err);
    _st.adcSum += current;
    _st.adcMax = std::max(_st.adcMax, current);
    int sign   = pid > 0 ? 1 : pid < 0 ? -1 : 0;
    if (sign != 0 && _st.lastSign != 0 && sign != _st.lastSign) {
        _st.flips++;
    }
    if (sign != 0) {
        _st.lastSign = sign;
    }
    if (!_st.moved && (_phase == Phase::MoveOut || _phase == Phase::MoveBack)) {
        bool moved = _phase == Phase::MoveOut ? pos > _stepPos0 + MOVED_MM : pos < _stepPos0 - MOVED_MM;
        if (moved) {
            _st.moved  = true;
            _st.deadMs = now - _stepStart;
        }
    }

    // Sample
    if (now - _lastSample >= _sampleMs && _n < MAX_SAMPLES) {
        _lastSample  = now;
        int floored  = int(pid);
        if (_p.floor) {
            long mag = PWM_FLOOR + long(std::fabs(pid)) * (PWM_MAX - PWM_FLOOR) / PWM_MAX;
            floored  = pid < 0 ? -int(mag) : int(mag);
        }
        Sample& s = _samples[_n++];
        s.t       = now - _testStart;
        s.sp      = int16_t(std::lround(_sp * 100));
        s.pos     = int16_t(std::lround(pos * 100));
        s.pid     = int16_t(std::lround(pid));
        s.out     = int16_t(floored);
        s.adc     = uint16_t(std::lround(current));
    }

    // Encoder not following the pay-out: the belt has no tension outside, so turn back now
    if ((_phase == Phase::MoveOut || (_phase == Phase::Hold && (_step - 1) % 4 == 1 && _step > 0)) && !_slackHandled) {
        if (pos > _lastAdvancePos + ADVANCE_MM) {
            _lastAdvancePos = pos;
            _lastAdvanceT   = now;
        } else if (_sp - pos > LAG_MM && now - _lastAdvanceT > (unsigned long)(_p.slackS * 1000)) {
            _slackHandled = true;
            char buf[160];
            snprintf(buf,
                     sizeof(buf),
                     "TRV slack arm=%s step=%d t=%.2f sp=%.3f pos=%.3f lag=%.3f pid=%.0f",
                     TensionTest::armName(_p.arm),
                     _step,
                     (now - _testStart) / 1000.0,
                     _sp,
                     pos,
                     _sp - pos,
                     pid);
            log_info(buf);
            endStep();
            int back = _step + ((_step - 1) % 4 == 0 ? 2 : 1);  // this cycle's "back" step
            beginStep(back);
            return;
        }
    }

    if (stepDone) {
        endStep();
        if (_step + 1 >= _stepCount) {
            stopAndDump(false);
        } else {
            beginStep(_step + 1);
        }
    }
}

bool ReversalTest::checkSafety(double current, double pos, double err, double out) {
    unsigned long now = millis();
    if (pos > _p.outMm + OVERRUN_MM) {
        abort("overrun");
        return false;
    }
    if (pos < -REEL_IN_MM) {
        abort("reeled_in");
        return false;
    }
    if (current > ADC_CAP) {
        if (++_overCount >= OVER_TICKS) {
            abort("over_current");
            return false;
        }
    } else {
        _overCount = 0;
    }
    // Near the setpoint at real PWM, a stalled motor must draw current.  Not while the encoder
    // lags, where the spool can spin freely into internal slack.
    if (std::fabs(out) >= DEAD_PWM && current < DEAD_ADC && std::fabs(err) < 0.3) {
        if (_deadSince == 0) {
            _deadSince = now;
        } else if (now - _deadSince > DEAD_MS) {
            abort("dead_current_sense");
            return false;
        }
    } else {
        _deadSince = 0;
    }
    return true;
}

void ReversalTest::stopAndDump(bool halted) {
    Maslow.axis[_p.arm].stop();
    if (sys.state() == State::Homing) {
        sys.set_state(State::Idle);
    }
    log_info("TRV " << (halted ? "halted" : "finished") << " arm=" << TensionTest::armName(_p.arm)
                    << " pos=" << (Maslow.axis[_p.arm].getPosition() - _startPos) << " samples=" << _n << ", printing them");
    _dumpIdx      = 0;
    _lastDumpLine = millis();
    _phase        = Phase::Dump;
}

void ReversalTest::freeSamples() {
    free(_samples);
    _samples = nullptr;
    _n       = 0;
}

void ReversalTest::abort(const char* reason) {
    if (_phase == Phase::Off || _phase == Phase::Dump) {
        return;  // the motor is already off; let the printout finish
    }
    MotorUnit& axis = Maslow.axis[_p.arm];
    axis.stop();
    char buf[180];
    snprintf(buf,
             sizeof(buf),
             "TRV halt arm=%s reason=%s step=%d kind=%s sp=%.3f pos=%.3f adc=%.0f t_s=%.2f",
             TensionTest::armName(_p.arm),
             reason,
             _step,
             stepName(_step),
             _sp,
             axis.getPosition() - _startPos,
             axis.getMotorCurrent(),
             (millis() - _testStart) / 1000.0);
    log_warn(buf);
    endStep();
    stopAndDump(true);
}
