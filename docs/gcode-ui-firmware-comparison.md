# G-code UI vs Firmware Comparison Report - G2/G3 Arc Analysis

> **Note**: This report documents critical G2/G3 arc rendering issues in the UI. These issues are **STILL PRESENT** in the current code.
>
> **See also**: [gcode-ui-firmware-comparison-comprehensive.md](gcode-ui-firmware-comparison-comprehensive.md) for analysis of ALL G-code commands.

## Executive Summary

This report compares how G-code arc commands (G2/G3) are interpreted and executed by the ESP3D-WEBUI (user interface) versus the FluidNC firmware in the Maslow CNC system.

**Critical Finding**: The UI and firmware handle G-code arcs differently in several critical areas that cause **incorrect toolpath visualization**:
1. **Arc center calculation** - Different formulas and sign conventions ❌ **UNFIXED**
2. **Direction handling** - The UI swaps start/end points for G2, firmware uses rotation direction ❌ **UNFIXED**
3. **Multi-turn arcs (P parameter)** - Different handling of extra rotations ⚠️ **Needs Verification**
4. **Arc mode** - Different interpretations of I/J/K as offsets vs absolute positions ✅ Compatible

## Overview

### UI G-code Processing
- **Location**: `ESP3D-WEBUI/www/js/`
- **Key Files**:
  - `simple-parser.js` - Parses G-code lines into tokens
  - `simple-interpreter.js` - Interprets parsed tokens and maintains modal state
  - `simple-toolpath.js` - Converts G-code to toolpath with arc calculations
  - `toolpath-displayer.js` - Renders toolpath on canvas

### Firmware G-code Processing
- **Location**: `firmware/FluidNC/src/`
- **Key Files**:
  - `GCode.cpp` - Parses and validates G-code commands
  - `MotionControl.cpp` - Executes arc motions with segmentation

## Detailed Comparison

### 1. Arc Parameter Interpretation (I, J, K)

#### UI Implementation
**File**: `ESP3D-WEBUI/www/js/simple-toolpath.js`
**Lines**: 810-823

```javascript
translateI: function translateI(i) {
    return this.translateX(i, true);  // Always relative mode
}
translateJ: function translateJ(j) {
    return this.translateY(j, true);  // Always relative mode
}
translateK: function translateK(k) {
    return this.translateZ(k, true);  // Always relative mode
}
```

**Interpretation**: I/J/K are ALWAYS treated as relative offsets from current position, regardless of G90/G91 mode.

#### Firmware Implementation
**File**: `firmware/FluidNC/src/GCode.cpp`
**Lines**: 1243-1255

```cpp
// Arc Center Format Offset Mode
if (!(ijk_words & (bitnum_to_mask(axis_0) | bitnum_to_mask(axis_1)))) {
    FAIL(Error::GcodeNoOffsetsInPlane);
}
// Convert IJK values to proper units.
if (gc_block.modal.units == Units::Inches) {
    for (size_t idx = 0; idx < n_axis; idx++) {
        if (ijk_words & bitnum_to_mask(idx)) {
            gc_block.values.ijk[idx] *= MM_PER_INCH;
        }
    }
}
```

**Interpretation**: I/J/K are treated as offsets from current position (relative mode by default in arc commands).

**Status**: ✅ COMPATIBLE - Both treat I/J/K as relative offsets.

### 2. Arc Center Calculation from I/J/K

#### UI Implementation
**File**: `ESP3D-WEBUI/www/js/simple-toolpath.js`
**Lines**: 187-271 (G2), 276-364 (G3)

```javascript
// G2 (Clockwise)
let v0 = { // fixed point (center)
    x: _this.translateI(params.I),
    y: _this.translateJ(params.J),
    z: _this.translateK(params.K)
};
// v0 becomes the center of the arc after plane selection
offsetAddArcCurve(v1, v2, v0, params.P ? params.P : 0);
```

The center is calculated as:
- `center.x = current.x + I`
- `center.y = current.y + J`
- `center.z = current.z + K`

#### Firmware Implementation
**File**: `firmware/FluidNC/src/MotionControl.cpp`
**Lines**: 127-128

