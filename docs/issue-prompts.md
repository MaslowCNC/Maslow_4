# GitHub Issue Prompts for G-code UI Fixes

This document contains ready-to-use prompts for creating GitHub issues to fix the G-code UI rendering problems documented in the comprehensive comparison report.

---

## Issue 1: Fix G2 Clockwise Arc Start/End Point Swap

### Title
```
Fix G2 arc rendering - UI swaps start/end points causing backwards arcs
```

### Description
```
## Problem

The UI swaps start and end points for G2 (clockwise) arcs before rendering, causing arcs to be drawn **backwards** compared to how the firmware executes them.

**Location**: `ESP3D-WEBUI/www/js/toolpath-displayer.js:959-963`

**Current Code**:
```javascript
if (modal.motion == 'G2') {  // clockwise
    var tmp = start;
    start = end;
    end = tmp;
}
```

## Impact

G2 arcs render from end-to-start instead of start-to-end. The toolpath preview shows arcs in the **opposite direction** from actual cutting.

**Example**:
```gcode
G0 X0 Y0
G2 X10 Y0 I5 J0  ; Quarter circle CW from (0,0) to (10,0), center at (5,0)
```

- **Expected**: Arc from (0,0) to (10,0), curving through Y-positive
- **Current UI**: Arc from (10,0) to (0,0) - BACKWARDS!
- **Firmware**: Correctly cuts from (0,0) to (10,0)

## Root Cause

The UI attempts to handle arc direction by swapping endpoints for G2, while the firmware correctly handles direction by adjusting the angular travel calculation without swapping points.

## Fix Required

**Remove the start/end swap** in `toolpath-displayer.js:959-963` and handle arc direction through proper angular travel calculation, matching the firmware implementation.

The firmware approach (from `MotionControl.cpp:141-160`):
```cpp
// CCW angle between position and target from circle center
float angular_travel = atan2f(r_axis0 * rt_axis1 - r_axis1 * rt_axis0, 
                               r_axis0 * rt_axis0 + r_axis1 * rt_axis1);
if (is_clockwise_arc) {
    if (angular_travel >= -ARC_ANGULAR_TRAVEL_EPSILON) {
        angular_travel -= 2 * float(M_PI);
    }
}
```

## Test Case

```gcode
; Test G2 arc direction
G21  ; Millimeters
G0 X0 Y0
G2 X10 Y0 I5 J0  ; Quarter circle CW

; Expected: Arc from (0,0) to (10,0), curving through Y-positive
; Current UI: Shows backwards arc
; After fix: Should match firmware execution
```

## References

- Comprehensive comparison report: `docs/gcode-ui-firmware-comparison-comprehensive.md`
- Detailed arc analysis: `docs/gcode-ui-firmware-comparison.md`
- Issue #1 in comprehensive report
```

---

## Issue 2: Fix R-mode Sign Convention for Arc Center Calculation

### Title
```
Fix R-mode arc rendering - UI and firmware use opposite sign conventions
```

### Description
```
## Problem

The UI and firmware use **opposite sign conventions** when calculating arc center from radius (R parameter), causing arcs to appear on the wrong side of the chord line.

**Location**: `ESP3D-WEBUI/www/js/simple-toolpath.js:255`

**Current UI Code**:
```javascript
let height = Math.sqrt(4 * radius * radius - x * x - y * y) / 2;  // POSITIVE
if (isClockwise) {
    height = -height;  // Negate for clockwise
}
```

**Firmware Code** (`GCode.cpp:1212`):
```cpp
h_x2_div_d = -sqrt(h_x2_div_d) / hypot_f(x, y);  // NEGATIVE from start
if (gc_block.modal.motion == Motion::CcwArc) {
    h_x2_div_d = -h_x2_div_d;  // Negate for counter-clockwise
}
```

## Impact

Arcs specified with R parameter are drawn on the **opposite side** of the chord line in UI versus firmware. An arc that should bulge left will bulge right, and vice versa.

**Affects both G2 and G3 arcs when using R parameter.**

**Example**:
```gcode
G0 X0 Y0
G2 X10 Y0 R5  ; Small semicircle (R positive = arc < 180°)
```

- **Expected**: Semicircle below the line (Y-negative)
- **Current UI**: Semicircle above the line (Y-positive) - WRONG SIDE!
- **Firmware**: Correctly cuts below the line

## Root Cause

The UI starts with a positive sqrt and negates for clockwise arcs. The firmware starts with a negative sqrt and negates for counter-clockwise arcs. This opposite convention causes the arc to be mirrored across the chord line.

## Fix Required

Change UI to match firmware sign convention. Either:
1. Start with negative sqrt: `let height = -Math.sqrt(4 * radius * radius - x * x - y * y) / 2;`
2. Adjust the clockwise/counter-clockwise negation logic to match firmware

The calculation should produce the same center point as the firmware for all R-mode arcs.

## Test Cases

```gcode
; Test R-mode with positive radius (small arc)
G21
G0 X0 Y0
G2 X10 Y0 R5  ; Small semicircle (< 180°)
; Expected: Curve in Y-negative direction
; Current UI: Shows in Y-positive - WRONG!

