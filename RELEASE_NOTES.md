# Maslow Version 1.20 Release Notes

Version 1.20 is a significant update focused on safety, usability, and reliability improvements. No changes to your maslow.yaml file are required with this update.

## New Features

**GCode line highlighting during execution.** The GCode file display in the tablet UI now highlights the line currently being cut in real time. Line numbers are shown alongside the GCode text so you can always see exactly where the machine is in the program.

**Z-axis automatically raises when Stop is pressed.** Pressing Stop now causes the Z-axis to lift to Z home + 2mm before the machine halts. This prevents the router bit from plunging into or dragging across the workpiece when a job is interrupted, reducing the risk of material damage.

**Stop button visual confirmation.** The Stop button turns orange the moment it is clicked to confirm your click was registered. It reverts to its normal color once the stop action has completed, giving clear feedback that the command is being processed.

## Bug Fixes

**Fixed pause button race condition.** A race condition in the pause logic could cause the machine to behave unpredictably when Pause was pressed during rapid motion. The issue has been resolved with atomic flag handling and improved event ordering, making Pause reliable in all conditions.

**Fixed "error 66" when loading a new GCode file.** After running one GCode file, attempting to load a second file could produce error 66 (too many open files). The SD card file handle limit has been increased from 1 to 4 to prevent this.

**Fixed position data not appearing in a second browser window during GCode execution.** When a new browser tab or window was opened while a job was running, it would not receive the current machine position until the job finished. Position data is now sent to new browser connections immediately.

**Fixed Z-axis raise height in imperial mode.** When the machine was in inch mode, the Z raise triggered by the Stop button used the wrong unit conversion and raised the axis by the wrong amount. It now correctly raises by 2mm regardless of whether the machine is in imperial or metric mode.

**Fixed Trace Outline units when machine is in inch mode.** The Trace Outline function now switches to millimeter mode internally before executing and restores the original unit setting when complete. This ensures the trace path is calculated and executed correctly even when the machine is configured for inches.

**Fixed browser disconnect during GCode execution.** Several related issues caused the Stop button to become unresponsive or the browser to fail to reconnect when the browser connection was interrupted while a GCode file was running. The WebSocket reconnection logic, stop command delivery, and browser state restoration have all been made more robust.

**Fixed belt and Z positions not saved reliably on stop.** Belt positions and the Z-axis position are now explicitly saved to non-volatile storage (NVS) when the Stop command is processed, ensuring the machine remembers its position correctly across power cycles even when stopped mid-job.

**Fixed false alarm triggered by watchdog during stop.** Under certain conditions — particularly when WiFi was transmitting data while Stop was being processed — the firmware watchdog could fire and put the machine into Alarm state instead of Idle. The stop sequence now resets the watchdog correctly, and the machine returns to Idle cleanly after stopping.

**Increased maximum frame dimension from 5000mm to 5500mm.** Users with larger frames were unable to enter the correct frame dimensions. The maximum allowed frame size has been increased to 5500mm.

**Fixed Z-axis motor direction pin in default configuration.** The default maslow.yaml configuration had an incorrect pin assignment for the Z-axis motor direction pin. The value has been corrected to NO_PIN to match the standard hardware wiring.
