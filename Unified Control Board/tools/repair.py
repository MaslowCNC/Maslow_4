# repair.py <board dir> <net> [--before islands.json]: tidy up after tools/reroute.py changed a net.  Removes the
# net's dead copper (tools/netreview.py's model), then stitches GND pour islands that the change cut off: with
# --before (written by `repair.py <board dir> --snapshot islands.json` before the change), every top or bottom GND
# fill island that is cut off from the main GND network now but was not before; without it, only islands with no GND
# item in them at all.  A stitch is a 0.6/0.3 via at a free spot where the other side's GND fill also is.
# Run with KiCad 10's Python.
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcbnew
import kirouter as kr
import netreview as nr

TO, MM = pcbnew.ToMM, pcbnew.FromMM
import json
D = sys.argv[1].rstrip("/") + "/"
PCB = D + "Unified Control Board.kicad_pcb"
b = pcbnew.LoadBoard(PCB)


def cut_off_islands(board):
    """top / bottom GND fill islands not in the largest GND component (union-find over fills, vias, pads, tracks)"""
    parent = {}

    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a, c):
        parent[find(a)] = find(c)
    isl = []
    for z in board.Zones():
        if z.GetIsRuleArea() or z.GetNetname() != "GND":
            continue
        for L in z.GetLayerSet().Seq():
            fp_ = z.GetFilledPolysList(L)
            for k in range(fp_.OutlineCount()):
                one = pcbnew.SHAPE_POLY_SET(); one.AddOutline(fp_.Outline(k))
                for h in range(fp_.HoleCount(k)):
                    one.AddHole(fp_.Hole(k, h))
                key = ("Z", z.GetZoneName(), L, k); find(key)
                bb = fp_.Outline(k).BBox()
                isl.append((key, L, one, bb))
    def hits(L, pos, r=0):
        return [k for k, l, poly, bb in isl if l == L and poly.Collide(pos, r)]
    for t in board.GetTracks():
        if t.GetNetname() != "GND":
            continue
        key = ("T", t.m_Uuid.AsString()); find(key)
        if t.GetClass() == "PCB_VIA":
            for L in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu):
                for h in hits(L, t.GetPosition(), MM(0.3)):
                    union(key, h)
        else:
            for pos in (t.GetStart(), t.GetEnd()):
                for h in hits(t.GetLayer(), pos, t.GetWidth() // 2):
                    union(key, h)
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.GetNetname() != "GND":
                continue
            key = ("P", fp.GetReference(), p.GetNumber()); find(key)
            for L in p.GetLayerSet().CuStack():
                ps = pcbnew.SHAPE_POLY_SET()
                p.TransformShapeToPolygon(ps, L, 0, MM(0.005), pcbnew.ERROR_INSIDE)
                for k, l, poly, bb in isl:
                    if l == L and bb.Intersects(p.GetBoundingBox()) and (poly.Collide(p.GetPosition()) or any(poly.Collide(ps.Outline(0).CPoint(i)) for i in range(ps.Outline(0).PointCount()))):
                        union(key, k)
    comps = {}
    for a in list(parent):
        comps.setdefault(find(a), []).append(a)
    big = max(comps.values(), key=len)
    out = []
    for k, l, poly, bb in isl:
        if find(k) != find(big[0]) and k[1] in ("GND top", "GND bottom"):
            out.append({"key": [k[1], int(l), k[3]], "layer": int(l), "bbox": [round(TO(bb.GetLeft()), 2), round(TO(bb.GetTop()), 2), round(TO(bb.GetRight()), 2), round(TO(bb.GetBottom()), 2)]})
    return out, isl


if "--snapshot" in sys.argv:
    json.dump(cut_off_islands(b)[0], open(sys.argv[sys.argv.index("--snapshot") + 1], "w"))
    sys.exit(0)
NET = sys.argv[2]
BEFORE = json.load(open(sys.argv[sys.argv.index("--before") + 1])) if "--before" in sys.argv else None
dru = open(D + "Unified Control Board.kicad_dru").read()
keep = []


def P(v):
    return (TO(v.x), TO(v.y))


def V(p):
    return pcbnew.VECTOR2I(MM(p[0]), MM(p[1]))


# 1. the net's dead copper
res = nr.analyse(NET, nr.load(b)[NET])
by_uid = {t.m_Uuid.AsString(): t for t in b.GetTracks()}
done = 0
for d in res["dead"]:
    t = by_uid.get(d["uid"])
    if t is None:
        continue
    a, c = P(t.GetStart()), P(t.GetEnd())
    p, q = tuple(d["p"]), tuple(d["q"])
    if (math.dist(a, p) < 1e-3 and math.dist(c, q) < 1e-3) or (math.dist(a, q) < 1e-3 and math.dist(c, p) < 1e-3) or math.dist(a, c) < 0.06:
        b.Remove(t); keep.append(t); done += 1
    elif math.dist(a, p) < 1e-3 or math.dist(a, q) < 1e-3:
        t.SetStart(V(q if math.dist(a, p) < 1e-3 else p)); done += 1
    elif math.dist(c, p) < 1e-3 or math.dist(c, q) < 1e-3:
        t.SetEnd(V(q if math.dist(c, p) < 1e-3 else p)); done += 1
for v in res["dead_vias"]:
    for t in list(b.GetTracks()):
        if t.GetClass() == "PCB_VIA" and t.GetNetname() == NET and math.dist(P(t.GetPosition()), tuple(v)) < 0.01:
            b.Remove(t); keep.append(t); done += 1
print("  %s: %d dead pieces removed" % (NET, done))
filler = pcbnew.ZONE_FILLER(b)
filler.Fill(b.Zones())

# 2. GND pour islands this change cut off
gnd = [z for z in b.Zones() if not z.GetIsRuleArea() and z.GetNetname() == "GND" and z.GetZoneName() in ("GND top", "GND bottom")]
other = {z.GetFirstLayer(): o for z in gnd for o in gnd if o is not z}
now, isl_all = cut_off_islands(b)
same = lambda a, c: a["layer"] == c["layer"] and all(abs(x - y) < 0.3 for x, y in zip(a["bbox"], c["bbox"]))
targets = [x for x in now if BEFORE is not None and not any(same(x, y) for y in BEFORE)]
if BEFORE is None:
    targets = []
added = 0
for x in targets:
    L = x["layer"]
    x0, y0, x1, y1 = x["bbox"]
    poly = next(p for k, l, p, bb in isl_all if l == L and all(abs(a - c) < 0.01 for a, c in zip(x["bbox"], [TO(bb.GetLeft()), TO(bb.GetTop()), TO(bb.GetRight()), TO(bb.GetBottom())])))
    R = kr.Router(b, dru, (x0 - 1, y0 - 1, x1 + 1, y1 + 1), step=0.05)
    m = None
    for ln in ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu"):
        mm_ = R.blocked(ln, "GND", 0.2 + 0.3, for_via=True)
        m = mm_ if m is None else (m | mm_)
    oz = other[L]
    otherfill = oz.GetFilledPolysList(oz.GetFirstLayer())
    best = None
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    for i_ in range(int((x1 - x0) / 0.05) + 1):
        for j_ in range(int((y1 - y0) / 0.05) + 1):
            px, py = x0 + i_ * 0.05, y0 + j_ * 0.05
            pt = V((px, py))
            if not poly.Contains(pt) or not otherfill.Contains(pt):
                continue
            gi, gj = R.grid.ij(px, py)
            if m[gj, gi]:
                continue
            dc = math.hypot(px - cx, py - cy)
            if best is None or dc < best[0]:
                best = (dc, px, py)
    if best:
        v = pcbnew.PCB_VIA(b)
        v.SetPosition(V((best[1], best[2]))); v.SetWidth(MM(0.6)); v.SetDrill(MM(0.3))
        v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetNet(b.FindNet("GND"))
        b.Add(v); keep.append(v); added += 1
        print("  GND island cut off on %s at x %.1f..%.1f y %.1f..%.1f: stitched at (%.2f, %.2f)" % (b.GetLayerName(L), x0, x1, y0, y1, best[1], best[2]))
    else:
        print("  GND island cut off on %s at x %.1f..%.1f y %.1f..%.1f: no room for a via" % (b.GetLayerName(L), x0, x1, y0, y1))
if added:
    filler.Fill(b.Zones())
pcbnew.SaveBoard(PCB, b)