; Test R-mode with negative radius (large arc)
G0 X0 Y0
G2 X10 Y0 R-5  ; Large arc (> 180°)
; Expected: Curve in Y-positive direction
; Current UI: Shows in Y-negative - WRONG!

; Test G3 R-mode
G0 X0 Y0
G3 X10 Y0 R5
; Expected: Curve in Y-positive direction
; Current UI: Shows in Y-negative - WRONG!
```

## References

- Comprehensive comparison report: `docs/gcode-ui-firmware-comparison-comprehensive.md`
- Detailed arc analysis: `docs/gcode-ui-firmware-comparison.md`
- Issue #2 in comprehensive report
- LinuxCNC G-code Arc documentation: https://linuxcnc.org/docs/html/gcode.html
```

---

## Issue 3: Implement G53 Machine Coordinate Handler

### Title
```
Implement G53 handler for machine coordinate moves in UI
```

### Description
```
## Problem

The UI has no handler for G53 (move in machine coordinates), so machine coordinate moves are displayed as if they were in work coordinates, showing **incorrect toolpath**.

**Location**: `ESP3D-WEBUI/www/js/simple-toolpath.js` - No G53 handler exists

## Impact

When the firmware encounters G53, it interprets the next move in machine coordinates (ignoring work coordinate system offsets). The UI displays these moves as if they were in the current work coordinate system, resulting in a **completely wrong path** being shown.

**Example**:
```gcode
G54        ; Select WCS1 (assume offset is X10 Y20)
G0 X0 Y0   ; Move to WCS origin -> Machine position (10, 20)
G53 G0 X0 Y0  ; Move to machine origin -> Machine position (0, 0)
```

- **UI Display**: Shows move to (0, 0) in current WCS, appears as machine (10, 20)
- **Firmware Execution**: Moves to true machine (0, 0)
- **Result**: UI shows completely WRONG path!

## Fix Required

Implement a G53 handler in `simple-toolpath.js` that:
1. Recognizes G53 command
2. Sets a flag indicating next move should ignore WCS and G92 offsets
3. Interprets the subsequent axis commands in machine coordinates only
4. Resets the flag after the move

**Implementation approach**:
```javascript
'G53': function G53(params) {
    // Set flag for next move to use machine coordinates
    _this.machineCoordinateMode = true;
}
```

Then modify the coordinate translation to check this flag and skip WCS/G92 offsets when active.

## Test Case

```gcode
; Setup WCS with offset
G54
G10 L2 P1 X100 Y100  ; Set G54 origin at machine (100, 100)
G0 X0 Y0    ; Move to WCS origin -> machine (100, 100)

; Test G53 machine coordinate move
G53 G0 X50 Y50  ; Should move to machine (50, 50)

G0 X0 Y0    ; Back to WCS origin -> machine (100, 100)
```

**Expected UI behavior after fix**:
- First G0: Display at work (0,0) / machine (100, 100)
- G53 G0: Display at machine (50, 50)
- Second G0: Display at work (0,0) / machine (100, 100)

## References

- Comprehensive comparison report: `docs/gcode-ui-firmware-comparison-comprehensive.md`
- Issue #3 in comprehensive report
- Firmware implementation: `firmware/FluidNC/src/GCode.cpp:277-280`
```

---

## Issue 4: Implement G10 Coordinate System Handler

### Title
```
Implement G10 L2/L20 handler for coordinate system changes in UI
```

