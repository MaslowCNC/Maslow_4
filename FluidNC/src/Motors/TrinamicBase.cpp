// Copyright (c) 2021 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TrinamicBase.h"
#include "../Machine/MachineConfig.h"

#include <atomic>

namespace MotorDrivers {
    EnumItem trinamicModes[] = { { TrinamicMode::StealthChop, "StealthChop" },
                                 { TrinamicMode::CoolStep, "CoolStep" },
                                 { TrinamicMode::StallGuard, "StallGuard" },
                                 EnumItem(TrinamicMode::StealthChop) };

    std::vector<TrinamicBase*> TrinamicBase::_instances;  // static list of all drivers for stallguard reporting
    int TrinamicBase::_log_counter = 0;  // Static counter for 1-second logging

    // Another approach would be to register a separate timer for each instance.
    // I think that timers are cheap so having only a single timer might not buy us much
    void TrinamicBase::read_sg(TimerHandle_t timer) {
        if (inMotionState()) {
            _log_counter++;
            bool should_log = (_log_counter >= 5);  // Log every 5 * 200ms = 1 second
            if (should_log) {
                _log_counter = 0;
            }

            for (TrinamicBase* t : _instances) {
                if (t->_stallguardDebugMode) {
                    //log_info("SG:" << t->_stallguardDebugMode);
                    t->debug_message();
                }

                // Monitor current when system is in run state (moving)
                uint16_t current_raw = t->read_current_sense();
                t->update_current_average(current_raw);

                // Log current info once per second
                if (should_log) {
                    log_info("Motor " << t->axisName() << " current: " << t->_current_average << " (raw: " << current_raw << ")");
                }
            }
        }
    }

    // calculate a tstep from a rate
    // tstep = fclk / (time between 1/256 steps)
    // This is used to set the stallguard window from the homing speed.
    // The percent is the offset on the window
    uint32_t TrinamicBase::calc_tstep(float speed, float percent) {
        double tstep = speed / 60.0 * config->_axes->_axis[axis_index()]->_stepsPerMm * (256.0 / _microsteps);
        tstep        = fclk / tstep * percent / 100.0;

        return static_cast<uint32_t>(tstep);
    }

    // =========== Reporting functions ========================

    bool TrinamicBase::report_open_load(bool ola, bool olb) {
        if (ola || olb) {
            log_warn("    Driver Open Load a:" << yn(ola) << " b:" << yn(olb));
            return true;
        }
        return false;  // no error
    }

    bool TrinamicBase::report_short_to_ground(bool s2ga, bool s2gb) {
        if (s2ga || s2gb) {
            log_warn("    Driver Short Coil a:" << yn(s2ga) << " b:" << yn(s2gb));
        }
        return false;  // no error
    }

    bool TrinamicBase::report_over_temp(bool ot, bool otpw) {
        if (ot || otpw) {
            log_warn("    Driver Temp Warning:" << yn(otpw) << " Fault:" << yn(ot));
            return true;
        }
        return false;  // no error
    }

    bool TrinamicBase::report_short_to_ps(bool vsa, bool vsb) {
        // check for short to power supply
        if (vsa || vsb) {
            log_warn("    Driver Short vsa:" << yn(vsa) << " vsb:" << yn(vsb));
            return true;
        }
        return false;  // no error
    }

    bool TrinamicBase::set_homing_mode(bool isHoming) {
        set_registers(isHoming);
        return true;
    }

    float TrinamicBase::holdPercent() {
        if (_run_current == 0) {
            return 0.0;
        }

        float hold_percent = _hold_current / _run_current;
        if (hold_percent > 1.0) {
            hold_percent = 1.0;
        }

        return hold_percent;
    }

    bool TrinamicBase::reportTest(uint8_t result) {
        switch (result) {
            case 1:
                log_error(axisName() << " driver test failed. Check connection");
                return false;
            case 2:
                log_error(axisName() << " driver test failed. Check motor power");
                return false;
            default:
                log_info(axisName() << " driver test passed");
                return true;
        }
    }

    bool TrinamicBase::checkVersion(uint8_t expected, uint8_t got) {
        if (expected != got) {
            log_error(axisName() << " TMC driver not detected - expected 0x" << to_hex(expected) << " got 0x" << to_hex(got));
            return false;
        }
        log_info(axisName() << " driver test passed");
        return true;
    }

    void TrinamicBase::reportCommsFailure(void) { log_info(axisName() << " communications check failed"); }

    bool TrinamicBase::startDisable(bool disable) {
        if (_has_errors) {
            return false;
        }

        if ((_disabled == disable) && _disable_state_known) {
            return false;
        }

        _disable_state_known = true;
        _disabled            = disable;

        _disable_pin.synchronousWrite(_disabled);
        return true;
    }

    void TrinamicBase::config_motor() {
        _has_errors = !test();  // Try communicating with motor. Prints an error if there is a problem.

        init_step_dir_pins();

        if (_has_errors) {
            return;
        }

        set_registers(false);
    }
    void TrinamicBase::registration() {
        // Display the stepper library version message once, before the first
        // TMC config message.
        if (_instances.empty()) {
            log_debug("TMCStepper Library Ver. 0x" << to_hex(TMCSTEPPER_VERSION));
            auto timer = xTimerCreate("Stallguard", 200, true, nullptr, read_sg);
            // Timer failure is not fatal because you can still use the system
            if (!timer) {
                log_error("Failed to create timer for stallguard");
            } else if (xTimerStart(timer, 0) == pdFAIL) {
                log_error("Failed to start timer for stallguard");
            }
        }

        _instances.push_back(this);

        config_message();
    }

    void TrinamicBase::update_current_average(uint16_t current_raw) {
        // Take absolute value (though current_raw should already be positive from cs_actual)
        float current_abs = static_cast<float>(current_raw);
        
        // Add to circular buffer
        _current_samples[_current_sample_index] = current_abs;
        _current_sample_index = (_current_sample_index + 1) % CURRENT_SAMPLE_SIZE;
        
        // Update sample count for initial fill
        if (_current_sample_count < CURRENT_SAMPLE_SIZE) {
            _current_sample_count++;
        }
        
        // Calculate simple moving average
        float sum = 0.0f;
        for (int i = 0; i < _current_sample_count; i++) {
            sum += _current_samples[i];
        }
        _current_average = sum / _current_sample_count;
    }
}
