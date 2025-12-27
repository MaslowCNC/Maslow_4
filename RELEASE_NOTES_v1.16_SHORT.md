# Maslow CNC Firmware v1.16 - Release Notes

**Release Date:** December 26, 2024

---

## What's New

Version 1.16 brings **dozens of improvements** to make your Maslow CNC machine easier to use, more accurate, and more reliable. No recalibration required!

---

## ✨ Key Features

### Calibration Improvements
- **Work area constraints** - Calibration grid automatically fits your machine size
- **Endless loop prevention** - Calibration won't get stuck anymore
- **Smarter initial guess** - Faster calibration with better starting point
- **Better validation** - Catches configuration issues before starting

### Interface Enhancements
- **Right-click to move** - Click anywhere on canvas to move machine there
- **Improved status display** - Clearer machine state information
- **Better button layout** - Consistent sizing and cleaner appearance
- **Enhanced bounding box** - More accurate part boundary visualization
- **Smoother toolpath preview** - Better arc rendering and trace display

### New Settings
- **WiFi configuration in UI** - Configure network without USB connection
- **Preferences menu** - Easier access to machine settings
- **Work area settings** - Define your machine's working area

### Bug Fixes
- Fixed Z-axis positioning accuracy
- Corrected arc G-code rendering
- Fixed calibration endless loops
- Improved file system reliability
- Better startup sequence
- Many UI fixes and improvements

---

## 📦 Download & Install

### Installation Files

- **Windows:** `fluidnc-maslow4-1.16-win64.zip` (13.3 MB)
- **Mac/Linux:** `fluidnc-maslow4-1.16-posix.zip` (11.5 MB)
- **Firmware only:** `firmware.bin` (1.9 MB)
- **Web interface:** `index.html.gz` (133 KB)

### Quick Install

1. Download installer for your platform
2. Connect Maslow controller via USB
3. Run flash script
4. Optional: Upload new `index.html.gz` via Settings → System

**Your settings and calibration are preserved** - No recalibration needed!

---

## ⚠️ Important Notes

- ✅ Compatible with v1.15 configurations
- ✅ Existing calibration works without changes  
- ✅ No breaking changes
- ❌ No recalibration required

---

## 🙏 Contributors

**Core Team:**
- @BarbourSmith - Project lead and firmware

**Community Contributors:**
- @md8n - Web interface improvements
- @wouldchuckit - Documentation
- @asmith26 - Bug fixes
- Many others who tested and provided feedback

**Third Party:**
- ESP3D-WEBUI by luc-github

---

## 📚 More Information

- **Full Release Notes:** `RELEASE_NOTES_v1.16_USER.md` - Complete feature list
- **Technical Details:** `RELEASE_NOTES_v1.16.md` - Developer documentation
- **Repository:** https://github.com/MaslowCNC/Maslow_4
- **Documentation:** https://maslowcnc.github.io/Maslow_4/
- **Community:** https://forums.maslowcnc.com/

---

**Happy cutting! 🪚✨**