### Description
```
## Problem

The UI has no handler for G10 L2/L20 commands that change work coordinate system offsets. These changes are invisible in the UI, causing subsequent moves to appear in wrong locations.

**Location**: `ESP3D-WEBUI/www/js/simple-toolpath.js` - No G10 handler exists (line 373 has empty function)

## Impact

G10 L2 and G10 L20 commands modify the work coordinate system origins. When the UI doesn't process these commands, it continues using the old WCS offsets, making all subsequent toolpath display incorrect.

**Example**:
```gcode
G54
G0 X0 Y0             ; Move to current G54 origin
G10 L2 P1 X5 Y10     ; Change G54 origin to machine (5, 10)
G0 X0 Y0             ; Move to NEW G54 origin
```

- **UI Display**: Shows second move at old G54 origin (wrong!)
- **Firmware Execution**: Moves to new origin at machine (5, 10)
- **Result**: All subsequent moves display at wrong positions

## G10 Variants

**G10 L2 Pn Xnn Ynn Znn**: Set coordinate system Pn origin to specified machine coordinates
- P1 = G54, P2 = G55, ..., P6 = G59

**G10 L20 Pn Xnn Ynn Znn**: Set coordinate system Pn so that current position becomes the specified coordinates

## Fix Required

Implement a G10 handler in `simple-toolpath.js` that:
1. Parses L and P parameters
2. For L2: Updates the WCS origin to the specified machine coordinates
3. For L20: Calculates new WCS origin based on current position and specified coordinates
4. Stores WCS offsets for G54-G59 (similar to how G92 offsets are stored)

**Implementation approach**:
```javascript
'G10': function G10(params) {
    if (params.L === 2) {
        // G10 L2 Pn - Set WCS origin to absolute machine coords
        var wcs_index = params.P || 1;  // P1-P6 for G54-G59
        // Update stored WCS offset for this coordinate system
    } else if (params.L === 20) {
        // G10 L20 Pn - Set WCS so current position = specified value
        var wcs_index = params.P || 1;
        // Calculate WCS offset based on current position
    }
}
```

## Test Cases

```gcode
; Test G10 L2 - Absolute WCS setting
G54
G0 X0 Y0
G10 L2 P1 X100 Y200  ; Set G54 origin at machine (100, 200)
G0 X0 Y0             ; Should display at machine (100, 200)

; Test G10 L20 - Set WCS from current position
G55
G0 X50 Y50           ; Move somewhere
G10 L20 P2 X0 Y0     ; Make current position the G55 origin
G0 X10 Y10           ; Should be relative to new origin
```

## References

- Comprehensive comparison report: `docs/gcode-ui-firmware-comparison-comprehensive.md`
- Issue #4 in comprehensive report
- Firmware implementation: `firmware/FluidNC/src/GCode.cpp:233-242`
- LinuxCNC G10 documentation: https://linuxcnc.org/docs/html/gcode.html
```

---

## Issue 5: Implement G28/G28.1/G30/G30.1 Homing Handlers

### Title
```
Implement G28/G30 homing move handlers in UI
```

### Description
```
## Problem

The UI has no handlers for G28/G28.1/G30/G30.1 homing commands. These commands are invisible in the toolpath preview.

**Location**: `ESP3D-WEBUI/www/js/simple-toolpath.js` - No G28/G30 handlers exist

## Impact

G28 and G30 moves to predefined home positions are not displayed in the UI preview, making the toolpath incomplete.

## Command Descriptions

**G28**: Move to predefined position 0 (home position)
- Moves through current position to stored position
- May move through intermediate point if axis words specified

**G28.1**: Store current position as position 0

**G30**: Move to predefined position 1 (alternate home)
- Same behavior as G28 but uses different stored position

**G30.1**: Store current position as position 1

**Example**:
```gcode
G0 X10 Y10
G28.1            ; Store current (10, 10) as home position
G0 X50 Y50
G28              ; Move to stored home (10, 10)
```

- **UI Display**: G28 move is invisible
- **Firmware**: Moves to (10, 10)
- **Result**: UI toolpath is incomplete

## Fix Required

Implement handlers for G28/G28.1/G30/G30.1 in `simple-toolpath.js`:

1. **G28.1 and G30.1**: Store current position
```javascript
'G28.1': function G281(params) {
    _this.homePosition0 = {
        x: _this.position.x,
        y: _this.position.y,
        z: _this.position.z
    };
}
```

2. **G28 and G30**: Move to stored position
```javascript
'G28': function G28(params) {
    // If axis words present, move through intermediate point first
    // Then move to stored home position
    if (_this.homePosition0) {
        // Display move to home position
    }
}
```

## Complexity Note

G28/G30 with axis words involves a two-stage move:
1. First to the position specified by axis words
2. Then to the stored home position

This intermediate move needs to be visualized correctly.

## Test Cases

```gcode
; Test basic G28
G0 X10 Y10 Z5
G28.1              ; Store as home
G0 X50 Y50 Z10
G28                ; Return to home (10, 10, 5)

