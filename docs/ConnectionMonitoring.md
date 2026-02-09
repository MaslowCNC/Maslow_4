# Connection Monitoring System

## Overview

The Maslow CNC firmware now includes an active connection monitoring system that validates bidirectional communication between the browser and the machine in real-time.

## How It Works

### Architecture

The connection monitoring system consists of two parts:

1. **Firmware (ESP32)**: Echoes back any command that starts with `ECHO:` followed by a number
2. **Browser (JavaScript)**: Sends random numbers every 250ms and monitors responses

### Communication Flow

```
Browser                           ESP32 Firmware
  |                                     |
  |------ ECHO:123456 ----------------->|
  |                                     |
  |                       Process ECHO command
  |                                     |
  |<----- ECHO:123456 -------------------|
  |                                     |
  Calculate latency & update stats
```

### Browser Implementation

The browser-side connection monitor (`connectionmonitor.js`) performs the following:

1. **Sends Pings**: Every 250ms, generates a random number (1-999999) and sends `ECHO:<number>` to the firmware
2. **Tracks Responses**: Monitors incoming messages for `ECHO:` responses
3. **Calculates Metrics**:
   - Round-trip latency (time between send and receive)
   - Packet loss percentage (pings sent vs received)
   - Foreign pings (numbers received that weren't sent by this tab)
4. **Updates UI**: Shows connection status with color-coded indicator

### Firmware Implementation

The firmware (`ProcessSettings.cpp`) handles ECHO commands:

```cpp
// In execute_line() function
if (strncmp(line, "ECHO:", 5) == 0) {
    channel.print(line);
    channel.print('\n');
    return Error::Ok;
}
```

This simply echoes the entire ECHO command back to the sender.

## UI Indicator

The connection status indicator is located directly below the "Setup" button on the Maslow tablet interface. It displays:

### Status Colors

- **Green**: Good connection (< 5% packet loss, < 500ms latency)
- **Orange**: Degraded connection (5-20% packet loss or 500-1000ms latency)
- **Dark Orange**: Poor connection (20-50% packet loss)
- **Red**: Connection lost (> 50% packet loss)

### Display Format

The indicator shows:
- **Static text**: "Connection Monitoring" (always the same)
- **Background color**: Changes based on connection status
- **Warning icon**: "!" appears when issues are detected
- **Hover tooltip**: Shows detailed metrics on mouse hover

### Status Messages (in Tooltip)

- `Connection Status: Good` with latency and packet loss details - Normal operation
- `Connection Status: Degraded` with detailed stats - Some issues detected
- `Connection Status: Poor` with warning message - Significant packet loss
- `Connection Status: LOST` with alert - Communication failure
- `WARNING: Multiple tabs detected!` - Another browser tab is connected

### Tooltip Content

When hovering over the indicator, users see:
- Connection status (Good/Degraded/Poor/Lost)
- Latency in milliseconds (when available)
- Packet loss percentage
- Pings sent, received, and lost counts
- Helpful messages for problem states

## Benefits

1. **Real-time Monitoring**: Continuous validation of bidirectional communication
2. **Early Warning**: Detects connection degradation before commands fail
3. **Multi-tab Detection**: Warns users when multiple tabs are controlling the machine
4. **Diagnostics**: Provides latency and packet loss metrics for troubleshooting
5. **Lightweight**: Minimal overhead (one 10-byte command every 250ms)

## Technical Details

### Configuration

The connection monitor can be configured by modifying `connectionmonitor.js`:

```javascript
connectionMonitor = {
    pingInterval: 250,         // ms between pings
    maxPingHistory: 50,        // keep last N pings for statistics
    timeoutThreshold: 2000,    // ms before considering a ping lost
    // ...
}
```

### Statistics Tracked

- `totalSent`: Total number of pings sent
- `totalReceived`: Total number of pings received
- `totalLost`: Total number of pings that timed out
- `totalForeign`: Total number of pings from other tabs
- `averageLatency`: Moving average of round-trip time
- `latencies[]`: Array of recent latency measurements

### Automatic Startup

The connection monitor automatically starts when:
- The WebSocket connection is established (`ws_source.onopen`)
- The user navigates to the web interface

It automatically stops when:
- The WebSocket connection is closed (`ws_source.onclose`)

## Files Modified

### Firmware
- `firmware/FluidNC/src/ProcessSettings.cpp`: Added ECHO command handler

### Web UI
- `ESP3D-WEBUI/www/js/connectionmonitor.js`: New connection monitoring module
- `ESP3D-WEBUI/www/js/socket.js`: Integrated ECHO message handling and auto-start
- `ESP3D-WEBUI/www/index.html`: Added connectionmonitor.js script import
- `ESP3D-WEBUI/www/sub/tablettab.html`: Added connection status indicator UI

## Future Enhancements

Possible future improvements:

1. User preference to enable/disable monitoring
2. Configurable ping interval
3. Historical graphs of connection quality
4. Alerts for sustained connection issues
5. Integration with error reporting system
