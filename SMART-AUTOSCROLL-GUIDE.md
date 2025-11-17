# Smart Auto-Scroll Feature - What to Expect

## Visual Description

When using the FluidNC WebUI Commands panel:

### Normal State (Auto-scrolling)
```
┌─────────────────────────────────────────┐
│ Commands                        [Clear] │
├─────────────────────────────────────────┤
│                                         │
│ Grbl 1.1f ['$' for help]               │
│ [MSG:INFO: FluidNC v3.x]               │
│ ok                                      │
│ ...                                     │
│ Latest messages auto-scroll here ▼     │
│                                         │
└─────────────────────────────────────────┘
[Send Command] [✓ Autoscroll] [✓ Verbose]
```
- Terminal shows latest messages
- Auto-scroll is working
- NO button visible

### Scrolled Up State (Paused)
```
┌─────────────────────────────────────────┐
│ Commands                        [Clear] │
├─────────────────────────────────────────┤
│                                         │
│ Grbl 1.1f ['$' for help]               │
│ [MSG:INFO: Machine: Maslow CNC]        │
│ ok ← Viewing this older message         │
│ ...                                     │
│                                    ┌──┐ │
│ (New messages arriving below...)  │⇓⇓│ │
│                                    └──┘ │
└─────────────────────────────────────────┘
[Send Command] [✓ Autoscroll] [✓ Verbose]
                     ↑
              Shows pause icon
```
- User has scrolled up
- Button appears bottom-right (blue circle with ⇓⇓)
- New messages continue arriving (not visible)
- Terminal stays at scrolled position

## Button Appearance

The scroll-to-bottom button:
- **Shape**: Circle
- **Color**: Blue (#337ab7)  
- **Icon**: Double chevron down (⇓⇓) in white
- **Size**: 50x50 pixels
- **Position**: Bottom-right corner, 10px from bottom, 20px from right
- **Hover**: Darkens to #286090, lifts slightly, shadow increases
- **Tooltip**: "Scroll to bottom"

## How to Test

1. **Start with auto-scroll ON** (checkbox checked)
2. **Send some commands** to generate output
3. **Scroll up** in the command output area with mouse wheel or scrollbar
4. **Observe**: Blue button should appear in bottom-right corner
5. **Click the button**: Should jump to bottom, button disappears
6. **Alternative**: Manually scroll to bottom instead of clicking

## Expected Behavior

### Scrolling Up
- ✅ Auto-scroll pauses immediately
- ✅ Button fades in (bottom-right)
- ✅ Terminal stays at your scroll position
- ✅ New messages still arrive (in background)

### Clicking Button
- ✅ Instantly scrolls to bottom
- ✅ Button fades out
- ✅ Auto-scroll resumes
- ✅ See all latest messages

### Manual Scroll to Bottom
- ✅ Get within 5px of bottom → button disappears
- ✅ Auto-scroll resumes automatically

### Turning Off Auto-scroll
- ❌ Button will NOT appear (auto-scroll disabled)
- ✅ Manual scrolling still works
- ✅ Turn auto-scroll back on to use smart features

## Troubleshooting

**Button doesn't appear when scrolling up:**
- Check that Autoscroll checkbox is enabled
- Try scrolling up more (needs clear upward scroll)
- Wait 1-3 seconds for initialization
- Refresh the page if needed

**Button appears but doesn't work:**
- Check browser console for JavaScript errors
- Try clicking directly on the button icon
- Verify WebUI version is correct (v1.14)

**Old behavior still happening (always scrolls):**
- Clear browser cache: Ctrl+Shift+R (or Cmd+Shift+R on Mac)
- Hard reload the page
- Check that firmware was flashed with new index.html.gz

## Browser Compatibility

Tested and working on:
- ✅ Chrome/Edge (modern versions)
- ✅ Firefox (modern versions)  
- ✅ Safari (modern versions)

The feature uses standard JavaScript (ES5+) and should work on any modern browser.

## Visual Mockup

```
Terminal Panel when scrolled up:

╔════════════════════════════════════════╗
║ Commands                       [Clear] ║
╠════════════════════════════════════════╣
║                                        ║
║ [MSG:INFO: FluidNC v3.8.1]            ║
║ Machine: Maslow CNC                    ║
║ ok                                     ║
║ G90                                    ║
║ ok                                     ║
║ ...                                    ║
║                                   ╔══╗ ║
║                                   ║⇓⇓║ ║ ← Click here
║                                   ╚══╝ ║
╚════════════════════════════════════════╝
```

The button is always visible when you're scrolled away from the bottom and auto-scroll is enabled!
