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

## Step 3b–3d: more detour passes

| Pass | Rule | Kept (old → new chain) |
|---|---|---|
| 3b | 8 % and 3 mm shorter, at most one extra via | 12V_GD 74.7 → 63.9 mm; INLC1 66.3 → 54.8 mm; VAC_CONTROL 116.5 → 101.1 mm, 5 → 3 vias; INHC2 53.2 → 45.5 mm, 4 → 3 vias; SWDIO 22.4 → 18.8 mm, 2 → 3 vias |
| 3c | finer router grid; kirouter now also keeps out of keep-outs drawn inside footprints (J1's no-via area, which DRC had rejected SWCLK for) | DETECTORLED 90.3 → 78.9 mm, 4 → 5 vias; SWCLK 24.1 → 19.3 mm; GLB2_G 30.7 → 26.0 mm |
| 3d | `tools/repair.py` runs before the DRC gate: it removes copper the re-route left dead and stitches GND pour it cut off | INHB2 62.5 → 57.1 mm, 3 → 1 vias |

Left as they are:

- **INLA1, SCL, BL IN2.** Their shorter routes split the GND pour so that DRC found pads no longer connected. For
  INLA1 the split is inside the current-sense filter block, and for SCL it is U6 pin 13's pour. There is no room
  for a stitching via in either.
- **BEAMBREAK.** Its shorter route breaks clearance where it is forced past the J1 area.
- **BoardCOM1.** Its shorter route starves a thermal relief.

## Step 4: every net looked at, and what that turned up

**Loops.** Every loop the checker reports is harmless:
- FET and driver pads with more than one via, and the Kelvin taps;
- wide motor and power tracks that overlap their pads;
- vias that overlap their own pad.

The exception was GLC1_G (below).

**Per-net review.** All 142 routed nets were drawn one at a time, 12 to a sheet, and each one was looked at.
- Most long routes are long because of where things are. The INH/INL lines, for example, have to get from U20 (whose
  pins face away from the drivers) round the power stage blocks to U21/U22.
- The gate-drive traces (GH*/GL*) cross the stage area, where In1 is the VCC plane and In2 is the stage return, so
  they can only use the top and bottom layers between the FET pads.
- For each of these the router confirms that nothing shorter exists without In1.

Fixed by hand or by a targeted re-route:

| Net | What was odd | Now |
|---|---|---|
| Net-(U2-IPROPI) | After the via beside U2, the In2 track ran 9 mm north and came back south-east: an up-and-over around nothing that blocks a direct line | 20.2 → 11.4 mm, direct |
| GLC1_G | The track from the left ran past R106's pad to a T between R106 and R112, then back 0.7 mm into R106 (it kept the diagonal clear of R111's pad) | Bends once under R111 and enters R106 directly; R106 → R112 is one straight link |
| BL OUT2 | A 0.04 mm, 1 mm wide dot beside U5 pin 8 | Removed |
| Net-(U5-IPROPI) | A 0.1 mm overshoot past a bend | Removed |
| Sense nets, U9_SYNC, 3V3A | Staircases left from the first routing | String-pulled with 0.25 mm *extra* clearance instead of 0.03 mm, so their spacing from noisy copper only grows: 114 → 76 segments, 6.8 mm shorter |
| U9_SYNC at U20 | A 0.1 mm staircase on both layers next to the pin | Re-routed: 16.7 → 15.9 mm. The hairpin itself stays. There is no legal via spot next to pin 57, and the vias south of it fence the bottom layer, so the route has to go south to a via and back north under U20. On a 0.025 mm grid the router finds nothing shorter. |

BSTA2's bootstrap link (C121 to U22 pin 20) uses three vias over 7.2 mm. It looks odd but is needed. With top and
bottom only there is no route, and with In2 the best is 14.7 mm, still with three vias.

**Fewer vias.** reroute.py's `--fewer K` keeps a route only if it has at least K fewer vias and is no more than 0.5 mm
longer. With K = 2 and a via cost of 5 mm:

