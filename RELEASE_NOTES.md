# Maslow Version 1.18 Release Notes

Version 1.18 is a major update with significant improvements to safety, calibration reliability, usability, and the web interface. No changes to your maslow.yaml file are required with this update.

## Safety Improvements

**Fixed motor runaway when WiFi connection drops.** Previously, if the WiFi connection was interrupted while the machine was moving, the motors could continue running uncontrolled. This release fixes the watchdog timing, prevents blocking WebSocket sends, stops arm motors on reset, and adds a hardware safety backstop. A LED indicator now shows when the watchdog is active. This is the most important fix in this release.

**Motors now maintain tension during pause.** When you pause a job, the machine will now actively hold belt tension to prevent the sled from drifting, which previously could cause positional errors when resuming.

## Calibration Improvements

**Calibration is now significantly faster.** Belt retraction during calibration now runs in parallel instead of sequentially, movement speeds have been increased, and unnecessary pauses have been removed. Expect the calibration process to complete noticeably faster than before.

**Better calibration retry logic.** The calibration process now uses randomized starting positions when retrying, which improves the chances of finding a good calibration solution. Retry points are chosen based on aspect ratio to give better coverage of the search space.

**Fixed getting stuck in the "Home" state at the end of calibration.** A bug that caused the machine to become unresponsive after completing the anchor-finding step has been fixed.

**Fixed transition from anchor point locating failure.** The machine now correctly recovers and returns to a usable state when anchor point location fails.

**Aligned "Find Anchors" terminology** across the UI and state machine for consistency.

## Home Position Improvements

**New "Set Home Position" popup.** You can now type exact X/Y coordinates directly when setting the home position, instead of only being able to jog to position. Coordinates are automatically clamped to the work area.

**Fixed the Home button behavior.** The Home button was incorrectly moving the machine to the stored home position instead of the machine origin. This has been corrected.

**Fixed home position loss on WiFi interruption.** When WiFi was interrupted during GCode execution, the XY home position could be lost (showing NaN). This has been fixed.

## Interface Improvements

**Real-time progress display during file execution.** The tablet view now shows the current progress percentage ("Run: 45.2%") while a GCode file is running, and correctly reverts to showing the machine state when the file completes.

**Machine coordinates now shown alongside work coordinates.** The tablet view now displays both work coordinates and machine (absolute) coordinates so you always know where the machine physically is.

**Improved GCode preview.** The GCode preview now shows the full work area with borders, has a corrected aspect ratio, and properly centers the content in the canvas. Large-radius arcs also now zoom correctly.

**Active GCode files are now remembered across browser sessions.** If you reload the page or reconnect, the previously loaded GCode file will be restored automatically.

**New "Clear GCode from Memory" option.** A menu option has been added to the file selector to explicitly clear the currently loaded GCode from memory.

**Inch mode is now clearly indicated.** The unit toggle button turns yellow/gold when the machine is in inch mode so it is immediately obvious which unit system is active.

**Machine position readouts updated.** The absolute position readouts now use pipe symbols (|Xm: 0.000|) instead of parentheses for a cleaner look.

**State-dependent action button.** The main action button now changes its label and behavior based on the current machine state, making it clearer what action will happen when pressed.

**Trace Boundary button moved and always visible.** The Trace Boundary button has been moved to row 4 and remains visible even when disabled, making it easier to find.

**Captive portal detection fixed.** Devices using Linux (KDE/Ubuntu) that perform connectivity checks when connecting to the Maslow's WiFi access point will no longer get stuck in a captive portal loop.

**Expanded WiFi SSID character support.** The WiFi configuration field now accepts a wider range of characters for network names.

**Calibration popup closes on Test.** The calibration settings popup now correctly closes when the Test button is clicked.

## Z-Axis Improvement

**Z-stop now establishes a work coordinate offset.** When you set the Z stop, a G92 Z0 command is now issued to establish a proper work coordinate offset, which improves consistency when using Z-axis tool height setting.

## Developer Improvements

**Build scripts for custom test firmware.** Scripts have been added to simplify the process of building your own test firmware builds.

**Calibration simulators.** The calibration math is now shared between the web UI and standalone simulators, making it easier to test and validate calibration algorithm changes.

**Removed automatic update checking.** The background GitHub version check and auto-update notification functionality has been removed to simplify the codebase.

## Hardware

**MonolithMaslow 1.0 design added** to the community CAD resources.

**Updated TurtleClamp design** (v1.1) with quality-of-life improvements and new variants.

---

As always, let us know if you find any bugs or have ideas about how it could be better!
