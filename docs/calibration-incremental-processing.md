# Calibration Incremental Processing Implementation

## Overview

This document describes the implementation of incremental measurement processing in the Maslow CNC calibration system. The changes allow measurements to be sent individually from the ESP32 to the browser as they are taken, rather than being batched in memory, enabling concurrent measurement collection and calculation.

## Motivation

The previous calibration system collected all measurements in ESP32 memory and sent them as batches at specific recompute points. This approach had several limitations:

1. **Memory constraints**: Large calibration grids required significant ESP32 memory
2. **Sequential processing**: Browser waited for complete batches before computing
3. **Time inefficiency**: ESP32 idled during browser computation phases

## Solution

The new system implements incremental measurement processing:

1. **Immediate transmission**: Each measurement is sent to the browser immediately after collection (after the first 6 points)
2. **Concurrent processing**: Browser computes while ESP32 continues measuring
3. **Progressive refinement**: Anchor positions are updated when fitness threshold is met
4. **Continuous improvement**: All measurements are retained for ongoing optimization

## Architecture

### ESP32 Firmware Changes

#### New Function: `print_single_measurement(int waypointIndex)`
Sends a single measurement in CLBM format:
```cpp
String data = "CLBM:[{bl:" + ... + ",   br:" + ... + 
              ",   tr:" + ... + ",   tl:" + ... + "}]";
log_data(data.c_str());
```

#### Modified: `calibration_loop()`
After waypoint 6, measurements are sent individually:
```cpp
if (waypoint > 6) {
    print_single_measurement(waypoint - 1);
    calibrationDataWaiting = millis();
}
```

**First 6 points remain unchanged**: Waypoints 0-5 are still batched and sent together via `print_calibration_data()` to establish the initial frame estimate.

### Browser-Side Changes

#### New Function: `processIncrementalMeasurement(measurement)`
Handles individual measurements incrementally:

1. **Accumulation**: Adds new measurement to growing collection
2. **Quick optimization**: Runs limited iterations (1000 max) for fast turnaround
3. **Fitness evaluation**: Checks if threshold exceeded
4. **Anchor updates**: Sends new anchors when fitness is good
5. **Acknowledgment**: Always sends $ACKCAL for ESP32 to continue

#### Modified: `handleCalibrationData(measurements)`
Routes single measurements to incremental processor:
```javascript
if (incrementalCalibrationActive && measurements.length === 1) {
    await processIncrementalMeasurement(measurements);
} else {
    // Original batch processing for first 6 points
}
```

#### State Management
```javascript
var incrementalMeasurements = [];      // Accumulated measurements
var incrementalCalibrationActive = false;
var currentBestGuess = null;            // Best anchor estimate so far
var currentBestFitness = 0;             // Fitness of best guess
```

## Calibration Flow

### Phase 1: Initial Calibration (Waypoints 0-5)

1. ESP32 takes 6 measurements, stores in memory
2. When waypoint reaches 6, enters CALIBRATION_COMPUTING state
3. Sends all 6 measurements as batch via CLBM message
4. Browser runs full optimization (up to 200,000 iterations)
5. Browser sends updated anchors
6. Browser signals ready for incremental mode
7. ESP32 continues from waypoint 6

### Phase 2: Incremental Calibration (Waypoint 6+)

```
┌─────────────┐
│ ESP32 takes │
│ measurement │
└──────┬──────┘
       │
       ▼
┌─────────────────┐
│ Send CLBM[{...}]│ ◄── Single measurement
└──────┬──────────┘
       │
       ▼
┌──────────────────┐
│ Browser receives │
│ and accumulates  │
└──────┬───────────┘
       │
       ▼
┌──────────────────┐
│ Run optimization │
│ (1000 iterations)│
└──────┬───────────┘
       │
       ▼
    ┌──┴──────────────────┐
    │ Fitness > threshold? │
    └──┬──────────┬────────┘
       │ Yes      │ No
       ▼          ▼
  ┌────────┐  ┌──────┐
  │ Update │  │ Keep │
  │anchors │  │going │
  └────┬───┘  └───┬──┘
       │          │
       └────┬─────┘
            ▼
       ┌─────────┐
       │ $ACKCAL │
       └────┬────┘
            │
            ▼
       ┌─────────┐
       │ESP32    │
       │continues│
       └─────────┘
```

