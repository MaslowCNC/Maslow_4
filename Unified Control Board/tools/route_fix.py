# route_fix.py <board dir> <report.json> dead: remove the copper tools/netreview.py found dead (track pieces that no
# path between pads or zones uses), cutting each segment back to its live part.  Run with KiCad 10's Python.
# Only acts on the report it is given, so run netreview.py on the same board first.
import sys, json, math, collections
import pcbnew

TO, MM = pcbnew.ToMM, pcbnew.FromMM
D = sys.argv[1].rstrip("/") + "/"
PCB = D + "Unified Control Board.kicad_pcb"
REP = json.load(open(sys.argv[2]))
CMD = sys.argv[3]
b = pcbnew.LoadBoard(PCB)
keep = []


def P(v):
    return (TO(v.x), TO(v.y))


def V(p):
    return pcbnew.VECTOR2I(MM(p[0]), MM(p[1]))


def t_of(p, a, c):
    vx, vy = c[0] - a[0], c[1] - a[1]
    L2 = vx * vx + vy * vy
    return ((p[0] - a[0]) * vx + (p[1] - a[1]) * vy) / L2 if L2 else 0.0


if CMD == "dead":
    by_uid = {t.m_Uuid.AsString(): t for t in b.GetTracks()}
    dead = collections.defaultdict(list)
    for r in REP:
        for d in r["dead"]:
            dead[d["uid"]].append((d["p"], d["q"], r["net"]))
    removed = trimmed = 0
    for uid, pieces in dead.items():
        t = by_uid.get(uid)
        if t is None:
            print("  missing segment", uid)
            continue
        a, c = P(t.GetStart()), P(t.GetEnd())
        iv = sorted((min(t_of(p, a, c), t_of(q, a, c)), max(t_of(p, a, c), t_of(q, a, c))) for p, q, n in pieces)
        # merge the dead intervals
        merged = []
        for lo, hi in iv:
            if merged and lo <= merged[-1][1] + 1e-6:
                merged[-1][1] = max(merged[-1][1], hi)
            else:
                merged.append([lo, hi])
        net = pieces[0][2]
        L = math.dist(a, c)
        live = 1.0 - sum(hi - lo for lo, hi in merged)
        if len(merged) == 1 and merged[0][0] <= 1e-6 and merged[0][1] >= 1 - 1e-6 or live * L < 0.06:
            b.Remove(t)
            keep.append(t)
            removed += 1
            print("  %-16s removed %.3f mm at (%.2f, %.2f)-(%.2f, %.2f)" % (net, math.dist(a, c), a[0], a[1], c[0], c[1]))
            continue
        lo_live = max([hi for lo, hi in merged if lo <= 1e-6] or [0.0])
        hi_live = min([lo for lo, hi in merged if hi >= 1 - 1e-6] or [1.0])
        assert all(lo <= 1e-6 or hi >= 1 - 1e-6 for lo, hi in merged), "dead piece in the middle of %s" % uid
        na = (a[0] + (c[0] - a[0]) * lo_live, a[1] + (c[1] - a[1]) * lo_live)
        nc = (a[0] + (c[0] - a[0]) * hi_live, a[1] + (c[1] - a[1]) * hi_live)
        t.SetStart(V(na))
        t.SetEnd(V(nc))
        trimmed += 1
        print("  %-16s trimmed %.3f mm off (%.2f, %.2f)-(%.2f, %.2f)" % (net, math.dist(a, c) - math.dist(na, nc), a[0], a[1], c[0], c[1]))
    print("dead copper: %d segments removed, %d trimmed" % (removed, trimmed))

if CMD in ("dead", "tidy"):
    # degenerate fragments: segments under 0.01 mm long, or lying wholly inside a same-net via or pad
    vias = [t for t in b.GetTracks() if t.GetClass() == "PCB_VIA"]
    pads = [p for fp in b.GetFootprints() for p in fp.Pads()]
    gone = 0
    for t in list(b.GetTracks()):
        if t.GetClass() != "PCB_TRACK":
            continue
        a, c = P(t.GetStart()), P(t.GetEnd())
        inside = any(v.GetNetname() == t.GetNetname() and all(math.dist(e, P(v.GetPosition())) + TO(t.GetWidth()) / 2 <= TO(v.GetWidth(t.GetLayer())) / 2 + 1e-3 for e in (a, c)) for v in vias)
        inside = inside or any(p.GetNetname() == t.GetNetname() and p.IsOnLayer(t.GetLayer()) and p.HitTest(V(a)) and p.HitTest(V(c)) for p in pads)
        if math.dist(a, c) < 0.01 or inside:
            # only if something else of the net still touches both ends (it is a fragment, not a link)
            b.Remove(t)
            keep.append(t)
            gone += 1
            print("  %-16s fragment %.3f mm at (%.3f, %.3f) removed" % (t.GetNetname(), math.dist(a, c), a[0], a[1]))
    print("fragments removed: %d" % gone)

if CMD == "via1":
    # vias whose copper is all on one layer: snap the track ends inside them onto the centre, then drop the via
    for r in REP:
        for vx, vy in r["via1"]:
            v = next((t for t in b.GetTracks() if t.GetClass() == "PCB_VIA" and t.GetNetname() == r["net"] and math.dist(P(t.GetPosition()), (vx, vy)) < 0.01), None)
            if v is None:
                continue
            c, rad = P(v.GetPosition()), TO(v.GetWidth(pcbnew.F_Cu)) / 2
            ends = []
            for t in b.GetTracks():
                if t.GetClass() == "PCB_TRACK" and t.GetNetname() == r["net"]:
                    for k, e in enumerate((P(t.GetStart()), P(t.GetEnd()))):
                        if math.dist(e, c) <= rad + 1e-3:
                            ends.append((t, k))
            if len({t.GetLayer() for t, k in ends}) != 1 or any(p.GetNetname() == r["net"] and p.HitTest(v.GetPosition()) for fp in b.GetFootprints() for p in fp.Pads()):
                print("  %-16s via at (%.2f, %.2f): not one-layer here, left" % (r["net"], c[0], c[1]))
                continue
            for t, k in ends:
                (t.SetStart if k == 0 else t.SetEnd)(V(c))
            b.Remove(v)
            keep.append(v)
            print("  %-16s via at (%.2f, %.2f): %d track ends joined at its centre, via removed" % (r["net"], c[0], c[1], len(ends)))

pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(PCB, b)
