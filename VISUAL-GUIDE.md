# Visual Guide: Terminal Scroll-to-Bottom Feature

This document provides a visual representation of the scroll-to-bottom button feature.

## Normal State (Auto-Scrolling)

```
┌────────────────────────────────────────────────────┐
│ 🖥️ Terminal                         ☰ ⛶ ✕        │
├────────────────────────────────────────────────────┤
│ [Command Input Box]                      [Send]    │
│                                                     │
│ ┌────────────────────────────────────────────────┐ │
│ │ Grbl 1.1f ['$' for help]                       │ │
│ │ [MSG:INFO: FluidNC v3.x]                       │ │
│ │ ok                                              │ │
│ │ <Idle|MPos:0.000,0.000,0.000|FS:0,0>          │ │
│ │ ok                                              │ │
│ │ ...                                             │ │
│ │ New messages appear here and scroll up         │ │
│ │ Terminal auto-scrolls to show latest output ▼  │ │
│ └────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────┘
```

**Status**: Auto-scroll ENABLED and ACTIVE
- New messages appear at bottom
- Terminal scrolls automatically
- No scroll-to-bottom button visible


## Scrolled Up State (Auto-Scroll Paused)

```
┌────────────────────────────────────────────────────┐
│ 🖥️ Terminal                      ⏸️ ☰ ⛶ ✕        │
├────────────────────────────────────────────────────┤
│ [Command Input Box]                      [Send]    │
│                                                     │
│ ┌────────────────────────────────────────────────┐ │
│ │ Grbl 1.1f ['$' for help]                       │ │
│ │ [MSG:INFO: FluidNC v3.x]                       │ │
│ │ [MSG:INFO: Machine: Maslow CNC]                │ │
│ │ ok                                              │ │
│ │ ← USER SCROLLED UP TO VIEW THIS                │ │
│ │ <Idle|MPos:0.000,0.000,0.000|FS:0,0>          │ │
│ │ ok                                              │ │
│ │                                        ┌──────┐ │ │
│ │ New messages still arriving below...   │  ⇓⇓  │ │ │
│ │                                        └──────┘ │ │
│ └────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────┘
                                               ↑
                                    Scroll to Bottom Button
```

**Status**: Auto-scroll PAUSED
- User has scrolled up to review history
- Terminal stays at scrolled position
- New messages continue to arrive (but not visible)
- Pause icon (⏸️) appears in auto-scroll toggle
- **Scroll-to-bottom button (⇓⇓) appears in bottom-right**


## Button Details

### Visual Appearance

```
    ┌────────┐
    │   ⇓⇓   │  ← Double chevron down icon
    └────────┘
     ↑      ↑
   Blue    Circular
  #5755d9   shape
```

**Styling**:
- Background: Primary blue (#5755d9)
- Icon: White double-chevron-down
- Shape: Circle (border-radius: 50%)
- Shadow: Subtle drop shadow (0 2px 8px rgba(0,0,0,0.3))
- Size: Compact but easily clickable

**Hover State**:
```
    ┌────────┐
    │   ⇓⇓   │  ← Slightly elevated
    └────────┘
     ↑      ↑
  Darker  Enhanced
   blue    shadow
 #4240c2
```

- Background changes to darker blue (#4240c2)
- Button moves up slightly (translateY: -2px)
- Shadow increases (0 4px 12px rgba(0,0,0,0.4))
- Smooth 0.2s transition


## User Interaction Flow

### Scenario 1: Reviewing History While Job Runs

1. **Initial State**: Machine running G-code, terminal auto-scrolling
   ```
   Terminal showing latest output ▼
   ```

2. **User scrolls up**: Wants to check earlier error message
   ```
   Terminal paused at earlier position
   [Scroll to Bottom] button appears
   ```

3. **New data arrives**: Job continues, messages keep coming
   ```
   Terminal STAYS at user's scroll position
   Button remains visible
   ```

4. **User clicks button**: Done reviewing, wants to see current status
   ```
   Terminal instantly scrolls to bottom
   Auto-scroll resumes
   Button disappears
   ```

### Scenario 2: Manual Scroll Resume

1. **User scrolls up** → Button appears
2. **User manually scrolls to bottom** → Auto-scroll resumes automatically
3. **Button disappears** → Back to normal auto-scrolling


## Button Positioning

```
Terminal Panel Layout:
┌────────────────────────────────────┐
│ Header Bar                          │
├────────────────────────────────────┤
│ Input Field                         │
├────────────────────────────────────┤
│                                     │
│  Terminal Output Area               │
│                                     │
│                        ┌──────┐    │
│                        │  ⇓⇓  │◄───┼─── Button
│                        └──────┘    │    appears here
│                           ↑        │
└───────────────────────────┼────────┘
                            │
                     1rem from bottom
                     1rem from right
```

**Position**:
- Absolute positioning within terminal panel
- Bottom: 1rem (16px)
- Right: 1rem (16px)
- z-index: 100 (appears above terminal content)


## Tooltip

When hovering over the button:
```
    ┌─────────────────┐
    │ Scroll to bottom │ ← Tooltip
    └────────▲────────┘
             │
         ┌────────┐
         │   ⇓⇓   │
         └────────┘
```

Text: "Scroll to bottom"


## Integration with Existing Controls

The scroll-to-bottom button works alongside existing terminal features:

```
Terminal Menu:
├─ Verbose Mode Toggle      (☑/○)
├─ Auto-Scroll Toggle       (☑/○/⏸)  ← Shows pause when scrolled up
├─ Clear Terminal           (×)
└─ [Scroll to Bottom]                ← Only appears when paused
```

**Behavior**:
- If auto-scroll is DISABLED via toggle → Button does NOT appear
- If auto-scroll is ENABLED but paused by scrolling → Button APPEARS
- Button only shows when: `isAutoScroll && isAutoScrollPaused`


## Responsive Design

### Desktop (Wide Screen)
```
┌──────────────────────────────────────────────┐
│  Full width terminal                          │
│                              ┌──────┐         │
│                              │  ⇓⇓  │         │
│                              └──────┘         │
└──────────────────────────────────────────────┘
```

### Tablet (Medium Screen)
```
┌────────────────────────────┐
│  Responsive terminal        │
│                  ┌──────┐   │
│                  │  ⇓⇓  │   │
│                  └──────┘   │
└────────────────────────────┘
```

### Mobile (Narrow Screen)
```
┌──────────────────┐
│  Terminal        │
│        ┌──────┐  │
│        │  ⇓⇓  │  │
│        └──────┘  │
└──────────────────┘
```

Button maintains consistent position relative to panel edges on all screen sizes.


## Accessibility

- **Visual**: Clear icon (double chevron) indicates downward movement
- **Tooltip**: Text label "Scroll to bottom" on hover
- **Color Contrast**: White icon on blue background meets WCAG standards
- **Click Target**: Large enough for easy clicking (even on mobile)
- **Keyboard**: Terminal maintains keyboard scroll functionality


## Summary

The scroll-to-bottom button provides:
1. **Clear visual feedback** when auto-scroll is paused
2. **Easy access** to resume scrolling and see latest output
3. **Non-intrusive design** - only appears when needed
4. **Smooth animations** for professional feel
5. **Consistent behavior** with existing UI patterns

This implementation solves the original issue while maintaining a clean, professional interface.
