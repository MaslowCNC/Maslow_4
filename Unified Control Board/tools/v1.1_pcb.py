# v1.1_pcb.py <board dir>: the Unified Control Board 36V v1.1 PCB changes, applied to the v1.0 board
# (git tag hw/ucb-36v/v1.0) after tools/v1.1_schematic.py.  Run with KiCad 10's Python:
#   /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 \
#       "Unified Control Board/tools/v1.1_pcb.py" "Unified Control Board"
# Kept as the record of how v1.1 was made; it asserts on v1.0's layout, so it does not run twice.
import sys, math, time
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # kirouter.py lives beside this script
import pcbnew
import kirouter as kr

D = sys.argv[1].rstrip("/") + "/"
PCB = D + "Unified Control Board.kicad_pcb"
LIB = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/"
SPINDLE = "/176e5b9f-9a9c-4b6b-b18a-17424bf72869/"
MM, TO = pcbnew.FromMM, pcbnew.ToMM
t0 = time.time()
b = pcbnew.LoadBoard(PCB)
dru = open(D + "Unified Control Board.kicad_dru").read()
keep = []   # KiCad 10 SWIG: removed items must stay referenced


def sch_uuid(sheet, ref):
    import re
    s = open(D + sheet, encoding="utf-8").read()
    for m in re.finditer(r'\n\t\(symbol\n\t\t\(lib_id "[^"]+"\)\n.*?\n\t\)\n', s, re.S):
        if '(property "Reference" "%s"' % ref in m.group(0):
            return re.search(r'\n\t\t\(uuid "([^"]+)"\)', m.group(0)).group(1)
    raise KeyError(ref)


def set_fields(fp, ref, value, mpn, lcsc):
    fp.SetReference(ref)
    fp.SetValue(value)
    for k, v in (("MPN", mpn), ("Supplier Part", lcsc), ("Supplier", "LCSC")):
        fp.SetField(k, v)
        f = fp.GetField(k)
        if f is not None:
            f.SetVisible(False)


def pad_xy(fp, num):
    p = fp.FindPadByNumber(num).GetPosition()
    return TO(p.x), TO(p.y)


# ---------------------------------------------------------------- title block
tb = b.GetTitleBlock()
tb.SetRevision("1.1")
tb.SetDate("2026-11-21")
b.SetTitleBlock(tb)

# ---------------------------------------------------------------- FB1 -> R178 (4.7 ohm 0603), same place and link
old = b.FindFootprintByReference("FB1")
r178 = pcbnew.FootprintLoad(LIB + "Resistor_SMD.pretty", "R_0603_1608Metric")
r178.SetFPID(pcbnew.LIB_ID("Resistor_SMD", "R_0603_1608Metric"))
r178.SetPosition(old.GetPosition())
r178.SetOrientation(old.GetOrientation())
set_fields(r178, "R178", "4.7", "AC0603FR-074R7L", "C144651")
r178.SetPath(old.GetPath())
ref = r178.Reference()   # the label where FB1's was
ref.SetPosition(old.Reference().GetPosition())
ref.SetTextAngle(old.Reference().GetTextAngle())
ref.SetVisible(False)   # "R178" is wider than "FB1" and would print over C142's pads; the fab layer keeps it
nets = {p.GetNumber(): p.GetNet() for p in old.Pads()}
b.Remove(old)
keep.append(old)
b.Add(r178)
for p in r178.Pads():
    p.SetNet(nets[p.GetNumber()])
print("R178 replaces FB1 at", TO(r178.GetPosition().x), TO(r178.GetPosition().y), [(p.GetNumber(), p.GetNetname()) for p in r178.Pads()])

# ---------------------------------------------------------------- U9_SYNC net on U20 pad 57 (PB4, TIM3_CH1)
ni = pcbnew.NETINFO_ITEM(b, "U9_SYNC")
b.Add(ni)
u20 = b.FindFootprintByReference("U20")
u20.FindPadByNumber("57").SetNet(b.FindNet("U9_SYNC"))

# ---------------------------------------------------------------- C165 10 pF (bottom, at U9's RT/CLK node) and C164 22 uF (top, 3V3A)
rt_region = kr.Router(b, dru, (-6.0, 56.0, 4.0, 66.0))
spot = kr.free_spot(rt_region, "B", (-0.4, 61.4), 3.0, 1.91 + 0.1, 1.01 + 0.1, rot_options=(90, 0),
                    pad_specs=[(0.48, 0, 0.56, 0.62, "RT"), (-0.48, 0, 0.56, 0.62, "U9_SYNC")])
