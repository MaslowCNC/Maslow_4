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
