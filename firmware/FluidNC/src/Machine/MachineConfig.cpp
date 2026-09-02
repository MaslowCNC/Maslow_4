// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "MachineConfig.h"

#include "Kinematics/Kinematics.h"
#include "Kinematics/MaslowKinematics.h"
#include "Maslow/Maslow.h"  // Maslow.using_default_config, M

#include "Motors/MotorDriver.h"
#include "Motors/NullMotor.h"

#include "Spindles/NullSpindle.h"
#include "ToolChangers/atc.h"
#include "Driver/Console.h"

#include "SettingsDefinitions.h"  // config_filename
#include "FileStream.h"

#include "Configuration/Parser.h"
#include "Configuration/ParserHandler.h"
#include "Configuration/Validator.h"
#include "Configuration/AfterParse.h"
#include "Config.h"  // ENABLE_*

#include "Driver/restart.h"
#include "Driver/backtrace.h"

#include "../Maslow/Maslow.h"  // using_default_config

#include <cstdio>
#include <cstring>
#include <atomic>
#include <memory>

Machine::MachineConfig* config;

// TODO FIXME: Split this file up into several files, perhaps put it in some folder and namespace Machine?

namespace Machine {
    void MachineConfig::group(Configuration::HandlerBase& handler) {
        // @config board
        // @default "None"
        // Descriptive text for the controller board, e.g. "ESP32 Dev Controller V4".
        // Informational only -- not validated against a list of known boards, and not
        // used to select behavior.
        handler.item("board", _board);

        // @config name
        // @default "None"
        // A basic description of the machine, e.g. "Router XYYZ 10V Spindle" -- shown in
        // startup/status messages.
        handler.item("name", _name);

        // @config meta
        // @default ""
        // @default_note empty
        // Free-form notes about the config file itself, e.g. "B. Dring 2022-03-15 Rev 2".
        handler.item("meta", _meta);

        groupM4Items(handler);

        // Maslow M4 - Limited sections
        handler.section("stepping", _stepping);

        handler.sections("uart", 1, MAX_N_UARTS, true, _uarts);
        handler.sections("uart_channel", 1, MAX_N_UARTS, true, _uart_channels);
#if MAX_N_I2SO
        // We currently support only one I2S bus
        handler.section("i2so", _i2so);
#endif
#if MAX_N_I2C
        handler.sections("i2c", 0, MAX_N_I2C, false, _i2c);
#endif
#if MAX_N_SPI
        // We currently support only one SPI bus
        handler.section("spi", _spi);
#endif

#if MAX_N_SDCARD
        handler.section("sdcard", _sdCard);
#endif

#if MAX_N_ETH
        handler.section("ethernet", _ethernet);
#endif

        handler.section("kinematics", _kinematics);
        handler.section("axes", _axes);

        handler.section("control", _control);
        handler.section("coolant", _coolant);
        handler.section("probe", _probe);
        handler.section("macros", _macros);
#if SUPPORT_PIN_EXTENDERS
        handler.section("extenders", _extenders);
#endif
        handler.section("start", _start);
        handler.section("parking", _parking);

        handler.section("user_outputs", _userOutputs);
        handler.section("user_inputs", _userInputs);

        ConfigurableModuleFactory::factory(handler);
        ATCs::ATCFactory::factory(handler);
        Spindles::SpindleFactory::factory(handler);
#if SUPPORT_LISTENERS
        Listeners::SysListenerFactory::factory(handler);
#endif

        // TODO: Consider putting these under a gcode: hierarchy level? Or motion control?

        // @config arc_tolerance_mm
        // @default 0.002
        // @tuning typical
        // Maximum deviation, in mm, allowed when tessellating G2/G3 arcs into straight-line
        // segments. Smaller values produce smoother arcs at the cost of more segments (and
        // therefore more planner/computation work). Rarely changed from the default.
        handler.item("arc_tolerance_mm", _arcTolerance, 0.001, 1.0);

        // @config junction_deviation_mm
        // @default 0.01
        // @tuning typical
        // Controls how aggressively the planner slows down at sharp corners between
        // consecutive moves (the Grbl-style "junction deviation" cornering algorithm).
        // Smaller values force more slowdown at corners; larger values allow faster
        // cornering at the cost of more deviation from the programmed path. Rarely changed
        // from the default -- see the planner source for the full derivation.
        handler.item("junction_deviation_mm", _junctionDeviation, 0.01, 1.0);

        // @config verbose_errors
        // @default true
        // @tuning typical
        // Includes descriptive text alongside the numeric error code in error responses.
        // Some GCode senders may not parse the extra text correctly.
        handler.item("verbose_errors", _verboseErrors);

        // @config report_inches
        // @default false
        // @tuning typical
        // Reports position and feed rate values in inches instead of millimeters. This
        // only affects reporting, not how input values in the GCode/config are interpreted.
        handler.item("report_inches", _reportInches);

        // @config enable_parking_override_control
        // @default false
        // @tuning typical
        // Enables the M56 GCode command, which lets a running program toggle the parking-
        // motion override on/off at runtime (M56 P0 disables parking, M56 P1 enables it).
        // This only gates whether M56 has any effect; start.deactivate_parking sets what
        // the override defaults to, and parking.enable is the separate switch for the
        // parking feature existing at all.
        handler.item("enable_parking_override_control", _enableParkingOverrideControl);

        // @config use_line_numbers
        // @default false
        // @tuning typical
        // When true, line numbers written into GCode as N<number> (e.g. N100) are tracked
        // and echoed back in status reports as Ln:100, so a report can be correlated with
        // the source line currently being executed. Reports Ln:0 when the GCode has no line
        // number information.
        handler.item("use_line_numbers", _useLineNumbers);

        // @config planner_blocks
        // @default 16
        // @tuning typical
        // Number of motion blocks held in the look-ahead planner buffer. Leave at the
        // default unless tuning for a special application.
        handler.item("planner_blocks", _planner_blocks, 10, 120);
    }

