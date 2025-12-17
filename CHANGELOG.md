# Changelog

All notable changes to the Maslow 4 firmware project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project uses semantic versioning.

## [Unreleased]

No unreleased changes at this time.

## [v1.16] - 2025-12-17

### Changed
- **Project Structure Reorganization** (#624): Major refactoring to improve organization
  - Moved all firmware files into `firmware/` directory
  - Reorganized build scripts and installation tools
  - Integrated ESP3D-WEBUI as a subproject within the repository
  - Moved example configurations to `firmware/example_configs/`
  
- **Web Interface Improvements** (#618, #624):
  - Unified machine coordinate system for work area display
  - Improved consistency in coordinate reporting across the interface
  - Better visual representation of work area boundaries

### Added
- Integrated ESP3D-WEBUI project into main repository for easier maintenance
- Enhanced project documentation and build instructions
- Additional GitHub workflows for automated testing and deployment

## [v1.15] - 2024-12-17

### Added
- **Automatic Orientation Detection**: Machine now automatically detects whether it's mounted horizontally or vertically during calibration (#467)
  - Detection runs at calibration start using motor speed test
  - Uses 70% motor speed for 1.5 seconds to measure belt extension
  - Threshold set at 35mm extension to distinguish orientation
  - Stores result in `Maslow_vertical` configuration parameter
  - Eliminates need for manual orientation configuration

### Fixed
- **Orientation Detection Improvements**:
  - Fixed repeated orientation detection by using static flag (#513)
  - Fixed infinite loop by resetting orientation detection state variables (#513)
  - Added motor settling pause after orientation detection (#513)
  - Removed console spam from orientation detection logging (#511)
  - Fixed detection to allow all phases to complete properly (#511)
  - Fixed timer initialization and belt extension measurement (#511)

- **Calibration Fixes**:
  - Fixed calibration grid shift by correcting spiral start position (#497)
  - Fixed center coordinate calculation after calibration results are loaded (#497)
  - Changed diagnostic log messages from INFO to DEBUG level to reduce noise (#497)

- **Stability and Performance**:
  - Fixed WiFiClientSecure cleanup to prevent crash on second test run (#507)
  - Moved firmware update check from startup to TEST command (#501)
  - Reduced logging verbosity in firmware update check (#501)

### Technical Details
- Orientation detection integrated directly into calibration loop at waypoint 0
- Detection uses active motor driving instead of passive compliance
- Added decompressBelt phase before comply for gravity-driven belt extension
- Applied code formatting (clang-format) to modified files

## [v1.14] - Previous Release

Release tag created but detailed changelog not maintained for earlier versions.

### Note on Version History

Versions prior to v1.15 (v1.14, v1.13, v1.12, v1.11, v1.10, v1.09, v1.08, v1.07, v1.06, v1.05, v1.04, v1.03, v1.02, v1.01, v1.0, and earlier 0.x versions) exist but detailed changelogs were not maintained. For historical reference, see:
- Git tags: https://github.com/MaslowCNC/Maslow_4/tags
- Commit history: https://github.com/MaslowCNC/Maslow_4/commits/

## Release Summaries

### v1.16 Summary

Version 1.16 is primarily a structural release that reorganizes the codebase for better maintainability and integration.

**Key Highlights:**
1. **Improved Project Structure** - Cleaner organization with firmware in dedicated directory
2. **ESP3D-WEBUI Integration** - Web interface now part of main repository
3. **Unified Coordinate System** - Better consistency in work area display

**For Users:**
- Web interface coordinate display is more consistent
- No functional changes to operation
- Easier to build and install from source

**For Developers:**
- Clearer project structure with firmware in `firmware/` directory
- ESP3D-WEBUI is now easier to modify and maintain
- Better organization of example configurations and build tools

### v1.15 Summary

Version 1.15 is a significant release focused on improving the calibration experience and system stability. The headline feature is **automatic orientation detection**, which eliminates a common source of user confusion by having the machine automatically determine whether it's mounted horizontally or vertically.

**Key Highlights:**
1. **Automatic Orientation Detection** - No more manual configuration needed
2. **Improved Calibration Accuracy** - Fixed grid positioning issues
3. **Enhanced Stability** - Fixed multiple crash scenarios
4. **Better User Experience** - Reduced log spam and moved update checks out of startup

---

## About This Changelog

This changelog documents changes starting from version 1.15. Future releases will maintain this changelog with detailed information about new features, bug fixes, and breaking changes.

For the latest firmware and installation instructions, visit:
- Website: https://www.maslowcnc.com/
- Documentation: https://maslowcnc.github.io/Maslow_4/
- Forums: https://forums.maslowcnc.com/
- Repository: https://github.com/MaslowCNC/Maslow_4