```cpp
float center_axis0 = position[axis_0] + offset[axis_0];
float center_axis1 = position[axis_1] + offset[axis_1];
```

Where `offset[axis_0]` = I and `offset[axis_1]` = J from the G-code block.

**Status**: ✅ COMPATIBLE - Both calculate center as current position + offset.

### 3. Arc Direction Handling (G2 vs G3)

#### UI Implementation
**File**: `ESP3D-WEBUI/www/js/simple-toolpath.js`

G2 (Lines 187-275):
```javascript
let isClockwise = true;
// ...
offsetAddArcCurve(v1, v2, v0, params.P ? params.P : 0);
```

G3 (Lines 276-364):
```javascript
let isClockwise = false;
// ...
offsetAddArcCurve(v1, v2, v0, params.P ? params.P : 0);
```

**File**: `ESP3D-WEBUI/www/js/toolpath-displayer.js`
**Lines**: 959-963

```javascript
if (modal.motion == 'G2') {  // clockwise
    var tmp = start;
    start = end;
    end = tmp;  // SWAP START AND END!
}
```

**Critical Difference**: The UI **swaps the start and end points** for G2 arcs in the display code!

#### Firmware Implementation
**File**: `firmware/FluidNC/src/GCode.cpp`
**Lines**: 1126-1128

```cpp
case Motion::CwArc:
    clockwiseArc = true;  // No break intentional.
case Motion::CcwArc:
```

**File**: `firmware/FluidNC/src/MotionControl.cpp`
**Lines**: 141-160

```cpp
// CCW angle between position and target from circle center.
float angular_travel = atan2f(r_axis0 * rt_axis1 - r_axis1 * rt_axis0, 
                               r_axis0 * rt_axis0 + r_axis1 * rt_axis1);
if (is_clockwise_arc) {  // Correct atan2 output per direction
    if (angular_travel >= -ARC_ANGULAR_TRAVEL_EPSILON) {
        angular_travel -= 2 * float(M_PI);
    }
    // ...
} else {
    if (angular_travel <= ARC_ANGULAR_TRAVEL_EPSILON) {
        angular_travel += 2 * float(M_PI);
    }
}
```

The firmware calculates the angular travel in CCW direction and then corrects for CW arcs by adjusting the angle.

**Status**: ⚠️ POTENTIALLY INCOMPATIBLE - Different approaches to direction handling.
- UI: Swaps start/end points for G2 and treats both as CCW arcs
- Firmware: Keeps start/end as-is and adjusts angular travel calculation

### 4. Arc Rendering/Segmentation

#### UI Implementation
**File**: `ESP3D-WEBUI/www/js/toolpath-displayer.js`
**Lines**: 1257-1302

```javascript
addArcCurve: function(modal, start, end, center, extraRotations) {
    var deltaX1 = start.x - center.x;
    var deltaY1 = start.y - center.y;
    var radius = Math.hypot(deltaX1, deltaY1);
    var deltaX2 = end.x - center.x;
    var deltaY2 = end.y - center.y;
    var theta1 = Math.atan2(deltaY1, deltaX1);
    var theta2 = Math.atan2(deltaY2, deltaX2);
    var cw = modal.motion == "G2";
    
    if (!cw && theta2 < theta1) {
        theta2 += Math.PI * 2;
    } else if (cw && theta2 > theta1) {
        theta2 -= Math.PI * 2;
    }
    if (theta1 == theta2) {
        theta2 += Math.PI * ((cw) ? -2 : 2);
    }
    if (extraRotations > 1) {
        theta2 += (extraRotations-1) * Math.PI * ((cw) ? -2 : 2);
    }
    
    // Render with 10 segments per PI radians
    n = 10 * Math.ceil(Math.abs(deltaTheta) / Math.PI);
    dt = (deltaTheta) / n;
    // ...
}
```

**Key Points**:
- Fixed segmentation: 10 segments per π radians
- Uses atan2 to calculate start and end angles
- Handles full circles when theta1 == theta2
- P parameter: adds (P-1) full rotations

#### Firmware Implementation
**File**: `firmware/FluidNC/src/MotionControl.cpp`
**Lines**: 166-252