    void MachineConfig::groupM4Items(Configuration::HandlerBase& handler) {
        handler.item(M + "_vertical", Maslow.calibration.orientation);
        handler.item(M + "_calibration_grid_width_mm_X", Maslow.calibration.calibration_grid_width_mm_X, 0, 3000);
        handler.item(M + "_calibration_grid_height_mm_Y", Maslow.calibration.calibration_grid_height_mm_Y, 0, 3000);
        handler.item(M + "_calibration_grid_size", Maslow.calibration.calibrationGridSize, 3, 9);

        handler.item(M + "_Retract_Current_Threshold", Maslow.calibration.retractCurrentThreshold, 0, 3500);
        handler.item(M + "_Acceptable_Calibration_Threshold", Maslow.calibration.acceptableCalibrationThreshold, 0, 1);
        handler.item(M + "_Apply_Tension_Belt_Retraction_Limit", Maslow.calibration.applyTensionBeltRetractionLimitMm, 0.0, 4250.0);
        handler.item(M + "_Apply_Tension_Allow_Limiting", Maslow.calibration.applyTensionAllowLimiting);
        handler.item(M + "_Extend_Dist", Maslow.calibration.extendDist, 0, 4250);

        handler.item(M + "_Scale_X", Maslow.scaleX, .8, 1.2);
        handler.item(M + "_Scale_Y", Maslow.scaleY, .8, 1.2);
        handler.item(M + "_debugEnabled", Maslow.debugEnabled);

        // Work area constraints
        handler.item(M + "_Work_Area_X", Maslow.workAreaX, 1.0, 10000.0);
        handler.item(M + "_Work_Area_Y", Maslow.workAreaY, 1.0, 10000.0);
        handler.item(M + "_Work_Area_Center_Offset_X", Maslow.workAreaCenterOffsetX, -5000.0, 5000.0);
        handler.item(M + "_Work_Area_Center_Offset_Y", Maslow.workAreaCenterOffsetY, -5000.0, 5000.0);

        // Material thickness parameters - temporary storage for machine-level config
        handler.item(M + "_spoilboardThickness", _tempSpoilboardThickness, 0.0, 50.0);
        handler.item(M + "_workThickness", _tempWorkThickness, 0.0, 50.0);

        // Park position settings (machine coordinates, Z is lifted first)
        handler.item(M + "_Park_Z", Maslow.parkZ, -100.0, 100.0);
        handler.item(M + "_Park_X", Maslow.parkX, -10000.0, 10000.0);
        handler.item(M + "_Park_Y", Maslow.parkY, -10000.0, 10000.0);
    }

