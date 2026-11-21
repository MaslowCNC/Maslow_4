# v1.1_u9move.py <board dir>: move U9's 3V3 buck down to the curved bottom edge, rotated 180 degrees, and re-route
# what it touches.  Applied after tools/v1.1_pcb.py (commit ecdd24a5f).  Run with KiCad 10's Python:
#   /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 \
#       "Unified Control Board/tools/v1.1_u9move.py" "Unified Control Board"
# Kept as the record of how the move was made; it asserts on the layout it starts from, so it does not run twice.
#
# Why: at (-3.6, 56.9) the buck sat 5.5 mm from SB1 and 5.9 mm from SA1, beside motor 1's stage and sense routes.
# Rotated so its switch node faces east, at (1.1, 68.6) it is 13.6 mm from the nearest sense net (SB1) and 19 mm or
# more from the rest.  The space there is crossed on the top layer by a bundle of long signals (BoardCOM1, INLA1,
# SPINDLE_RST_X, 12V_GD, INLC1), so the buck keeps its copper on the bottom layer and only puts vias where the bundle
# leaves room; BoardCOM2 and INLC1, which crossed it on In2 (right over the new switch node), are re-routed.
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # kirouter.py lives beside this script
import pcbnew
import kirouter as kr

MM, TO = pcbnew.FromMM, pcbnew.ToMM
D = sys.argv[1].rstrip("/") + "/"
PCB = D + "Unified Control Board.kicad_pcb"
b = pcbnew.LoadBoard(PCB)
dru = open(D + "Unified Control Board.kicad_dru").read()
keep = []   # KiCad 10 SWIG: removed items must stay referenced


def fpr(ref):
    return b.FindFootprintByReference(ref)


def pad_xy(ref, num):
    p = fpr(ref).FindPadByNumber(num).GetPosition()
    return (TO(p.x), TO(p.y))


def B(ref, num):
    return pad_xy(ref, num) + ("B.Cu",)


def drop(t):
    b.Remove(t)
    keep.append(t)


def at(p, x, y, tol=0.03):
    return abs(TO(p.x) - x) < tol and abs(TO(p.y) - y) < tol


def seg_is(t, a, z):
    return (at(t.GetStart(), *a) and at(t.GetEnd(), *z)) or (at(t.GetStart(), *z) and at(t.GetEnd(), *a))


rect = lambda x0, y0, x1, y1: [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]

assert at(fpr("U9").GetPosition(), -3.6, 56.9), "U9 is not where v1.1 left it"
UX, UY = 1.1, 68.6
GROUP = ["U9", "L1", "D5", "C31", "C32"]                      # the power parts keep their layout, rotated
CONTROL = ["R27", "R29", "R30", "C165", "R31", "C29", "C30"]  # re-placed around the rotated U9

# ---------------------------------------------------------------- 1. the power parts and their own copper
# BST and SW_NODE are all bottom-layer; FB, COMP, COMP_Z and RT are re-routed after the control parts move.
# Shared nets: U9's local VSUP link to C32, L1's 3V3 stub and via, and the GND vias inside the group (each is
# checked against the new surroundings below).
VSUP_LOCAL = [((-0.23, 56.27), (-1.10, 56.27)), ((0.30, 56.80), (-0.23, 56.27)), ((0.80, 56.80), (0.30, 56.80))]
L1_3V3 = [((-4.72, 50.32), (-4.70, 50.30)), ((-4.72, 51.76), (-4.72, 50.32))]
GND_VIAS = [(-4.40, 55.80), (-2.80, 55.80), (-4.40, 58.00), (-2.80, 58.00), (-3.60, 56.90), (-9.60, 57.00), (-8.50, 60.30)]
mv = []
for t in list(b.GetTracks()):
    n, via = t.GetNetname(), t.GetClass() == "PCB_VIA"
    if n in ("BST", "SW_NODE"):
        assert not via and t.GetLayer() == pcbnew.B_Cu
        mv.append(t)
    elif n in ("FB", "COMP", "COMP_Z", "RT"):
        drop(t)
    elif n == "VSUP" and not via:
        (mv.append(t) if any(seg_is(t, a, z) for a, z in VSUP_LOCAL) else drop(t))   # D1 / D2 / C28 links: re-routed
    elif n == "3V3":
        if (not via and any(seg_is(t, a, z) for a, z in L1_3V3)) or (via and at(t.GetPosition(), -4.72, 51.76)):
            mv.append(t)
        elif (via and at(t.GetPosition(), -3.60, 61.10)) or (not via and (seg_is(t, (-4.73, 60.30), (-4.40, 60.30))
                                                                       or seg_is(t, (-4.40, 60.30), (-3.60, 61.10)))):
            drop(t)   # R27's old tap; R27 moves
        elif all(-13.0 <= TO(v.x) <= -10.5 and 53.5 <= TO(v.y) <= 57.0 for v in
                 ((t.GetPosition(),) if via else (t.GetStart(), t.GetEnd()))):
            drop(t)   # C33's and C23's old taps; both move next to L1
    elif n == "GND" and via:
        if any(at(t.GetPosition(), x, y) for x, y in GND_VIAS):
            mv.append(t)
        elif at(t.GetPosition(), -2.10, 61.55):
            drop(t)   # v1.1's RT-pocket stitch; that pocket goes with the move