assert spot, "no room for C165"
c165 = pcbnew.FootprintLoad(LIB + "Capacitor_SMD.pretty", "C_0402_1005Metric")
c165.SetFPID(pcbnew.LIB_ID("Capacitor_SMD", "C_0402_1005Metric"))
set_fields(c165, "C165", "10pF", "CL05C100JB5NNNC", "C32949")
c165.SetPath(pcbnew.KIID_PATH("/" + sch_uuid("Unified Control Board.kicad_sch", "C165")))
b.Add(c165)
c165.SetLayerAndFlip(pcbnew.B_Cu)
c165.SetPosition(pcbnew.VECTOR2I(MM(spot[0]), MM(spot[1])))
c165.SetOrientationDegrees(270)   # pad 1 (RT) toward R30 / U9 pin 4, pad 2 (sync) away from U9
c165.Reference().SetVisible(False)   # no room for a silk label between R30, R31 and D1 (the fab layer keeps it)
c165.FindPadByNumber("1").SetNet(b.FindNet("RT"))
c165.FindPadByNumber("2").SetNet(b.FindNet("U9_SYNC"))
print("C165 at", spot, "pads", pad_xy(c165, "1"), pad_xy(c165, "2"))

a3_region = kr.Router(b, dru, (-30.0, 60.5, -12.0, 78.0), step=0.1)
c164 = pcbnew.FootprintLoad(LIB + "Capacitor_SMD.pretty", "C_0805_2012Metric")
c164.SetFPID(pcbnew.LIB_ID("Capacitor_SMD", "C_0805_2012Metric"))
pads = [(TO(p.GetPosition().x), TO(p.GetPosition().y), TO(p.GetSize().x), TO(p.GetSize().y)) for p in c164.Pads()]
cy = c164.GetCourtyard(pcbnew.F_CrtYd).BBox()
spot = kr.free_spot(a3_region, "B", (-21.0, 69.5), 7.0, TO(cy.GetWidth()) + 0.1, TO(cy.GetHeight()) + 0.1, rot_options=(0, 90), step=0.2,
                    pad_specs=[(-pads[0][0], pads[0][1], pads[0][2], pads[0][3], "3V3A"), (-pads[1][0], pads[1][1], pads[1][2], pads[1][3], "GND")])
assert spot, "no room for C164"
set_fields(c164, "C164", "22uF", "CL21A226MAQNNNE", "C45783")
c164.SetPath(pcbnew.KIID_PATH(SPINDLE + sch_uuid("Spindle Drive.kicad_sch", "C164")))
b.Add(c164)
c164.SetLayerAndFlip(pcbnew.B_Cu)
c164.SetPosition(pcbnew.VECTOR2I(MM(spot[0]), MM(spot[1])))
c164.SetOrientationDegrees(spot[2])
c164.FindPadByNumber("1").SetNet(b.FindNet("3V3A"))
c164.FindPadByNumber("2").SetNet(b.FindNet("GND"))
print("C164 at", spot, "pads", pad_xy(c164, "1"), pad_xy(c164, "2"))

# ---------------------------------------------------------------- SB1: off the buck
# v1.0's SB1 ran on In2 straight over U9's switch node and L1 (the 5x noise on motor 1 phase B).  Remove all of
# it (also the old tap at Q13's source pads) and route again from R114's own pad (a Kelvin tap at the shunt) to
# R117's via-in-pad, with In2 / B.Cu banned around the buck and F.Cu banned right over the switch node.
removed = 0
for t in list(b.GetTracks()):
    if t.GetNetname() != "SB1":
        continue
    if t.GetClass() == "PCB_VIA":
        x, y = TO(t.GetPosition().x), TO(t.GetPosition().y)
        if abs(x + 24.0) < 0.15 and abs(y - 66.2) < 0.15:
            continue   # R117's via-in-pad
    b.Remove(t)
    keep.append(t)
    removed += 1
print("SB1: removed", removed, "items")

