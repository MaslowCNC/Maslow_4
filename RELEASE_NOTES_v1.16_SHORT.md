# FluidNC for Maslow - Version 1.16

**Release Date:** December 26, 2025

---

## Quick Summary

Version 1.16 represents a **major infrastructure update** focused on integrating the ESP3D-WEBUI project directly into the Maslow_4 repository. This release consolidates web interface development into the main codebase, making it easier for contributors to work on both firmware and UI together.

---

## 🎯 Top Changes

### 1. **ESP3D-WEBUI Integration** ✨

The most significant change in v1.16 is the integration of the ESP3D-WEBUI project directly into the Maslow_4 repository. The web interface source code is now included in the `ESP3D-WEBUI/` directory.

**What this means:**
- Unified development - firmware and UI in one place
- Easier for contributors to make coordinated changes
- Version consistency between firmware and web interface
- Multi-language support (14 languages available)

**For developers:**
- Build command: `cd ESP3D-WEBUI && gulp package --lang en`
- See `ESP3D-WEBUI/COMPILATION.md` for full build instructions
- Includes web simulator for testing: `fluidnc-web-sim.py`

### 2. **Documentation Improvements** 📚

**GitHub Pages Deployment**
- Documentation now automatically published to GitHub Pages
- Added README files to all CAD subdirectories
- Improved accessibility for users

### 3. **Build System Updates** 🔧

**Stale PR/Issue Management**
- Automatically marks and closes inactive PRs and issues
- Keeps the project organized and manageable

**Updated CI/CD Workflows**
- Improved compilation workflows
- Better handling of review-triggered builds

---

## 📦 Updated Files

### What You Need to Update

1. **firmware.bin** (1,985,296 bytes) - Main firmware
2. **index.html.gz** (132,623 bytes) - Web interface 
3. **maslow.yaml** (6,603 bytes) - Configuration file

### Installers

- **Windows:** `fluidnc-maslow4-1.16-win64.zip` (13.3 MB)
- **Mac/Linux:** `fluidnc-maslow4-1.16-posix.zip` (11.5 MB)

---

## 🚀 Upgrade Instructions

### Standard Users

1. Download the firmware package for your platform
2. Flash using the included installer
3. Upload `index.html.gz` via web interface (optional but recommended)
4. Your existing configuration will be preserved

### Developers

1. Pull latest from `Maslow-Main` branch
2. Install Node.js v20+ if needed
3. Build web UI: `cd ESP3D-WEBUI && npm install && gulp package --lang en`
4. Build firmware: `pio run -e wifi_s3`

**No recalibration required** - your v1.15 calibration will work with v1.16.

---

## 📋 Configuration Notes

- **No breaking changes** - v1.15 configurations work with v1.16
- Configuration file format unchanged
- All calibration data preserved

---

## 🐛 Known Issues

No specific issues reported for v1.16.

**Build Considerations:**
- Use single language build (`gulp package --lang en`) for most ESP32 boards
- Multi-language build may exceed storage on 4MB flash chips

---

## 🤝 Credits

**ESP3D-WEBUI Integration:**
- Original ESP3D-WEBUI by Luc (luc-github) - https://github.com/luc-github/ESP3D-WEBUI
- Licensed under GPL v3.0

**Maslow CNC Team:**
- @BarbourSmith - Project lead and v1.16 integration
- Community contributors - Testing and feedback

---

## 📚 Documentation

- **Repository:** https://github.com/MaslowCNC/Maslow_4
- **Documentation:** https://maslowcnc.github.io/Maslow_4/
- **Forums:** https://forums.maslowcnc.com/
- **Detailed Release Notes:** See `RELEASE_NOTES_v1.16.md`

---

## 💬 Support

- **Issues:** https://github.com/MaslowCNC/Maslow_4/issues
- **Forums:** https://forums.maslowcnc.com/

---

**Thank you for using Maslow CNC!**
