# Numpad Keyboard Integration for Jogging and Dynamic Step Selection

## Overview

The ESP3D WebUI now includes full keyboard mapping for the numeric keypad (Numpad), allowing users to jog the machine, adjust step increments, and manage job execution without relying on a mouse or touch screen. This feature is particularly useful in workshop environments where using a physical numpad (wireless or wired) is more reliable and ergonomic than using a mouse or trackpad.

## Numpad Key Mapping

The layout is designed to follow the physical arrangement of a standard Numpad for intuitive "blind" operation.

| Key | UI / Machine Action | Description |
|-----|---------------------|-------------|
| **8** | **Y+ (Up)** | Move gantry up by current Step value |
| **2** | **Y- (Down)** | Move gantry down by current Step value |
| **4** | **X- (Left)** | Move gantry left by current Step value |
| **6** | **X+ (Right)** | Move gantry right by current Step value |
| **7** | **Diagonal Top-Left** | Combined X- and Y+ movement |
| **9** | **Diagonal Top-Right** | Combined X+ and Y+ movement |
| **1** | **Diagonal Bottom-Left** | Combined X- and Y- movement |
| **3** | **Diagonal Bottom-Right** | Combined X+ and Y- movement |
| **5** | **STOP / ABORT** | Immediate halt of all current movement commands |
| **+ (Plus)** | **Z-Axis Up** | Retract the tool (Z-axis) by current Step value |
| **- (Minus)** | **Z-Axis Down** | Extend the tool (Z-axis) by current Step value |
| **0 (Ins)** | **Step UP** | Cycle step size UP (0.1 → 1 → 10 → 100mm) |
| **. (Del)** | **Step DOWN** | Cycle step size DOWN (100 → 10 → 1 → 0.1mm) |
| **/ (Divide)** | **Home XY** | Execute "Define XY Home" for both X and Y axes |
| **\* (Multiply)** | **Home Z** | Execute "Define Z Home" for Z axis |
| **Enter** | **START / RESUME** | Execute "Cycle Start" (requires 3-second long press) |

## Functional Requirements

### Dynamic Step Cycling

Pressing `0` (Numpad Insert) or `.` (Numpad Delete) updates the step value in the UI in real-time:
- Available step sizes: **0.1mm, 1mm, 10mm, 100mm**
- The UI reflects the change immediately
- Both XY and Z step sizes are synchronized
- Changes are visible in the control panel

### Focus Management

The keyboard listeners are **only active** when:
1. The main dashboard tab is visible
2. No input fields (text boxes, text areas) have focus
3. The UI is not locked (except for the ABORT key which always works)

This prevents accidental jogging while typing in:
- Console commands
- File names
- Configuration fields
- Search boxes

### Numpad-Specific Detection

The handler **only responds to actual numpad keys**, not main keyboard numbers. This prevents conflicts with:
- The tablet.js keyboard handler (which uses arrow keys and +/- from main keyboard)
- Text input fields
- Other keyboard shortcuts

## Security & Safety Protocols

### ABORT Key (5) - Highest Priority

- The **5** key (STOP/ABORT) works **even when the UI is locked**
- Clears the command buffer immediately
- Has the highest priority in the event handler
- Uses the `abort` command to stop all movement

### Enter Key Long-Press Interlock

To prevent catastrophic accidental starts, the **Enter** key requires a **3-second long press**:

1. **Press and hold** the Enter key
2. After 3 seconds, the key becomes "armed"
3. **Release** the key to execute the cycle start

Safety checks before cycle start:
- Machine state must be `IDLE`
- A valid G-Code file must be loaded
- If released before 3 seconds, no action is taken

Console feedback is provided at each stage:
- "Enter key armed - release to start cycle" (after 3 seconds)
- "Enter key released too early" (if released before 3 seconds)
- "Cannot start job: Machine state is 'X', expected 'Idle'" (if not idle)
- "Cannot start job: No G-Code file loaded" (if no file loaded)