NOISY = r"(PH|GH|GL|SH|BST|INH|INL)[ABC][12].*|SW_NODE|VSUP|COMP|FB|VCC"
SENSITIVE = r"IS[ABC][12]|S[ABC][12]|3V3A|TEMP[12]"
rect = lambda x0, y0, x1, y1: [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
BUCK = rect(-13.5, 46.0, 8.0, 64.5)
r114 = b.FindFootprintByReference("R114")
s0 = pad_xy(r114, "1")
R = kr.Router(b, dru, (-34.0, 38.0, 10.0, 72.0), step=0.1)
noise = R.noise_field(NOISY, 0.6)
runs = R.route("SB1", [(s0[0], s0[1], "B.Cu")], [(-24.0, 66.2, "B.Cu"), (-24.0, 66.2, "F.Cu"), (-24.0, 66.2, "In2.Cu")], 0.15, 0.15,
               layers=("F.Cu", "In2.Cu", "B.Cu"), via_cost=3.0, noise=noise, noise_cost=5.0,
               extra_keep={"In2.Cu": [BUCK], "B.Cu": [BUCK], "F.Cu": [rect(-11.5, 49.0, -1.5, 60.5)]})
assert runs, "SB1: no route"
ln = sum(math.hypot(b2[0] - a[0], b2[1] - a[1]) for _, pts in runs for a, b2 in zip(kr.simplify(pts), kr.simplify(pts)[1:]))
print("SB1 route: %.1f mm, %d vias, layers %s" % (ln, len(runs) - 1, [r[0] for r in runs]))
kr.add_runs(b, "SB1", runs, 0.15, snap_start=s0, snap_end=(-24.0, 66.2), keep=keep)
# SB1's bottom leg past (-23, 50) cuts off a small GND island whose only items were two F/B stitching vias (no other
# route exists there); the refill drops the island and leaves them dangling, so they go.
for t in list(b.GetTracks()):
    if t.GetClass() == "PCB_VIA" and t.GetNetname() == "GND":
        x, y = TO(t.GetPosition().x), TO(t.GetPosition().y)
        if abs(y - 50.5) < 0.05 and (abs(x + 22.56) < 0.05 or abs(x + 23.56) < 0.05):
            b.Remove(t)
            keep.append(t)
            print("removed GND stitching via at", round(x, 2), round(y, 2))

# RT: C165 pad 1 -> R30 pad 1 (U9 pin 4's node), bottom
r30 = b.FindFootprintByReference("R30")
a, z = pad_xy(c165, "1"), pad_xy(r30, "1")
R2 = kr.Router(b, dru, (-6.0, 56.0, 4.0, 66.0))
runs = R2.route("RT", [(a[0], a[1], "B.Cu")], [(z[0], z[1], "B.Cu")], 0.15, 0.2, layers=("B.Cu",))
assert runs, "RT: no route"
kr.add_runs(b, "RT", runs, 0.15, snap_start=a, snap_end=z, keep=keep)
print("RT stub:", [(l, kr.simplify(p)) for l, p in runs])

# U9_SYNC (U20 pad 57 -> C165 pad 2) is left unrouted: see Unified Control Board/REVISIONS.md

# C164 3V3A pad -> the nearest bias resistor's 3V3A pad in the sense RC block (bottom)
a = pad_xy(c164, "1")
goals = []
for ref in ("R119", "R120", "R121", "R149", "R150", "R151"):
    for p in b.FindFootprintByReference(ref).Pads():
        if p.GetNetname() == "3V3A":
            goals.append((TO(p.GetPosition().x), TO(p.GetPosition().y), "B.Cu"))
R4 = kr.Router(b, dru, (-30.0, 58.0, -12.0, 76.0))
runs = R4.route("3V3A", [(a[0], a[1], "B.Cu")], goals, 0.2, 0.15, layers=("B.Cu",))
assert runs, "3V3A: no route"
z = min(goals, key=lambda g_: (g_[0] - runs[-1][1][-1][0]) ** 2 + (g_[1] - runs[-1][1][-1][1]) ** 2)
kr.add_runs(b, "3V3A", runs, 0.2, snap_start=a, snap_end=(z[0], z[1]), keep=keep)
print("3V3A link:", [(l, kr.simplify(p)) for l, p in runs])

# ---------------------------------------------------------------- refill, stitch U9's RT / compensation ground, save
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
# C165 and its RT stub close the only bottom-layer exit of the GND pocket under U9 that holds R30's and C29's ground
# pads: stitch it to the top GND pour with a via inside both fills, clear of every other net on all layers.
r30g = pad_xy(r30, "2")
zb = [z for z in b.Zones() if z.GetNetname() == "GND" and z.GetZoneName() == "GND bottom"][0]
zt = [z for z in b.Zones() if z.GetNetname() == "GND" and z.GetZoneName() == "GND top"][0]
R5 = kr.Router(b, dru, (-7.0, 57.0, 2.0, 65.5), step=0.05)
vm = None
for ln in ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu"):
    m = R5.blocked(ln, "GND", 0.2 + 0.3, for_via=True)
    vm = m if vm is None else (vm | m)
best = None
for j in range(R5.grid.ny):
    for i in range(R5.grid.nx):
        if vm[j, i]:
            continue
        x, y = R5.grid.xy(i, j)
        if not (-5.5 < x < 0.0 and 59.0 < y < 63.6):
            continue
        pos = pcbnew.VECTOR2I(MM(x), MM(y))
        ok = all(z.HitTestFilledArea(L, pos, MM(0.31)) for z, L in ((zb, pcbnew.B_Cu), (zt, pcbnew.F_Cu)))
        if ok:
            d = math.hypot(x - r30g[0], y - r30g[1])
            if d < 0.8:
                continue   # beside the pad (the pour joins them), not in it: a 0.6 mm via is bigger than an 0402 pad
            if best is None or d < best[0]:
                best = (d, x, y)
assert best, "no stitching via spot under U9"
v = pcbnew.PCB_VIA(b)
v.SetPosition(pcbnew.VECTOR2I(MM(best[1]), MM(best[2])))
v.SetWidth(MM(0.6))
v.SetDrill(MM(0.3))
v.SetViaType(pcbnew.VIATYPE_THROUGH)
v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
v.SetNet(b.FindNet("GND"))
b.Add(v)
keep.append(v)
print("GND stitching via under U9 at (%.2f, %.2f), %.2f mm from R30's GND pad" % (best[1], best[2], best[0]))
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(PCB, b)
print("saved in %.0f s" % (time.time() - t0))