    void MachineConfig::afterParse() {
        if (_axes == nullptr) {
            log_config_error(M + "M4 expects the 'axes' section to be defined in the file or the default config");
            // The following is NOT expected to yield the correct result for the M4
            _axes = new Axes();
        }

        // coolant, kinematics, probe, userOutputs all run with their FluidNC defaults
        if (_coolant == nullptr) {
            _coolant = new CoolantControl();
        }

        if (_kinematics == nullptr) {
            _kinematics = new Kinematics();
        }

        if (_probe == nullptr) {
            _probe = new Probe();
        }

        if (_userOutputs == nullptr) {
            _userOutputs = new UserOutputs();
        }

        if (_userInputs == nullptr) {
            _userInputs = new UserInputs();
        }

#if MAX_N_SDCARD
        if (_sdCard == nullptr) {
            log_config_error(M + " M4 expects the 'scCard' section to be defined in the file or the default config");
            // The following is NOT expected to yield the correct result for the M4
            _sdCard = new SDCard();
        }
#endif

#if MAX_N_SPI
        if (_spi == nullptr) {
            log_config_error(M + " M4 expects the 'spi' section to be defined in the file or the default config");
            // The following is NOT expected to yield the correct result for the M4
            _spi = new SPIBus();
        }
#endif

        if (_stepping == nullptr) {
            log_config_error(M + " M4 expects the 'stepping' section to be defined in the file or the default config");
            // The following is NOT expected to yield the correct result for the M4
            _stepping = new Stepping();
        }

        // Synchronize machine-level material thickness parameters with MaslowKinematics
        auto maslowKinematics = ::Kinematics::getMaslowKinematics();
        if (maslowKinematics != nullptr) {
            maslowKinematics->setSpoilboardThickness(_tempSpoilboardThickness);
            maslowKinematics->setWorkThickness(_tempWorkThickness);
        }

        // We do not auto-create an I2SO bus config node
        // Only if an i2so section is present will config->_i2so be non-null
        // control, start, parking all run with their FluidNC defaults
        if (_control == nullptr) {
            _control = new Control();
        }

        if (_start == nullptr) {
            _start = new Start();
        }

        if (_parking == nullptr) {
            _parking = new Parking();
        }

        auto& spindles = Spindles::SpindleFactory::objects();
        if (spindles.size() == 0) {
            spindles.push_back(new Spindles::Null("NoSpindle"));
            //            Spindles::SpindleFactory::add(new Spindles::Null());
        }

        std::sort(spindles.begin(), spindles.end(), [](Spindles::Spindle* s1, Spindles::Spindle* s2) { return s1->_tool < s2->_tool; });

        // Precaution in case the full spindle initialization does not happen
        // due to a configuration error
        spindle = spindles[0];

        int32_t last_tool = -1;
        for (auto s : Spindles::SpindleFactory::objects()) {
            if (last_tool == -1 && s->_tool != 0) {  // first must be 0
                log_warn(s->name() << " spindle set to tool 0");
                s->_tool = 0;
            } else if (s->_tool <= last_tool) {
                s->_tool = last_tool + 100;
                log_warn(s->name() << " spindle tool set to:" << s->_tool);
            }
            last_tool = s->_tool;
        }

        // macros runs with its FluidNC defaults
        if (_macros == nullptr) {
            _macros = new Macros();
        }
    }

