# Comprehensive G-code UI vs Firmware Comparison Report

## Executive Summary

This report provides a comprehensive comparison of ALL G-code and M-code commands supported by the FluidNC firmware versus the ESP3D-WEBUI (user interface) in the Maslow CNC system.

**Critical Note**: Analysis of current code reveals that G2/G3 arc handling issues identified in the original report are **STILL PRESENT**. PR #592 mentioned in issue discussions has **NOT been merged** into the current codebase.

**Overview of Findings**:
- **Total Firmware G-codes analyzed**: 30+ commands
- **Total Firmware M-codes analyzed**: 15+ commands  
- **UI implements**: Most display-relevant commands
- **UI does NOT implement**: Many firmware-specific commands (G10, G28, G30, G53, G61, M56, M62-M68)
- **CRITICAL**: G2/G3 arc rendering has multiple incompatibilities with firmware (see Critical Issues)

## Command Support Matrix

### Legend
- ✅ **Fully Compatible**: Command works identically in both systems
- ⚠️ **Partial/Different**: Command exists but behaves differently
- ❌ **Not Implemented**: UI does not handle this command
- 🔧 **Firmware Only**: Command is firmware-specific (no UI equivalent needed)

## G-Code Commands

### Motion Commands (Modal Group 1)

#### G0 - Rapid Linear Move
**Status**: ✅ Fully Compatible

**UI Implementation** (`simple-toolpath.js:111-145`):
```javascript
'G0': function G0(params) {
    if (_this.modal.motion !== 'G0') {
        _this.setModal({ motion: 'G0' });
    }
    // Handles X, Y, Z parameters
    // Creates line segment for display
}
```

**Firmware Implementation** (`GCode.cpp:283-287`):
```cpp
case 0:  // G0 - linear rapid traverse
    axis_command          = AxisCommand::MotionMode;
    gc_block.modal.motion = Motion::Seek;
    mg_word_bit           = ModalGroup::MG1;
```

**Behavior**: Both interpret G0 as rapid positioning. UI draws the path, firmware executes at maximum speed.

---

#### G1 - Linear Feed Move
**Status**: ✅ Fully Compatible

**UI Implementation** (`simple-toolpath.js:147-185`):
```javascript
'G1': function G1(params) {
    if (_this.modal.motion !== 'G1') {
        _this.setModal({ motion: 'G1' });
    }
    // Handles X, Y, Z, F parameters
    // Creates line segment with feed rate
}
```

**Firmware Implementation** (`GCode.cpp:288-292`):
```cpp
case 1:  // G1 - linear feedrate move
    axis_command          = AxisCommand::MotionMode;
    gc_block.modal.motion = Motion::Linear;
    mg_word_bit           = ModalGroup::MG1;
```

**Behavior**: Both handle coordinated linear motion with feed rate control. UI displays path, firmware executes motion.

---

#### G2 - Clockwise Arc
**Status**: ❌ **INCOMPATIBLE - Critical Issues Present**

**UI Implementation** (`simple-toolpath.js:187-275`):
```javascript
'G2': function G2(params) {
    // Calculates arc center from I, J, K or R
    // Handles P parameter for multi-turn arcs
    // Renders arc on canvas
}
```

**Firmware Implementation** (`GCode.cpp:293-297`, `MotionControl.cpp:110-253`):
```cpp
case 2:  // G2 - clockwise arc
    axis_command          = AxisCommand::MotionMode;
    gc_block.modal.motion = Motion::CwArc;
```

**Behavior**: **CRITICAL INCOMPATIBILITIES EXIST**:

1. **Start/End Point Swap** (Issue #1): UI swaps start and end points for G2 arcs in `toolpath-displayer.js:959-963`:
   ```javascript
   if (modal.motion == 'G2') {  // clockwise
       var tmp = start;
       start = end;
       end = tmp;
   }
   ```
   Firmware does NOT swap points. **Result**: G2 arcs drawn backwards in UI vs firmware execution.

2. **R-mode Sign Convention** (Issue #2): UI uses positive sqrt at `simple-toolpath.js:255`:
   ```javascript
   let height = Math.sqrt(4 * radius * radius - x * x - y * y) / 2;
   ```
   Firmware uses negative sqrt. **Result**: Arcs on opposite side of chord line in UI vs firmware.

3. **Direction Handling**: UI treats G2 as CCW by swapping endpoints; firmware adjusts angular_travel calculation.

These issues cause **incorrect arc visualization** in the UI compared to actual firmware cutting paths.

---

#### G3 - Counter-Clockwise Arc
**Status**: ⚠️ **Partial Compatibility - Minor Issues**

**UI Implementation** (`simple-toolpath.js:276-365`):
```javascript
'G3': function G3(params) {
    // Same as G2 but counter-clockwise
}
```

**Firmware Implementation** (`GCode.cpp:298-302`):
```cpp
case 3:  // G3 - counterclockwise arc
    axis_command          = AxisCommand::MotionMode;
    gc_block.modal.motion = Motion::CcwArc;
```

**Behavior**: G3 (CCW) has better compatibility than G2, but still affected by:

1. **R-mode Sign Convention**: Same issue as G2 - UI uses positive sqrt, firmware uses negative sqrt. May cause arcs on wrong side of chord.

2. **Segmentation Strategy**: UI uses fixed 10 segments per π; firmware uses adaptive segmentation based on arc tolerance.

G3 does NOT have the start/end swap issue that affects G2, so simple CCW arcs display more accurately.

---

#### G38.2 - Probe Toward (with error)
**Status**: ⚠️ Partial Implementation

**UI Implementation** (`simple-toolpath.js:407-412`):
```javascript
'G38.2': function G382(params) {
    if (_this.modal.motion !== 'G38.2') {
        _this.setModal({ motion: 'G38.2' });
    }
    // Only sets modal state, no actual probing display
}
```

**Firmware Implementation** (`GCode.cpp:303-342`):
```cpp
case 38:  // G38 - probe
    if (!config->_probe->exists()) {
        FAIL(Error::GcodeUnsupportedCommand);
    }
    // Full probe cycle with hardware interaction
```

**Differences**:
- **UI**: Only tracks modal state for display purposes
- **Firmware**: Performs actual probe cycle with hardware
- **Impact**: UI cannot visualize probe paths accurately

---

#### G38.3 - Probe Toward (no error)
**Status**: ⚠️ Partial Implementation

**UI**: Modal state tracking only (`simple-toolpath.js:413-418`)  
**Firmware**: Full probe support with no-error mode (`GCode.cpp:327-329`)

Same limitations as G38.2.

---

#### G38.4 - Probe Away (with error)
**Status**: ⚠️ Partial Implementation

**UI**: Modal state tracking only  
**Firmware**: Full probe support  

---

#### G38.5 - Probe Away (no error)
**Status**: ⚠️ Partial Implementation

**UI**: Modal state tracking only  
**Firmware**: Full probe support  

---

#### G80 - Cancel Canned Cycle
**Status**: ✅ Fully Compatible

**UI Implementation** (`simple-toolpath.js:473-478`):
```javascript
'G80': function G80() {
    if (_this.modal.motion !== 'G80') {
        _this.setModal({ motion: 'G80' });
    }
}
```

**Firmware Implementation** (`GCode.cpp:344-347`):
```cpp
case 80:  // G80 - cancel canned cycle
    gc_block.modal.motion = Motion::None;
    mg_word_bit           = ModalGroup::MG1;
```

**Behavior**: Both cancel motion mode. UI stops expecting motion commands, firmware clears modal motion state.

---

### Non-Modal Commands (Modal Group 0)

#### G4 - Dwell
**Status**: ⚠️ Different Implementations

**UI Implementation** (`simple-toolpath.js:371`):
```javascript
'G4': function G4(params) {},  // Empty function - ignored
```

**Firmware Implementation** (`GCode.cpp:273-276`):
```cpp
case 4:
    gc_block.non_modal_command = NonModal::Dwell;
```

**Differences**:
- **UI**: Completely ignores dwell commands (no visual representation)
- **Firmware**: Executes timed pause using P parameter (milliseconds)
- **Impact**: Dwells are not shown in UI preview

---

#### G10 - Set Coordinate System Data
**Status**: ❌ Not Implemented in UI

**UI**: No handler defined  
**Firmware Implementation** (`GCode.cpp:233-242`):
```cpp
case 10:
    gc_block.non_modal_command = NonModal::SetCoordinateData;
    // Updates coordinate system offsets
```

**Firmware Behavior**:
- G10 L2: Set coordinate system origin
- G10 L20: Set coordinate system at current position

**Impact**: UI cannot visualize coordinate system changes from G10 commands.

---

#### G28 - Go to Predefined Position 0
**Status**: ❌ Not Implemented in UI

**UI**: No handler defined  
**Firmware Implementation** (`GCode.cpp:244-246`):
```cpp
case 28:
    gc_block.non_modal_command = mantissa ? NonModal::SetHome0 : NonModal::GoHome0;
```

**Firmware Behavior**:
- G28: Move to stored position 0 (home)
- G28.1: Store current position as position 0

**Impact**: UI cannot show G28 homing moves in toolpath preview.

---

#### G30 - Go to Predefined Position 1
**Status**: ❌ Not Implemented in UI

**UI**: No handler defined  
**Firmware Implementation** (`GCode.cpp:247-249`):
```cpp
case 30:
    gc_block.non_modal_command = mantissa ? NonModal::SetHome1 : NonModal::GoHome1;
```

Same as G28 but for alternate home position.

---

#### G53 - Move in Machine Coordinates
**Status**: ❌ Not Implemented in UI

**UI**: No handler defined  
**Firmware Implementation** (`GCode.cpp:277-280`):
```cpp
case 53:
    gc_block.non_modal_command = NonModal::AbsoluteOverride;
```

**Firmware Behavior**: Next move ignores work coordinate system offsets and moves in machine coordinates.

**Impact**: UI displays G53 moves as if they were in work coordinates, which is INCORRECT.

---

### Plane Selection (Modal Group 2)

#### G17 - XY Plane
**Status**: ✅ Fully Compatible

**UI Implementation** (`simple-toolpath.js:376-380`):
```javascript
'G17': function G17(params) {
    if (_this.modal.plane !== 'G17') {
        _this.setModal({ plane: 'G17' });
    }
}
```

**Firmware Implementation** (`GCode.cpp:348-351`):
```cpp
case 17:
    gc_block.modal.plane_select = Plane::XY;
```

**Behavior**: Both set arc plane to XY. Used for G2/G3 arc interpretation.

---

#### G18 - ZX Plane
**Status**: ✅ Fully Compatible

**UI**: `simple-toolpath.js:382-386`  
**Firmware**: `GCode.cpp:352-355`

---

#### G19 - YZ Plane
**Status**: ✅ Fully Compatible

**UI**: `simple-toolpath.js:388-392`  
**Firmware**: `GCode.cpp:356-359`

---

### Distance Mode (Modal Group 3)

#### G90 - Absolute Distance Mode
**Status**: ✅ Fully Compatible

**UI Implementation** (`simple-toolpath.js:479-487`):
```javascript
'G90': function G90() {
    if (_this.modal.distance !== 'G90') {
        _this.setModal({ distance: 'G90' });
    }
}
```

**Firmware Implementation** (`GCode.cpp:360-365`):
```cpp
case 90:
    switch (mantissa) {
        case 0:
            gc_block.modal.distance = Distance::Absolute;
    }
```

**Behavior**: Coordinates interpreted as absolute positions from work coordinate system origin.

---

#### G90.1 - Absolute Arc Distance Mode
**Status**: ❌ Not Supported

**UI**: Not implemented  
**Firmware**: `GCode.cpp:366-370` - Explicitly returns error (not supported)

**Note**: Both systems reject G90.1 - arcs always use incremental I/J/K offsets.

---

#### G91 - Incremental Distance Mode
**Status**: ✅ Fully Compatible

**UI Implementation** (`simple-toolpath.js:488-496`):
```javascript
'G91': function G91() {
    if (_this.modal.distance !== 'G91') {
        _this.setModal({ distance: 'G91' });
    }
}
```

**Firmware Implementation** (`GCode.cpp:376-381`):
```cpp
case 91:
    switch (mantissa) {
        case 0:
            gc_block.modal.distance = Distance::Incremental;
    }
```

**Behavior**: Coordinates interpreted as relative to current position.

---

#### G91.1 - Incremental Arc Distance Mode
**Status**: ⚠️ Different Default Behavior

**UI**: Not explicitly implemented (defaults to incremental I/J/K)  
**Firmware Implementation** (`GCode.cpp:382-387`):
```cpp
case 10:
    mantissa = 0;
    // Arc incremental mode is the default and only supported mode
```

**Behavior**: Both systems default to incremental I/J/K for arcs (which is correct), but UI doesn't explicitly track G91.1 modal state.

---

### Feed Rate Mode (Modal Group 5)

#### G93 - Inverse Time Mode
**Status**: ⚠️ Partial Implementation

**UI Implementation** (`simple-toolpath.js:548-556`):
```javascript
'G93': function G93() {
    if (_this.modal.feedmode !== 'G93') {
        _this.setModal({ feedmode: 'G93' });
    }
}
```

**Firmware Implementation** (`GCode.cpp:393-396`):
```cpp
case 93:
    gc_block.modal.feed_rate = FeedRate::InverseTime;
```

**Differences**:
- **UI**: Tracks modal state but doesn't adjust display timing
- **Firmware**: Interprets F word as inverse time (1/F minutes per move)
- **Impact**: UI cannot accurately represent move durations in G93 mode

---

#### G94 - Units Per Minute Mode
**Status**: ✅ Fully Compatible

**UI**: `simple-toolpath.js:557-566`  
**Firmware**: `GCode.cpp:397-400`

Standard feed rate mode (mm/min or in/min).

---

#### G95 - Units Per Revolution Mode
**Status**: ⚠️ Partial Implementation

**UI Implementation** (`simple-toolpath.js:567-576`):
```javascript
'G95': function G95() {
    if (_this.modal.feedmode !== 'G95') {
        _this.setModal({ feedmode: 'G95' });
    }
}
```

**Firmware**: Not explicitly shown in GCode.cpp (may not be fully supported)

**Note**: G95 requires spindle encoder feedback, which may not be available on all Maslow systems.

---

### Units (Modal Group 6)

#### G20 - Inches
**Status**: ✅ Fully Compatible

**UI Implementation** (`simple-toolpath.js:394-398`):
```javascript
'G20': function G20(params) {
    if (_this.modal.units !== 'G20') {
        _this.setModal({ units: 'G20' });
    }
}
```

**Firmware Implementation** (`GCode.cpp:401-404`):
```cpp
case 20:
    gc_block.modal.units = Units::Inches;
```

**Behavior**: All subsequent coordinates in inches. Both systems convert to mm internally.

---

#### G21 - Millimeters
**Status**: ✅ Fully Compatible

**UI**: `simple-toolpath.js:400-405`  
**Firmware**: `GCode.cpp:405-408`

Standard metric mode.

---

### Cutter Compensation (Modal Group 7)

#### G40 - Cancel Cutter Compensation
**Status**: ✅ Compatible (Not Fully Implemented in Either)

**UI**: Not implemented (cutter comp not used)  
**Firmware Implementation** (`GCode.cpp:409-414`):
```cpp
case 40:
    // NOTE: Not required since cutter radius compensation is always disabled.
    mg_word_bit = ModalGroup::MG7;
```

**Behavior**: Both systems ignore cutter compensation. Firmware accepts G40 for compatibility but doesn't implement compensation.

---

### Tool Length Offset (Modal Group 8)

#### G43.1 - Dynamic Tool Length Offset
**Status**: ⚠️ Partial Implementation

**UI Implementation** (`simple-toolpath.js:430-435`):
```javascript
'G43.1': function G431(params) {
    if (_this.modal.tlo !== 'G43.1') {
        _this.setModal({ tlo: 'G43.1' });
    }
}
```

**Firmware Implementation** (`GCode.cpp:415-434`):
```cpp
case 43:
    if (mantissa == 10) {  // G43.1
        gc_block.modal.tool_length = ToolLengthOffset::EnableDynamic;
    }
```

**Differences**:
- **UI**: Tracks modal state only, doesn't apply offset to display
- **Firmware**: Applies Z-axis offset using axis word values
- **Impact**: UI toolpath doesn't show tool length offset adjustments

---

#### G49 - Cancel Tool Length Offset
**Status**: ⚠️ Partial Implementation

**UI**: `simple-toolpath.js:436-441`  
**Firmware**: `GCode.cpp:425-427`

Same limitation as G43.1.

---

### Work Coordinate Systems (Modal Group 12)

#### G54 - Select Work Coordinate System 1
**Status**: ✅ Fully Compatible

**UI**: `simple-toolpath.js:443-447`  
**Firmware**: `GCode.cpp:435-438`

---

#### G55 - Select WCS 2
**Status**: ✅ Fully Compatible

**UI**: `simple-toolpath.js:448-452`  
**Firmware**: `GCode.cpp:439-442`

---

#### G56 - Select WCS 3
**Status**: ✅ Fully Compatible

**UI**: `simple-toolpath.js:453-457`  
**Firmware**: `GCode.cpp:443-446`

---

#### G57 - Select WCS 4
**Status**: ✅ Fully Compatible

**UI**: `simple-toolpath.js:458-462`  
**Firmware**: `GCode.cpp:447-450`

---

#### G58 - Select WCS 5
**Status**: ✅ Fully Compatible

**UI**: `simple-toolpath.js:463-467`  
**Firmware**: `GCode.cpp:451-454`

---

#### G59 - Select WCS 6
**Status**: ✅ Fully Compatible

**UI**: `simple-toolpath.js:468-472`  
**Firmware**: `GCode.cpp:455-458`

---

### Control Mode (Modal Group 13)

#### G61 - Exact Path Mode
**Status**: ❌ Not Implemented in UI

**UI**: No handler defined  
**Firmware Implementation** (`GCode.cpp:460-466`):
```cpp
case 61:
    if (mantissa != 0) {
        FAIL(Error::GcodeUnsupportedCommand);  // [G61.1 not supported]
    }
    mg_word_bit = ModalGroup::MG13;
```

**Firmware Behavior**: Affects path planning (exact stop vs blending). Commented out in current implementation.

**Impact**: Minimal - feature not fully active in firmware either.

---

### Coordinate System Data

#### G92 - Set Position / Coordinate Offset
**Status**: ✅ Fully Compatible

**UI Implementation** (`simple-toolpath.js:497-536`):
```javascript
'G92': function G92(params) {
    // Sets coordinate offset for specified axes
    // Stores offset in g92offset object
    // Updates current position
}
```

**Firmware**: Implements G92 for coordinate offset management.

**Behavior**: Both track G92 offsets separately from work coordinate systems.

---

#### G92.1 - Cancel G92 Offsets
**Status**: ✅ Fully Compatible

**UI Implementation** (`simple-toolpath.js:537-547`):
```javascript
'G92.1': function G921(params) {
    // Clears all G92 offsets
    _this.position.x += _this.g92offset.x;
    _this.g92offset.x = 0;
    // Similar for Y and Z
}
```

**Firmware**: Handles G92.1 for offset cancellation.

---

## M-Code Commands

### Program Flow (Modal Group MM4)

#### M0 - Program Pause
**Status**: ⚠️ Partial Implementation

**UI Implementation** (`simple-toolpath.js:577-582`):
```javascript
'M0': function M0() {
    if (_this.modal.program !== 'M0') {
        _this.setModal({ program: 'M0' });
    }
}
```

**Firmware Implementation** (`GCode.cpp:487-491`):
```cpp
case 0:
    // M0 - Pause
    gc_block.modal.program_flow = ProgramFlow::Paused;
```

**Differences**:
- **UI**: Tracks state but doesn't pause display rendering
- **Firmware**: Actually pauses program execution, waits for cycle start
- **Impact**: UI doesn't show pause points visually

---

#### M1 - Optional Stop
**Status**: ⚠️ Partial Implementation

**UI**: `simple-toolpath.js:583-588`  
**Firmware**: `GCode.cpp:492-495` (accepted but may not be fully implemented)

---

#### M2 - Program End
**Status**: ✅ Fully Compatible

**UI**: `simple-toolpath.js:589-594`  
**Firmware**: `GCode.cpp:496-500`

Marks end of program.

---

#### M30 - Program End and Rewind
**Status**: ✅ Fully Compatible

**UI**: `simple-toolpath.js:595-600`  
**Firmware**: `GCode.cpp:501-505`

---

### Tool Change (Modal Group MM6)

#### M6 - Tool Change
**Status**: ⚠️ Partial Implementation

**UI Implementation** (`simple-toolpath.js:620-625`):
```javascript
'M6': function M6(params) {
    if (params && params.T !== undefined) {
        _this.setModal({ tool: params.T });
    }
}
```

**Firmware Implementation** (`GCode.cpp:526-530`):
```cpp
case 6:  // tool change
    gc_block.modal.tool_change = ToolChange::Enable;
```

**Differences**:
- **UI**: Only tracks tool number for modal state
- **Firmware**: May trigger actual tool change procedure
- **Impact**: UI doesn't show tool change pauses or procedures

---

### Spindle Control (Modal Group MM7)

#### M3 - Spindle On Clockwise
**Status**: ⚠️ Partial Implementation

**UI Implementation** (`simple-toolpath.js:601-607`):
```javascript
'M3': function M3(params) {
    if (_this.modal.spindle !== 'M3') {
        _this.setModal({ spindle: 'M3' });
    }
}
```

**Firmware Implementation** (`GCode.cpp:506-524`):
```cpp
case 3:
    gc_block.modal.spindle = SpindleState::Cw;
```

**Differences**:
- **UI**: Modal state only
- **Firmware**: Controls actual spindle hardware with S parameter for speed
- **Impact**: UI doesn't show spindle speed changes

---

#### M4 - Spindle On Counter-Clockwise
**Status**: ⚠️ Partial Implementation

**UI**: `simple-toolpath.js:608-613`  
**Firmware**: `GCode.cpp:513-519` (only if spindle is reversible or laser mode)

---

#### M5 - Spindle Off
**Status**: ✅ Fully Compatible

**UI**: `simple-toolpath.js:614-619`  
**Firmware**: `GCode.cpp:520-523`

---

### Coolant Control (Modal Group MM8)

#### M7 - Mist Coolant On
**Status**: ⚠️ Partial Implementation

**UI Implementation** (`simple-toolpath.js:626-637`):
```javascript
'M7': function M7() {
    var coolants = _this.modal.coolant.split(',');
    // Tracks coolant state
    _this.setModal({
        coolant: coolants.indexOf('M8') >= 0 ? 'M7,M8' : 'M7'
    });
}
```

**Firmware Implementation** (`GCode.cpp:531-542`):
```cpp
case 7:
    if (config->_coolant->hasMist()) {
        gc_block.coolant = GCodeCoolant::M7;
    }
```

**Differences**:
- **UI**: Tracks modal state
- **Firmware**: Controls actual mist coolant hardware (if configured)
- **Impact**: UI shows coolant state but not actual coolant flow

---

#### M8 - Flood Coolant On
**Status**: ⚠️ Partial Implementation

**UI**: `simple-toolpath.js:638-648`  
**Firmware**: `GCode.cpp:543-550`

---

#### M9 - All Coolant Off
**Status**: ✅ Fully Compatible

**UI**: `simple-toolpath.js:649-654`  
**Firmware**: `GCode.cpp:551-556`

---

### Parking Override (Modal Group MM9)

#### M56 - Parking Motion Override
**Status**: ❌ Not Implemented in UI

**UI**: No handler defined  
**Firmware Implementation** (`GCode.cpp:559-566`):
```cpp
case 56:
    if (config->_enableParkingOverrideControl) {
        gc_block.modal.override = Override::ParkingMotion;
    }
```

**Firmware Behavior**: Enables/disables parking motion for safety door.

**Impact**: UI doesn't track parking override state.

---

### Digital I/O Control (Modal Group MM10)

#### M62 - Digital Output On (Sync)
**Status**: ❌ Not Implemented in UI

**UI**: No handler defined  
**Firmware Implementation** (`GCode.cpp:567-570`):
```cpp
case 62:
    gc_block.modal.io_control = IoControl::DigitalOnSync;
```

---

#### M63 - Digital Output Off (Sync)
**Status**: ❌ Not Implemented in UI

**Firmware**: `GCode.cpp:571-574`

---

#### M64 - Digital Output On (Immediate)
**Status**: ❌ Not Implemented in UI

**Firmware**: `GCode.cpp:575-578`

---

#### M65 - Digital Output Off (Immediate)
**Status**: ❌ Not Implemented in UI

**Firmware**: `GCode.cpp:579-582`

---

#### M67 - Set Analog Output (Sync)
**Status**: ❌ Not Implemented in UI

**Firmware**: `GCode.cpp:583-586`

---

#### M68 - Set Analog Output (Immediate)
**Status**: ❌ Not Implemented in UI

**Firmware**: `GCode.cpp:587-590`

---

## Summary Tables

### G-Code Command Summary

| Command | Description | UI Status | Firmware Status | Notes |
|---------|-------------|-----------|-----------------|-------|
| G0 | Rapid Move | ✅ Full | ✅ Full | Compatible |
| G1 | Linear Feed | ✅ Full | ✅ Full | Compatible |
| G2 | CW Arc | ❌ Incompatible | ✅ Full | **CRITICAL: Start/end swap + R-mode sign error** |
| G3 | CCW Arc | ⚠️ Partial | ✅ Full | R-mode sign issue |
| G4 | Dwell | ⚠️ Ignored | ✅ Full | UI doesn't show dwells |
| G10 | Set Coordinate Data | ❌ None | ✅ Full | Not in UI |
| G17 | XY Plane | ✅ Full | ✅ Full | Compatible |
| G18 | ZX Plane | ✅ Full | ✅ Full | Compatible |
| G19 | YZ Plane | ✅ Full | ✅ Full | Compatible |
| G20 | Inches | ✅ Full | ✅ Full | Compatible |
| G21 | Millimeters | ✅ Full | ✅ Full | Compatible |
| G28 | Go Home 0 | ❌ None | ✅ Full | Not in UI |
| G28.1 | Set Home 0 | ❌ None | ✅ Full | Not in UI |
| G30 | Go Home 1 | ❌ None | ✅ Full | Not in UI |
| G30.1 | Set Home 1 | ❌ None | ✅ Full | Not in UI |
| G38.2 | Probe Toward | ⚠️ Partial | ✅ Full | UI modal only |
| G38.3 | Probe No Error | ⚠️ Partial | ✅ Full | UI modal only |
| G38.4 | Probe Away | ⚠️ Partial | ✅ Full | UI modal only |
| G38.5 | Probe Away No Error | ⚠️ Partial | ✅ Full | UI modal only |
| G40 | Cancel Cutter Comp | ⚠️ N/A | ⚠️ N/A | Neither implements |
| G43.1 | Tool Length Offset | ⚠️ Partial | ✅ Full | UI doesn't apply offset |
| G49 | Cancel TLO | ⚠️ Partial | ✅ Full | UI doesn't apply |
| G53 | Machine Coords | ❌ None | ✅ Full | CRITICAL: UI wrong |
| G54-G59 | Work Coord Systems | ✅ Full | ✅ Full | Compatible |
| G61 | Exact Path | ❌ None | ⚠️ Partial | Both incomplete |
| G80 | Cancel Motion | ✅ Full | ✅ Full | Compatible |
| G90 | Absolute Mode | ✅ Full | ✅ Full | Compatible |
| G90.1 | Absolute Arc | ❌ None | ❌ None | Not supported |
| G91 | Incremental Mode | ✅ Full | ✅ Full | Compatible |
| G91.1 | Incremental Arc | ⚠️ Default | ✅ Full | Compatible (default) |
| G92 | Set Position | ✅ Full | ✅ Full | Compatible |
| G92.1 | Cancel G92 | ✅ Full | ✅ Full | Compatible |
| G93 | Inverse Time | ⚠️ Partial | ✅ Full | UI doesn't adjust timing |
| G94 | Units/Min | ✅ Full | ✅ Full | Compatible |
| G95 | Units/Rev | ⚠️ Partial | ⚠️ Partial | Spindle encoder needed |

### M-Code Command Summary

| Command | Description | UI Status | Firmware Status | Notes |
|---------|-------------|-----------|-----------------|-------|
| M0 | Pause | ⚠️ Partial | ✅ Full | UI doesn't pause display |
| M1 | Optional Stop | ⚠️ Partial | ⚠️ Partial | Both track but may not implement |
| M2 | Program End | ✅ Full | ✅ Full | Compatible |
| M3 | Spindle CW | ⚠️ Partial | ✅ Full | UI modal only |
| M4 | Spindle CCW | ⚠️ Partial | ✅ Full | UI modal only |
| M5 | Spindle Off | ✅ Full | ✅ Full | Compatible |
| M6 | Tool Change | ⚠️ Partial | ✅ Full | UI doesn't show procedure |
| M7 | Mist On | ⚠️ Partial | ✅ Full | UI modal only |
| M8 | Flood On | ⚠️ Partial | ✅ Full | UI modal only |
| M9 | Coolant Off | ✅ Full | ✅ Full | Compatible |
| M30 | Program End | ✅ Full | ✅ Full | Compatible |
| M56 | Parking Override | ❌ None | ✅ Full | Not in UI |
| M62 | Digital Out On Sync | ❌ None | ✅ Full | Not in UI |
| M63 | Digital Out Off Sync | ❌ None | ✅ Full | Not in UI |
| M64 | Digital Out On Immed | ❌ None | ✅ Full | Not in UI |
| M65 | Digital Out Off Immed | ❌ None | ✅ Full | Not in UI |
| M67 | Analog Out Sync | ❌ None | ✅ Full | Not in UI |
| M68 | Analog Out Immed | ❌ None | ✅ Full | Not in UI |

## Critical Issues

### Issue #1: G2 Clockwise Arc - Start/End Point Swap
**Severity**: HIGH  
**Location**: `ESP3D-WEBUI/www/js/toolpath-displayer.js:959-963`

**Problem**: The UI swaps start and end points for G2 (clockwise) arcs before rendering:

```javascript
if (modal.motion == 'G2') {  // clockwise
    var tmp = start;
    start = end;
    end = tmp;
}
```

The firmware does **NOT** swap points. It maintains start and end as specified in the G-code and adjusts the angular travel direction instead.

**Impact**: G2 arcs are drawn **backwards** in the UI compared to how they're cut by the firmware. The arc starts where it should end and ends where it should start.

**Example**:
```gcode
G0 X0 Y0
G2 X10 Y0 I5 J0  ; Quarter circle CW from (0,0) to (10,0), center at (5,0)
```

**UI Display**: Shows arc from (10,0) to (0,0) (reversed!)  
**Firmware Execution**: Cuts arc from (0,0) to (10,0) (correct)  
**Result**: Toolpath preview is backwards!

**Recommendation**: Remove the start/end swap in UI and handle direction through angular travel calculation like the firmware does.

---

### Issue #2: R-mode Sign Convention Mismatch (G2 and G3)
**Severity**: HIGH  
**Location**: 
- UI: `ESP3D-WEBUI/www/js/simple-toolpath.js:255`
- Firmware: `firmware/FluidNC/src/GCode.cpp:1212`

**Problem**: The UI and firmware use **opposite sign conventions** when calculating arc center from radius (R parameter).

**UI Implementation** (`simple-toolpath.js:255`):
```javascript
let height = Math.sqrt(4 * radius * radius - x * x - y * y) / 2;  // POSITIVE
if (isClockwise) {
    height = -height;  // Negate for clockwise
}
```

**Firmware Implementation** (`GCode.cpp:1212`):
```cpp
h_x2_div_d = -sqrt(h_x2_div_d) / hypot_f(x, y);  // NEGATIVE from start
if (gc_block.modal.motion == Motion::CcwArc) {
    h_x2_div_d = -h_x2_div_d;  // Negate for counter-clockwise
}
```

**Impact**: Arcs specified with R parameter are drawn on the **opposite side** of the chord line in UI versus firmware. An arc that should bulge left will bulge right, and vice versa.

**Example**:
```gcode
G0 X0 Y0
G2 X10 Y0 R5  ; Small semicircle (R positive = arc < 180°)
```

**Expected**: Semicircle below the line (Y negative)  
**UI Shows**: Semicircle above the line (Y positive) - WRONG SIDE!  
**Result**: Arc appears mirrored across the chord line!

**Recommendation**: Change UI to use negative sqrt initially (match firmware) or adjust the logic to produce the same result.

---

### Issue #3: G53 Machine Coordinates Not Implemented
**Severity**: HIGH  
**Location**: UI has no G53 handler

**Problem**: When firmware encounters G53, it interprets the next move in machine coordinates (ignoring work coordinate system offsets). The UI has no G53 handler, so it will display G53 moves as if they were in work coordinates.

**Example**:
```gcode
G54        ; Select WCS1 (assume offset is X10 Y20)
G0 X0 Y0   ; Move to WCS origin -> Machine (10, 20)
G53 G0 X0 Y0  ; Move to machine origin -> Machine (0, 0)
```

**UI Display**: Would show move to (0, 0) in current WCS (appears as (10, 20) machine)  
**Firmware Execution**: Moves to true machine (0, 0)  
**Result**: UI shows WRONG path!

**Recommendation**: Implement G53 handler in UI to temporarily ignore WCS offsets for the next move.

---

### Issue #4: G10 Coordinate System Changes Invisible
**Severity**: MEDIUM  
**Location**: UI has no G10 handler

**Problem**: G10 L2/L20 commands change work coordinate system offsets. UI doesn't handle these, so subsequent moves may appear in wrong locations.

**Example**:
```gcode
G54
G10 L2 P1 X5 Y10  ; Set G54 origin to (5, 10) in machine coords
G0 X0 Y0          ; Move to new G54 origin
```

**UI Display**: Would show move to previous G54 origin  
**Firmware Execution**: Moves to new origin at machine (5, 10)

**Recommendation**: Implement G10 L2/L20 handlers to update coordinate system offsets in UI.

---

### Issue #5: Probe Commands Not Visualized
**Severity**: LOW  
**Location**: G38.x handlers only track modal state

**Problem**: Probe moves are displayed as regular moves, but probe commands have special behavior (stop on contact, optional error).

**Impact**: Users cannot distinguish probe moves from regular moves in UI preview.

**Recommendation**: Add visual indication for probe moves (different color, style, or annotation).

---

### Issue #6: Tool Length Offset Not Applied
**Severity**: LOW  
**Location**: G43.1 handler doesn't adjust Z display

**Problem**: Tool length offsets affect actual cutting depth but aren't shown in UI preview.

**Impact**: Preview shows tool at wrong Z height after G43.1.

**Recommendation**: Track and apply tool length offset to Z coordinates in display.

---

### Issue #7: I/O Control Commands Invisible
**Severity**: LOW  
**Location**: M62-M68 not implemented

**Problem**: Digital and analog I/O commands are completely invisible in UI.

**Impact**: Users can't see when I/O operations occur in program flow.

**Recommendation**: Add markers or annotations for I/O commands in timeline/toolpath view.

---

## Recommendations

### High Priority - CRITICAL ARC ISSUES
1. **Fix G2 start/end point swap** - Remove the swap in `toolpath-displayer.js:959-963` and handle direction via angular travel calculation
2. **Fix R-mode sign convention** - Align UI calculation with firmware (negative sqrt or equivalent logic)
3. **Validate arc rendering** - Create comprehensive test suite for G2/G3 arcs with I/J/K and R modes
4. **Implement G53 handler** - Critical for accurate machine coordinate moves
5. **Implement G10 handler** - Important for coordinate system management

### Medium Priority
6. **Add probe visualization** - Different display for G38.x moves
7. **Implement G28/G30 handlers** - Show homing moves
8. **Add tool length offset display** - Apply G43.1 offsets to preview

### Low Priority
9. **Add I/O command markers** - Visual indicators for M62-M68
10. **Improve dwell visualization** - Show G4 pauses with timing
11. **Add spindle speed indicators** - Show S parameter changes with M3/M4

### Testing
12. **Create comprehensive test suite** - Test files for all command combinations, especially arcs
13. **Document expected behaviors** - Clear spec for UI vs firmware differences
14. **Add validation warnings** - Warn users about commands UI cannot visualize

## Test Cases

### Test G2 Start/End Swap Issue
```gcode
; Test that G2 arc is drawn in correct direction
G21  ; Millimeters
G0 X0 Y0
G2 X10 Y0 I5 J0  ; Quarter circle CW, should go from (0,0) to (10,0)

; Expected firmware behavior: Arc from (0,0) to (10,0), curving through Y-positive
; Current UI behavior: Arc from (10,0) to (0,0) - BACKWARDS!
; Correct UI behavior: Should match firmware
```

### Test R-mode Sign Convention Issue
```gcode
; Test R-mode arc with positive radius
G21
G0 X0 Y0
G2 X10 Y0 R5  ; Small semicircle (< 180°)

; Expected: Semicircle curving in Y-negative direction (below the line)
; Current UI: Shows semicircle in Y-positive direction - WRONG SIDE!
; Firmware: Correctly cuts in Y-negative direction

; Test with negative R (large arc)
G0 X0 Y0
G2 X10 Y0 R-5  ; Large arc (> 180°)

; Expected: Large arc curving in Y-positive direction (above the line)
; Current UI: Shows in Y-negative - WRONG SIDE!
```

### Test G3 CCW Arc (Better Compatibility)
```gcode
; G3 should work better since no start/end swap
G21
G0 X0 Y0
G3 X10 Y0 I5 J0  ; CCW quarter circle

; Expected: Arc from (0,0) to (10,0), curving through Y-negative
; UI behavior: Should be mostly correct (no swap), but may have R-mode issues
```

### Test G53 Machine Coordinate Issue
```gcode
; Setup WCS with offset
G54
G10 L2 P1 X100 Y100
G0 X0 Y0    ; Should go to machine (100, 100)

; Test G53
G53 G0 X50 Y50  ; Should go to machine (50, 50)
; UI will INCORRECTLY show (150, 150)

G0 X0 Y0    ; Back to WCS origin (100, 100)
```

### Test G10 Coordinate System Changes
```gcode
G54
G0 X0 Y0
G10 L20 P1 X10 Y20  ; Set WCS origin at current position
G0 X0 Y0            ; Move to new origin
; UI may show wrong position
```

### Test Probe Visualization
```gcode
G0 Z10
G38.2 Z-10 F100  ; Probe downward
; UI should show this differently than G1 Z-10
```

## References

1. **LinuxCNC G-code Documentation**: https://linuxcnc.org/docs/html/gcode.html
2. **NIST RS274NGC Standard**
3. **Grbl Arc Implementation**: https://github.com/grbl/grbl/issues/236
4. **FluidNC Documentation**: https://github.com/bdring/FluidNC/wiki
5. **Original G2/G3 Analysis**: See `gcode-ui-firmware-comparison.md` for detailed arc issue analysis

## Conclusion

This comprehensive analysis reveals significant gaps and incompatibilities between the UI and firmware G-code handling:

**CRITICAL - Arc Rendering Issues (G2/G3)**:
- **G2 start/end swap** - UI draws clockwise arcs backwards (Issue #1)
- **R-mode sign convention** - UI draws arcs on wrong side of chord line (Issue #2)
- **Impact**: Toolpath preview does NOT match actual cutting for G2/G3 arcs
- **Status**: **UNFIXED** - These issues are present in current code despite PR #592 references

**Critical Gaps**:
- G53 machine coordinates (HIGH impact - wrong display)
- G10 coordinate system management (MEDIUM impact)
- G28/G30 homing operations (MEDIUM impact)

**Acceptable Differences**:
- M62-M68 I/O control (firmware-only hardware control)
- M56 parking override (firmware safety feature)
- Probe details (hardware interaction)

**Important Note**: While the UI successfully provides toolpath preview for basic machining operations (G0/G1 moves, plane selection, units, coordinate systems), **arc commands (G2/G3) have critical rendering errors** that cause the preview to differ significantly from actual firmware execution. Users relying on the UI preview for arc-heavy toolpaths will see incorrect visualizations.

**Immediate Action Required**: Fix Issues #1 and #2 (G2 start/end swap and R-mode sign convention) to make arc preview accurate. Until these are fixed, the UI preview cannot be trusted for programs containing G2 or G3 commands.
