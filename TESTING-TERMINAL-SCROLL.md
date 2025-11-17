# Testing Terminal Auto-Scroll Feature

This document provides test cases for verifying the smart auto-scroll feature in the terminal panel.

## Prerequisites
- FluidNC device or emulator with WiFi enabled
- Access to the Web UI at the device's IP address
- Ability to generate serial output (e.g., run G-code or send commands)

## Test Cases

### Test 1: Default Auto-Scroll Behavior
**Objective**: Verify terminal auto-scrolls by default

**Steps**:
1. Open the Web UI and navigate to the Terminal panel
2. Send multiple commands that generate output (e.g., `$$` to show settings)
3. Continue sending commands to generate more than one screen of output

**Expected Result**:
- Terminal output should automatically scroll to show the latest messages
- The scroll-to-bottom button should NOT be visible
- New messages should appear at the bottom as they arrive

**Status**: ⬜ Not tested

---

### Test 2: Scroll-Up Pauses Auto-Scroll
**Objective**: Verify that scrolling up pauses auto-scroll and shows the button

**Steps**:
1. Generate continuous serial output (e.g., run a G-code file or use `?` status queries)
2. While output is being generated, scroll UP in the terminal window
3. Observe the terminal behavior

**Expected Result**:
- Auto-scrolling should pause (terminal stays at scrolled position)
- A circular "Scroll to Bottom" button should appear in the bottom-right corner
- Button should have a double-chevron-down icon
- Terminal should stay at the scrolled position even as new data arrives
- The auto-scroll toggle button should show a "pause" icon

**Status**: ⬜ Not tested

---

### Test 3: Scroll-to-Bottom Button Functionality
**Objective**: Verify the button scrolls to bottom and resumes auto-scroll

**Steps**:
1. Follow Test 2 to get into paused state with button visible
2. Click the "Scroll to Bottom" button

**Expected Result**:
- Terminal should immediately scroll to the bottom
- The button should disappear
- Auto-scrolling should resume
- New messages should continue to auto-scroll

**Status**: ⬜ Not tested

---

### Test 4: Manual Scroll to Bottom Resumes Auto-Scroll
**Objective**: Verify manual scroll to bottom also resumes auto-scroll

**Steps**:
1. Follow Test 2 to get into paused state
2. Manually scroll to the very bottom of the terminal (without clicking the button)

**Expected Result**:
- When within 5 pixels of the bottom, the button should disappear
- Auto-scrolling should resume automatically
- The auto-scroll toggle should no longer show pause icon

**Status**: ⬜ Not tested

---

### Test 5: Auto-Scroll Toggle Still Works
**Objective**: Verify the existing auto-scroll toggle button still functions

**Steps**:
1. Click the auto-scroll toggle button in the terminal menu to disable auto-scroll
2. Generate new terminal output
3. Click the toggle again to re-enable auto-scroll

**Expected Result**:
- When auto-scroll is disabled via toggle, terminal should NOT auto-scroll
- The scroll-to-bottom button should NOT appear (it only appears when auto-scroll is enabled but paused)
- Re-enabling auto-scroll should resume normal auto-scrolling behavior

**Status**: ⬜ Not tested

---

### Test 6: Button Appearance and Styling
**Objective**: Verify button visual appearance

**Steps**:
1. Get into paused state with button visible
2. Inspect the button appearance
3. Hover over the button

**Expected Result**:
- Button should be circular with primary blue color (#5755d9)
- Button should have white icon (double chevron down)
- Button should have a subtle drop shadow
- On hover, button should:
  - Change to darker blue (#4240c2)
  - Move slightly up (translateY)
  - Have enhanced shadow
  - Transition smoothly (0.2s)
- Tooltip should appear on hover showing "Scroll to bottom"

**Status**: ⬜ Not tested

---

### Test 7: Responsive Behavior
**Objective**: Verify button works on different screen sizes

**Steps**:
1. Test on desktop browser (wide screen)
2. Test on tablet viewport
3. Test on mobile viewport
4. Test in fullscreen mode

**Expected Result**:
- Button should remain visible and positioned correctly in bottom-right
- Button should not overlap terminal content
- Button should be easily clickable on all screen sizes
- Tooltip should be readable on all devices

**Status**: ⬜ Not tested

---

### Test 8: Verbose Mode Interaction
**Objective**: Verify feature works correctly with verbose mode toggle

**Steps**:
1. Enable verbose mode
2. Generate terminal output
3. Scroll up to pause
4. Toggle verbose mode on/off

**Expected Result**:
- Button should continue to work correctly
- Scrolling behavior should be consistent regardless of verbose mode
- Button should maintain position when verbose mode changes

**Status**: ⬜ Not tested

---

## Known Limitations

None identified at this time.

## Reporting Issues

If any test fails or unexpected behavior occurs:

1. Note which test case failed
2. Document the actual behavior vs expected behavior
3. Include:
   - Browser type and version
   - Device type (desktop/mobile/tablet)
   - Screen size
   - Console errors (if any)
   - Screenshots or video if possible
4. Report to the GitHub issue tracker

## Test Results Summary

| Test Case | Status | Notes |
|-----------|--------|-------|
| Test 1: Default Auto-Scroll | ⬜ Not tested | |
| Test 2: Scroll-Up Pauses | ⬜ Not tested | |
| Test 3: Button Functionality | ⬜ Not tested | |
| Test 4: Manual Scroll Resume | ⬜ Not tested | |
| Test 5: Toggle Still Works | ⬜ Not tested | |
| Test 6: Button Styling | ⬜ Not tested | |
| Test 7: Responsive Behavior | ⬜ Not tested | |
| Test 8: Verbose Mode | ⬜ Not tested | |

---

*Last updated: 2025-11-17*