    // Maslow M4 built-in default configuration, used when the config file
    // cannot be loaded.  Composed from partial strings for readability.
    const std::string mcgrid = M + "_calibration_grid_";

    const std::string dcBoard = "name: Default (" + M + " S3 Board)\nboard: " + M + "\n";

    const std::string dcM4Vert            = M + "_vertical: false\n";
    const std::string dcM4CalibrationGrid = mcgrid + "width_mm_X: 0\n" + mcgrid + "height_mm_Y: 0\n" + mcgrid + "size: 9\n";

    const std::string dcM4CurrentThreshold = M + "_Retract_Current_Threshold: 1300\n" + M + "_Acceptable_Calibration_Threshold: 0.5\n";
    const std::string dcM4ApplyTensionLimit =
        M + "_Apply_Tension_Belt_Retraction_Limit: 300.0\n" + M + "_Apply_Tension_Allow_Limiting: true\n";

    const std::string dcM4Thickness = M + "_spoilboardThickness: 0.0\n" + M + "_workThickness: 0.0\n";

    const std::string dcM4Park = M + "_Park_Z: 2.0\n" + M + "_Park_X: 0.0\n" + M + "_Park_Y: 0.0\n";

    const std::string dcSpi      = "spi:\n  miso_pin: gpio.13\n  mosi_pin: gpio.11\n  sck_pin: gpio.12\n";
    const std::string dcSDCard   = "sdcard:\n  card_detect_pin: NO_PIN\n  cs_pin: gpio.10\n";
    const std::string dcStepping = "stepping:\n  engine: RMT\n  idle_ms: 240\n";
    const std::string dcUart1    = "uart1:\n  txd_pin: gpio.1\n  rxd_pin: gpio.2\n  baud: 115200\n  mode: 8N1\n";

    const std::string dcZMotor = "        uart_num: 1\n        cs_pin: NO_PIN\n        r_sense_ohms: 0.110\n"
                                 "        run_amps: 1.000\n        hold_amps: 0.500\n        microsteps: 0\n"
                                 "        stallguard: 0\n        stallguard_debug: false\n"
                                 "        toff_disable: 0\n        toff_stealthchop: 5\n        toff_coolstep: 3\n"
                                 "        run_mode: StealthChop\n        homing_mode: StealthChop\n        use_enable: true\n";

    const std::string dcKinematics = "kinematics:\n"
                                     "  MaslowKinematics:\n"
                                     "    tlX: 0.0\n"
                                     "    tlY: 2000.0\n"
                                     "    tlZ: 100.0\n"
                                     "    trX: 3000.0\n"
                                     "    trY: 2000.0\n"
                                     "    trZ: 100.0\n"
                                     "    blX: 0.0\n"
                                     "    blY: 0.0\n"
                                     "    blZ: 100.0\n"
                                     "    brX: 3000.0\n"
                                     "    brY: 0.0\n"
                                     "    brZ: 100.0\n"
                                     "    beltEndExtension: 30.0\n"
                                     "    armLength: 123.4\n"
                                     "    beltToothSpacing: 1.9988\n"
                                     "    encoderTeeth: 22.0\n"
                                     "    maxSegmentLength: 5.0\n"
                                     "    fixedZ: false\n";

    const std::string defaultConfig =
        dcBoard +
        // Maslow M4 default items
        dcM4Vert + dcM4CalibrationGrid + dcM4CurrentThreshold + dcM4ApplyTensionLimit + dcM4Thickness + dcM4Park +
        // Default sections
        dcSpi + dcSDCard + dcStepping + dcUart1 + dcKinematics +
        "axes:\n"
        "  z:\n    max_rate_mm_per_min: 400\n    acceleration_mm_per_sec2: 10\n    max_travel_mm: 100\n    steps_per_mm: 100\n"
        "    homing:\n      cycle: -1\n"
        "    motor0:\n      tmc_2209:\n        addr: 0\n        direction_pin: gpio.16\n        step_pin: gpio.15\n" +
        dcZMotor + "    motor1:\n      tmc_2209:\n        addr: 1\n        direction_pin: NO_PIN\n        step_pin: gpio.46\n" + dcZMotor;

