# FluidNC for Maslow - Version 1.16 Release Notes

**Release Date:** December 26, 2025  
**Previous Version:** v1.15 (November 20, 2025)

---

## Quick Summary

Version 1.16 represents a **major infrastructure update** focused on integrating the ESP3D-WEBUI project directly into the Maslow_4 repository and improving documentation workflows. This release consolidates the web interface development into a single repository, making it easier for contributors to work on both firmware and UI together.

**Key Highlights:**
- ✨ **ESP3D-WEBUI Integration** - Web interface now included in main repository
- 📚 **Enhanced Documentation** - GitHub Pages deployment and improved CAD documentation
- 🔧 **Build System Improvements** - Updated workflows and stale PR/issue management
- 🌐 **Updated Web Interface** - Latest version with improved UI components

---

## 🎯 Major Changes

### 1. **ESP3D-WEBUI Integration** ✨

The most significant change in v1.16 is the integration of the [ESP3D-WEBUI project](https://github.com/luc-github/ESP3D-WEBUI) directly into the Maslow_4 repository. This web interface was previously maintained separately but is now part of the main codebase.

**What's Included:**
- Complete ESP3D-WEBUI source code in the `ESP3D-WEBUI/` directory
- Build system for generating compressed web interface (`index.html.gz`)
- Multi-language support (14 languages including English, Spanish, French, German, etc.)
- Comprehensive documentation for building and testing locally
- Web simulator for testing without hardware (`fluidnc-web-sim.py`)

**Benefits:**
- Unified development workflow - firmware and UI in one place
- Easier for contributors to make coordinated changes
- Version consistency between firmware and web interface
- Simplified build and release process

**For Developers:**
- See `ESP3D-WEBUI/COMPILATION.md` for build instructions
- See `ESP3D-WEBUI/HOWTO-Test-Locally.md` for testing guide
- Use `gulp package --lang en` to build English-only version (~122KB)
- Use `gulp package` to build all languages (~150KB+ - may exceed ESP32 storage)

### 2. **Documentation Infrastructure** 📚

**GitHub Pages Deployment**
- Added automated deployment workflow (`.github/workflows/deploy-docs.yml`)
- Documentation now automatically published to GitHub Pages
- Makes documentation more accessible to users

**CAD Documentation**
- Added README files to all CAD subdirectories:
  - `CAD/Accessories/AccessoriesReadme.md`
  - `CAD/Anchors/AnchorDesignsReadme.md`
  - `CAD/Frames/FramesReadme.md`
  - `CAD/Maslow3DPrintVariations/Maslow3DPrintVariationsReadme.md`
  - `CAD/MaslowMainBuild3DPrint/MaslowMainBuild3DPrintReadme.md`
  - `CAD/RouterAdaptors/RouterAdaptors.md`

### 3. **Build System & CI/CD Updates** 🔧

**Stale PR/Issue Management**
- New workflow: `.github/workflows/stale.yml`
- Automatically marks and closes inactive PRs and issues
- Helps keep the project organized and manageable
- Documentation: `.github/workflows/README-stale.md`

**Compilation Improvements**
- Updated `compile-on-review.yml` workflow
- Better handling of review-triggered builds
- Improved PR assignment automation

**Copilot Instructions**
- Updated `.github/copilot-instructions.md` with comprehensive build information
- Added details about ESP3D-WEBUI build system
- Improved guidance for AI-assisted development

---

## 📦 Updated Components

### Web Interface
- **Updated:** `index.html.gz` (132,623 bytes)
- **Size increase:** ~1.9KB from v1.15 (130,713 bytes)
- Includes latest ESP3D-WEBUI improvements and bug fixes

### Configuration Files
- **maslow.yaml** - Updated to v1.16 (6,603 bytes)
- No breaking configuration changes
- Compatible with v1.15 configurations

### Binary Assets
- **firmware.bin** - 1,985,296 bytes
- **Windows installer:** `fluidnc-maslow4-1.16-win64.zip` (13.3 MB)
- **POSIX installer:** `fluidnc-maslow4-1.16-posix.zip` (11.5 MB)

---

## 🔍 Technical Details

### Repository Structure Changes

The major structural change is the addition of the `ESP3D-WEBUI/` directory at the repository root:

```
Maslow_4/
├── ESP3D-WEBUI/          # NEW - Web interface source
│   ├── www/              # Source files
│   ├── dist/             # Built files
│   ├── languages/        # Language packs
│   ├── gulpfile.js       # Build configuration
│   ├── package.json      # NPM dependencies
│   └── docs/             # ESP3D documentation
├── firmware/             # ESP32 firmware (previously at root)
├── docs/                 # Documentation
├── CAD/                  # CAD files with new READMEs
└── .github/              # CI/CD workflows
```

### Build Process Changes

**Web UI Build:**
- Requires Node.js v20+
- Build command: `cd ESP3D-WEBUI && gulp package --lang en`
- Output: `ESP3D-WEBUI/dist/index.html.gz`
- Must be copied to `firmware/FluidNC/data/index.html.gz` for deployment

**Firmware Build:**
- No changes to firmware build process
- Still uses PlatformIO: `pio run -e wifi_s3`
- Web UI must be built separately and deployed

---

## 📋 File Changes Summary

**New Files:**
- Entire `ESP3D-WEBUI/` directory tree (~13,000+ lines added)
- 6 new CAD README files
- `.github/workflows/deploy-docs.yml`
- `.github/workflows/stale.yml`
- `.github/workflows/README-stale.md`

**Modified Files:**
- `.github/copilot-instructions.md` - Major updates (+91 lines)
- `.github/workflows/compile-on-review.yml` - Build improvements
- `.github/workflows/assign-issue-author-to-pr.yml` - Minor fixes
- `firmware/FluidNC/data/index.html.gz` - Updated web interface

**Total Changes:**
- Files changed: ~100+
- Lines added: ~13,000+
- Binary files: 20+ (language packs, images, etc.)

---

## 🚀 Upgrade Instructions

### From v1.15 to v1.16

**Standard Users:**
1. Download the firmware package for your platform (Win64 or POSIX)
2. Use the included flash tool to update firmware
3. Upload the new `index.html.gz` via the web interface (Settings → System)
4. Your existing `maslow.yaml` configuration will continue to work

**Developers:**
1. Pull the latest code from `Maslow-Main` branch
2. Install Node.js v20+ if not already installed
3. Build ESP3D-WEBUI: `cd ESP3D-WEBUI && npm install && gulp package --lang en`
4. Build firmware: `cd firmware && pio run -e wifi_s3`
5. Upload both firmware and web UI to your ESP32

**No Calibration Required:**
- Your machine calibration from v1.15 will be preserved
- No need to recalibrate unless you want to

---

## 🐛 Known Issues & Limitations

### From v1.15 (Still Applicable)
- None reported for v1.16 specifically
- See v1.15 release notes for any ongoing issues

### Build Considerations
- **Single language recommended:** Building all language packs may exceed ESP32 storage limits
- **Use English build:** `gulp package --lang en` for most deployments
- **Multi-language build:** Only if you have ESP32-S3 with 8MB+ flash

---

## 📚 Documentation Resources

### For Users
- **Quick Start Guide:** `docs/QuickStart.md`
- **User Guide:** See generated GitHub Pages
- **Calibration Guide:** In web interface

### For Developers
- **Building Web UI:** `ESP3D-WEBUI/COMPILATION.md`
- **Testing Locally:** `ESP3D-WEBUI/HOWTO-Test-Locally.md`
- **Firmware Build:** `.github/copilot-instructions.md`
- **Web Simulator:** `ESP3D-WEBUI/fluidnc-web-sim.py`

### Online Resources
- **GitHub Repository:** https://github.com/MaslowCNC/Maslow_4
- **Documentation Site:** https://maslowcnc.github.io/Maslow_4/
- **Community Forums:** https://forums.maslowcnc.com/

---

## 🤝 Credits & Acknowledgments

### ESP3D-WEBUI
- **Original Author:** Luc (luc-github)
- **Project:** https://github.com/luc-github/ESP3D-WEBUI
- **License:** GPL v3.0
- Integration into Maslow_4 repository for unified development

### Maslow CNC Team
- **@BarbourSmith** - Project lead and v1.16 integration
- **Community Contributors** - Testing, feedback, and bug reports

---

## 📊 Release Statistics

**Time Since Last Release:** 36 days (v1.15 on November 20, 2025)

**Repository Changes:**
- Commits: 1 major integration commit
- Files changed: 100+
- Lines added: ~13,000+
- Binary files: 20+
- Size increase: Repository now ~100MB larger due to ESP3D-WEBUI assets

**Downloads (from GitHub Release):**
- firmware.bin: 0 (just released)
- Windows installer: 0 (just released)
- POSIX installer: 0 (just released)
- index.html.gz: 0 (just released)
- maslow.yaml: 0 (just released)

---

## 🔜 Looking Forward

### Future Development Benefits

With ESP3D-WEBUI now integrated into the main repository:
- Faster iteration on UI/firmware coordination
- Easier for contributors to improve the web interface
- Better version synchronization between components
- Simplified release process

### Potential Future Features
- Enhanced web interface features
- Better mobile responsiveness
- Additional language support
- Improved local testing capabilities

---

## 💬 Feedback & Support

### Reporting Issues
- **GitHub Issues:** https://github.com/MaslowCNC/Maslow_4/issues
- **Forums:** https://forums.maslowcnc.com/

### Getting Help
- Check documentation first
- Search existing forum threads
- Ask questions in the community forums
- Report bugs on GitHub with detailed information

### Contributing
- Pull requests welcome!
- See `.github/copilot-instructions.md` for developer setup
- Test your changes thoroughly before submitting
- Follow existing code style and conventions

---

## 📄 License

This project remains under the GPL v3.0 license. The integrated ESP3D-WEBUI is also GPL v3.0 licensed.

---

**Thank you for using Maslow CNC!**

For the complete commit history and detailed file changes, visit:
https://github.com/MaslowCNC/Maslow_4/compare/v1.15...v1.16