## Usage Example

### Basic Jogging Workflow

1. Load the ESP3D WebUI in your browser
2. Navigate to the main dashboard (Controls tab)
3. Ensure no input fields are focused (click on the dashboard background if needed)
4. Use the numpad to jog:
   - `8` to move Y+
   - `2` to move Y-
   - `4` to move X-
   - `6` to move X+
   - `7`, `9`, `1`, `3` for diagonal movements

### Step Size Adjustment

1. Press `0` to increase step size (e.g., 1mm → 10mm)
2. Press `.` to decrease step size (e.g., 10mm → 1mm)
3. Check the control panel to see the current step size
4. Jog with the new step size

### Emergency Stop

- Press `5` at any time to immediately abort all movements
- Works even when UI is locked
- No confirmation required

### Homing

- Press `/` to home both X and Y axes
- Press `*` to home the Z axis

### Starting a Job (with Safety)

1. Load a G-Code file
2. Ensure machine is in IDLE state
3. **Press and hold** the numpad `Enter` key
4. Wait for 3 seconds (you'll see a console message)
5. **Release** the key to start the job
6. If you release early, no action is taken

## Technical Details

### Implementation

- **File**: `www/js/numpadJog.js`
- **Integration**: Loaded in `index.html` after grbl.js
- **Dependencies**: 
  - `controls.js` (SendJogcommand, SendHomecommand)
  - `grblpanel.js` (grblPanelResume for cycle start)
  - `printercmd.js` (SendPrinterCommand, SendRealtimeCmd)
  - `util.js` (id, getChecked helper functions)

### Key Functions

- `executeXYJog(xCmd, yCmd)` - Execute XY axis jog commands
- `executeZJog(direction)` - Execute Z axis jog commands
- `cycleStepSize(up)` - Cycle through step sizes
- `executeAbort()` - Emergency stop function
- `executeHomeXY()` - Home XY axes
- `executeHomeZ()` - Home Z axis
- `executeCycleStart()` - Start/resume with safety checks
- `isNumpadKey(event)` - Detect if key is from numpad
- `isNumpadControlActive()` - Check if controls should be active

### Event Handlers

- `handleNumpadKeyDown(event)` - Main keydown handler
- `handleNumpadKeyUp(event)` - Main keyup handler (for Enter long-press)

## Browser Compatibility

The numpad handler uses standard JavaScript APIs:
- `KeyboardEvent.code` - Supported in all modern browsers
- `KeyboardEvent.location` - Supported in all modern browsers
- `KeyboardEvent.DOM_KEY_LOCATION_NUMPAD` - Standard constant

Tested and working on:
- Chrome/Chromium
- Firefox
- Edge
- Safari

## Troubleshooting

### Numpad not responding

1. **Check NumLock**: Ensure NumLock is ON
2. **Check tab**: Ensure you're on the main dashboard tab
3. **Check focus**: Click on the dashboard background (not in an input field)
4. **Check lock**: Ensure UI is not locked (except for ABORT key)

### Enter key not starting job

1. **Hold for 3 seconds**: The Enter key requires a full 3-second press
2. **Check machine state**: Machine must be in IDLE state
3. **Check file loaded**: A G-Code file must be loaded
4. **Check console**: Look for error messages in the browser console

### Numbers not working

- Ensure you're using the **numpad keys**, not the main keyboard numbers
- The handler specifically filters for numpad-origin keys to avoid conflicts

### Step size not changing

- Verify you're pressing `0` or `.` on the numpad
- Check the control panel display to confirm step size
- The step cycles through: 0.1, 1, 10, 100 (mm)

## Future Enhancements

Possible future improvements:
- Visual feedback on Enter key arming (on-screen indicator)
- Customizable step sizes via preferences
- Configurable long-press duration
- Sound feedback on key press
- On-screen numpad overlay for reference

## Credits

Developed for the Maslow CNC project as an enhancement to the ESP3D-WEBUI interface.