```cpp
// Adaptive segmentation based on arc tolerance
uint16_t segments = uint16_t(floorf(
    fabsf(0.5 * angular_travel * radius) / 
    sqrtf(config->_arcTolerance * (2 * radius - config->_arcTolerance))
));

float theta_per_segment = angular_travel / segments;

// Small angle approximation for efficiency
float cos_T = 2.0f - theta_per_segment * theta_per_segment;
float sin_T = theta_per_segment * 0.16666667f * (cos_T + 4.0f);
cos_T *= 0.5;

// Periodic correction every N_ARC_CORRECTION segments
if (count < N_ARC_CORRECTION) {
    // Apply vector rotation matrix
    r_axisi = r_axis0 * sin_T + r_axis1 * cos_T;
    r_axis0 = r_axis0 * cos_T - r_axis1 * sin_T;
    r_axis1 = r_axisi;
    count++;
} else {
    // Arc correction to radius vector
    cos_Ti  = cosf(i * theta_per_segment);
    sin_Ti  = sinf(i * theta_per_segment);
    r_axis0 = -offset[axis_0] * cos_Ti + offset[axis_1] * sin_Ti;
    r_axis1 = -offset[axis_0] * sin_Ti - offset[axis_1] * cos_Ti;
    count   = 0;
}
```

**Key Points**:
- Adaptive segmentation based on arc tolerance setting
- Uses small-angle approximation for performance
- Periodic correction for accuracy
- Much more sophisticated than UI rendering

**Status**: ⚠️ DIFFERENT APPROACHES - UI uses fixed segmentation, firmware uses adaptive.

### 5. Radius Mode (R parameter)

#### UI Implementation
**File**: `ESP3D-WEBUI/www/js/simple-toolpath.js`
**Lines**: 250-269 (G2), 340-359 (G3)

```javascript
if (params.R) {
    let radius = _this.translateR(Number(params.R) || 0);
    let x = v2.x - v1.x;
    let y = v2.y - v1.y;
    let distance = Math.hypot(x, y);
    let height = Math.sqrt(4 * radius * radius - x * x - y * y) / 2;
    
    if (isClockwise) {
        height = -height;
    }
    if (radius < 0) {
        height = -height;
    }
    
    let offsetX = x / 2 - y / distance * height;
    let offsetY = y / 2 + x / distance * height;
    
    v0.x = v1.x + offsetX;
    v0.y = v1.y + offsetY;
}
```

**Formula**:
- `height = sqrt(4*R² - x² - y²) / 2`
- Negate height if clockwise
- Negate height if R < 0 (large arc)
- `centerX = startX + x/2 - y/distance * height`
- `centerY = startY + y/2 + x/distance * height`

#### Firmware Implementation
**File**: `firmware/FluidNC/src/GCode.cpp`
**Lines**: 1205-1242

```cpp
// h_x2_div_d = sqrt(4 * r^2 - x^2 - y^2) / sqrt(x^2 + y^2)
float h_x2_div_d = 4.0f * gc_block.values.r * gc_block.values.r - x * x - y * y;
if (h_x2_div_d < 0) {
    FAIL(Error::GcodeArcRadiusError);
}

h_x2_div_d = -sqrt(h_x2_div_d) / hypot_f(x, y);  // == -(h * 2 / d)

// Invert sign if counter clockwise
if (gc_block.modal.motion == Motion::CcwArc) {
    h_x2_div_d = -h_x2_div_d;
}

// Handle negative R (arc > 180 degrees)
if (gc_block.values.r < 0) {
    h_x2_div_d        = -h_x2_div_d;
    gc_block.values.r = -gc_block.values.r;
}

gc_block.values.ijk[axis_0] = 0.5f * (x - (y * h_x2_div_d));
gc_block.values.ijk[axis_1] = 0.5f * (y + (x * h_x2_div_d));
```

**Formula**:
- `h_x2_div_d = -sqrt(4*R² - x² - y²) / hypot(x, y)`  ← **Note the negative sign!**
- Negate if CCW (opposite of UI!)
- Negate if R < 0
- `I = 0.5 * (x - y * h_x2_div_d)`
- `J = 0.5 * (y + x * h_x2_div_d)`

