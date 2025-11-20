# FluidNC v1.15 Release Notes

**Release Date:** November 20, 2025  
**Previous Version:** v1.14 (November 14, 2025)

## Overview

Version 1.15 builds on the bug fixes from v1.14 with significant improvements to calibration accuracy, automatic orientation detection, memory optimization, and stability enhancements. This release focuses on improving the user experience for Maslow CNC machines with better automatic configuration and more reliable firmware updates.

---

## Major Features

### 🎯 Automatic Orientation Detection (PR #467, #511, #513)

The firmware now automatically detects whether the Maslow machine is mounted horizontally or vertically during calibration, eliminating the need for manual configuration.

**Key Features:**
- Automatically detects orientation by measuring belt extension when motors are powered down
- Uses gravity and belt compliance to determine if the frame is horizontal or vertical
- Integrates seamlessly into the calibration process at waypoint 0
- Stores detected orientation in `Maslow_vertical` config parameter
- Runs motor settling phase (500ms) after detection to prevent current spikes
- Configurable thresholds (default: 35mm difference indicates horizontal orientation)

**Technical Details:**
- Detection phases: retract belts → power down motors → wait for gravity → measure extension → determine orientation
- Uses 70% motor speed for 1.5 seconds during test phase
- Multiple iterations to fix edge cases including infinite loops and repeated detection

**Commits:**
- Initial implementation with NVS storage
- Changed to use config parameter instead of NVS
- Added motor settling pause to fix TL belt tension issues
- Fixed infinite loop by resetting state variables correctly
- Fixed repeated orientation detection with static flag
- Fixed automatic detection not running during calibration

### 📐 Automatic Calibration Grid Density Selection (PR #452)

The calibration system now automatically selects the optimal grid density based on frame dimensions, removing guesswork for users.

**Features:**
- Auto-calculates grid size (3x3, 5x5, 7x7, or 9x9) based on spacing constraint
- Default spacing: 0.5 units for width, 0.2 units for height
- Ensures consistent calibration point density across different frame sizes
- Prevents overly sparse or dense calibration patterns

### 🔧 Calibration Accuracy Improvements (PR #497)

Fixed calibration grid shift problem for rectangular frames that was causing accuracy issues.

**Fix:**
- Corrected spiral start position calculation
- Fixed center coordinate calculation after calibration results are loaded
- Added coordinate logging for debugging

---

## Stability & Bug Fixes

### 💥 OTA Update Crash Fix (PR #489)

Fixed critical firmware crash that occurred after OTA (over-the-air) updates due to memory pressure.

**Root Cause:** The auto-update check on boot was allocating ~18KB for JSON response parsing, causing heap fragmentation and crashes.

**Solution:**
- Implemented streaming JSON parser with 2KB sliding window (down from 18KB buffer)
- Deferred auto-update check by 30 seconds after boot to allow heap stabilization
- Only runs update check when machine is idle or in alarm state
- Improved memory allocation with pre-allocation and exception handling
- Added proper handling for escaped quotes in JSON values

**Memory Reduction:** Peak memory usage reduced from ~18KB to ~2KB during version check

### 🔄 Configuration Loading Fix (PR #481, #485, #487)

Fixed multiple issues related to configuration loading and parsing.

**PR #481: Buffer Overflow Fix**
- Fixed buffer overflow in VERSION_NUMBER when git tag doesn't match exactly
- Version string like "v1.13-47-ge6de732c" (18 chars) was overflowing 10-byte buffer
- Replaced unsafe `strcpy` with `strncpy` and null termination
- Prevented memory corruption that caused kinematics settings to become inaccessible

**PR #485: Missing MaslowKinematics**
- Added MaslowKinematics section to default configuration
- Used rounded default values for kinematics coordinates
- Fixed intermittent loading issues

**PR #487: Error 152 on Power Cycle**
- Fixed malformed YAML in default configuration
- Removed invalid x and y axis definitions (Maslow uses A/B/C/D axes)
- Allowed config loading even after panic (panic is not from config file)
- Fixed infinite panic loop

### 🧪 Test Function Crash Fix (PR #507)

Fixed resource leak in AutoUpdate causing crash on repeated test runs.

**Issue:** WiFiClientSecure cleanup was not happening properly between test invocations

**Fix:** Proper cleanup to prevent resource exhaustion on second run

---

## User Experience Improvements

### 📝 Better Error Messages (PR #479)

