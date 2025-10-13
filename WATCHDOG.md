# Watchdog System

## Overview
The Maslow CNC firmware includes a watchdog system to protect against firmware hangs and ensure system reliability. If the main thread stops responding for more than 750ms, the watchdog automatically halts all motors, sets a persistent flag, and restarts the system.

## Features
- **Automatic hang detection**: Monitors main thread with 750ms timeout
- **Motor protection**: Halts all A/B/C/D axis motors before restart
- **Persistent recovery**: Remembers watchdog restarts across power cycles
- **Alarm mode**: Forces safe state on recovery boot
- **Long operation support**: Automatically disarms during uploads and updates

## How It Works

### Normal Operation
1. Watchdog task runs on Core 0 with high priority (2)
2. Main thread pings watchdog every 200ms
3. Watchdog monitors time since last ping
4. If >750ms without ping, watchdog triggers

### Watchdog Trigger Sequence
1. Set persistent flag in NVS storage
2. Halt all motors (A/B/C/D axes)
3. Wait 100ms for motors to stop
4. Restart the system

### Recovery on Boot
1. Check persistent flag in NVS
2. If flag is set:
   - Log: "WARNING RESTARTING AFTER WATCHDOG TIMEOUT"
   - Enter alarm mode (all motion disabled)
   - Clear the flag
3. Operator must acknowledge alarm before resuming

### Long Operations
For operations that intentionally take >750ms, the watchdog automatically disarms for 60 seconds:
- Web file uploads
- Firmware OTA updates
- Xmodem serial file transfers (send/receive)

After the operation completes, the next ping re-arms the watchdog.

## Architecture

### Core 0 Tasks (SUPPORT_TASK_CORE = 0)
| Task | Priority | Purpose |
|------|----------|---------|
| watchdog | **2** | Monitor main thread health |
| telemetry | 1 | Log telemetry data to SD |
| polling | 1 | Poll input sources |
| output | 1 | Manage output messages |

The watchdog has **higher priority** than other Core 0 tasks, ensuring it runs even under heavy load.

### Core 1 (Main Thread)
- `protocol_main_loop()` - Pings watchdog every 200ms
- `Maslow.update()` - Main control loop
- G-code execution and motion planning

### WiFi Activity
WiFi runs on ESP32's internal WiFi stack (typically Core 0). The 750ms timeout accommodates WiFi latency and occasional delays from network operations.

## Configuration

### Timing Parameters
```cpp
WATCHDOG_PING_INTERVAL_MS  = 200   // How often main thread pings
WATCHDOG_TIMEOUT_MS         = 750   // Timeout before triggering
WATCHDOG_DISARM_DURATION_MS = 60000 // Disarm time for long ops
```

### NVS Storage
```cpp
Namespace: "maslow"
Key:       "wdg_reset"
Value:     1 (triggered) / 0 (normal)
```

## Testing

### Manual Test (Hardware Required)
1. **Normal operation**: Run for extended time, verify no false triggers
2. **Timeout test**: Intentionally hang main thread, verify recovery
3. **Upload test**: Upload large file, verify no timeout during transfer
4. **Boot test**: After watchdog trigger, verify alarm mode and warning message

### Validation Checklist
- [ ] System runs 1+ hour without false triggers
- [ ] Watchdog detects 1-second hang in main thread
- [ ] Motors halt before restart
- [ ] Boot shows warning message after trigger
- [ ] System enters alarm mode after recovery
- [ ] File uploads >1 minute complete successfully

## Files

### Implementation
- `/FluidNC/esp32/maslow_watchdog.h` - Public API
- `/FluidNC/esp32/maslow_watchdog.cpp` - Core implementation

### Integration
- `/FluidNC/src/Protocol.cpp` - Init, ping loop, boot check
- `/FluidNC/src/WebUI/WebServer.cpp` - Upload disarm
- `/FluidNC/src/ProcessSettings.cpp` - Xmodem disarm

## API Reference

### Initialization
```cpp
#include "esp32/maslow_watchdog.h"

// Initialize watchdog (call once at startup)
MaslowWatchdog::init();

// Start watchdog task on Core 0
MaslowWatchdog::start();
```

### Main Thread
```cpp
// Ping watchdog (call every 200ms from main thread)
MaslowWatchdog::ping();
```

### Long Operations
```cpp
// Disarm watchdog for 60 seconds
MaslowWatchdog::disarm();

// ... perform long operation ...

// Watchdog re-arms automatically on next ping
```

### Boot Check
```cpp
// Check if last boot was due to watchdog
if (MaslowWatchdog::was_watchdog_reset()) {
    log_error("WARNING RESTARTING AFTER WATCHDOG TIMEOUT");
    sys.set_state(State::Alarm);
    MaslowWatchdog::clear_watchdog_flag();
}
```

## Performance Impact

| Resource | Usage |
|----------|-------|
| Flash Memory | ~5 KB |
| RAM | ~4 KB (stack + state) |
| CPU (Core 0) | ~0.1% |
| Ping Overhead | <1 μs |

## Troubleshooting

### Frequent False Triggers
- Check for slow I/O operations in main thread
- Review WiFi activity and network conditions
- Increase timeout if environment requires it

### Watchdog Not Triggering
- Verify `MaslowWatchdog::init()` and `start()` called
- Confirm ping loop is running in main thread
- Check watchdog task is active (priority 2, Core 0)

### Recovery Loop
- Check for errors in startup code
- Verify alarm mode can be cleared
- Review system logs for root cause

## See Also
- [Full Technical Analysis](/tmp/WATCHDOG_ANALYSIS.md)
- [ESP32 Task Watchdog Timer (TWDT)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/wdts.html)
- [FreeRTOS Task Management](https://www.freertos.org/taskandcr.html)
