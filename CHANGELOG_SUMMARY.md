# FluidNC for Maslow - Change Summary

## Quick Summary of Changes Since v1.14

### What's New in v1.15 (November 20, 2025)

Version 1.15 was released just 6 days after v1.14, focusing on significant improvements to the calibration system and overall stability.

---

## 🎯 Top 3 Major Features

### 1. **Automatic Orientation Detection** ✨
Your Maslow machine will now automatically detect whether it's mounted horizontally or vertically during calibration. No more manual configuration needed!

### 2. **Smart Calibration Grid** 🎲
The firmware automatically selects the optimal calibration grid density (3x3, 5x5, 7x7, or 9x9) based on your frame size. Better accuracy without the guesswork.

### 3. **OTA Update Crash Fix** 💪
Fixed a critical crash that occurred after over-the-air firmware updates. Memory usage during version checks reduced from 18KB to 2KB through streaming JSON parser.

---

## 🐛 Key Bug Fixes

| Issue | Impact | Fix |
|-------|--------|-----|
| Buffer overflow in VERSION_NUMBER | Kinematics settings became inaccessible | Replaced strcpy with strncpy, proper bounds checking |
| Error 152 on power cycle | Machine wouldn't boot properly | Fixed malformed YAML in default config |
| OTA update crashes | Firmware unstable after updates | Deferred update check, streaming JSON parser |
| Calibration grid shift | Reduced accuracy on rectangular frames | Fixed spiral start position calculation |
| Test function crash | Repeated tests caused crashes | Fixed WiFiClientSecure cleanup |
| Missing MaslowKinematics | Configuration loading issues | Added to default configuration |

---

## 🔧 Improvements by Category

### Calibration & Kinematics
- ✅ Automatic orientation detection (horizontal vs vertical)
- ✅ Automatic grid density selection
- ✅ Fixed calibration grid shift for rectangular frames
- ✅ Fixed center coordinate calculation after loading results
- ✅ Motor settling pause after orientation detection

### Stability & Reliability  
- ✅ OTA update crash fix with streaming parser
- ✅ Fixed buffer overflow in version string
- ✅ Fixed Error 152 on power cycle
- ✅ Configuration loading improvements
- ✅ Test function crash fix

### User Experience
- ✅ Better error messages (error:3 now shows context)
- ✅ Firmware update check moved to TEST command (faster startup)
- ✅ Reduced logging verbosity
- ✅ Cleaner console output

### Build System
- ✅ Auto-detect platformio command
- ✅ Dynamic version extraction from git tags
- ✅ More portable build scripts

---

## 📊 By The Numbers

- **13** major pull requests merged
- **80+** commits
- **6** days of active development
- **18KB → 2KB** memory reduction for update checks
- **0** breaking changes

---

## 🎓 What This Means For Users

### If You're Upgrading from v1.14:

**Good News:**
- Calibration will be easier and more accurate
- Firmware updates won't crash your machine anymore
- Configuration errors are explained better
- Startup is faster (update check moved to TEST command)

**What Changed:**
- Orientation detection is now automatic (but you can still override manually)
- Calibration grid is selected automatically based on your frame size
- Update checks only happen when you run TEST command

**Action Required:**
- None! Just install the update and enjoy the improvements

---

## 🔮 What's Coming Next

Based on ongoing work, future releases may include:
- Further memory optimizations (PR #440 in progress)
- Additional calibration enhancements
- More diagnostic improvements

---

## 📥 Download v1.15

Choose your platform:
- [Windows (win64.zip)](https://github.com/BarbourSmith/FluidNC/releases/tag/v1.15)
- [macOS/Linux (posix.zip)](https://github.com/BarbourSmith/FluidNC/releases/tag/v1.15)
- [Direct firmware.bin](https://github.com/BarbourSmith/FluidNC/releases/tag/v1.15)

---

## 📝 Full Details

For complete technical details, see [RELEASE_NOTES_v1.15.md](RELEASE_NOTES_v1.15.md)

---

*FluidNC for Maslow is a specialized fork optimized for Maslow CNC machines*