    void MachineConfig::load() {
        // Maslow: unlike stock FluidNC, always try the user config file even
        // after a panic - the fixed built-in default config is the fallback,
        // so a bad config file cannot cause an unrecoverable reset loop.
        if (restart_was_panic()) {
            log_warn("Previous boot ended in panic - attempting to load config anyway");
            backtrace_t bt;
            if (backtrace_get(&bt)) {
                char buf[16];
                snprintf(buf, sizeof(buf), "0x%08x", bt.pc);
                log_error("Previous crash backtrace (PC=" << buf << " cause=" << bt.exccause << "):");
                std::string btLine = "Backtrace:";
                for (size_t i = 0; i < bt.num_addresses; i++) {
                    snprintf(buf, sizeof(buf), " 0x%08x", bt.addresses[i]);
                    btLine += buf;
                    btLine += ":0x00000000";
                }
                log_error(btLine.c_str());
            }
        }
        load_file(config_filename->get());
    }

    void MachineConfig::load_file(const std::string_view filename) {
        try {
            FileStream file(std::string { filename }, "rb", LocalFS);

            auto filesize = file.size();
            if (filesize <= 0) {
                log_config_error("Configuration file:" << filename << " is empty");
                load_yaml("");
                return;
            }

            auto buffer      = std::make_unique<char[]>(filesize + 1);
            buffer[filesize] = '\0';
            auto actual      = file.read(buffer.get(), filesize);
            if (actual != filesize) {
                log_config_error("Configuration file:" << filename << " read error - expected " << filesize << " got " << actual);
                return;
            }
            log_info("Configuration file:" << filename);
            load_yaml(std::string_view { buffer.get(), filesize });
        } catch (...) {
            // Maslow: fall back to the fully-functional built-in default config
            // instead of entering ConfigAlarm, so a fresh or corrupted filesystem
            // still yields a usable machine.
            log_warn("Cannot open configuration file:" << filename);
            log_info("Using default configuration");
            Maslow.using_default_config = true;
            load_yaml(defaultConfig);
        }
    }

    void MachineConfig::load_yaml(std::string_view input) {
        try {
            try {
                Configuration::Parser        parser(input);
                Configuration::ParserHandler handler(parser);

                // instance() is by reference, so we can just get rid of an old instance and
                // create a new one here:
                {
                    auto& machineConfig = instance();
                    if (machineConfig != nullptr) {
                        delete machineConfig;
                    }
                    machineConfig = new MachineConfig();
                }
                config = instance();

                handler.enterSection("machine", config);

                log_debug("Running after-parse tasks");
            } catch (std::exception& ex) {
                // Log exception:
                log_config_error("Configuration parse error: " << ex.what());
            }

            try {
                Configuration::AfterParse afterParse;
                config->afterParse();
                config->group(afterParse);
            } catch (std::exception& ex) {
                // Log exception:
                log_config_error("Configuration after-parse error: " << ex.what());
            }

            try {
                log_debug("Checking configuration");

                Configuration::Validator validator;
                config->validate();
                config->group(validator);

                // log_info("Heap size after configuration load is " << uint32_t(xPortGetFreeHeapSize()));
            } catch (std::exception& ex) {
                // Log exception:
                log_config_error("Configuration validation error: " << ex.what());
            }

        } catch (...) {
            // Get rid of buffer and return
            log_config_error("Unknown error while processing config file");
        }

        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    MachineConfig::~MachineConfig() {
        delete _axes;
#if MAX_N_I2SO
        delete _i2so;
#endif
        delete _coolant;
        delete _probe;
#if MAX_N_SDCARD
        delete _sdCard;
#endif
#if MAX_N_ETH
        delete _ethernet;
#endif
#if MAX_N_SDCARD
        delete _spi;
#endif
        delete _control;
        delete _macros;
    }
}
