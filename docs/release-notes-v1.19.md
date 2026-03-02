# Maslow Version 1.19 Release Notes

Version 1.19 is a focused bug-fix release addressing issues encountered during the Find Anchors calibration sequence. No changes to your maslow.yaml file are required with this update.

## Calibration Improvements

**Fixed false emergency stop during Find Anchors.** After the Find Anchors computation completed and the anchor values were saved, the firmware could trigger a false emergency stop with the message "Update function not being called enough." This happened because two code paths blocked the main loop for more than 100ms without resetting the watchdog timer:

1. Starting calibration (`maslow_start_calibration`) performs memory allocation and position computation before returning.
2. Saving the 8 anchor coordinate settings in rapid succession triggered a full configuration validation traversal that had no watchdog protection.

Both paths now reset the watchdog timer correctly, preventing the false emergency stop.

**Reduced spurious error messages during belt transitions.** The message "MaslowKinematics: Failed to compute X,Y from belt lengths" was being logged repeatedly during normal belt state transitions (while extending, retracting, taking slack, and releasing tension). These states are expected to produce forward-solve failures because the belt lengths are in motion and kinematics cannot produce valid results. The message is now suppressed during these transitional states and only shown in states where a failure is unexpected (such as during cutting or active calibration computation).
