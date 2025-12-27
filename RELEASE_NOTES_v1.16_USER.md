# Maslow CNC Firmware v1.16 - What's New

**Release Date:** December 26, 2024  
**Previous Version:** v1.15 (November 20, 2024)

---

## 🎉 Overview

Version 1.16 brings dozens of improvements to make your Maslow CNC machine easier to use, more accurate, and more reliable. This release focuses on calibration improvements, interface enhancements, and bug fixes based on community feedback.

**No recalibration required** - Your existing calibration from v1.15 will work perfectly with v1.16.

---

## ✨ New Features

### Calibration Improvements

**🎯 Work Area Constraints (PR #610, #616)**
- Calibration grid now automatically constrained to your defined work area
- Prevents calibration points from being placed outside your machine's reach
- Smarter grid generation that adapts to your machine size

**🔄 Endless Loop Prevention (PR #589)**
- Added automatic retry limit to prevent calibration from getting stuck
- If calibration can't improve after multiple attempts, it will stop and notify you
- No more waiting indefinitely for calibration to complete

**📊 Improved Calibration Settings (PR #583)**
- Combined retraction and calibration settings into a more intuitive interface
- Easier to configure calibration parameters
- Better defaults for faster calibration

**🔧 Better Initial Guess (PR #230)**
- Calibration now starts with a smarter initial estimate
- Faster convergence to accurate results
- Reduced calibration time for most machines

**✅ Calibration Verification (PR #553)**
- Enhanced validation of calibration settings before starting
- Catches configuration issues early
- Better error messages when settings are invalid

### Interface Enhancements

**🖱️ Right-Click to Move (PR #208)**
- Right-click anywhere on the G-code canvas to move the machine there
- Quick and intuitive machine positioning
- Context menu shows current position

**📍 Improved Position Display (PR #214, #255)**
- Added machine state information to the Maslow tab
- Updated status labels for clarity
- Better visual feedback about what the machine is doing

**🎨 Better Button Layout (PR #216, #218)**
- All arrow buttons are now the same size
- Removed extra whitespace for a cleaner look
- More consistent user interface

**📏 Enhanced Bounding Box (PR #247, #273)**
- Improved visualization of part boundaries
- More accurate bounding box calculation
- Easier to see where your part will be cut

**🎯 Purple Dot Positioning (PR #257)**
- Fixed positioning of the current location indicator
- More accurate visual representation
- Better alignment with actual machine position

**🔄 Smoother Trace Boundary (PR #259)**
- Improved smoothness of traced boundaries
- Better visual quality when previewing toolpaths
- More accurate representation of actual cut path

**🧪 Version Check on Test (PR #263)**
- Test connection button now verifies firmware version
- Helps identify firmware/interface compatibility issues
- Better diagnostic information

### Configuration & Settings

**📡 WiFi Configuration in Interface (PR #585)**
- Configure WiFi settings directly from the tablet interface
- No need to connect via USB for WiFi setup
- Access settings popup for quick network configuration

**🔧 Preferences & YAML Support (PR #224)**
- Added preferences dropdown menu
- Better YAML configuration file handling
- Easier to manage machine settings

### G-code & Toolpath

**⚙️ Arc Rendering Fixes (PR #608, #655)**
- Fixed G-code arc handling in the user interface
- Smoother arc rendering and preview
- More accurate toolpath visualization

**📐 Toolpath Display Updates (PR #116)**
- Enhanced toolpath display system
- Better performance when viewing complex paths
- Improved visual clarity

### Z-Axis Improvements

**⬆️ Z-Axis Position Fix (PR #580)**
- Fixed Z-axis position accounting in belt length measurements
- More accurate Z-axis positioning
- Better consistency in multi-layer cuts

**🔘 Z-Axis Button Improvements**
- Z-axis controls more accessible
- Better integration with main interface
- Simplified Z-axis adjustments

### File Management

**📁 File System Improvements (PR #84, #93, #95)**
- Better file directory detection
- Improved SPIFFS file system handling
- More reliable file uploads and downloads

**📄 Configuration Tab Updates (PR #93)**
- Enhanced configuration file management
- Easier to view and edit settings
- Better organization of configuration options

### Communication & Connectivity

**🔒 HTTP Communication Lock (PR #115)**
- Added check for HTTP communication lock
- Prevents command conflicts
- More reliable web interface communication

**🔧 Startup Sequence (PR #88)**
- Improved firmware startup sequence
- Faster boot times
- More reliable initial connection

**🔗 Connection Dialog (PR #70)**
- Updated connection interface
- Better error messages
- Clearer connection status

### Camera & Monitoring

**📷 Camera Tab Updates (PR #85)**
- Improved camera interface
- Better video stream handling
- Enhanced camera controls

### Documentation

**📚 Quick Start Guide (PR #630, #632, #612, #614)**
- Comprehensive new quick start guide
- Step-by-step setup instructions with pictures
- Easier for new users to get started

**📖 Extensive Documentation Updates**
- Frame library documentation
- Bit library information
- Software library guides
- CAD file documentation for all sections

---

## 🐛 Bug Fixes

**Fixed in This Release:**
- Calibration no longer gets stuck in endless loops (PR #589)
- Z-axis positioning more accurate (PR #580)
- Purple dot (current position) displays correctly (PR #257)
- Bounding box calculations fixed (PR #273)
- Trace boundary rendering smoother (PR #259)
- Arc G-code rendering corrected (PR #608, #655)
- File system directory detection works properly (PR #95)
- Startup errors resolved (PR #226)
- MasloBot action bugs fixed (PR #212)
- Various UI state and display issues resolved

---

## 🔧 Under the Hood

**For Advanced Users:**
- ESP3D-WEBUI source code now integrated into main repository
- Web interface can be built from source
- Improved build system and CI/CD pipelines
- Better development tools and documentation
- GitHub Pages hosting for documentation

---

## 📦 Upgrade Instructions

### Standard Installation

1. **Download the Installer**
   - Windows: `fluidnc-maslow4-1.16-win64.zip`
   - Mac/Linux: `fluidnc-maslow4-1.16-posix.zip`

2. **Run the Flash Tool**
   - Unzip the package
   - Connect your Maslow controller via USB
   - Run the included flash script
   - Wait for completion

3. **Update Web Interface (Optional but Recommended)**
   - Connect to your Maslow via WiFi
   - Go to Settings → System
   - Upload the new `index.html.gz` file
   - Refresh your browser

4. **Start Using**
   - Your existing configuration and calibration are preserved
   - No recalibration needed
   - All your settings remain intact

### Files to Update

- ✅ **firmware.bin** - Main firmware (required)
- ✅ **index.html.gz** - Web interface (recommended)
- ⚪ **maslow.yaml** - Only if you want the latest defaults

---

## ⚠️ Important Notes

### Compatibility
- ✅ Fully compatible with v1.15 configurations
- ✅ Existing calibration data works without changes
- ✅ All YAML configuration files compatible
- ✅ No breaking changes

### What You Don't Need to Do
- ❌ No recalibration required
- ❌ No configuration file changes needed
- ❌ No manual settings adjustments necessary

---

## 🎓 Getting Help

### Resources
- **Quick Start Guide:** See the new quick start guide in `docs/QuickStart.md`
- **Documentation:** https://maslowcnc.github.io/Maslow_4/
- **Forums:** https://forums.maslowcnc.com/
- **Issue Tracker:** https://github.com/MaslowCNC/Maslow_4/issues

### Common Questions

**Q: Do I need to recalibrate after upgrading?**  
A: No! Your v1.15 calibration will work perfectly with v1.16.

**Q: Will my configuration file still work?**  
A: Yes, all v1.15 configurations are fully compatible.

**Q: What if something goes wrong during the upgrade?**  
A: You can always flash back to v1.15. Keep a copy of your old firmware just in case.

**Q: Do I need to update the web interface?**  
A: It's recommended but not required. The v1.15 web interface will mostly work, but you'll miss out on new features and bug fixes.

---

## 🙏 Credits

### Core Development
- **@BarbourSmith** - Project lead, core firmware development

### Community Contributors
Special thanks to everyone who contributed code, testing, and documentation:

- **@md8n** - Extensive web interface improvements and bug fixes
- **@wouldchuckit** - Documentation and user guides
- **@asmith26** - Bug reports and fixes
- Many others who tested, reported issues, and provided feedback

### Third-Party Software
- **ESP3D-WEBUI** by Luc (luc-github) - Web interface framework
- **FluidNC** project - Base firmware platform

---

## 📊 Release Statistics

**Development Period:** 36 days (November 20 - December 26, 2024)

**Changes:**
- 285+ merged pull requests
- Dozens of bug fixes
- 20+ new features
- Extensive documentation updates
- 100+ files modified

**Community Impact:**
- Multiple active contributors
- Improved user experience based on forum feedback
- Better documentation for new users
- More stable and reliable operation

---

## 🔜 What's Next?

We're continuously working to improve Maslow CNC. Future releases may include:

- Enhanced web interface features
- More calibration improvements  
- Better error diagnostics
- Additional documentation
- Community-requested features

**Join the Community:**
Share your ideas, report bugs, and help make Maslow better for everyone at https://forums.maslowcnc.com/

---

## 📝 Full Technical Details

For complete technical details, developer information, and implementation specifics, see:
- `RELEASE_NOTES_v1.16.md` - Comprehensive technical documentation
- `RELEASE_NOTES_README.md` - Release notes process documentation
- GitHub Compare: https://github.com/MaslowCNC/Maslow_4/compare/v1.15...v1.16

---

**Thank you for being part of the Maslow CNC community!**

We hope v1.16 makes your CNC experience even better. Happy cutting! 🪚✨