| Net | Before | After |
|---|---|---|
| BR IN2 | 92.6 mm, 5 vias | 90.2 mm, 2 vias |
| SPINDLE_RST_X | 78.2 mm, 4 vias | 77.4 mm, 2 vias |
| TEMP2 (RT2 to R174) | 46.6 mm, 5 vias | 46.8 mm, 3 vias; the GND pour island it cut off (R144's) is stitched at (−24.69, 33.30) |

**Much shorter with extra vias.** These were rejected earlier for adding vias. Kept now because they save at
least 10 mm and 15 %, with at most three extra vias:

| Net | Before | After |
|---|---|---|
| INHC1 | 83.4 mm, 3 vias | 52.2 mm, 6 vias |
| SDA (U8 to U6) | 53.0 mm, 3 vias | 42.3 mm, 5 vias |

SDA1 stays: its shorter route needs seven vias.

**The GND island check was wrong, and is fixed.** repair.py decides whether a change cut off a piece of GND pour.
- **What was wrong.** Its first version joined pads only to pours, never to tracks, and missed pads joined to a
  pour by thermal spokes alone. So it reported islands near U20 as cut off when they were connected, for example
  through U20 pin 47 and C143. It also took the biggest group of copper as "the" GND, not the one with the pads.
- **The effect.** It "stitched" three islands that were fine, and one of its vias sat 0.17 mm from an existing
  GND via. Those three vias are removed.
- **What it does now.** It joins copper only where copper meets: pad and pour (spokes included), track and pour,
  track ends on pads, vias and other tracks, T-junctions. The main GND is the group with the most pads.
- **Checked against known states.** It reports nothing cut off at v1.0, at 1f082cd76 or after step 3d. In a test
  copy with TEMP2's stitch removed, it finds R144's island, which DRC also flags.
- `repair.py <dir> --list` prints the cut-off islands and `--stitch-all` stitches them.

DRC with schematic parity after step 4: no errors, nothing unconnected or dangling, 198 warnings (unchanged).

![Net-(U2-IPROPI) beside U2, before and after: the In2 up-and-over is gone](route-review-ipropi.png)

![Step 4, before (1f082cd76) and after, two nets to a row: BR IN2, SPINDLE_RST_X / TEMP2, INHC1 / SDA, Net-(U2-IPROPI) / GLC1_G, U9_SYNC](route-review-step4.png)

## Step 5: detours inside routes

The pad-to-pad check can't see a detour in the middle of a long route, and Net-(U2-IPROPI)'s was one.
`netreview.local_detours()` walks every chain of copper between junctions. It flags any stretch at least 2 mm and
40 % longer than the straight line between its ends. It found 52 in 47 nets. 38 of those are in nets the re-router
may change: the rest are pours, or the sense nets, which stay as routed.

`detours.py --local` tried each one with step 3's rule and gate: at least 15 % and 2 mm shorter, at most one extra
via, then repair and a full DRC.

| Net | What changed | Before | After |
|---|---|---|---|
| BoardCOM1 | The stretch into U8 pin 9 started on an old In1 segment; it is now off the VCC plane | 19.0 mm | 11.7 mm |
| Axis Health LED | The hook back to its pad is gone | 33.1 mm, 1 via | 24.1 mm, 2 vias |
| SDA1 | Beside U6, the top-layer track climbed 6 mm north and came back down between two vias 6 mm apart | 13.8 mm | 6.4 mm |
| SPINDLE_BOOT_X | A down-and-up jog | 14.6 mm, 2 vias | 9.6 mm, 1 via |

At first DRC rejected SDA1 and SPINDLE_BOOT_X. The new route joined the net elsewhere and left an end via with
copper on one layer only, which KiCad calls dangling. repair.py now joins the track ends in such a via and removes
it, as `route_fix.py via1` does, and both then passed.

Every other window stays, because the router's best alternative is no better:

| Result | Nets |
|---|---|
| Longer | SCL1, TR IN2, INLC2, INLB2 (two windows), ADC TR |
| About the same | GHB1_G, INHB2 |
| No legal route at all without In1 | INLA1, GLA1_G (GLA1_G's 38.6 mm stretch with 3 vias is the low-side gate trace going round the stage blocks) |
| DRC still finds GND split | SCL (U6 pin 13's pour) |

Some windows had a shorter route, but only with three or more extra vias, so they stay:
- SDA1's main run: 88.4 → 80.3 mm, 0 → 7 vias;
- INLC1: 54.8 → 42.1 mm, 1 → 4 vias;
- BEAMBREAK: 33.6 → 26.3 mm, 1 → 4 vias.

**INHC1, again.** Step 4's shorter INHC1 crossed U20's via field with four layer changes in 14 mm. A `--fewer 2`
re-route of that stretch (allowed up to 4 mm longer) found it with two: 20.1 mm, 4 vias → 20.4 mm, 2 vias.

## Step 6: final tidy

- **String-pull.** `route_fix.py pull` ran over every net again (full clearance plus 0.03 mm; the sense nets,
  U9_SYNC and the USB pair skipped). It straightened 66 chains, mostly staircases the router left on re-routed nets:
  497 → 336 segments, 10.8 mm shorter.
- **Fragments under 0.06 mm.** 49 were left from v1.0's routing. 43 went:
  - 23 links inside a chain, merged into their neighbours (one on ISC1, where the copper moves by under 0.03 mm);
  - 5 micro-tails (0.01–0.04 mm, on TL OUT1, TR OUT2, SC1, PHC2 and PHB2), each covered by its neighbour's end;
  - 3 end pieces into a pad or via, absorbed by the neighbouring segment;
  - 2 that lay inside a via;
  - 2 identical INLB2 segments stacked on each other, one of which went.
- **Six fragments stay**, on INHA1, INHB1, INLA1 and INLB2. Each sits where v1.0's tracks are exactly at the
  clearance limit, and merging them moved a vertex into a clearance error (tried, and undone).

## Result

| | 1f082cd76 | Now |
|---|---|---|
| Track segments | 3194 | 1909 |
| Vias | 597 | 577 |
| Track length | 5795.7 mm | 5307.4 mm |

- **DRC with schematic parity:** no errors, nothing unconnected, no dangling tracks or vias. There are 189 warnings,
  against 237 at 1f082cd76. Most of the drop is track-width warnings that went with fragments and merged segments.
- **GND:** `repair.py --list` finds no pour island cut off from the main GND.
- **Tracing:** netreview finds no dead copper and no one-layer vias in any of the 141 routed nets.
- **What's left.** The detour checker still lists 162 pad-to-pad pairs, and every one was looked at on the net
  sheets. For each, the router's alternatives on top, In2 and bottom are longer, need three or more extra vias, or
  split the GND pour. Two kinds account for most of them:
  - the gate drive and INH/INL lines between U20, U21/U22 and the FETs;
  - nets that cross the board round the stage blocks.

![Step 5 and 6, before (after step 4) and after, two nets to a row: Axis Health LED, SDA1 / BoardCOM1, SPINDLE_BOOT_X / INHC1, 12V_GD](route-review-step5.png)
