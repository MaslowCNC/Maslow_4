# Telemetry System Report

## Overview

This report documents the telemetry system implemented in FluidNC for Maslow CNC machines. The telemetry system provides comprehensive data logging capabilities for monitoring machine performance, motor behavior, and system state during operation.

## How Telemetry is Enabled

### Enabling Methods

Telemetry can be enabled through two user commands:

1. **Toggle Command**: `$TELEM` or `Maslow/setTelemetry`
   - Toggles telemetry on/off when called without parameters
   - Command implementation in `FluidNC/src/ProcessSettings.cpp` (lines 856-865)

2. **Explicit Set Command**: `$TELEM=1` or `$TELEM=0`
   - `$TELEM=1` - Enables telemetry
   - `$TELEM=0` - Disables telemetry

### Implementation Details

The telemetry system is controlled through the `Maslow_::set_telemetry(bool enabled)` function in `FluidNC/src/Maslow/Maslow.cpp` (lines 1143-1168):

- **When enabled**: Creates a new telemetry file with a header containing structure metadata
- **When disabled**: Stops writing telemetry data (file remains on SD card)
- The telemetry gathering task runs continuously in the background but only writes data when enabled

## Default State

**Telemetry is DISABLED by default.**

Evidence:
- `telemetry_enabled` is initialized to `false` in `FluidNC/src/Maslow/Maslow.h` (line 157)
- The telemetry task (`telemetry_loop`) always runs after system startup (started in `protocol_main_loop()` at `FluidNC/src/Protocol.cpp` line 305)
- The task only writes data when `Maslow.telemetry_enabled` is `true`

## Telemetry Filename

**Filename**: `M4_telemetry.bin`

Defined as a constant in `FluidNC/src/Maslow/Maslow.h` (line 27):
```cpp
#define MASLOW_TELEM_FILE "M4_telemetry.bin"
```

The file is stored on the SD card and written in binary format for efficiency.

## Data Structure

The telemetry file uses a binary format with two main structures:

### 1. TelemetryFileHeader Structure

Located at the beginning of the file (`FluidNC/src/Maslow/Maslow.h` lines 34-39):

```cpp
struct TelemetryFileHeader {
    unsigned int structureSize;  // 4 bytes - Size of TelemetryData structure
    char         version[10];    // 10 bytes - Firmware version string
    char         _unused[64];    // 64 bytes - Reserved for future expansion
};
```

**Total size**: 78 bytes

**Fields**:
- `structureSize` (4 bytes): Size in bytes of the `TelemetryData` structure. This allows the reader to adapt to different versions of the data structure.
- `version` (10 bytes): Null-terminated string containing the firmware version number (from `VERSION_NUMBER`)
- `_unused` (64 bytes): Reserved space for future header expansion

### 2. TelemetryData Structure

Contains the actual telemetry samples (`FluidNC/src/Maslow/Maslow.h` lines 41-100):

```cpp
struct TelemetryData {
    unsigned long timestamp;      // Milliseconds since boot
    
    // Motor currents (mA)
    double tlCurrent;
    double trCurrent;
    double blCurrent;
    double brCurrent;
    
    // Motor power (PWM duty cycle)
    double tlPower;
    double trPower;
    double blPower;
    double brPower;
    
    // Belt speeds (mm/s)
    double tlSpeed;
    double trSpeed;
    double blSpeed;
    double brSpeed;
    
    // Belt positions (mm)
    double tlPos;
    double trPos;
    double blPos;
    double brPos;
    
    // Motor states
    int tlState;
    int trState;
    int blState;
    int brState;
    
    // Extension flags
    bool extendedTL;
    bool extendedTR;
    bool extendedBL;
    bool extendedBR;
    
    // Global state flags
    bool extendingALL;
    bool complyALL;
    bool takeSlack;
    bool safetyOn;
    
    // Target positions (mm)
    double targetX;
    double targetY;
    double targetZ;
    
    // Actual positions (mm)
    double x;
    double y;
    double z;
    
    // Calibration data
    bool          test;
    int           pointCount;
    int           waypoint;
    int           calibrationGridSize;
    unsigned long holdTimer;
    bool          holding;
    unsigned long holdTime;
    float         centerX;
    float         centerY;
    
    // Timing diagnostics (milliseconds)
    unsigned long lastCallToPID;
    unsigned long lastCallToUpdate;
    unsigned long lastMiss;
    unsigned long extendCallTimer;
    unsigned long complyCallTimer;
};
```

**Motor labels**:
- `tl` = Top Left
- `tr` = Top Right
- `bl` = Bottom Left
- `br` = Bottom Right

## File Format

The telemetry file follows this structure:

```
[TelemetryFileHeader - 78 bytes]
[TelemetryData Sample 1 - sizeof(TelemetryData) bytes]
[TelemetryData Sample 2 - sizeof(TelemetryData) bytes]
[TelemetryData Sample 3 - sizeof(TelemetryData) bytes]
...
```

## Use of Telemetry Data

### Purpose

The telemetry system serves multiple purposes:

1. **Performance Analysis**: Monitor motor currents, power consumption, and belt speeds during operation
2. **Debugging**: Capture system state for troubleshooting issues
3. **Calibration Verification**: Record data during calibration procedures
4. **Position Tracking**: Log target vs. actual positions to analyze accuracy
5. **Timing Diagnostics**: Monitor system timing to detect performance issues

