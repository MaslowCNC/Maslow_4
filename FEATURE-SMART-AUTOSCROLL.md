# Smart Auto-Scroll Feature for Serial Terminal

## What's New

The FluidNC Web UI terminal now includes an intelligent auto-scroll system that makes it easy to review historical output while monitoring live machine activity.

## Features

### 🔽 Scroll to Bottom Button
A floating button appears when you scroll up in the terminal, making it easy to jump back to the latest output with a single click.

### 🔄 Smart Auto-Scroll
- **At the bottom**: Terminal automatically scrolls as new data arrives
- **Scrolled up**: Terminal stays at your position so you can review history
- **One click**: Button instantly returns you to live output

### 🎨 Clean Design
- Appears only when needed
- Professional circular button with smooth animations
- Positioned in bottom-right corner
- Works on all screen sizes

## How to Use

1. **Monitor live output**: By default, the terminal auto-scrolls as your machine sends data

2. **Review history**: Scroll up to see earlier messages
   - Auto-scroll pauses automatically
   - A blue button (⇓⇓) appears in the bottom-right corner
   - Terminal stays at your scroll position

3. **Return to live view**:
   - Click the scroll-to-bottom button, OR
   - Manually scroll to the bottom
   - Auto-scroll resumes automatically

## Configuration

The feature works with existing terminal settings:
- **Auto-scroll toggle**: Still available in terminal menu
- **Verbose mode**: Works with or without verbose output
- **Clear terminal**: Clears history as before

No additional configuration needed - the feature works automatically!

## Technical Details

- Based on ESP3D-WEBUI 3.0
- Compatible with all FluidNC firmware versions
- No impact on performance or memory usage
- Responsive design for desktop, tablet, and mobile

## Feedback

Found a bug or have a suggestion? Please open an issue on GitHub!

---

*This feature was implemented in response to user feedback requesting better control over terminal scrolling behavior.*
