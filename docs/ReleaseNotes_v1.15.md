# Release Notes - Version 1.15

**Release Date:** December 17, 2024

Version 1.15 represents a significant improvement to the Maslow 4 firmware, with a focus on automation, user experience, and stability.

## 🎯 Headline Feature: Automatic Orientation Detection

The biggest change in v1.15 is the introduction of **automatic orientation detection**. Previously, users had to manually configure whether their Maslow machine was mounted horizontally (on a table) or vertically (on a wall). This was a common source of confusion and setup errors.

### How It Works

During the calibration process, the machine now automatically:
1. Drives the motors at 70% speed for 1.5 seconds
2. Measures how far the belts extend under gravity
3. Compares the extension to a 35mm threshold
4. Determines if the machine is horizontal or vertical
5. Stores the result in the `Maslow_vertical` configuration parameter

This detection happens automatically at the start of calibration (waypoint 0), so users no longer need to worry about this setting.

### Technical Implementation

- **Pull Request:** #467
- **Related Fixes:** #511, #513
- **Method:** Active motor driving with belt extension measurement
- **Threshold:** 35mm extension (vertical machines extend less due to gravity)
- **Storage:** `Maslow_vertical` config parameter (replaces manual configuration)

## 🐛 Bug Fixes

### Calibration Improvements

**Calibration Grid Shift Fix (#497)**
- Fixed an issue where the calibration grid would shift from its expected position
- Corrected the spiral start position calculation
- Fixed center coordinate calculation after calibration results are loaded
- Added coordinate logging for debugging purposes

**Diagnostic Logging (#497)**
- Changed diagnostic log messages from INFO to DEBUG level
- Reduces console noise during normal operation
- Makes it easier to focus on important messages

### Orientation Detection Refinements

**Repeated Detection Prevention (#513)**
- Fixed issue where orientation detection would run multiple times
- Implemented static flag to ensure detection runs only once per calibration
- Added motor settling pause after detection completes

**Infinite Loop Fix (#513)**
- Fixed infinite loop caused by uninitialized orientation detection state
- Properly resets all state variables between calibration runs

**Detection Completion (#511)**
- Fixed issue where not all detection phases would complete
- Removed excessive console logging (spam) during detection
- Ensured proper timer initialization and measurement accuracy

### System Stability

**WiFiClientSecure Crash Fix (#507)**
- Fixed crash that occurred when running TEST command a second time
- Properly cleans up WiFiClientSecure resources
- Improves overall system stability

**Firmware Update Check (#501)**
- Moved firmware update check from system startup to TEST command
- Reduces startup time and complexity
- Update check still available when needed, just not automatic
- Reduced logging verbosity during update check

## 🔧 Technical Changes

### Code Quality
- Applied clang-format to modified files for consistent code style
- Improved floating-point comparisons for orientation detection
- Enhanced code clarity based on review feedback

### Architecture Improvements
- Removed DETECTING_ORIENTATION state, integrated detection into calibration_loop
- Changed from passive compliance to active motor driving for detection
- Added decompressBelt phase before comply for gravity-driven extension
- Orientation detection now runs AFTER calibration starts, not after belt extension

### State Management
- Proper initialization of detection timer
- Correct measurement of belt extension (only extension during test, not total position)
- Static flags to prevent repeated detection
- Proper cleanup of state variables

## 📊 Statistics

- **Total Commits:** 43 commits between v1.14 and v1.15
- **Major Features:** 1 (Automatic Orientation Detection)
- **Bug Fixes:** 8 major fixes
- **Pull Requests Merged:** 7
- **Lines Changed:** Significant refactoring of calibration and detection code

## 🎓 For Users

### What You Need to Know

1. **Automatic Setup:** The machine will automatically detect its orientation during calibration. You don't need to configure this anymore.

2. **Cleaner Console:** Less diagnostic spam in the console during normal operation.

3. **Faster Startup:** Firmware update checks no longer happen at startup.

4. **More Stable:** Several crash scenarios have been fixed, especially around the TEST command.

5. **Better Calibration:** Calibration grid positioning is now more accurate.

### Migration Notes

- If you previously set `Maslow_vertical` manually, the automatic detection will override it during the next calibration
- The orientation detection happens transparently - you don't need to do anything special
- If you want to verify the detected orientation, check the console output during calibration

## 🔬 For Developers

### Key Changes to Be Aware Of

1. **Orientation Detection API:**
   - Detection is now part of the calibration loop
   - Uses `Maslow_vertical` config parameter
   - Hardcoded constants (35mm threshold, 70% speed, 1.5 seconds)

2. **Calibration State Machine:**
   - No more DETECTING_ORIENTATION state
   - Detection integrated at waypoint 0
   - DecompressBelt phase added before comply

3. **Logging Levels:**
   - Diagnostic messages moved to DEBUG level
   - Reduced verbosity in firmware update checks

4. **Resource Management:**
   - WiFiClientSecure properly cleaned up
   - State variables properly initialized and reset

### Testing Recommendations

When testing changes related to:
- **Calibration:** Verify orientation detection works in both horizontal and vertical configurations
- **WiFi Features:** Test TEST command multiple times in sequence
- **Startup:** Verify startup time is reasonable without update check
- **Logging:** Check that console output is clean and informative

## 📝 Known Issues

None specifically introduced in v1.15. The release focused on fixing existing issues.

## 🔗 Related Resources

- **Forum Discussion:** https://forums.maslowcnc.com/
- **Documentation:** https://maslowcnc.github.io/Maslow_4/
- **Issue Tracker:** https://github.com/MaslowCNC/Maslow_4/issues
- **Wiki:** https://github.com/MaslowCNC/Maslow_4/wiki

## 📦 Download and Installation

Version 1.15 firmware can be installed:
1. Through the web interface (recommended)
2. Via USB using installation scripts in `firmware/install_scripts/`
3. By building from source with PlatformIO

For detailed installation instructions, see the [Quick Start Guide](./QuickStart.md).

## 🙏 Acknowledgments

This release was made possible by contributions from:
- The Maslow community for testing and feedback
- GitHub Copilot for automated code improvements
- All users who reported issues and helped debug problems

## 🔜 What's Next

After v1.15, development continues with:
- Further improvements to calibration accuracy
- Enhanced web interface features
- Additional stability improvements
- Community-requested features

See the [issue tracker](https://github.com/MaslowCNC/Maslow_4/issues) for upcoming work.

---

*For the complete commit history, see the [CHANGELOG.md](../CHANGELOG.md) in the root directory.*