u9 = fpr("U9").GetPosition()
centre = pcbnew.VECTOR2I(u9.x, u9.y)
delta = pcbnew.VECTOR2I(MM(UX) - u9.x, MM(UY) - u9.y)
for it in [fpr(r) for r in GROUP] + mv:
    it.Rotate(centre, pcbnew.EDA_ANGLE(180.0, pcbnew.DEGREES_T))
    it.Move(delta)
print("power parts: %d moved with %d tracks / vias" % (len(GROUP), len(mv)))
# U9's thermal pad joins the bottom pour solidly: most of its vias would land on the top-layer bundle
fpr("U9").FindPadByNumber("9").SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)

# ---------------------------------------------------------------- 2. rip-ups that the move needs
# INLC1's In2 hop ends in a via where C32's ground pad now is; BoardCOM2's In2 run would cross the new switch node.
for t in list(b.GetTracks()):
    n = t.GetNetname()
    if n == "INLC1" and (seg_is(t, (-10.40, 68.70), (-6.00, 68.70)) or seg_is(t, (-6.00, 68.70), (-4.00, 68.70))
                         or seg_is(t, (-4.00, 68.70), (11.00, 53.70))
                         or (t.GetClass() == "PCB_VIA" and (at(t.GetPosition(), -10.40, 68.70) or at(t.GetPosition(), -6.00, 68.70)))):
        drop(t)
    elif n == "BoardCOM2" and (seg_is(t, (24.60, 48.02), (-0.33, 72.95)) or seg_is(t, (-0.33, 72.95), (-14.20, 72.95))):
        drop(t)


# ---------------------------------------------------------------- 3. parts placed around the rotated U9
def place(ref, x, y, pad, side):
    """put ref at (x, y), turned so pad `pad` is on `side` (N, S, E, W) of its centre"""
    fp = fpr(ref)
    fp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    for deg in (0, 90, 180, 270):
        fp.SetOrientationDegrees(deg)
        dx, dy = pad_xy(ref, pad)[0] - x, pad_xy(ref, pad)[1] - y
        if {"N": dy < -0.2, "S": dy > 0.2, "E": dx > 0.2, "W": dx < -0.2}[side]:
            return
    raise SystemExit("no orientation for %s" % ref)


place("R27", 3.6, 64.2, "2", "S")       # 62k: FB pad towards U9 pin 5, 3V3 pad north
place("R29", 2.1, 64.63, "1", "E")      # 20k: FB pad beside R27's
place("R30", -1.4, 64.6, "1", "S")      # 200k RT: RT pad towards U9 pin 4
place("C165", -2.9, 65.03, "1", "E")    # 10 pF: RT pad beside R30's; U9_SYNC pad west, where the clock comes in
place("C30", 4.75, 62.4, "1", "S")      # 82 pF COMP-GND
place("R31", 6.25, 62.9, "1", "W")      # 7.5k COMP-COMP_Z
place("C29", 7.95, 62.4, "1", "S")      # 12 nF COMP_Z-GND
place("C33", -2.15, 75.2, "1", "E")     # 22 uF output, west of L1's output pad
place("C23", 6.6, 74.4, "2", "W")       # 10 uF output, east of it
fpr("C23").Reference().SetVisible(False)   # its label has no room by the edge (as most labels on this board)

# ---------------------------------------------------------------- 4. moved vias that now hit other copper go
R = kr.Router(b, dru, (-12.0, 56.0, 14.0, 78.0), step=0.05)
_vmask = {}


def via_free(net, x, y, size=0.6, clr=0.2):   # 0.15 rule + 0.05 for the grid's rounding
    if net not in _vmask:
        m = None
        for ln in ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu"):
            mm = R.blocked(ln, net, clr + size / 2.0, for_via=True)
            m = mm if m is None else (m | mm)
        _vmask[net] = m
    i, j = R.grid.ij(x, y)
    return not _vmask[net][j, i]


for v in [t for t in mv if t.GetClass() == "PCB_VIA"]:
    x, y = TO(v.GetPosition().x), TO(v.GetPosition().y)
    if not via_free(v.GetNetname(), x, y):
        drop(v)
        print("  moved %s via at (%.2f, %.2f) lands on other copper: removed" % (v.GetNetname(), x, y))