; Test G28 with intermediate point
G0 X0 Y0 Z0
G28 Z5             ; Move to Z5, then to stored home

; Test G30 (alternate home)
G0 X20 Y20 Z0
G30.1              ; Store as alternate home
G0 X100 Y100
G30                ; Move to alternate home
```

## References

- Comprehensive comparison report: `docs/gcode-ui-firmware-comparison-comprehensive.md`
- Firmware implementation: `firmware/FluidNC/src/GCode.cpp:244-249`
- LinuxCNC G28/G30 documentation: https://linuxcnc.org/docs/html/gcode.html
```

---

## Issue 6: Implement G43.1/G49 Tool Length Offset Display

### Title
```
Apply tool length offset (G43.1/G49) to Z coordinates in UI display
```

### Description
```
## Problem

The UI tracks G43.1 (tool length offset) and G49 (cancel TLO) modal state but does not apply the offset to the displayed Z coordinates. This causes the preview to show the tool at the wrong Z height.

**Location**: `ESP3D-WEBUI/www/js/simple-toolpath.js:430-441`

**Current Code**:
```javascript
'G43.1': function G431(params) {
    if (_this.modal.tlo !== 'G43.1') {
        _this.setModal({ tlo: 'G43.1' });
    }
    // Missing: Apply offset to Z coordinates
}
```

## Impact

Tool length offsets affect actual cutting depth but aren't shown in UI preview. The toolpath displays at incorrect Z heights after G43.1 is applied.

**Example**:
```gcode
G0 Z10
G43.1 Z-5      ; Set tool length offset of -5mm
G0 Z10         ; Actual Z position is 10-5=5mm
```

- **UI Display**: Shows Z=10 (wrong!)
- **Firmware**: Tool is at Z=5
- **Result**: Preview shows tool 5mm higher than reality

## Fix Required

Implement tool length offset tracking and application:

1. Store TLO value when G43.1 is called
```javascript
'G43.1': function G431(params) {
    _this.setModal({ tlo: 'G43.1' });
    // G43.1 uses axis word (typically Z) as the offset
    if (params.Z !== undefined) {
        _this.toolLengthOffset = _this.translateZ(params.Z, false);
    }
}
```

2. Apply offset to Z coordinate display
```javascript
// In coordinate translation functions
function getDisplayZ(z) {
    if (_this.modal.tlo === 'G43.1') {
        return z + _this.toolLengthOffset;
    }
    return z;
}
```

3. Cancel offset with G49
```javascript
'G49': function G49() {
    _this.setModal({ tlo: 'G49' });
    _this.toolLengthOffset = 0;
}
```

## Note on G92 Interaction

Tool length offset is separate from G92 offset. The display should apply:
- Work coordinate offset (G54-G59)
- G92 offset
- Tool length offset

All three may be active simultaneously.

## Test Cases

```gcode
; Test basic TLO
G0 Z10
G43.1 Z-5      ; Offset by -5mm
G0 Z10         ; Should display at effective Z=5

; Test TLO cancellation
G49            ; Cancel offset
G0 Z10         ; Should display at Z=10

; Test TLO with G92
G92 Z0         ; Zero Z at current position
G43.1 Z-5      ; Apply TLO
G0 Z5          ; Should consider both offsets
```

## References

- Comprehensive comparison report: `docs/gcode-ui-firmware-comparison-comprehensive.md`
- Issue #6 in comprehensive report
- Firmware implementation: `firmware/FluidNC/src/GCode.cpp:415-434`
```

---

## Summary

Six issue prompts have been created for:
1. **G2 arc start/end swap** (Critical)
2. **R-mode sign convention** (Critical)
3. **G53 machine coordinates** (High priority)
4. **G10 coordinate system changes** (Medium priority)
5. **G28/G30 homing moves** (Medium priority)
6. **G43.1/G49 tool length offset** (Low priority)

Each prompt includes:
- Problem description with code locations
- Impact explanation with examples
- Root cause analysis
- Fix requirements with implementation hints
- Test cases
- References to documentation

Copy and paste each prompt into a new GitHub issue as needed.
