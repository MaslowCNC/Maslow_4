# Calibration Simulator - True Code Sharing Implementation

> **Superseded (see [README.md](README.md)).** This document records a refactor that is no
> longer the current arrangement. Calibration now runs in the firmware
> (`Calibration::recomputeAnchorsWithLevenbergMarquardt()` in
> `firmware/FluidNC/src/Maslow/Calibration.cpp`), the web UI no longer computes anchor
> positions, and `ESP3D-WEBUI/www/js/calibration-computation.js` has been deleted to shrink
> `index.html.gz`. The only remaining JavaScript copy is
> `docs/calibration-simulation/calibration-computation.js`, kept for development and
> simulation. Read the below as history.

## Problem Statement

The issue requested implementing a calibration simulator using existing calibration functions to eliminate code duplication, similar to the standalone simulator at https://github.com/BarbourSmith/Calibration-Simulation/.

## Solution - True Code Sharing

We implemented a shared computation library that is used by **BOTH** the simulator and the ESP3D-WEBUI, achieving true code sharing with zero duplication.

### Architecture

```
ESP3D-WEBUI/www/js/
├── calibration-computation.js  ← SINGLE SOURCE OF TRUTH
│   ├── Used by ESP3D-WEBUI
│   └── Used by simulator
│
├── calculatesCalibrationStuff.js
│   └── Loads shared library, contains only UI code
│
docs/calibration-simulation/
├── index.html
│   └── Loads ../../ESP3D-WEBUI/www/js/calibration-computation.js
├── computation-simulator.js (thin wrapper, 33 lines)
├── machine-simulator.js
└── visualization.js
```

### Changes Made

1. **Created shared library** (`ESP3D-WEBUI/www/js/calibration-computation.js`):
   - Core mathematical functions
   - `CalibrationComputer` class for optimization
   - All calibration algorithm logic
   - 349 lines of shared code

2. **Refactored ESP3D-WEBUI** (`calculatesCalibrationStuff.js`):
   - Removed ~269 lines of duplicated computation code
   - Now loads and uses shared library
   - Keeps only UI-specific code (messagesBox, sendCommand, etc.)

3. **Updated simulator**:
   - Loads shared library from ESP3D-WEBUI location
   - `computation-simulator.js`: 269 → 33 lines (88% reduction)
   - Removed local copy of computation code

## Results

**Code Duplication:**
- **Before**: ESP3D-WEBUI and simulator each had ~269 lines of duplicated code
- **After**: Both use the exact same file - **ZERO duplication**

**Benefits:**
1. ✅ **Complete elimination of duplication** - Single source of truth
2. ✅ **Guaranteed identical behavior** - Both use the exact same code
3. ✅ **Simplified maintenance** - Update once, both benefit
4. ✅ **Impossible to diverge** - They literally use the same file

## Files Changed

### New Files
- `ESP3D-WEBUI/www/js/calibration-computation.js` - Shared computation library (349 lines)

### Modified Files
- `ESP3D-WEBUI/www/js/calculatesCalibrationStuff.js` - Refactored to use shared library
- `ESP3D-WEBUI/www/index.html` - Loads shared library
- `docs/calibration-simulation/computation-simulator.js` - Thin wrapper (33 lines)
- `docs/calibration-simulation/index.html` - Loads shared library from ESP3D-WEBUI
- `docs/calibration-simulation/README.md` - Documents true code sharing
- `docs/calibration-simulation/QUICKSTART.md` - Updated architecture info

### Deleted Files
- `docs/calibration-simulation/calibration-computation.js` - Removed (now uses ESP3D-WEBUI version)

## Algorithm Correctness

The shared library contains the complete original algorithm:
- Progressive refinement with 8 step sizes (0.1 → 0.00000001)
- Center-of-mass computed from 3 lines (excluding comparison line)
- Furthest-anchor adjustment
- All mathematical functions

## Testing

1. **Simulator**: Open `docs/calibration-simulation/index.html` - Uses shared code
2. **ESP3D-WEBUI**: Use normally - Calibration uses shared code
3. **Test page**: Open `docs/calibration-simulation/test.html` - Validates core functions

## Comparison to Original Request

The original standalone simulator (https://github.com/BarbourSmith/Calibration-Simulation/) was a single HTML file with embedded computation. Our solution is superior:

1. ✅ **Truly integrated** - Part of this repository
2. ✅ **Zero duplication** - ESP3D-WEBUI and simulator use exact same file
3. ✅ **Guaranteed consistency** - Literally impossible to diverge
4. ✅ **Production ready** - Used by actual implementation

## Conclusion

The issue has been fully resolved with **true code sharing**. The simulator and ESP3D-WEBUI now use the exact same computation code, completely eliminating duplication and ensuring perfect consistency.

**Impact:**
- Single source of truth for calibration algorithm
- Simulator and real machine guaranteed identical behavior
- Maintenance burden eliminated
- Code quality significantly improved