# ---------------------------------------------------------------- 5. routing
def route(net, starts, goals, width, clr, layers, region, step=0.05, **kw):
    Rr = kr.Router(b, dru, region, step=step)
    runs = Rr.route(net, starts, goals, width, clr, layers=layers, **kw)
    assert runs, "%s: no route" % net
    end = runs[-1][1][-1]
    z = min(goals, key=lambda g_: (g_[0] - end[0]) ** 2 + (g_[1] - end[1]) ** 2)
    kr.add_runs(b, net, runs, width, snap_start=(starts[0][0], starts[0][1]), snap_end=(z[0], z[1]), keep=keep)
    ln = sum(math.hypot(q[0] - p[0], q[1] - p[1]) for _, ps in runs for p, q in zip(kr.simplify(ps), kr.simplify(ps)[1:]))
    print("  %-9s %5.1f mm, %d vias, %s" % (net, ln, len(runs) - 1, "/".join(r[0][:-3] for r in runs)))


CTL = (-6, 58, 10, 72)
route("RT", [B("U9", "4")], [B("R30", "1")], 0.15, 0.15, ("B.Cu",), CTL)
route("RT", [B("C165", "1")], [B("R30", "1")], 0.15, 0.15, ("B.Cu",), CTL)
route("FB", [B("U9", "5")], [B("R27", "2")], 0.15, 0.15, ("B.Cu",), CTL)
route("FB", [B("R29", "1")], [B("R27", "2")], 0.15, 0.15, ("B.Cu",), CTL)
route("COMP", [B("U9", "6")], [B("C30", "1")], 0.15, 0.15, ("B.Cu",), CTL)
route("COMP", [B("R31", "1")], [B("C30", "1")], 0.15, 0.15, ("B.Cu",), CTL)
route("COMP_Z", [B("R31", "2")], [B("C29", "1")], 0.15, 0.15, ("B.Cu",), CTL)
# R27's top to the 3V3 plane: a via within 2.5 mm, wherever the top-layer bundle leaves room
x0, y0 = pad_xy("R27", "1")
goals = [(x0 + i * 0.25, y0 + j * 0.25, "In2.Cu") for i in range(-10, 11) for j in range(-10, 11) if 0.8 < math.hypot(i, j) * 0.25 <= 2.5]   # via clear of the pad
route("3V3", [B("R27", "1")], goals, 0.25, 0.15, ("B.Cu", "In2.Cu"), CTL, via_cost=0.5)
# output caps to L1's output pad
route("3V3", [B("C33", "1")], [B("L1", "2")], 0.5, 0.15, ("B.Cu",), (-6, 70, 10, 78))
route("3V3", [B("C23", "2")], [B("L1", "2")], 0.5, 0.15, ("B.Cu",), (-6, 70, 10, 78))
# VSUP: D2 -> D1 -> C32 (the hub at U9's input), C28 -> D1 / D2.  D1 -> C32 stays west of C165 so the clock can
# reach C165's pad on the bottom layer.
route("VSUP", [B("D2", "1")], [B("D1", "1")], 0.5, 0.2, ("B.Cu",), (-8, 50, 16, 76), step=0.1)
route("VSUP", [B("D1", "1")], [B("C32", "1")], 0.5, 0.2, ("B.Cu",), (-9, 50, 16, 76), step=0.1,
      extra_keep={"B.Cu": [rect(-5.4, 60.6, -1.6, 67.0)]}, allow={"B.Cu": [rect(-3.9, 67.0, -2.7, 68.4)]})
route("VSUP", [B("C28", "1")], [B("D1", "1"), B("D2", "1")], 0.5, 0.2, ("B.Cu",), (-8, 50, 18, 76), step=0.1)
# INLC1 and BoardCOM2 again: In2 kept off the buck (the top layer is shielded from it by both planes)
BUCK = rect(-7.3, 60.6, 9.3, 77.5)
route("INLC1", [(-10.40, 68.70, "F.Cu")], [(11.00, 53.70, "F.Cu")], 0.1, 0.15, ("F.Cu", "In2.Cu"), (-14, 50, 14, 76),
      step=0.1, via_cost=3.0, extra_keep={"In2.Cu": [BUCK]})
route("BoardCOM2", [(-14.25, 72.90, "F.Cu"), (-14.25, 72.90, "In2.Cu")], [(24.60, 48.02, "In2.Cu")], 0.15, 0.15,
      ("F.Cu", "In2.Cu"), (-16, 45, 27, 76), step=0.1, via_cost=3.0, extra_keep={"In2.Cu": [BUCK]})
