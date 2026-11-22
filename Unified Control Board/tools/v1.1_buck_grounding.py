# v1.1_buck_grounding.py <board dir>: the pre-order review's changes to the 3V3 buck (U9), applied once to v1.1 after
# the route review.  A record of the edit: it checks for its own zone and stops if it is already there.
#  - U9's exposed pad: 2 -> 5 GND vias (v1.0 had 5; the buck move left 2), in the gaps between the top-layer tracks
#    that cross above the pad (INLA1, BoardCOM1, SPINDLE_RST_X).
#  - 3V3 output: 1 -> 3 vias to the In2 3V3 plane (v1.0 had 3): one between C33 and L1, one below C23.
#  - C32 (input capacitor): a GND via at its ground pad.
#  - A GND zone on In2 under the switching loop (C32, U9, D5, the switch node, L1's switch pad), with the settings
#    of "GND In2 under the stages".  The loop's return current then runs 0.2 mm under it instead of round the
#    top-layer pour, and the switch node faces GND instead of the 3V3 plane.
# Run with KiCad 10's Python; then DRC with schematic parity and `repair.py <dir> --list`.
import sys, math, pcbnew
TO, MM = pcbnew.ToMM, pcbnew.FromMM
D = sys.argv[1].rstrip("/") + "/"
PCB = D + "Unified Control Board.kicad_pcb"
b = pcbnew.LoadBoard(PCB)
KEEP = []
NAME = "GND In2 under the buck"
if any(z.GetZoneName() == NAME for z in b.Zones()):
    sys.exit("already applied: zone '%s' exists" % NAME)
u9 = b.FindFootprintByReference("U9")
assert abs(TO(u9.GetPosition().x) - 1.1) < 0.01 and abs(TO(u9.GetPosition().y) - 68.6) < 0.01, "U9 is not where this script expects it"


def V(p):
    return pcbnew.VECTOR2I(MM(p[0]), MM(p[1]))


def seg_dist(p, a, c):
    vx, vy = c[0] - a[0], c[1] - a[1]
    L2 = vx * vx + vy * vy
    t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((p[0] - a[0]) * vx + (p[1] - a[1]) * vy) / L2))
    return math.hypot(p[0] - a[0] - t * vx, p[1] - a[1] - t * vy)


def check_via(p, net):
    """clearance of a 0.6/0.3 via at p to other nets' tracks, vias and pads on every layer (mm), and to other holes"""
    worst = 9.0
    for t in b.GetTracks():
        if t.GetClass() == "PCB_VIA":
            q = (TO(t.GetPosition().x), TO(t.GetPosition().y))
            d = math.dist(p, q)
            assert d >= 0.60, "hole too close to the via at (%.2f, %.2f): %.2f mm" % (q[0], q[1], d)
            if t.GetNetname() != net:
                worst = min(worst, d - 0.3 - TO(t.GetWidth(pcbnew.F_Cu)) / 2)
        elif t.GetNetname() != net:
            a, c = (TO(t.GetStart().x), TO(t.GetStart().y)), (TO(t.GetEnd().x), TO(t.GetEnd().y))
            worst = min(worst, seg_dist(p, a, c) - 0.3 - TO(t.GetWidth()) / 2)
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() == net:
                continue
            bb = pad.GetBoundingBox()
            dx = max(TO(bb.GetLeft()) - p[0], 0, p[0] - TO(bb.GetRight()))
            dy = max(TO(bb.GetTop()) - p[1], 0, p[1] - TO(bb.GetBottom()))
            worst = min(worst, math.hypot(dx, dy) - 0.3)
    return worst


def via(p, net):
    c = check_via(p, net)
    assert c >= 0.2, "via at %s on %s: only %.3f mm clear" % (p, net, c)
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(V(p)); v.SetWidth(MM(0.6)); v.SetDrill(MM(0.3))
    v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetNet(b.FindNet(net))
    b.Add(v); KEEP.append(v)
    print("  via %-4s at (%.2f, %.2f), %.2f mm clear of other nets" % (net, p[0], p[1], c))


def track(a, c, w, net, layer=pcbnew.B_Cu):
    t = pcbnew.PCB_TRACK(b)
    t.SetStart(V(a)); t.SetEnd(V(c)); t.SetWidth(MM(w)); t.SetLayer(layer); t.SetNet(b.FindNet(net))
    b.Add(t); KEEP.append(t)


print("U9 exposed pad:")
for p in ((1.55, 67.30), (2.11, 69.08), (0.15, 67.60)):
    via(p, "GND")
print("C32 ground pad:")
via((-5.30, 69.80), "GND")
track((-5.30, 68.70), (-5.30, 69.80), 0.6, "GND")
print("3V3 output:")
via((-0.05, 75.20), "3V3")
via((5.60, 75.45), "3V3")
track((5.60, 74.40), (5.60, 75.45), 0.5, "3V3")

src = next(z for z in b.Zones() if z.GetZoneName() == "GND In2 under the stages")
dup = src.Duplicate(False)
nz = pcbnew.Cast_to_ZONE(dup) if hasattr(pcbnew, "Cast_to_ZONE") else dup.Cast()
nz.UnFill()
nz.Outline().RemoveAllContours()
OUTLINE = [(-6.8, 65.8), (5.2, 65.8), (5.2, 63.5), (8.8, 63.5), (8.8, 72.0), (4.6, 72.0), (4.6, 73.05), (-3.0, 73.05),
           (-3.0, 71.0), (-6.8, 71.0)]
nz.Outline().NewOutline()
for p in OUTLINE:
    nz.Outline().Append(MM(p[0]), MM(p[1]))
nz.SetZoneName(NAME)
nz.SetNet(b.FindNet("GND"))
b.Add(nz); KEEP.append(nz)
print("zone '%s' on %s, priority %d, min width %.2f, %d corners" % (nz.GetZoneName(), [b.GetLayerName(l) for l in nz.GetLayerSet().Seq()], nz.GetAssignedPriority(), TO(nz.GetMinThickness()), nz.Outline().FullPointCount()))
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(PCB, b)
print("saved")
