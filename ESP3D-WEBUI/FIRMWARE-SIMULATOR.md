# Firmware Simulator for Maslow State Testing

## Overview

The firmware simulator (`firmware-simulator.py`) enables testing of the ESP3D-WEBUI interface with different Maslow calibration states without requiring physical hardware. This is essential for UI development and verification.

## Features

- **Simulates all 10 Maslow calibration states** (0-9)
- **WebSocket interface** on `ws://localhost:8081` with persistent connections
- **HTTP server** on `http://localhost:8080` serving the ESP3D interface
- **Periodic state updates** sent to connected clients every 2 seconds
- **Compatible with ESP3D-WEBUI** message formats

## Maslow States

The simulator supports all 10 calibration states defined in `firmware/FluidNC/src/Maslow/MaslowEnums.h`:

| State | Name | Description |
|-------|------|-------------|
| 0 | UNKNOWN | Initial state, machine status unknown |
| 1 | RETRACTING | Belts retracting |
| 2 | RETRACTED | Belts fully retracted |
| 3 | EXTENDING | Belts extending |
| 4 | EXTENDED | Belts fully extended |
| 5 | TAKING_SLACK | Removing slack from belts |
| 6 | CALIBRATION_IN_PROGRESS | Calibration routine running |
| 7 | READY_TO_CUT | Machine calibrated and ready |
| 8 | RELEASE_TENSION | Releasing belt tension |
| 9 | CALIBRATION_COMPUTING | Computing calibration results |

## Usage

### Starting the Simulator

```bash
cd ESP3D-WEBUI

# Start simulator in specific state (0-9)
python3 firmware-simulator.py 0

# Example: Start in READY_TO_CUT state
python3 firmware-simulator.py 7
```

### Accessing the Interface

1. Open browser to `http://localhost:8080`
2. Click on the "Maslow" tab to see state-specific UI
3. The interface will display the current state and available actions

### Stopping the Simulator

Press `Ctrl+C` or send SIGTERM to the process.

## Technical Details

### WebSocket Messages

The simulator sends two types of state messages that the ESP3D-WEBUI recognizes:

1. **Text format**: `[MSG:INFO: Current state: N]`
2. **JSON format**: `MINFO: {"state": N, "homed": true, "extended": boolean}`

These messages are sent:
- Once when a client connects
- Every 2 seconds to all connected clients
- In response to client messages (echo)

### HTTP Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Serves `dist/index.html` |
| `/command?plain=[ESP800]` | GET | Returns firmware identification |
| `/command?plain=[ESP400]` | GET | Returns settings configuration |
| `/files` | GET | Returns file list (empty in simulator) |
| `/upload` | GET | Returns upload status (not implemented) |

### ESP800 Response Format

The simulator returns a FluidNC-compatible identification string:

```
FW version: FluidNC v3.6.7 (Simulator) # FW target:grbl-embedded  # FW HW:Direct SD  # primary sd:/sd # secondary sd:none  # authentication:no # webcommunication: Sync: 8081:localhost # hostname:maslow # axis:3
```

## Generating Screenshots

### Automated Screenshot Generation (Recommended)

Use the provided automation tools with playwright/browser:

```python
# Example: Generate screenshot for state 0
1. Start simulator: python3 firmware-simulator.py 0
2. Wait 4 seconds for startup
3. Navigate browser to http://localhost:8080
4. Wait 5 seconds for interface to load
5. Click on "Maslow" tab
6. Wait 2 seconds for state to update
7. Take screenshot
8. Stop simulator
```

### Manual Screenshot Generation

Use `generate-state-screenshots.py` for interactive screenshot capture:

```bash
python3 generate-state-screenshots.py
```

This script will:
1. Start the simulator for each state
2. Wait for you to open the browser and capture a screenshot
3. Prompt you to press Enter to move to the next state
4. Continue through all 10 states

## Known Limitations

1. **WebSocket Connection**: The WebSocket requires the `async for` loop to stay open. Clients must send periodic messages or the connection will be treated as inactive by some browsers.

2. **File Operations**: File upload, download, and filesystem operations are not implemented. The simulator returns empty file lists.

3. **GCode Execution**: The simulator does not execute GCode commands or simulate machine motion.

4. **Multiple States**: Each simulator instance runs in a single state. To test transitions, you need to restart the simulator with a different state number.

5. **Maslow Tab Display**: Due to the way the ESP3D-WEBUI handles tab switching, the Maslow-specific UI may not immediately display. The state information is still being received and processed.

## Future Enhancements

- [ ] Add state transition simulation (automatic cycling through states)
- [ ] Implement basic file system simulation for testing uploads
- [ ] Add command-line option to cycle through states automatically
- [ ] Support for multiple simultaneous state simulations on different ports
- [ ] Add position and telemetry data simulation
- [ ] Create automated screenshot generator that captures all 10 states

## Example Output

When the simulator starts, you'll see:

```
Starting simulator in state 0: UNKNOWN

=== Maslow Firmware Simulator ===
State: 0 - UNKNOWN
HTTP Server: http://localhost:8080
WebSocket: ws://localhost:8081

Open http://localhost:8080 in your browser
Press Ctrl+C to stop

Starting HTTP server on http://localhost:8080
 * Running on http://localhost:8080
WebSocket server started on ws://localhost:8081
WebSocket client connected. Total connections: 1
```

## See Also

- `firmware/FluidNC/src/Maslow/MaslowEnums.h` - State definitions
- `ESP3D-WEBUI/www/js/maslow.js` - UI state handling
- `ESP3D-WEBUI/COMPILATION.md` - Building the WebUI
- `ESP3D-WEBUI/HOWTO-Test-Locally.md` - Local testing guide
