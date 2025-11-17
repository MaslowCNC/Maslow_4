# WebUI Modifications

This document describes modifications made to the ESP3D-WEBUI for FluidNC.

## Modified Features

### Smart Auto-Scroll for Terminal/Command Output

**Feature Request**: Serial output keeps scrolling, preventing users from reviewing historical output while new data arrives.

**Solution**: 
- Added JavaScript that dynamically injects a "Scroll to Bottom" button 
- The button appears when the user has scrolled away from the bottom of the command output
- Auto-scrolling now intelligently pauses when the user scrolls up
- A floating button with a double-chevron-down icon appears in the bottom-right corner when auto-scroll is paused
- Clicking the button instantly scrolls to the bottom and resumes auto-scrolling
- Auto-scroll automatically resumes if the user manually scrolls to within 5 pixels of the bottom

**Implementation**:
- Injected JavaScript at the end of index.html.gz
- Overrides `Monitor_check_autoscroll()` function to respect pause state
- Adds scroll event listener to `cmd_content` element
- Dynamically creates and injects the scroll-to-bottom button
- No changes to HTML structure - purely runtime JavaScript injection

**Modified Files**:
- `FluidNC/data/index.html.gz` - Added smart auto-scroll JavaScript

## Technical Details

The modification works by:
1. Injecting a self-executing JavaScript function at the end of the HTML
2. Overriding the `Monitor_check_autoscroll()` function to check a pause flag
3. Adding a scroll event listener to detect when user scrolls up/down
4. Dynamically creating and injecting a floating button element
5. Managing button visibility based on scroll state

This approach ensures:
- No breaking changes to existing functionality
- Works with the ESP3D-WEBUI v1.14 (2.1 branch) that FluidNC uses
- Can be easily updated or removed
- No build process required - direct HTML modification

## Version Information

- ESP3D-WEBUI: v1.14 (from 2.1 branch)
- FluidNC: See main README
- Modification: Injected JavaScript for smart auto-scroll