**Status**: ⚠️ SIGN DIFFERENCE DETECTED!
- UI: `height = +sqrt(...) / 2`, then negates for CW
- Firmware: `h_x2_div_d = -sqrt(...) / hypot`, then negates for CCW

This is a **critical difference** that could lead to arcs being drawn in opposite directions!

### 6. Multi-turn Arcs (P parameter)

#### UI Implementation
**File**: `ESP3D-WEBUI/www/js/simple-toolpath.js`
**Lines**: 271, 361

```javascript
offsetAddArcCurve(v1, v2, v0, params.P ? params.P : 0);
```

**File**: `ESP3D-WEBUI/www/js/toolpath-displayer.js`
**Lines**: 1276-1278

```javascript
if (extraRotations > 1) {
    theta2 += (extraRotations-1) * Math.PI * ((cw) ? -2 : 2);
}
```

**Interpretation**: P value adds (P-1) full rotations (2π radians).

#### Firmware Implementation
**File**: `firmware/FluidNC/src/MotionControl.cpp`
**Lines**: 147-159

```cpp
// See https://linuxcnc.org/docs/2.6/html/gcode/gcode.html#sec:G2-G3-Arc
// The P word specifies the number of extra rotations.  Missing P, P0 or P1
// is just the programmed arc.  Pn adds n-1 rotations
if (pword_rotations > 1) {
    angular_travel -= (pword_rotations - 1) * 2 * float(M_PI);  // CW
}
// ...
if (pword_rotations > 1) {
    angular_travel += (pword_rotations - 1) * 2 * float(M_PI);  // CCW
}
```

**Interpretation**: P value adds (P-1) full rotations (2π radians).

**Status**: ✅ COMPATIBLE - Both add (P-1) full rotations.

### 7. Arc Distance Mode (G90.1 / G91.1)

#### UI Implementation
**File**: `ESP3D-WEBUI/www/js/simple-toolpath.js`
**Lines**: 810-823

The UI has functions `translateI`, `translateJ`, `translateK` that **always** call the translate functions with `relative = true`, meaning I/J/K are always treated as relative offsets.

**Status**: The UI does NOT implement G90.1/G91.1 arc distance mode. I/J/K are always relative.

#### Firmware Implementation
**File**: `firmware/FluidNC/src/GCode.cpp`

The firmware stores `modal.arc` but I/J/K appear to always be treated as offsets in arc mode based on the code at lines 1243-1272.

**Status**: ⚠️ INCOMPLETE - Neither fully implements arc distance mode switching, but both default to relative I/J/K.

## Summary of Differences

| Feature | UI Behavior | Firmware Behavior | Compatible? |
|---------|-------------|-------------------|-------------|
| I/J/K Interpretation | Always relative offsets | Relative offsets (default) | ✅ Yes |
| Arc Center Calculation | current + offset | current + offset | ✅ Yes |
| G2 Direction Handling | **Swaps start/end points** | Adjusts angular_travel | ⚠️ Different |
| G3 Direction Handling | Normal CCW calculation | Adjusts angular_travel | ⚠️ Different |
| R-mode Sign Convention | height = +sqrt, negate for CW | h = -sqrt, negate for CCW | ❌ **Opposite!** |
| Segmentation Strategy | Fixed (10 seg/π) | Adaptive (tolerance-based) | ⚠️ Different |
| P Parameter (multi-turn) | (P-1) * 2π | (P-1) * 2π | ✅ Yes |
| Arc Distance Mode | Not implemented | Not fully implemented | ⚠️ Both incomplete |
| Error Checking | Minimal | Extensive validation | ⚠️ Different |

## Critical Issues Identified

### Issue #1: G2 Start/End Point Swap
**Severity**: HIGH
**Location**: `ESP3D-WEBUI/www/js/toolpath-displayer.js:959-963`

The UI swaps start and end points for G2 arcs before rendering:
```javascript
if (modal.motion == 'G2') {  // clockwise
    var tmp = start;
    start = end;
    end = tmp;
}
```

This is **NOT** done in the firmware. The firmware maintains start and end as specified in the G-code and adjusts the angular travel direction instead.

**Impact**: G2 arcs may be drawn backwards in the UI compared to how they're cut by the firmware.

