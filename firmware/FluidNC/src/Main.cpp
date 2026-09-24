// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#ifndef UNIT_TEST

#    include "Main.h"
#    include "Machine/MachineConfig.h"

#    include "Config.h"
#    include "Report.h"
#    include "Settings.h"
#    include "SettingsDefinitions.h"
#    include "Limit.h"
#    include "Protocol.h"
#    include "System.h"
#    include "Driver/Console.h"
#    include "MotionControl.h"
#    include "Platform.h"
#    include "StartupLog.h"
#    include "Module.h"
#    include "FluidPath.h"

#    include "Driver/localfs.h"

#    include "ToolChangers/atc.h"

#    include <Arduino.h>

extern void make_user_commands();

// TEMPORARY boot diagnostics: record milestones and echo the most recent one
// every 2s from a watchdog task, so the stall point is visible whenever a
// terminal attaches.  Remove after bring-up.
#    include "USBCDC.h"
extern USBCDC TUSBCDCSerial;
static volatile const char* lastBootMark = "none";
void bootTrace(const char* m) {
    lastBootMark = m;  // recorded only; the heartbeat task is the sole CDC writer
}
static void bootTraceTask(void*) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        TUSBCDCSerial.print("[TRACE last=");
        TUSBCDCSerial.print((const char*)lastBootMark);
        TUSBCDCSerial.println("]");
    }
}
static void startBootTrace() {
    xTaskCreate(bootTraceTask, "boottrace", 4096, nullptr, 1, nullptr);
}
#    define BOOTMARK(x) bootTrace(x)


#    ifdef BOARD_HAS_PSRAM
#        include "esp_heap_caps.h"
#    endif

void setup() {
    platform_preinit();

#    ifdef BOARD_HAS_PSRAM
    // The framework is built with CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096, so
    // only allocations larger than 4 KB land in PSRAM and everything smaller
    // competes for internal DRAM.  On a Maslow M4 that leaves ~20 KB free once
    // WiFi and the web server are up, against ~70 KB on the previous firmware,
    // and WebUI cannot load: concurrent XHRs plus the 149 KB index.html.gz
    // transfer exhaust it, so requests come back truncated.  Lower the
    // threshold so ordinary allocations go to the 2 MB of PSRAM as well.
    // DMA-capable requests are unaffected - those ask for MALLOC_CAP_DMA and
    // are always served from internal memory.
    // 256 rather than something larger because the framework also sets
    // CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=0, so nothing holds internal DRAM
    // back for the allocations that can only live there - FreeRTOS objects and
    // DMA buffers.  newlib does not fail gracefully when those run out: fopen()
    // aborts inside lock_init_generic() instead of returning NULL, which took
    // the board down mid "Retract All".
    heap_caps_malloc_extmem_enable(256);
#    endif

    set_state(State::Starting);

    try {
        timing_init();

        // Load settings from non-volatile storage
        settings_init();

        Console.init();  // Setup main interaction channel
        startBootTrace();

        // Setup input polling loop after loading the configuration,
        // because the polling may depend on the config
        allChannels.init();
        BOOTMARK("A allChannels.init done");

        // WebUI::WiFiConfig::reset();

        protocol_init();
        BOOTMARK("B protocol_init done");

        make_coordinates();

        log_info("FluidNC " << git_info << " " << git_url);

        if (localfs_mount()) {
            log_info("Local filesystem is " << LocalFS.prefix);
        }

        config->load();
        BOOTMARK("C config->load done");

        make_user_commands();
        BOOTMARK("D make_user_commands done");

        log_info("Machine " << config->_name);
        log_info("Board " << config->_board);

        // The initialization order reflects dependencies between the subsystems
        for (size_t i = 1; i < MAX_N_UARTS; i++) {
            if (config->_uarts[i]) {
                config->_uarts[i]->begin();
            }
        }
        for (size_t i = 1; i < MAX_N_UARTS; i++) {
            if (config->_uart_channels[i]) {
                config->_uart_channels[i]->init();
            }
        }

#    if MAX_N_I2SO
        if (config->_i2so) {
            config->_i2so->init();
        }
#    endif

#    if MAX_N_SPI
        if (config->_spi) {
            config->_spi->init();
#        if MAX_N_SDCARD
            if (config->_sdCard != nullptr) {
                config->_sdCard->init();
            }
#        endif
        }
#    endif

#    if MAX_N_I2C
        for (size_t i = 0; i < MAX_N_I2C; i++) {
            if (config->_i2c[i]) {
                config->_i2c[i]->init();
            }
        }
#    endif

#    if SUPPORT_PIN_EXTENDERS
        // We have to initialize the extenders first, before pins are used
        if (config->_extenders) {
            config->_extenders->init();
        }
#    endif

#    if SUPPORT_LISTENERS
        auto listeners = Listeners::SysListenerFactory::objects();
        for (auto l : listeners) {
            l->init();
        }
#    endif

        Stepping::init();  // Configure stepper interrupt timers
        BOOTMARK("E Stepping::init done");

        plan_init();

        config->_userOutputs->init();

        config->_userInputs->init();

        Axes::init();
        BOOTMARK("F Axes::init done");

        config->_control->init();

        config->_kinematics->init();
        BOOTMARK("G kinematics init done");

        limits_init();

        // Initialize system state.
        for (auto const& module : Modules()) {
            module->init();
        }
        for (auto const& module : ConfigurableModules()) {
            module->init();
        }

        auto atcs = ATCs::ATCFactory::objects();
        for (auto const& atc : atcs) {
            atc->init();
        }

        if (!state_is(State::ConfigAlarm)) {
            auto spindles = Spindles::SpindleFactory::objects();
            for (auto const& spindle : spindles) {
                spindle->init();
            }
            bool stopped_spindle, new_spindle;
            Spindles::Spindle::switchSpindle(0, spindles, spindle, stopped_spindle, new_spindle);

            config->_coolant->init();
            config->_probe->init();
        }

        make_proxies();
        BOOTMARK("H make_proxies done");

    } catch (std::exception& ex) {
        // Log exception:
        log_config_error("Critical error in setup(): " << ex.what());
    }

    poll_gpios();  // Initial poll to send events for initial pin states

    allChannels.ready();
    allChannels.deregistration(&startupLog);
    protocol_send_event(&startEvent);
    BOOTMARK("I setup complete");
}

void loop() {
    vTaskPrioritySet(NULL, 2);
    static size_t tries = 0;
    try {
        // Start the main loop. Processes program inputs and executes them.
        // This can exit on a system abort condition, in which case loop()
        // is re-executed by an enclosing loop.  It can also exit via a
        // throw that is caught and handled below.
        protocol_main_loop();
    } catch (std::exception& ex) {
        // If an assertion fails, we display a message and restart.
        // This could result in repeated restarts if the assertion
        // happens before waiting for input, but that is unlikely
        // because the code in reset_variables() and the code
        // that precedes the input loop has few configuration
        // dependencies.  The safest approach would be to set
        // a "reconfiguration" flag and redo the configuration
        // step, but that would require combining setup()
        // and loop() into a single control flow, and it would
        // require careful teardown of the existing configuration
        // to avoid memory leaks. It is probably worth doing eventually.
        log_config_error("Critical error in loop(): " << ex.what());
    }
    // sys.abort is a user-initiated exit via ^x so we don't limit the number of occurrences
    if (!sys.abort() && ++tries > 1) {
        log_info("Stalling due to too many failures");
        while (1) {}
    }
}
#endif

void WEAK_LINK machine_init() {}
