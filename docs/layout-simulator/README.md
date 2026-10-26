# Layout Simulator

Interactive tool for choosing anchor point spacing. It shades the area inside your
four anchors to show where Maslow 4 can be expected to cut well, and where cut
quality will start to degrade.

**[Open the Layout Simulator](index.html)**

## Why you want it

The usable cutting area is always smaller than the rectangle formed by your four
anchor points. How much smaller depends on the spacing you choose. This tool lets
you try a frame size before you build it, so you can check that the material you
intend to cut actually fits inside the good-quality region.

## Using it

1. Enter the **Height** and **Width** of your anchor point spacing, in millimeters
   (measured center-to-center between anchors). Values are capped at 5000 mm.
2. Enter the **Work Area Width** and **Work Area Height** of the material you plan
   to cut. The default is a 2438 x 1219 mm sheet (4x8 foot). This rectangle is drawn
   in the frame so you can see whether it lands inside the good region.
3. Adjust the **Resolution** slider to trade off between a finer grid and faster
   redraws.
4. Toggle the criteria checkboxes to see which one is limiting a given area.

Drag to pan and scroll to zoom the canvas.

## The four criteria

Each checkbox turns one limit on or off in the color coding. Hovering over a
checkbox in the tool shows the same explanation.

- **Belt Angle** - As the sled approaches an anchor point, the angle from the machine
  to that anchor gets more aggressive, which leads to accuracy problems.
- **Arm Contact** - The arms can only rotate so far before they contact the machine's
  uprights. The angle between adjacent arms has to stay between roughly 20 and 130
  degrees, and the angle between opposite arms has to stay above roughly 130 degrees.
- **Belt Tension** - In vertical orientation, tension in the upper belts approaches
  infinity as the sled moves directly between the two upper anchors.
- **Belt Length** - The belts are finite. Out of the box they cannot reach further
  than about 4419 mm (14.5 feet) from an anchor.

Areas are shaded from green (good) through yellow to red (expect trouble). Red does
not necessarily mean the machine will refuse to move there, it means accuracy and
reliability will suffer.

## Related

- [Frame Library](../FrameLibrary.md) - Frame designs, anchor requirements, and other
  frame calculators
- [Quick Start Guide](../QuickStart.md) - Choosing anchors and setting up your frame
- [Calibration Simulator](../calibration-simulation/) - Simulates the calibration
  process rather than the cutting area
- [Manual Anchor Locator](../manual-anchor-locator/) - Compute anchor coordinates from
  tape measure readings

## Provenance

This tool previously lived in its own repository at
[MaslowCNC/Layout-Simulator](https://github.com/MaslowCNC/Layout-Simulator) and was
published at `https://maslowcnc.github.io/Layout-Simulator/`. It has been moved here
so that it lives alongside the rest of the Maslow 4 documentation. It is licensed
under the GPL-3.0, see [LICENSE](LICENSE).

The color gradient helper is by Michele Locati and is used under the MIT license
(<https://gist.github.com/mlocati/7210513>).