### Issue #2: R-mode Sign Convention Mismatch
**Severity**: HIGH
**Location**: 
- UI: `ESP3D-WEBUI/www/js/simple-toolpath.js:250-269`
- Firmware: `firmware/FluidNC/src/GCode.cpp:1205-1242`

The UI and firmware use **opposite sign conventions** in the radius mode calculation:
- **UI**: Starts with positive sqrt, negates for clockwise
- **Firmware**: Starts with negative sqrt, negates for counter-clockwise

**Impact**: Arcs specified with R parameter may be drawn on the opposite side of the chord line in the UI versus how they're cut by the firmware.

### Issue #3: Different Direction Handling Strategy
**Severity**: MEDIUM

The UI and firmware take fundamentally different approaches to arc direction:
- **UI**: Treats both G2 and G3 as CCW arcs by swapping endpoints for G2
- **Firmware**: Calculates CCW angle and adjusts sign for CW arcs

**Impact**: While the end result may be the same in simple cases, complex arcs (large arcs, multi-turn arcs) may differ.

## Recommendations

1. **Fix the R-mode sign convention** in either UI or firmware to match the other. The firmware implementation appears to follow the LinuxCNC standard more closely.

2. **Remove the start/end swap** in the UI for G2 arcs, and instead handle direction through the angle calculation like the firmware does.

3. **Add comprehensive test cases** for arc rendering, including:
   - Small CW and CCW arcs (< 180°)
   - Large CW and CCW arcs (> 180°)
   - Multi-turn arcs (P > 1)
   - R-mode arcs with positive and negative R
   - I/J/K mode arcs
   - Full circles (start == end)

4. **Consider aligning segmentation strategies** - The firmware's adaptive approach is more accurate but the UI's fixed approach is simpler. Document the expected differences.

5. **Document the expected behavior** - Create reference G-code test files with known correct visual and physical output.

## Test Cases for Validation

Here are some test G-code snippets to validate the differences:

```gcode
; Test 1: Quarter circle CW
G0 X0 Y0
G2 X10 Y0 I5 J0
; Expected: Quarter circle from (0,0) to (10,0) with center at (5,0), radius 5

; Test 2: Three-quarter circle CCW
G0 X0 Y0
G3 X10 Y0 I5 J0
; Expected: Three-quarter circle from (0,0) to (10,0) with center at (5,0), radius 5

; Test 3: R-mode small arc, CW
G0 X0 Y0
G2 X10 Y0 R5
; Expected: Small semicircle below the line (< 180°)

; Test 4: R-mode large arc, CW
G0 X0 Y0
G2 X10 Y0 R-5
; Expected: Large semicircle above the line (> 180°)

; Test 5: Multi-turn arc
G0 X0 Y0
G2 X0 Y0 I10 J0 P3
; Expected: Full circle repeated 3 times (P3 = 1 base + 2 extra rotations)
```

## References

1. **LinuxCNC G-code Documentation**: https://linuxcnc.org/docs/2.6/html/gcode/gcode.html#sec:G2-G3-Arc
2. **NIST RS274NGC G-code Standard**
3. **Grbl Arc Implementation**: https://github.com/grbl/grbl/issues/236
4. **cncjs gcode-toolpath** (UI basis): https://github.com/cncjs/gcode-toolpath

## Conclusion

**Current Status**: The UI and firmware have **significant differences** in how they handle G2/G3 arc commands, particularly:
1. G2 direction handling (start/end swap in UI) - ❌ **STILL PRESENT**
2. R-mode center calculation (opposite sign conventions) - ❌ **STILL PRESENT**

These differences explain the discrepancies noted in issue discussions. **These issues are NOT fixed** - they remain in the current codebase and cause incorrect arc visualization in the UI.

**Action Required**: To resolve these issues, the UI code needs to be updated to match the firmware's arc handling logic, which correctly implements the LinuxCNC/NIST G-code standard. Specifically:
1. Remove the start/end swap in `toolpath-displayer.js:959-963`
2. Fix the R-mode sign convention in `simple-toolpath.js:255` to match firmware

**See**: The comprehensive report ([gcode-ui-firmware-comparison-comprehensive.md](gcode-ui-firmware-comparison-comprehensive.md)) documents these and other G-code command differences.
