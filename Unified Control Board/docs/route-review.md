# Unified Control Board 36V v1.1: route review

Every routed net, traced one at a time, for stubs, loops, detours and anything else odd. This file is the running
log. It started on 2026-11-21 from commit 1f082cd76.

## How the routes are traced

`tools/netreview.py` builds each net's copper the way KiCad connects it, with one difference: it errs on the side of
calling copper connected. A via or track end whose copper overlaps a track or pad counts as connected, as KiCad's own
ratsnest does. On that model it reports:

- **dead**: track pieces and vias that no path between two pads (or a pad and a zone) uses. These are stubs, including
  the ones DRC misses because their end happens to touch something.
- **loop**: copper that closes a loop. Links through zones don't count.
- **one-layer via**: a via whose copper is all on one layer, so it joins nothing between layers.
- **detour**: a pad-to-pad copper path much longer than the straight line between the pads. Only pairs whose path
  passes no third pad are checked.

It also draws each net on its own, so every route can be looked at.

## Step 1: stubs and fragments

- 19 dead pieces in 17 nets were removed or trimmed. All were under 0.5 mm: overshoots past a bend, or bits poking
  out of a via or pad. DRC does not count them as dangling because their ends touch something.
- 267 fragments were removed: segments lying wholly inside a via or pad of their own net, or shorter than 0.01 mm.
  They are left over from the router (Net-(U2-IPROPI) alone had 53 near-zero pieces). Some 0.5–2.2 mm runs inside big
  pads went too.
- GHB1_G's via at (−16.98, 42.08) joined two bottom-layer tracks that ended 0.23 mm apart inside it. They now meet at
  its centre and the via is gone.
- Result: 3194 → 2914 segments and 597 → 596 vias. DRC with schematic parity: no errors, nothing unconnected, nothing
  dangling, 219 warnings (was 237).

## Step 2: staircases and wiggles

- `tools/route_fix.py pull` takes each chain of same-layer, same-width segments between real junctions (pads, vias,
  zone edges, T-junctions, width changes) and string-pulls it. From each point it jumps to the farthest later point
  reachable in a straight line with full clearance: the board's rules plus 0.03 mm.
- The chain's ends stay put, so nothing gets connected or disconnected.
- Left alone on purpose: the sense nets (IS*, S[ABC][12]), U9_SYNC and the USB data pair, whose spacing was chosen.
- Result: 232 chains, 1294 → 747 segments, 82.7 mm of track gone. DRC with schematic parity: no errors, 201 warnings.
  The nine "new" ones are the same narrow VCC, USB5V and phase runs as before, now as merged segments with their
  widths kept.

## Step 3: detours, first pass

`tools/detours.py` drives `tools/reroute.py` over every pad-to-pad detour the checker reports, worst first:

- It takes the copper path between the two pads and splits it at real junctions.
- It removes the longest chain, including router fragments sitting on it, and joins the two parts of the net again
  by the shortest route kirouter finds. That route keeps full clearance plus 0.03 mm, uses top, In2 and bottom (never
  In1, the VCC plane that feeds the motor stages) and keeps off the analog nets.
- The new route is kept only if it is at least 15 % and 2 mm shorter, and a full DRC afterwards shows nothing new:
  no error, unconnected item or dangling track. Otherwise the board goes back.

A first run without the DRC gate was thrown away. It left router fragments disconnected, and some new routes split
GND and 3V3 pour islands.

Kept (old → new chain length):

| Net | Before | After |
|---|---|---|
| SCL | 72.5 mm, 5 vias | 48.6 mm, 3 vias |
| GHC1_G | 48.1 mm, 3 vias | 18.6 mm, 1 via (the high-side gate trace no longer loops round the FET rows) |
| BoardCOM2 | 99.6 mm, 7 vias | 73.9 mm, 3 vias |
| INLC2 | 35.6 mm, 3 vias | 27.8 mm, 2 vias |
| INHA2 | 60.0 mm, 3 vias | 46.1 mm, 4 vias |
| SDA3 | 84.2 mm, 5 vias | 65.7 mm, 3 vias |
| BR IN1 | 99.9 mm, 4 vias | 83.2 mm, 4 vias |
| VSUP (D2 to the hub) | 16.4 mm | 13.2 mm |
| SDA (R7 to U6) | 3.6 mm, 1 via | 1.1 mm |
| USB5V (VBUS pin B4 to B9) | 15.1 mm | 5.3 mm |
| Net-(U5-IPROPI) | 42.5 mm | 34.7 mm |
| GD_REF | 12.6 mm | 7.0 mm |
| GHB1 | 13.5 mm | 11.2 mm |
| ADC BL | 9.0 mm, 2 vias | 6.1 mm, 1 via |

Undone by DRC: SDA1, INHC1, BEAMBREAK, INLC1, BoardCOM1 and SWCLK.

DRC with schematic parity afterwards: no errors, nothing unconnected or dangling, 198 warnings. The two new warnings
are the new USB5V pin link, which is as narrow as the one it replaced.