### Data Collection

**Collection frequency**: ~2 Hz (samples collected every 500ms)
- Implemented in `telemetry_loop()` in `FluidNC/src/Maslow/Maslow.cpp` (line 1334)
- Runs on the utility core (separate from main control loop)

**Buffering**: 
- Uses a 5000-byte buffer to batch writes to SD card
- Reduces SD card write operations and improves performance
- Buffer is flushed when full or when telemetry is disabled

### Data Retrieval

**Dump Command**: `$TELEMDUMP` or `Maslow/telemetryDump`
- Reads the telemetry file and outputs data in CSV format
- Includes header row with field names
- Implemented in `Maslow_::dump_telemetry()` (lines 1273-1304)
- Can specify alternative filename or uses default `M4_telemetry.bin`

**CSV Output Format**:
The dump command outputs comma-separated values with the following columns:
```
millis,tlCurrent,trCurrent,blCurrent,brCurrent,tlPower,trPower,blPower,brPower,
tlSpeed,trSpeed,blSpeed,brSpeed,tlPos,trPos,blPos,brPos,extendedTL,extendedTR,
extendedBL,extendedBR,extendingALL,complyALL,takeSlack,safetyOn,targetX,targetY,
targetZ,x,y,test,pointCount,waypoint,calibrationGridSize,holdTimer,holding,
holdTime,centerX,centerY,lastCallToPID,lastMiss,lastCallToUpdate,
extendCallTimer,complyCallTimer
```

## Why TelemetryFileHeader Has _unused Field

### Purpose of _unused Field

The `_unused[64]` field in `TelemetryFileHeader` serves as **reserved space for future expansion**. This is a common practice in binary file formats for several important reasons:

### 1. Forward Compatibility

By reserving space in the header, future versions of the firmware can add new metadata fields without breaking compatibility with existing file readers. For example:
- Sample rate configuration
- Recording start/stop times
- Machine configuration snapshots
- Calibration parameters
- Additional version information

### 2. File Format Stability

The header size remains constant (78 bytes) even as new fields are added. This means:
- Readers can skip directly to the data section by reading exactly 78 bytes
- No need to implement complex header parsing logic
- Older tools can still read files created by newer firmware versions

### 3. Alignment and Performance

The reserved space can help with:
- Memory alignment for better CPU performance
- Consistent sector alignment on SD cards
- Easier memory mapping and buffering strategies

### 4. Design Pattern

The comment in the source code (`FluidNC/src/Maslow/Maslow.h` line 37) explicitly states:
```cpp
// if you add to the header take bytes from this
```

This indicates the intended design: when adding new header fields in the future, developers should:
1. Add the new field before `_unused`
2. Reduce the size of `_unused` by the same amount
3. Keep total header size at 78 bytes

### Example Future Expansion

If a developer wanted to add a `recordingMode` field (4 bytes), they would modify the header like this:

```cpp
struct TelemetryFileHeader {
    unsigned int structureSize;  // 4 bytes
    char         version[10];    // 10 bytes
    unsigned int recordingMode;  // 4 bytes - NEW FIELD
    char         _unused[60];    // 60 bytes (reduced from 64)
};
```

Total size remains 78 bytes, maintaining compatibility.

## Technical Implementation Details

### Thread Safety

The code includes commented-out mutex code (`telemetry_mutex`) at lines 159 and 1205 in `Maslow.h` and `Maslow.cpp`. The developers note:
- "probably need to use this for all fields in telemetry, but we'll try without first"
- Currently relies on atomic operations for simple data types
- May be enhanced with mutexes in future if race conditions are detected

### Task Architecture

The telemetry system uses FreeRTOS tasks:
- **Task Name**: "telemetry"
- **Stack Size**: 16000 bytes
- **Priority**: 1 (low priority)
- **Core**: Runs on `SUPPORT_TASK_CORE` (utility core, separate from main control)
- **Lifecycle**: Started during protocol initialization, suspended/resumed as needed

### Performance Considerations

1. **Low Priority**: Telemetry runs at low priority to avoid interfering with critical motion control
2. **Buffered Writes**: 5000-byte buffer reduces SD card write frequency
3. **Binary Format**: More compact and faster than text formats like CSV
4. **Separate Core**: Runs on utility core to prevent blocking main control loop

## Related Files

- **Definition**: `FluidNC/src/Maslow/Maslow.h` (lines 27, 34-100, 157-165, 224-225)
- **Implementation**: `FluidNC/src/Maslow/Maslow.cpp` (lines 1143-1336)
- **Commands**: `FluidNC/src/ProcessSettings.cpp` (lines 848-865, 1087-1088)
- **Task Management**: `FluidNC/src/Protocol.cpp` (lines 185-210, 305)

## Summary

The telemetry system in FluidNC provides a robust mechanism for recording detailed machine operation data. It is disabled by default to conserve SD card space but can be easily enabled via user commands. The binary file format with reserved header space ensures forward compatibility, while the buffered collection approach on a dedicated core ensures minimal impact on machine performance. The comprehensive data structure captures motor performance, position accuracy, and system timing, making it invaluable for debugging, performance analysis, and machine calibration.
