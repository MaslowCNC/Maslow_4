// Copyright 2024 Maslow CNC
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstdint>

namespace MaslowWatchdog {
    // Initialize the watchdog system
    void init();

    // Start the watchdog task on Core 0
    void start();

    // Ping the watchdog from main thread (should be called every 200ms)
    void ping();

    // Disarm the watchdog for 60 seconds (for long operations like file upload/firmware update)
    void disarm();

    // Check if system was restarted due to watchdog timeout
    bool was_watchdog_reset();

    // Clear the watchdog reset flag
    void clear_watchdog_flag();
}