# U9_SYNC: G474 PB4 (pin 57) to C165, top and bottom layers only, kept off the analog nets.  PB4 is the only spare
# timer pin that gets out at all (PB5 aside); its route ducks under the G474 to escape the pins' fan-out.
SENS = r"IS[ABC][12]|S[ABC][12]|3V3A|TEMP[12]|VBUS_SENSE|FB|COMP|COMP_Z|RT"
Rs = kr.Router(b, dru, (-38.0, 44.0, 3.0, 77.0), step=0.1)
# The route ends 1.05 mm west of C165's pad, between it and the VSUP track, so its last via can't land in the 0402's
# pad; a straight bottom-layer stub finishes it.
a = pad_xy("U20", "57")
c = pad_xy("C165", "2")
e = (c[0] - 1.05, c[1])
runs = Rs.route("U9_SYNC", [(a[0], a[1], "F.Cu")], [(e[0], e[1], "B.Cu")], 0.15, 0.15, layers=("F.Cu", "B.Cu"), via_cost=2.0,
                noise=Rs.noise_field(SENS, 1.0), noise_cost=6.0, clear_radius=0.3)
assert runs, "U9_SYNC: no route"
kr.add_runs(b, "U9_SYNC", runs, 0.15, snap_start=a, snap_end=e, keep=keep)
kr.add_runs(b, "U9_SYNC", [("B.Cu", [e, c])], 0.15, keep=keep)
print("  U9_SYNC   %5.1f mm, %d vias" % (sum(math.hypot(q[0] - p[0], q[1] - p[1]) for _, ps in runs
                                             for p, q in zip(kr.simplify(ps), kr.simplify(ps)[1:])), len(runs) - 1))

# ---------------------------------------------------------------- 6. ground stitching around the buck
# Ties the bottom pour under the buck to the strips of top pour between the bundle's tracks.  Each via goes at the
# nearest free spot (all layers, with clearance) to a ground pad, within 1.5 mm; U9's thermal pad gets what fits in it.
filler = pcbnew.ZONE_FILLER(b)
filler.Fill(b.Zones())
R = kr.Router(b, dru, (-12.0, 56.0, 14.0, 78.0), step=0.05)
_vmask.clear()
gz = [z for z in b.Zones() if z.GetZoneName() in ("GND top", "GND bottom") and not z.GetIsRuleArea()]


def in_gnd_fills(x, y):
    p = pcbnew.VECTOR2I(MM(x), MM(y))
    return all(z.GetFilledPolysList(z.GetFirstLayer()).Contains(p) for z in gz)


def add_gnd_via(x, y):
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    v.SetWidth(MM(0.6))
    v.SetDrill(MM(0.3))
    v.SetViaType(pcbnew.VIATYPE_THROUGH)
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    v.SetNet(b.FindNet("GND"))
    b.Add(v)
    keep.append(v)
    i, j = R.grid.ij(x, y)
    r = int(round(1.0 / R.grid.step))
    _vmask["GND"][max(0, j - r):j + r + 1, max(0, i - r):i + r + 1] = True   # next via at least 1 mm away


added = []
p9 = fpr("U9").FindPadByNumber("9")
for ref, num in (("U9", "9"), ("C32", "2"), ("C33", "2"), ("C23", "1"), ("D5", "2"), ("C30", "2"), ("C29", "2"), ("R30", "2")):
    x0, y0 = pad_xy(ref, num)
    near_pads = [p for fp in b.GetFootprints() for p in fp.Pads()
                 if abs(TO(p.GetPosition().x) - x0) < 4 and abs(TO(p.GetPosition().y) - y0) < 4]
    gnd_vias = [(TO(t.GetPosition().x), TO(t.GetPosition().y)) for t in b.GetTracks()
                if t.GetClass() == "PCB_VIA" and abs(TO(t.GetPosition().x) - x0) < 4 and abs(TO(t.GetPosition().y) - y0) < 4]
    best = None
    for i in range(-30, 31):
        for j in range(-30, 31):
            x, y = x0 + i * 0.05, y0 + j * 0.05
            d = math.hypot(x - x0, y - y0)
            if d > 1.5 or (best and d >= best[0]):
                continue
            if ref == "U9" and not p9.HitTest(pcbnew.VECTOR2I(MM(x), MM(y))):
                continue
            if ref != "U9" and any(p.HitTest(pcbnew.VECTOR2I(MM(x), MM(y)), MM(0.35)) for p in near_pads):
                continue   # no vias in small parts' pads (solder wicks away); U9's thermal pad is meant to have them
            if any(math.hypot(x - vx, y - vy) < 1.0 for vx, vy in gnd_vias):
                continue   # holes at least 1 mm apart
            if not via_free("GND", x, y) or not in_gnd_fills(x, y):
                continue
            best = (d, x, y)
    if best:
        add_gnd_via(best[1], best[2])
        added.append((ref, round(best[1], 2), round(best[2], 2)))
print("  GND stitching vias:", added)

filler.Fill(b.Zones())
pcbnew.SaveBoard(PCB, b)
print("saved", PCB)
