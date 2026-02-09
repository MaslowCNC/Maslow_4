# Connection Status Indicator UI Visualization

## Location

The connection status indicator is positioned directly below the "Setup" button in the Maslow tablet interface.

## Layout

```
┌──────────────────────────────────────────────────┐
│  Maslow Tablet Interface - Control Grid          │
├──────────────────────────────────────────────────┤
│                                                   │
│  Row 1: Control Buttons                          │
│  ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌──────────┐       │
│  │ Z↑ │ │ ↖  │ │ ↑  │ │ ↗  │ │  Setup   │       │
│  │    │ │    │ │    │ │    │ │  (gear)  │       │
│  └────┘ └────┘ └────┘ └────┘ └──────────┘       │
│                                                   │
│  Row 2: Distance & Connection Status             │
│  ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌──────────────┐   │
│  │  5 │ │ ←  │ │100 │ │ →  │ │ Connection   │   │
│  │    │ │    │ │    │ │    │ │   Status     │   │
│  └────┘ └────┘ └────┘ └────┘ └──────────────┘   │
│                                    ▲              │
│                                    │              │
│                          New indicator here!     │
└──────────────────────────────────────────────────┘
```

## Visual States

### 1. Good Connection (Green)
```
┌────────────────────────────────────┐
│ ✓ Connected (45ms, 0.5% loss)     │  <- Green background
│                                    │     White text
└────────────────────────────────────┘
Background color: #4aa85c (green)
```

### 2. Degraded Connection (Orange)
```
┌────────────────────────────────────┐
│ ⚠️ Degraded (235ms, 8.2% loss)    │  <- Orange background
│                                    │     White text
└────────────────────────────────────┘
Background color: #ffa500 (orange)
```

### 3. Poor Connection (Dark Orange)
```
┌────────────────────────────────────┐
│ ⚠️ Poor connection (32.5% loss)   │  <- Dark orange background
│                                    │     White text
└────────────────────────────────────┘
Background color: #ff6600 (dark orange)
```

### 4. Connection Lost (Red)
```
┌────────────────────────────────────┐
│ ✗ Connection lost (78.3% loss)    │  <- Red background
│                                    │     White text
└────────────────────────────────────┘
Background color: #ce654c (red)
```

### 5. Multiple Tabs Warning (Orange)
```
┌────────────────────────────────────┐
│ ⚠️ Multiple tabs detected (7)     │  <- Orange background
│                                    │     White text
└────────────────────────────────────┘
Background color: #ff8c00 (warning orange)
```

### 6. Initializing (Gray)
```
┌────────────────────────────────────┐
│ Initializing...                    │  <- Gray background
│                                    │     Black text
└────────────────────────────────────┘
Background color: #888 (gray)
```

## Design Details

### Styling
- **Padding**: 3px horizontal, 6px vertical
- **Border radius**: 3px (slightly rounded corners)
- **Font size**: 0.75rem (responsive, scales with browser settings)
- **Font weight**: Bold
- **Text alignment**: Center
- **Text overflow**: Ellipsis (...) if too long
- **White space**: No wrap (single line)

### Color Scheme
The color scheme matches the existing Maslow UI:
- Green (#4aa85c): Matches the "Play" button color (good state)
- Red (#ce654c): Matches the "Stop" button color (error state)
- Orange/Yellow: Warning states
- Gray (#f2f0e4): Matches surrounding button background

### Responsiveness
- Uses `rem` units for font size (not viewport width)
- Flexbox layout ensures proper centering
- Text overflow handled with ellipsis
- Maintains readability across different screen sizes

## Information Displayed

The indicator dynamically shows:

1. **Status Icon**
   - ✓ (checkmark): Good connection
   - ⚠️ (warning): Degraded or multiple tabs
   - ✗ (cross): Connection lost

2. **Status Text**
   - "Connected", "Degraded", "Poor connection", "Connection lost"
   - "Multiple tabs detected"
   - "Initializing..."

3. **Metrics** (when available)
   - Latency in milliseconds: "(45ms)"
   - Packet loss percentage: "(0.5% loss)"
   - Foreign ping count: "(7 foreign pings)"

## User Experience

### Typical Usage Flow

1. **Page Load**: Shows "Initializing..." in gray
2. **Connection Established**: Quickly changes to green "✓ Connected (XXms, X.X% loss)"
3. **Normal Operation**: Green indicator with real-time latency updates
4. **Network Issues**: Transitions through orange states as connection degrades
5. **Multiple Tabs**: Shows warning if user opens another tab
6. **Disconnection**: Red indicator alerts user to connection loss

### Update Frequency

The indicator updates:
- Every ping cycle (250ms) when status changes
- Only when status changes (not every ping to avoid flickering)
- Immediately on connection events (connect/disconnect)

## Accessibility

- **Color**: Not the only indicator (icons + text also used)
- **Contrast**: White text on colored backgrounds meets WCAG AA standards
- **Font size**: Responsive units scale with user preferences
- **Clarity**: Clear, concise messages

## Future Enhancement Ideas

1. Add tooltip with detailed statistics on hover
2. Clickable to show connection history graph
3. Animated pulse on state changes
4. Sound alerts for connection loss (optional)
5. Integration with notification system