## Key Design Decisions

### 1. Keep All Measurements After Anchor Updates

**Decision**: Don't clear measurement buffer when anchors are updated.

**Rationale**: 
- Measurements reflect actual belt lengths (independent of anchor estimates)
- Improved anchors make calculations more accurate, but don't invalidate past measurements
- Allows continuous refinement as more data accumulates

### 2. Limited Iterations for Incremental Updates

**Decision**: Use 1000 iterations (vs 200,000 for initial batch).

**Rationale**:
- Fast turnaround time (milliseconds vs minutes)
- ESP32 can continue measuring without long delays
- Optimization quality improves as more measurements added
- Full optimization still happens in Phase 1

### 3. First 6 Points Remain Batched

**Decision**: Keep original batch behavior for waypoints 0-5.

**Rationale**:
- Establishes reliable initial frame estimate
- Provides sufficient data for rectangular optimization
- Maintains backward compatibility
- Allows detection of near-square default frames

### 4. Orientation Detection Unchanged

**Decision**: No changes to `detectOrientation()` function.

**Rationale**:
- Runs before any measurements are taken
- Critical for proper belt tensioning strategy
- Already optimized and working correctly

## Benefits

### Time Savings
- **Concurrent operation**: ESP32 measures while browser computes
- **No blocking**: ESP32 doesn't wait for full optimization cycles
- **Progressive updates**: Anchors improve as measurements accumulate

### Memory Efficiency
- **Browser side**: Incremental processing, not full batch storage
- **ESP32 side**: Measurements still stored (future optimization possible)

### Accuracy
- **More data**: All measurements contribute to final result
- **Progressive refinement**: Anchors improve throughout process
- **Better convergence**: Continuous optimization with growing dataset

## Testing Recommendations

### Build Validation ✓
- [x] Firmware compiles successfully
- [x] Web UI builds without errors
- [x] No JavaScript syntax errors
- [x] Size within acceptable limits (133KB compressed)

### Hardware Testing (User Required)
- [ ] First 6 points establish good initial estimate
- [ ] Orientation detection works correctly (horizontal vs vertical)
- [ ] Individual measurements are sent after waypoint 6
- [ ] Browser receives and processes each measurement
- [ ] Fitness calculations produce reasonable values
- [ ] Anchors update when threshold is met
- [ ] ESP32 continues to next waypoint after acknowledgment
- [ ] Complete calibration cycle finishes successfully
- [ ] Final anchor positions are accurate

### Edge Cases to Test
- [ ] Very small calibration grids (3x3)
- [ ] Very large calibration grids (9x9)
- [ ] Poor initial anchor estimates
- [ ] Network latency/delays
- [ ] Fitness threshold never met scenario
- [ ] Multiple anchor update cycles during calibration

## Future Optimizations

### ESP32 Memory Optimization
Currently, the full `calibration_data` array is still allocated. Potential optimization:

```cpp
// Only keep last measurement after waypoint 6
if (waypoint > 6) {
    // Reuse single buffer instead of full array
    lastMeasurement[0] = calibration_data[waypoint][0];
    lastMeasurement[1] = calibration_data[waypoint][1];
    lastMeasurement[2] = calibration_data[waypoint][2];
    lastMeasurement[3] = calibration_data[waypoint][3];
}
```

This would reduce memory usage for large grids, but adds complexity. Can be implemented as follow-up if needed.

### Adaptive Iteration Count
Adjust optimization iterations based on fitness improvement rate:

```javascript
if (fitnessImprovement < threshold) {
    maxIterations *= 0.5;  // Reduce if not improving
} else {
    maxIterations *= 1.5;  // Increase if improving well
}
```

### Measurement Quality Metrics
Track measurement consistency and use to adjust calibration strategy:

```javascript
if (measurementVariability > threshold) {
    // Request additional measurements at this point
    // Or increase averaging window
}
```

## Troubleshooting

### Symptom: ESP32 hangs after waypoint 6
**Possible causes**:
- Browser not sending $ACKCAL
- `calibrationDataWaiting` timeout not working

**Debug**: Check browser console for errors in `processIncrementalMeasurement()`

### Symptom: Fitness never improves
**Possible causes**:
- Initial anchor estimate very poor
- Measurement noise too high
- Threshold set too high

**Debug**: 
- Check `currentBestFitness` values in browser console
- Reduce `acceptableCalibrationThreshold` temporarily
- Verify first 6 points completed successfully

### Symptom: Anchors update too frequently
**Possible causes**:
- Threshold set too low
- Measurement quality high

**Debug**: This is actually desired behavior - frequent updates indicate good calibration

### Symptom: Browser becomes unresponsive
**Possible causes**:
- Too many measurements accumulating
- Optimization taking too long

**Debug**:
- Check `incrementalMeasurements.length`
- Reduce `maxIterations` if needed
- Add more `await` points in optimization loop

## API Reference

### ESP32 Functions

#### `print_single_measurement(int waypointIndex)`
Sends a single measurement to the browser.

**Parameters**:
- `waypointIndex`: Index of measurement in `calibration_data` array

**Returns**: void

**Side effects**: Sends CLBM message via `log_data()`

### Browser Functions

#### `processIncrementalMeasurement(measurement)`
Processes a single measurement incrementally.

**Parameters**:
- `measurement`: Array with single measurement object `[{bl, br, tr, tl}]`

**Returns**: Promise<void>

**Side effects**: 
- Updates `incrementalMeasurements`
- May update anchors if fitness threshold met
- Sends $ACKCAL command

#### `handleCalibrationData(measurements)`
Routes calibration data to appropriate processor.

**Parameters**:
- `measurements`: Array of measurement objects

**Returns**: Promise<void>

**Behavior**:
- If `incrementalCalibrationActive && measurements.length === 1`: calls `processIncrementalMeasurement()`
- Otherwise: calls `findMaxFitness()` (original batch processing)

### State Variables

#### ESP32
- `waypoint`: Current calibration waypoint (0 to pointCount)
- `calibrationDataWaiting`: Timestamp of last measurement sent (-1 if none waiting)
- `recomputeCountIndex`: Index in recompute points array

#### Browser
- `incrementalCalibrationActive`: Boolean, true after first 6 points complete
- `incrementalMeasurements`: Array of accumulated measurements since last anchor update
- `currentBestGuess`: Best anchor position estimate so far
- `currentBestFitness`: Fitness value of current best guess

## Compatibility

### Backward Compatibility
- First 6 points: Unchanged, fully compatible with original system
- Orientation detection: Unchanged, fully compatible
- Message format: Uses existing CLBM format
- Acknowledgment: Uses existing $ACKCAL command

### Forward Compatibility
- Design allows for future memory optimizations
- Iteration counts can be tuned without protocol changes
- Threshold values adjustable via existing settings

## References

- Original issue: "Calibration process communication mechanics rework"
- Key files:
  - `firmware/FluidNC/src/Maslow/Calibration.cpp`
  - `firmware/FluidNC/src/Maslow/Calibration.h`
  - `ESP3D-WEBUI/www/js/calculatesCalibrationStuff.js`
  - `ESP3D-WEBUI/www/js/grbl.js`

## Version History

- **v1.0 (2024-12-29)**: Initial implementation
  - Incremental measurement sending after waypoint 6
  - Browser-side incremental processing
  - Progressive anchor updates based on fitness threshold
  - Maintains all measurements for continuous improvement