Added detailed context to `error:3` (InvalidStatement) messages to help users understand what went wrong.

**Before:** Just "error:3"  
**After:** Descriptive message explaining the specific problem with the command

### 🔍 Firmware Update Check Optimization (PR #501)

Moved firmware update check from startup to TEST command execution.

**Benefits:**
- Reduces startup time and resource usage
- Update check now runs on-demand when user executes TEST command
- Reduced logging verbosity for cleaner output

---

## Build System & Tooling

### 📦 Release Build Improvements (PR #499)

Replaced hardcoded paths with configurable local variables in build-release.py.

**Features:**
- Auto-detection for platformio command with multiple fallback options
- Tries `pio`, `platformio`, `~/.platformio/penv/bin/platformio`, `~/.local/bin/pio`
- Default PLATFORMIO_CMD changed from 'platformio' to 'pio'
- More portable across different development environments

### 🏷️ Version Extraction Fix (PR #462)

Fixed release file versioning to use git tags instead of hardcoded values.

**Features:**
- Dynamically extracts version from git tags
- Fallback to "dev" when git is not available
- Proper error handling for FileNotFoundError

---

## Code Quality & Maintenance

### 🧹 Cleanup and Refactoring (PR #421)

Cleaned up stale state variables by converting to state machine references.

**Changes:**
- Removed duplicate state tracking variables
- Uses centralized state machine instead of scattered flags
- Improved code maintainability

### 📋 Development Guidelines (PR #361)

Added Copilot instructions for code quality checks.

**Guidelines:**
- Trailing whitespace removal before every commit
- Dead code detection and removal
- Mandatory use of `git diff --check`
- Pre-commit validation checklist

### 🔊 Logging Improvements

Multiple commits improved logging verbosity and clarity:
- Changed diagnostic log messages from INFO to DEBUG level
- Reduced logging verbosity in firmware update check
- Removed console spam from orientation detection
- Cleaned up prints across multiple modules

---

## Memory Optimization (PR #440)

**Status:** In Progress - Merged into main  
Comprehensive memory usage improvements through string and container optimizations with shared log buffer.

---

## Breaking Changes

None - This release maintains backward compatibility with v1.14 configurations.

---

## Known Issues

1. **Unit Tests:** Native unit tests currently fail due to FreeRTOS compatibility issues. Hardware testing is required.
2. **npm Audit Warnings:** Web UI build system shows security warnings for old packages (build-time only, no runtime impact).

---

## Upgrade Notes

### From v1.14 to v1.15:

1. **Automatic Orientation Detection:** The firmware will now automatically detect your machine orientation during calibration. You no longer need to manually set `Maslow_vertical`. However, you can still override it manually if needed.

2. **Calibration Grid:** The grid density is now calculated automatically. If you had custom calibration grid settings, they may be overridden by the automatic selection.

3. **Firmware Updates:** Update checks now only run when you execute the TEST command, not automatically on startup.

4. **Configuration:** If you experienced Error 152 on power cycle with v1.14, this is now fixed. Your saved configurations should load correctly.

---

## Statistics

- **Development Period:** 6 days (Nov 14-20, 2025)
- **Pull Requests Merged:** 13 major PRs
- **Commits:** 80+ commits
- **Lines Changed:** Extensive changes across calibration, configuration, and memory management

---

## Contributors

- @BarbourSmith - Project maintainer and reviewer
- @Copilot (copilot-swe-agent) - Automated development assistant
- @MaslowBot - Additional automated assistance

---

## Installation

Download the appropriate package for your platform:

- **Windows:** `fluidnc-maslow4-1.15-win64.zip`
- **macOS/Linux:** `fluidnc-maslow4-1.15-posix.zip`
- **Direct Firmware:** `firmware.bin` (for manual flashing)
- **Web UI:** `index.html.gz` (for web interface updates)
- **Configuration:** `maslow.yaml` (reference configuration)

---

## Next Steps

Looking ahead to future releases:
- Further memory optimizations
- Additional calibration improvements
- Enhanced error reporting and diagnostics

---

## Support

For issues, questions, or feedback:
- GitHub Issues: https://github.com/BarbourSmith/FluidNC/issues
- Discussions: https://github.com/BarbourSmith/FluidNC/discussions

---

*Note: This is a fork of FluidNC specifically optimized for Maslow CNC machines. For the original FluidNC project, visit https://github.com/bdring/FluidNC*
