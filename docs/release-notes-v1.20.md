---
layout: default
title: Release Notes v1.20
---

# Maslow Version 1.20 Release Notes

Version 1.20 is a major update focused on safety, reliability, and usability improvements. **Users who rely on the Aux2 output pin should update their maslow.yaml file** — see the configuration fix below. All other users can update firmware and index.html.gz without changing their maslow.yaml.

## Stop Button Improvements

**Z axis now raises to a safe height when stop is pressed.** Previously, pressing stop would halt XY motion but leave the router bit at its current depth, risking damage to your workpiece if you then jogged or pressed home. The machine now raises the Z axis to Z home + 2mm automatically when stop is pressed, keeping the bit clear of the material. This works correctly regardless of whether the machine is in mm or inch mode.

**Stop button now turns orange when clicked.** The stop button now immediately changes to orange when you click it so you have visual confirmation that your click was registered. It reverts to its normal color once the firmware confirms the machine has stopped.

**Stop button now works reliably after a browser reconnect.** If your browser disconnects and reconnects while a large GCode file is running, the stop button would silently fail — the `$STOP` command was sent with a stale connection ID that the firmware rejected. The stop command is now sent directly over WebSocket (bypassing the old HTTP routing), retried every 300ms until the firmware confirms the machine has stopped, and automatically resent when the WebSocket reconnects. This makes stop much more reliable during long jobs.

**Stop now cleanly halts all motion.** Several edge cases where motion could continue briefly after stop was pressed have been fixed: residual PID integral buildup no longer causes post-stop XY drift, belt motor step targets are synced to encoder positions so the PID has no error to correct, and pressing stop while paused (in Hold state) no longer causes the main loop to hang.

## Pause Button Fix

**Pause button now reliably pauses the machine.** When running a GCode file, pressing pause near a command boundary could be silently ignored — the machine would continue running as if nothing happened. Four overlapping race conditions in the protocol loop were identified and fixed, including a dual-core timing issue where a cycle-start event could be processed before the pause command on the ESP32-S3's second core. Pause is now reliable regardless of when during execution you press it.

## GCode Execution Display

**Each line of GCode is now highlighted as it executes.** The GCode viewer in the web interface now tracks and highlights the line currently being executed by the machine, and automatically scrolls to keep it in view. Line numbers are also now shown alongside each line in the viewer. This makes it easy to follow along with a long file and to know exactly where the machine is in the program.

## Reliability Improvements

**New browser windows now show position immediately during GCode execution.** If you opened a second browser tab while a GCode file was running, the position display would be blank until the browser completed its configuration handshake with the firmware (~500ms). The firmware now immediately begins sending position data to new connections, so position is visible right away.

## Frame Size Improvement

**Maximum supported frame dimension increased from 5000mm to 5500mm.** Builders with larger frames can now configure their frame size correctly. The kinematics validation, calibration solver, and web UI have all been updated to accept dimensions up to 5500mm.

## Trace Outline Fix

**Trace Outline now works correctly in imperial mode.** When the machine was in inch mode (G20), the Trace Outline function generated movement coordinates in mm but did not switch to mm mode, causing the coordinates to be interpreted as inches — a ~25.4x overshoot. Trace Outline now forces G21 (mm) mode at the start of the trace and restores the original unit mode when complete.

## Configuration Fix

**GPIO 38 conflict with Aux2 resolved.** The default maslow.yaml had GPIO 38 incorrectly assigned as the direction pin for the Z-axis secondary motor, conflicting with Aux2 output functionality. This has been corrected to `NO_PIN`. **If you use Aux2 and have updated your maslow.yaml from a previous release, add `direction_pin: NO_PIN` under `z_axis: motor1:` to resolve the conflict.** Users installing fresh will get the correct configuration automatically.

## Documentation Improvements

**Quick Start Guide enhanced with safety measures.** The Quick Start Guide now includes an overall safety measures section covering important precautions to take before and during machine operation.

**New Troubleshooting Guide added.** A comprehensive troubleshooting guide has been added to the documentation covering common issues with belt tension and calibration, YAML configuration problems, and firmware update procedures.
