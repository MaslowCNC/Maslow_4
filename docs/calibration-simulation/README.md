# Maslow CNC Calibration Simulators

**[🚀 Launch the Machine Simulator](index.html)** | **[📊 Launch the Data Parser](data-parser.html)**

This directory contains two calibration simulators for the Maslow CNC machine, both using the shared calibration computation library to ensure identical behavior with the actual machine.

## Two Simulators

### 1. Machine Simulator (`index.html`)
Provides an accurate simulation of the complete Maslow CNC machine calibration process with **synthetic data**. 

**Use this for:**
- Testing calibration algorithm changes
- Understanding how calibration works
- Developing new features
- Validating algorithm behavior with controlled data

[📖 Full Documentation](README.md#machine-simulator)

### 2. Data Parser (`data-parser.html`)
Processes **real calibration measurement data** pasted by the user to compute optimal anchor positions.

**Use this for:**
- Analyzing actual calibration data from your machine
- Troubleshooting calibration issues
- Testing different initial anchor guesses
- Verifying calibration results

[📖 Full Documentation](DATA-PARSER.md)

## Quick Start

**Machine Simulator:**
1. Open `index.html`
2. Configure machine parameters
3. Click "Start Calibration Simulation"
4. Watch the simulated calibration process

**Data Parser:**
1. Open `data-parser.html`
2. Paste your measurement data
3. Click "Parse and Validate"
4. Click "Compute Anchor Positions"

## Code Sharing

Both simulators run `calibration-computation.js` in this folder, a JavaScript port of the
calibration math the machine actually runs. That math lives in the firmware, in
`Calibration::recomputeAnchorsWithLevenbergMarquardt()`
(`firmware/FluidNC/src/Maslow/Calibration.cpp`) — the firmware is the source of truth, and
this file is a port kept in step with it by hand.

The port used to be shipped to the machine as well, at
`ESP3D-WEBUI/www/js/calibration-computation.js`. It no longer is: once calibration moved
into C++, the web UI stopped calling the JS solver entirely, so it was deleted from the
bundle to keep `index.html.gz` small. This copy is for development and simulation only.

⚠️ **If you change the firmware routine, update this file to match** — nothing enforces it
automatically, and a stale port makes the simulator disagree with the real machine.

---

# Machine Simulator

The machine simulator provides an accurate representation of the Maslow CNC machine's calibration process with synthetic measurement data.

## Key Features

### Accurate Machine Simulation
- **State Machine Behavior**: Mimics the exact calibration state machine from `Calibration.cpp`
- **Grid Generation**: Replicates the spiral pattern grid generation algorithm
- **Chunked Data Flow**: Simulates how measurements are collected and sent in stages (matching `recomputePoints`)
- **Measurement Accuracy**: Models real-world measurement errors and Z-plane projection
- **Non-Rectangular Frame**: Simulates realistic frame imperfections (±20mm variations) instead of perfect rectangles

### Browser-Side Computation
- **Iterative Optimization**: Implements the same "magnetically attracted lines" algorithm the firmware uses
- **Multi-Stage Processing**: Accurately simulates the multiple computation stages that occur during calibration
- **Fitness Tracking**: Shows how anchor position estimates improve over time

### Real-Time Visualization
- **Machine State**: Shows calibration grid, current waypoint, and true anchor positions
- **Computation Progress**: Displays fitness evolution and computed anchor positions
- **Error Analysis**: Compares computed positions with actual positions at completion

## How It Works

### Calibration Process Flow

1. **Grid Generation** (Machine Side)
   - Generates a calibration grid based on frame size and grid density
   - Creates waypoints in a spiral pattern starting from center
   - Defines recompute points where computation should be triggered

2. **Measurement Collection** (Machine Side)
   - Moves to each waypoint in sequence
   - Takes measurements of belt lengths to all four anchors
   - Projects measurements to XY plane (accounting for Z-height)
   - Adds configurable measurement error

3. **Data Transmission** (Machine → Browser)
   - Sends measurement chunks via `CLBM:` format messages
   - Triggered at specific waypoints (recomputePoints)
   - Mimics the actual serial communication protocol

4. **Anchor Computation** (Browser Side)
   - Receives measurement chunk
   - Runs iterative optimization to find best anchor positions
   - Uses "line walking" algorithm to minimize endpoint distances
   - Updates anchor position estimates

5. **Multi-Stage Refinement**
   - Process repeats for each stage (typically 4-5 stages)
   - Each stage uses more measurements for better accuracy
   - Final stage uses all measurements for best fit

## Usage

### Opening the Simulator

Simply open `index.html` in a modern web browser. No server or build process is required.

### Configuration Options

**Machine Configuration:**
- **Frame Width/Height**: The actual dimensions of the machine frame (500-5000mm)
- **Grid Size**: 
  - `Auto`: Automatically selects grid density based on max spacing
  - `3x3` to `9x9`: Manual selection of calibration point density

**Simulation Settings:**
- **Measurement Error**: Random error added to each measurement (0-5mm)
  - Models encoder inaccuracy, belt stretch, frame flex, etc.
  - Higher values make calibration more challenging
- **Simulation Speed**: 
  - `Real-time`: Matches actual machine timing
  - `10x` to `100x`: Faster simulation for quick testing
  - `Instant`: No delays, completes as fast as possible
- **Orientation**: `Vertical` or `Horizontal` machine mounting

### Running a Simulation

1. Configure the machine parameters to match your setup
2. Adjust simulation settings as desired
3. Click "Start Calibration Simulation"
4. Watch the visualization as calibration progresses
5. Review the log for detailed progress information
6. Check final error measurements when complete

## Understanding the Output

### Machine Visualization (Left Panel)
- **Green dots**: True anchor positions (what we're trying to find)
- **Red dot**: Current measurement waypoint
- **Blue dots**: Already measured waypoints
- **Gray dots**: Future waypoints
- **Blue line**: Path taken through calibration grid

### Computation Progress (Right Panel)
- **Fitness Over Time**: Shows how well the anchor estimates match measurements
  - Higher is better (perfect fit = 1.0)
  - Typically converges to 0.99+
- **Anchor Positions**: Current best estimates for each anchor
- **Iteration Count**: Number of optimization iterations performed

### Log Output
- **Measurement progress**: "Waypoint X/Y completed"
- **Computation stages**: "Sending N measurements for computation (Stage X)"
- **Fitness scores**: "Computation complete: Fitness = X.XXXXXX"
- **Final results**: Computed positions and errors vs actual positions

## Technical Details

### Key Differences from Original Simulation

The original simulation (https://github.com/BarbourSmith/Calibration-Simulation/) processes all measurements at once. This simulation accurately models:

1. **Staged Computation**: Anchor positions are recomputed multiple times as more data arrives
2. **Data Chunking**: Measurements sent in groups matching firmware behavior
3. **Iterative Refinement**: Each stage builds on previous results
4. **Protocol Accuracy**: Uses actual `CLBM:` message format

### Algorithm Overview

The calibration algorithm works by:

1. Drawing "lines" from each anchor point with length equal to measured belt length
2. Adjusting line angles to make all four lines meet at a single point
3. Finding anchor positions that minimize the distance between line endpoints
4. Using an iterative "hill climbing" approach to optimize positions

This "magnetically attracted lines" approach is robust to measurement errors and works well even with poor initial guesses.

### Performance Characteristics

- **Grid Size**: 3x3 (13 points) to 9x9 (81 points)
- **Computation Time**: ~1000-5000 iterations per stage
- **Typical Accuracy**: <5mm position error with 0.5mm measurement error
- **Stages**: 4-5 recomputation stages depending on grid size

## Use Cases

### Development and Testing
- Test calibration algorithm changes without hardware
- Explore impact of measurement errors
- Validate grid generation logic
- Debug computation convergence issues

### Education and Demonstration
- Understand how Maslow calibration works
- Visualize the iterative refinement process
- See impact of different parameters
- Learn about the algorithm's robustness

### Quality Assurance
- Verify calibration produces accurate results
- Test edge cases (extreme errors, unusual frame sizes)
- Benchmark computation performance
- Validate protocol changes

## Files

- `index.html`: Main simulator interface
- `machine-simulator.js`: ESP32 firmware simulation
- `computation-simulator.js`: Wrapper that uses the shared computation library
- `visualization.js`: Canvas-based rendering
- `main.js`: Orchestration and UI management
- `test.html`: Test page for verification
- `README.md`: This documentation

**Computation library:**
- `calibration-computation.js`: JavaScript port of the firmware's calibration math, used by
  every page in this folder
- `firmware-lm.selftest.js`: Node self-test for the Levenberg-Marquardt port
  (`node docs/calibration-simulation/firmware-lm.selftest.js`)
- `algorithm-comparison.test.js`: Node comparison of the two solver strategies

## Architecture

### Where the math lives

The machine runs its calibration solver in C++, in
`Calibration::recomputeAnchorsWithLevenbergMarquardt()`
(`firmware/FluidNC/src/Maslow/Calibration.cpp`). The web UI does not compute anchor
positions at all any more — it starts calibration and waits for the firmware's
`Calibration complete` message.

`calibration-computation.js` is a hand-maintained JavaScript port of that C++ routine,
so these tools can reproduce and inspect what the machine does without a machine
attached. It is loaded only by pages in this folder and is never shipped to the device.

**Keeping the port honest:** the firmware is authoritative, and the port only tracks it
because someone keeps it in step. When you change the C++ routine, mirror the change here
and re-run the self-test.

### Diagram

```
firmware/FluidNC/src/Maslow/Calibration.cpp   ← SOURCE OF TRUTH (runs on the machine)
        │
        │ hand-maintained port
        ▼
docs/calibration-simulation/
├── calibration-computation.js   ← JS port, dev/simulation only
├── index.html          ─┐
├── data-parser.html     ├─ load calibration-computation.js
├── test.html           ─┘
├── computation-simulator.js (thin wrapper)
├── machine-simulator.js
└── visualization.js

ESP3D-WEBUI/  ← ships index.html.gz; contains no calibration solver
```

## Limitations

This simulation does not model:
- Belt tension dynamics
- Motor current sensing
- Actual belt movement physics
- Network latency
- Serial communication errors
- Multiple measurement runs per waypoint

These factors exist in the real machine but don't significantly affect the calibration algorithm's behavior.

## Frame Imperfections

The simulator now includes realistic frame imperfections to better match real-world conditions:
- Each simulation run generates a non-rectangular frame with random variations (±20mm per anchor)
- The visualization shows both the actual frame shape (solid line) and ideal rectangle (dashed line)
- This helps test the calibration algorithm's robustness to frame irregularities
- The bottom-left anchor (BL) is kept at (0, 0) as a reference point

## Future Enhancements

Potential improvements:
- Export/import measurement datasets
- Compare multiple calibration runs
- Visualize line intersection during computation
- Add noise profiles (systematic vs random errors)
- Animate the optimization process
- Support for non-rectangular frames
- Historical fitness comparison

## Related Resources

- **Original Simulation**: https://github.com/BarbourSmith/Calibration-Simulation/
- **FluidNC Firmware**: `firmware/FluidNC/src/Maslow/Calibration.cpp`
- **JS Port of the Algorithm**: `docs/calibration-simulation/calibration-computation.js`
- **Maslow Forum**: https://forums.maslowcnc.com/

## License

This simulation is part of the Maslow CNC project and follows the same open-source license as the main repository.
