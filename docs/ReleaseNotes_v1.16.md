# Release Notes - Version 1.16

**Release Date:** December 17, 2025

Version 1.16 is primarily a structural and organizational release that improves the codebase layout and integrates the web interface more closely with the main project.

## 🏗️ Project Structure Reorganization

The biggest change in v1.16 is the reorganization of the project structure to improve maintainability and clarity.

### What Changed

**Firmware Organization (#624)**
- All firmware-related files moved into a dedicated `firmware/` directory
- Example configurations relocated to `firmware/example_configs/`
- Build scripts and tools organized under `firmware/`
- Installation scripts grouped in `firmware/install_scripts/`

**ESP3D-WEBUI Integration**
- ESP3D-WEBUI project integrated as a subproject within the repository
- Previously, the web interface was maintained separately
- Now part of the main Maslow_4 repository for easier coordinated development
- Web UI source files located in `ESP3D-WEBUI/` directory

**Build System Updates**
- Consolidated build configuration
- Improved PlatformIO integration
- Enhanced GitHub workflows for automated builds and testing

### Why This Matters

**For Users:**
- No immediate functional changes to how the machine operates
- Future updates will be easier to deploy
- Building from source is now more straightforward

**For Developers:**
- Clearer project structure makes it easier to find and modify code
- Web interface changes can be coordinated with firmware changes
- Better organization reduces confusion when contributing

## 🎨 Web Interface Improvements

### Unified Coordinate System (#618, #624)

**What Changed:**
- Standardized how machine coordinates are displayed in the work area
- Improved consistency across different parts of the web interface
- Better visual representation of work area boundaries

**Benefits:**
- More predictable coordinate display
- Easier to understand machine position
- Consistent behavior across all interface screens

## 📦 Integration Details

### ESP3D-WEBUI Integration

Previously, ESP3D-WEBUI was a separate project that needed to be built and integrated manually. Now it's part of the main repository:

**Location:** `ESP3D-WEBUI/` directory in the repository root

**Building:**
```bash
cd ESP3D-WEBUI
npm install
gulp package --lang en  # For English only (recommended)
```

**Output:** Compiled web interface goes to `ESP3D-WEBUI/dist/index.html.gz`

**Documentation:**
- Build instructions: `ESP3D-WEBUI/COMPILATION.md`
- Local testing: `ESP3D-WEBUI/HOWTO-Test-Locally.md`

### Firmware Structure

All firmware code now lives under `firmware/`:

```
firmware/
├── FluidNC/          # Main firmware source code
│   ├── src/          # C++ source files
│   └── data/         # Configuration files
├── embedded/         # Embedded tool UI
├── example_configs/  # Example machine configurations
├── install_scripts/  # Installation helpers (Win/Mac/Linux)
├── libraries/        # Custom libraries
└── platformio.ini    # Build configuration
```

## 🔧 Technical Changes

### File Moves (Not Functional Changes)

Most changes in this release are file relocations. The actual code functionality remains the same:

- `FluidNC/` → `firmware/FluidNC/`
- `example_configs/` → `firmware/example_configs/`
- `embedded/` → `firmware/embedded/`
- `install_scripts/` → `firmware/install_scripts/`

### Build System

**PlatformIO Configuration:**
- Still use `pio run -e wifi_s3` for building
- Working directory is now `firmware/` for build commands
- Installation scripts unchanged in functionality

**Web UI Build:**
- Now integrated in repository
- Use `gulp package --lang en` for production builds
- Multiple language support available but English recommended for size

## 📊 Statistics

- **Primary Change:** Project reorganization
- **Lines Moved:** ~252,000+ (mostly ESP3D-WEBUI integration)
- **Files Reorganized:** 1,150+
- **Functional Code Changes:** Minimal (coordinate system improvements)
- **Pull Request:** #624

## 🎓 For Users

### What You Need to Know

1. **No Operational Changes:** Your machine works exactly the same way
2. **Coordinate Display:** Work area coordinates may look slightly different but are more consistent
3. **Updates:** Future firmware updates follow the same installation process
4. **Source Builds:** If building from source, note the new `firmware/` directory

### Migration Notes

- Existing configurations work without changes
- No need to recalibrate
- Web interface looks and functions the same
- Installation procedures unchanged

## 🔬 For Developers

### Key Changes to Be Aware Of

1. **Working Directory:**
   - Build commands now run from `firmware/` directory
   - Example: `cd firmware && pio run -e wifi_s3`

2. **Web UI Development:**
   - Web UI source is now in repository
   - Make changes in `ESP3D-WEBUI/www/`
   - Build with `gulp package --lang en`
   - Output goes to `ESP3D-WEBUI/dist/`

3. **Example Configurations:**
   - Located in `firmware/example_configs/`
   - Use these as templates for custom machines

4. **Installation Scripts:**
   - Located in `firmware/install_scripts/`
   - Separate scripts for Windows, Mac, and Linux

### Testing Recommendations

- Test builds with new directory structure
- Verify web interface builds correctly
- Check that example configs load properly
- Test installation scripts on target platforms

## 📝 Known Issues

None specific to v1.16. This release focused on organization rather than new features.

## 🔗 Related Resources

- **Forum Discussion:** https://forums.maslowcnc.com/
- **Documentation:** https://maslowcnc.github.io/Maslow_4/
- **Issue Tracker:** https://github.com/MaslowCNC/Maslow_4/issues
- **Wiki:** https://github.com/MaslowCNC/Maslow_4/wiki

## 📦 Download and Installation

Version 1.16 firmware can be installed:
1. Through the web interface (recommended for users)
2. Via USB using installation scripts in `firmware/install_scripts/`
3. By building from source with PlatformIO (for developers)

For detailed installation instructions, see the [Quick Start Guide](./QuickStart.md).

## 🔜 What's Next

Version 1.16 sets the stage for future improvements by providing a cleaner, more maintainable codebase. Upcoming work may include:
- Additional web interface enhancements
- More calibration improvements
- Performance optimizations
- Community-requested features

See the [issue tracker](https://github.com/MaslowCNC/Maslow_4/issues) for planned work.

---

## Previous Releases

For information about v1.15 and earlier, see:
- [v1.15 Release Notes](./ReleaseNotes_v1.15.md)
- [Complete Changelog](../CHANGELOG.md)
