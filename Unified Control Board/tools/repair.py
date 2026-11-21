# repair.py <board dir> <net> [--before islands.json]: tidy up after tools/reroute.py changed a net.  Removes the
# net's dead copper (tools/netreview.py's model), then stitches GND pour islands the change left floating: with
# --before (written by `repair.py <board dir> --snapshot islands.json` before the change), every top or bottom GND
# fill island that is cut off from the main GND now but was not before.  Cut off means no copper path (fills on any
# layer, vias, tracks, pads) to the part of GND with the most pads: either floating (KiCad keeps an island that a via
# joins to another stray island, and DRC does not flag it) or holding pads of its own (DRC flags those).  A stitch is
# a 0.6/0.3 via at a free spot of the island, at least 1 mm from other holes, over main GND fill on top, In2 or bottom.
# repair.py <board dir> --list: print the cut-off islands.  repair.py <board dir> --stitch-all: stitch them all.
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
    """top / bottom GND fill islands cut off from the main GND: union-find over the GND fills (all layers), vias,
    tracks and pads, joined only where copper meets (a pad and the fill its edge touches, thermal spokes included; a
    track and the fill along it; track ends landing on pads, vias and other tracks).  The main GND is the part with the
    most pads.  Returns (cut-off top / bottom islands, every island, keys of the islands in the main GND)."""
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
    pads = []   # (key, layer, polygon)
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.GetNetname() != "GND":
                continue
            key = ("P", fp.GetReference(), p.GetNumber()); find(key)
            for L in p.GetLayerSet().CuStack():
                ps = pcbnew.SHAPE_POLY_SET()
                p.TransformShapeToPolygon(ps, L, 0, MM(0.005), pcbnew.ERROR_INSIDE)
                pads.append((key, L, ps))
                o = ps.Outline(0)
                edge = []
                for i in range(o.PointCount()):
                    a_, c_ = o.CPoint(i), o.CPoint((i + 1) % o.PointCount())
                    n_ = max(1, int(math.hypot(c_.x - a_.x, c_.y - a_.y) / MM(0.05)))
                    edge += [pcbnew.VECTOR2I(int(a_.x + (c_.x - a_.x) * t / n_), int(a_.y + (c_.y - a_.y) * t / n_)) for t in range(n_)]
                pbb = p.GetBoundingBox()
                for k, l, poly, bb in isl:
                    if l == L and bb.Intersects(pbb) and (poly.Collide(p.GetPosition()) or any(poly.Collide(e, MM(0.01)) for e in edge)):
                        union(key, k)
    vias = []
    for t in board.GetTracks():
        if t.GetNetname() != "GND" or t.GetClass() != "PCB_VIA":
            continue
        key = ("T", t.m_Uuid.AsString()); find(key)
        r = t.GetWidth(pcbnew.F_Cu) // 2
        vias.append((key, t.GetPosition(), r))
        for L in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu):
            for h in hits(L, t.GetPosition(), r):
                union(key, h)
            for pk, pl, ps in pads:
                if pl == L and ps.Collide(t.GetPosition(), r):
                    union(key, pk)
    segs = []
    for t in board.GetTracks():
        if t.GetNetname() != "GND" or t.GetClass() == "PCB_VIA":
            continue
        key = ("T", t.m_Uuid.AsString()); find(key)
        L, hw, s0, s1 = t.GetLayer(), t.GetWidth() // 2, t.GetStart(), t.GetEnd()
        segs.append((key, L, hw, s0, s1))
        n_ = max(1, int(math.hypot(s1.x - s0.x, s1.y - s0.y) / MM(0.25)))
        for i in range(n_ + 1):
            for h in hits(L, pcbnew.VECTOR2I(int(s0.x + (s1.x - s0.x) * i / n_), int(s0.y + (s1.y - s0.y) * i / n_)), hw):
                union(key, h)
        for pos in (s0, s1):
            for pk, pl, ps in pads:
                if pl == L and ps.Collide(pos, hw):
                    union(key, pk)
            for vk, vp, vr in vias:
                if math.hypot(pos.x - vp.x, pos.y - vp.y) <= vr + hw:
                    union(key, vk)

    def seg_dist(p, a_, c_):
        vx, vy = c_.x - a_.x, c_.y - a_.y
        L2 = vx * vx + vy * vy
        t_ = 0.0 if L2 == 0 else max(0.0, min(1.0, ((p.x - a_.x) * vx + (p.y - a_.y) * vy) / L2))
        return math.hypot(p.x - (a_.x + t_ * vx), p.y - (a_.y + t_ * vy))
    for key, L, hw, s0, s1 in segs:
        for k2, L2, hw2, q0, q1 in segs:
            if k2 != key and L2 == L and (seg_dist(s0, q0, q1) <= hw + hw2 or seg_dist(s1, q0, q1) <= hw + hw2):
                union(key, k2)
    npads = {}
    for pk, pl, ps in pads:
        npads.setdefault(find(pk), set()).add(pk)
    main = max(npads, key=lambda r: len(npads[r])) if npads else None
    anchored = {k for k, l, poly, bb in isl if find(k) == main}
    out = []
    for k, l, poly, bb in isl:
        if k not in anchored and k[1] in ("GND top", "GND bottom"):
            out.append({"key": [k[1], int(l), k[3]], "layer": int(l), "area": round(poly.Area() / 1e12, 2),
                        "bbox": [round(TO(bb.GetLeft()), 2), round(TO(bb.GetTop()), 2), round(TO(bb.GetRight()), 2), round(TO(bb.GetBottom()), 2)],
                        "pads": sorted("%s.%s" % (pk[1], pk[2]) for pk in npads.get(find(k), ()))})
    return out, isl, anchored


