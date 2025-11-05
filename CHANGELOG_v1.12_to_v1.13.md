# FluidNC Firmware Changelog: v1.12 to v1.13

## Overview

Version 1.13 brings a much-requested feature to reduce the need to retract and extend the belts. The machine will now remember if the belts have been extended when it is powered off, meaning you won't need to retract and extend them again. Just press "Apply Tension" and you should be good to go.

**Release Date:** October 24, 2025  
**Credits:** The bulk of the changes in this update are thanks to @davidelang

## Major Features

### Belt Position Memory (Primary Feature)
- **Save/Restore Belt Positions**: The machine now saves belt positions and state to non-volatile storage (NVS)
  - Belt lengths are automatically saved when in READY_TO_CUT or RETRACTED states
  - Machine state is preserved across power cycles
  - On startup, the system restores the previous belt positions if they were extended
  - Eliminates the need to retract and extend belts after every power cycle

### Enhanced State Management
- **Improved State Persistence**: Better handling of machine states across restarts
  - `loadBeltPositions()` now sets EXTENDEDOUT state instead of READY_TO_CUT to prevent unwanted auto-slack
  - Added safety mechanism to clear motor overrides when leaving Homing state
  - Fixed transitions: Allow TAKING_SLACK transition from READY_TO_CUT state

### Encoder Position Validation
- **Belt Movement Detection**: System now saves and validates encoder angles to detect if belts moved during power-off
  - Prevents incorrect position assumptions if belts were manually moved while powered down
  - Provides warnings if significant position changes are detected

## Bug Fixes

### Critical Fixes

1. **Frame Dimension Error Handling**
   - Replaced auto-correction with proper error state for invalid frame dimensions
   - Now calls `Maslow.eStop()` instead of using Assert for frame dimension errors
   - Prevents machine from operating with invalid geometry

2. **Belt Length Reset Issue**
   - Fixed belt lengths resetting to zero on telnet connection
   - Prevented telnet negotiation from triggering system reset

3. **Automatic Update System**
   - Fixed firmware download to properly handle HTTP redirects
   - Improved API endpoint redirect handling in `getLatestReleaseInfo()`
   - Implemented semantic version comparison to prevent unnecessary upgrades
   - Changed firmware installation to download to SD card first for better validation

4. **Z-Axis Probe Issues**
   - Fixed probe direction logic for better clarity and Maslow CNC compatibility
   - Corrected Z-axis motor direction for proper probe movement
   - Fixed probe step capture to handle custom Maslow axis mapping

5. **Calibration Issues**
   - Fixed calibration grid consistency by resetting recomputeCount
   - Moved generation of calibration grid for better organization
   - Added proper error handling for Tool Length Offset (TLO) misbehavior

### Minor Fixes

- **Compile Warnings**: Fixed ISO C++ compile warnings in Calibration.h
- **Version Reporting**: Fixed version reporting in shallow git clones
  - Improved `git-version.py` to use `git describe` directly
  - Fixed grbl_version extraction to handle major.minor versions correctly
  - Added fallback to 3.0 when no tag is available
- **Logging Improvements**: 
  - Only log spoilboard/work thickness when value changes (reduces console spam)
  - Extended startup log capture until first websocket connection
  - Better capture of initialization messages
- **Soft Limits**: Temporarily turned off soft limits (can be re-enabled if needed)

## Build System Improvements

### CI/CD Enhancements

1. **Automatic Builds**
   - Fixed automatic builds versioning
   - Disabled build attempts on review requests, kept user-requested builds
   - Improved firmware.bin packaging in maslowbot build action
   - Added workflow to auto-assign issue authors to PRs

2. **Build Scripts**
   - Fixed `build-release.py`: removed bufsize=1 and hardcoded paths
   - Use `python3` explicitly instead of `python` for better compatibility
   - Added explicit git fetch --tags step to workflow
   - Added fetch-tags parameter and verification step

## Code Quality

### Refactoring

- **AutoUpdate.cpp**: 
  - Removed dead code and consolidated duplicated asset parsing logic
  - Consolidated redirect handling into single location
  - Refactored HTTP request and header parsing into reusable function
  - Simplified version comparison logic

- **Probe.cpp**: Applied clang-format for consistent code style
- **Main.cpp**: Applied clang-format for consistent code style

### Code Cleanup

- Removed duplicated download logic
- Cleaned up dead code throughout the codebase
- Improved code organization and maintainability

## Configuration Changes

### YAML Configuration
- Fixed maslow.yaml configuration issues
- Belt position save/restore is automatic and requires no configuration changes
- Existing configurations from v1.12 remain compatible

### Machine Behavior

- Belt retraction is no longer required after every power cycle
- "Apply Tension" button can be used immediately after power-on if belts were extended
- More predictable state transitions during startup

## Testing Improvements

- Added testing template for standardized validation
- Added checklist to builds for quality assurance
- Improved compile-time warnings for non-tagged versions

## Known Issues & Limitations

- Soft limits are currently disabled (v0.78 feature, maintained in v1.13)
- If belts are manually moved while machine is powered off, the system will detect this but may need recalibration

## Upgrade Notes

### From v1.12 to v1.13

1. **Firmware Update**: Standard over-the-air update process
2. **Configuration**: No changes to maslow.yaml required
3. **First Use**: After upgrade, the system will start saving belt positions automatically
4. **Testing**: Verify that belt positions are correctly restored after power cycling

### File Changes

- **firmware.bin**: Main firmware binary (2,013,312 bytes)
- **index.html.gz**: Web interface (129,786 bytes)
- **maslow.yaml**: Configuration file (6,650 bytes) - minor updates

## Pull Requests Included

Notable PRs merged between v1.12 and v1.13:

- #446: Fix build-release.py error
- #443: Fix belt length loading state
- #441: Add testing templates
- #438: Move generation of calibration grid
- #437: Fix TLO misbehaving issue
- #431: Add checklist for testing
- #430: Turn off soft limits
- #426: Fix belt length restore
- #424: Fix BR belt length storage
- #422: Improve compile warning
- #413: Fix compile time warnings
- #411: Fix belt positions saving
- #395: Fix belt length reset issue
- #385: Disable build on review request
- #381: Fix automatic builds versioning
- #377: Fix frame size error handling
- #372: Fix repeated thickness logs
- #368: Fix belt movement detection
- #363: Fix startup log capture
- #357: Implement belt position save/restore
- #351: Enable soft limits (later disabled)
- #342, #344: Fix automatic update system
- #336: Fix firmware packaging
- #334: Fix probe issues
- #326: Fix probe direction logic
- #333: Consolidate AutoUpdate code

## Statistics

- **Total Commits**: 124 commits between v1.12 and v1.13
- **Files Changed**: Multiple core firmware files, build scripts, and workflows
- **Binary Size Change**: Increased from 1,973,680 bytes (v1.12) to 2,013,312 bytes (v1.13)
  - Increase of ~40KB due to new belt position persistence features

## Credits

- **@davidelang**: Major contributions to belt position save/restore functionality
- **@BarbourSmith**: Project maintenance and integration
- **GitHub Copilot**: Automated fixes and improvements via copilot-driven PRs

## Support

For issues, questions, or feedback:
- GitHub Issues: https://github.com/BarbourSmith/FluidNC/issues
- Maslow CNC Forums: https://forums.maslowcnc.com/

---

**Note**: This changelog is based on the official release notes, commit history, and pull request descriptions between v1.12 (released September 17, 2025) and v1.13 (released October 24, 2025).
