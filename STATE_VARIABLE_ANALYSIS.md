# State Variable Redundancy Analysis

## Issue Context
This analysis addresses the request to "analyze the code and report where else these variables are used and if they are still needed now that there is the state definition in use."

## Summary
There are **duplicate state variables** in both the `Maslow_` class and the `Calibration` class. This analysis documents their usage and provides recommendations for cleanup.

## Duplicate State Variables

### 1. retractingTL/TR/BL/BR Flags

#### Locations:
- **Maslow.h (lines 144-147)**: Public member variables of `Maslow_` class
- **Calibration.h (lines 113-116)**: Private member variables of `Calibration` class

#### Usage Analysis:

**In Calibration class (Primary Usage):**
- `Calibration.cpp:70-73`: Set to `true` when entering RETRACTING state
- `Calibration.cpp:297-327`: Used in `home()` function to track which belts are still retracting
- `Calibration.cpp:327`: Checked to determine when all belts are retracted (transition to RETRACTED state)
- `Calibration.cpp:131-134, 263-266, 1494-1497`: Reset to `false` in various state transitions
- `Calibration.cpp:404`: Checked in safety control logic

**In Maslow class (Minimal Usage):**
- `Maslow.cpp:242`: Used to determine if cooling fan should stay on
- `Maslow.cpp:1002-1005`: Reset to `false` in `stop()` function

**Recommendation:** The Maslow class versions are **REDUNDANT**. They can be removed since:
1. The Calibration class manages the retracting process via state machine
2. Maslow class only uses them for cooling fan control, which could check `calibration.getCurrentState() == RETRACTING` instead

---

### 2. extendedTL/TR/BL/BR Flags

#### Locations:
- **Maslow.h (lines 149-152)**: Public member variables of `Maslow_` class
- **Calibration.h (lines 122-125)**: Private member variables of `Calibration` class
- **Maslow.h (lines 69-72)**: Public members of `TelemetryData` struct

#### Usage Analysis:

**In Calibration class (Primary Usage):**
- `Calibration.cpp:50-53`: Set via `setExtendedState()` when restoring from NVS
- `Calibration.cpp:102-105`: Reset to `false` when entering EXTENDING state
- `Calibration.cpp:301, 308, 315, 322`: Set to `false` when each belt finishes retracting
- `Calibration.cpp:346-354`: Used in EXTENDING state to track which belts have extended
- `Calibration.cpp:1609`: `allAxisExtended()` checks if all are `true`

**In Maslow class (Telemetry Only):**
- `Maslow.cpp:1224-1227`: Copied to telemetry data structure
- `Maslow.cpp:1174, 1186-1187`: Logged in telemetry CSV output

**Recommendation:** The Maslow class versions are **PARTIALLY REDUNDANT**. They are only used for telemetry logging. Options:
1. Keep for telemetry but rename to make purpose clear (e.g., `telemetry_extendedTL`)
2. Remove and access calibration data directly for telemetry
3. Keep as-is for performance (avoids accessing private Calibration members)

---

### 3. extendingALL Flag

#### Locations:
- **Maslow.h (line 154)**: Public member variable of `Maslow_` class
- **Calibration.h (line 126)**: Private member variable of `Calibration` class (marked for deletion with comment!)

#### Usage Analysis:

**In Calibration class:**
- `Calibration.cpp:99`: Set to `true` when entering EXTENDING state (comment says "should be replaced by state variables")
- `Calibration.cpp:355`: Set to `false` when all belts are extended
- `Calibration.cpp:75, 135, 267, 1498`: Reset to `false` in various state transitions
- `Calibration.cpp:404`: Checked in safety control logic

**In Maslow class:**
- `Maslow.cpp:242`: Used to determine if cooling fan should stay on
- `Maslow.cpp:1006`: Reset to `false` in `stop()` function
- `Maslow.cpp:1228`: Copied to telemetry data

**Recommendation:** This flag is **COMPLETELY REDUNDANT** with the state machine. The code comment on line 126 of Calibration.h explicitly says "This is replaced by the state machine. Delete". Both versions can be replaced with:
- Check: `calibration.getCurrentState() == EXTENDING`

---

### 4. complyALL Flag

#### Locations:
- **Maslow.h (line 155)**: Public member variable of `Maslow_` class
- **Calibration.h (line 127)**: Private member variable of `Calibration` class

#### Usage Analysis:

**In Calibration class:**
- `Calibration.cpp:268`: Set to `true` when entering RELEASE_TENSION state
- `Calibration.cpp:384`: Set to `false` when exiting RELEASE_TENSION state
- `Calibration.cpp:1499`: Set to `true` in `comply()` function
- `Calibration.cpp:74, 136, 404`: Reset/checked in various state transitions

**In Maslow class:**
- `Maslow.cpp:242`: Used to determine if cooling fan should stay on (but cooling fan logic doesn't actually check this!)
- `Maslow.cpp:1007`: Reset to `false` in `stop()` function
- `Maslow.cpp:1229`: Copied to telemetry data

**Recommendation:** The Maslow class version is **REDUNDANT**. Can be replaced with:
- Check: `calibration.getCurrentState() == RELEASE_TENSION`

---

## Cleanup Recommendations

### High Priority (Safe to Remove)
1. **Maslow.retractingTL/TR/BL/BR** - Replace line 242 check with `calibration.getCurrentState() == RETRACTING`
2. **Maslow.extendingALL** - Replace usage with `calibration.getCurrentState() == EXTENDING`
3. **Calibration.extendingALL** - Replace with state machine checks

### Medium Priority (Consider for Cleanup)
4. **Maslow.complyALL** - Replace with `calibration.getCurrentState() == RELEASE_TENSION`
5. **Calibration.complyALL** - Evaluate if needed beyond state machine

### Low Priority (Keep for Now)
6. **Maslow.extendedTL/TR/BL/BR** - Currently used for telemetry, could refactor later

## Benefits of Cleanup
1. **Reduced Code Complexity**: Fewer variables to track and maintain
2. **Single Source of Truth**: State machine becomes the authoritative source
3. **Easier Debugging**: No risk of state variables getting out of sync
4. **Smaller Memory Footprint**: Removing ~12 bool variables (though minimal impact)

## Implementation Notes
- The state machine (via `currentState` and `requestStateChange()`) is already the primary state tracking mechanism
- Most of these flags were created before the state machine was fully implemented
- The code comments (e.g., "This should be replaced by state variables") confirm this redundancy was known
- Changes should be made incrementally with testing after each removal