if "--snapshot" in sys.argv:
    json.dump(cut_off_islands(b)[0], open(sys.argv[sys.argv.index("--snapshot") + 1], "w"))
    sys.exit(0)
if "--list" in sys.argv:
    for x in cut_off_islands(b)[0]:
        print("  cut off: %s %s %.2f mm2 at x %.1f..%.1f y %.1f..%.1f, pads %s" % (x["key"][0], b.GetLayerName(x["layer"]), x["area"], x["bbox"][0], x["bbox"][2], x["bbox"][1], x["bbox"][3], x["pads"] or "none"))
    sys.exit(0)
STITCH_ALL = "--stitch-all" in sys.argv
NET = None if STITCH_ALL else sys.argv[2]
BEFORE = json.load(open(sys.argv[sys.argv.index("--before") + 1])) if "--before" in sys.argv else None
dru = open(D + "Unified Control Board.kicad_dru").read()
keep = []


def P(v):
    return (TO(v.x), TO(v.y))


def V(p):
    return pcbnew.VECTOR2I(MM(p[0]), MM(p[1]))


# 1. the net's dead copper
res = nr.analyse(NET, nr.load(b)[NET]) if NET else {"dead": [], "dead_vias": []}
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
if NET:
    print("  %s: %d dead pieces removed" % (NET, done))
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())

# 2. GND pour islands cut off from the main GND: stitch each to it
def island_poly(isl_all, x):
    for k, l, p, bb in isl_all:
        if l == x["layer"] and all(abs(a - c) < 0.01 for a, c in zip(x["bbox"], [TO(bb.GetLeft()), TO(bb.GetTop()), TO(bb.GetRight()), TO(bb.GetBottom())])):
            return p
    return None


def overlaps(a, c):
    """two islands' bounding boxes share at least half of the smaller one"""
    ix = min(a[2], c[2]) - max(a[0], c[0]); iy = min(a[3], c[3]) - max(a[1], c[1])
    if ix <= 0 or iy <= 0:
        return False
    return ix * iy >= 0.5 * min((a[2] - a[0]) * (a[3] - a[1]), (c[2] - c[0]) * (c[3] - c[1]))


def stitch(targets):
    added = 0
    holes = [P(t.GetPosition()) for t in b.GetTracks() if t.GetClass() == "PCB_VIA"]
    holes += [P(p.GetPosition()) for fp in b.GetFootprints() for p in fp.Pads() if p.GetDrillSize().x > 0]
    for x0_ in sorted(targets, key=lambda x: -x["area"]):
        now, isl_all, anchored = cut_off_islands(b)
        x = next((y for y in now if y["layer"] == x0_["layer"] and overlaps(y["bbox"], x0_["bbox"])), None)
        if x is None:
            print("  GND island on %s at x %.1f..%.1f y %.1f..%.1f: joined by an earlier stitch" % (
                b.GetLayerName(x0_["layer"]), x0_["bbox"][0], x0_["bbox"][2], x0_["bbox"][1], x0_["bbox"][3]))
            continue
        L = x["layer"]
        x0, y0, x1, y1 = x["bbox"]
        poly = island_poly(isl_all, x)
        ground = [p for k, l, p, bb in isl_all if k in anchored and l != L and l in (pcbnew.F_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)]
        R = kr.Router(b, dru, (x0 - 1, y0 - 1, x1 + 1, y1 + 1), step=0.05)
        m = None
        for ln in ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu"):
            mm_ = R.blocked(ln, "GND", 0.2 + 0.3, for_via=True)
            m = mm_ if m is None else (m | mm_)
        ring = [(0, 0), (0.3, 0), (-0.3, 0), (0, 0.3), (0, -0.3)]
        best = None
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        for i_ in range(int((x1 - x0) / 0.05) + 1):
            for j_ in range(int((y1 - y0) / 0.05) + 1):
                px, py = x0 + i_ * 0.05, y0 + j_ * 0.05
                if not all(poly.Contains(V((px + dx, py + dy))) for dx, dy in ring):
                    continue
                if not any(all(g_.Contains(V((px + dx, py + dy))) for dx, dy in ring) for g_ in ground):
                    continue
                gi, gj = R.grid.ij(px, py)
                if m[gj, gi] or any(math.hypot(px - hx, py - hy) < 1.0 for hx, hy in holes):
                    continue
                dc = math.hypot(px - cx, py - cy)
                if best is None or dc < best[0]:
                    best = (dc, px, py)
        where = "%s %.2f mm2 at x %.1f..%.1f y %.1f..%.1f" % (b.GetLayerName(L), x["area"], x0, x1, y0, y1)
        if best:
            v = pcbnew.PCB_VIA(b)
            v.SetPosition(V((best[1], best[2]))); v.SetWidth(MM(0.6)); v.SetDrill(MM(0.3))
            v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetNet(b.FindNet("GND"))
            b.Add(v); keep.append(v); added += 1
            holes.append((best[1], best[2]))
            filler.Fill(b.Zones())
            print("  GND island cut off on %s: stitched at (%.2f, %.2f)" % (where, best[1], best[2]))
        else:
            print("  GND island cut off on %s: no room for a via" % where)
    return added


filler = pcbnew.ZONE_FILLER(b)
if STITCH_ALL:
    stitch(cut_off_islands(b)[0])
else:
    now = cut_off_islands(b)[0]
    if BEFORE is not None:
        stitch([x for x in now if not any(x["layer"] == y["layer"] and overlaps(x["bbox"], y["bbox"]) for y in BEFORE)])
pcbnew.SaveBoard(PCB, b)
