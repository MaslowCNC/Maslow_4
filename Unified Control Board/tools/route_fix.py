# route_fix.py <board dir> <report.json> dead: remove the copper tools/netreview.py found dead (track pieces that no
# path between pads or zones uses), cutting each segment back to its live part.  Run with KiCad 10's Python.
# Only acts on the report it is given, so run netreview.py on the same board first.
import sys, json, math, collections, re
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

if CMD == "pull":
    # Straighten each chain of same-layer, same-width segments between real junctions (pads, vias, zones, T-junctions,
    # width changes): from each point jump to the farthest later point reachable in a straight line with full
    # clearance.  The chain's ends stay put, so nothing is connected or disconnected.
    import os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import kirouter as kr
    dru = open(D + "Unified Control Board.kicad_dru").read()
    SKIP = re.compile(r"IS[ABC][12]|S[ABC][12]|U9_SYNC|USBD[+-]")   # spacing chosen on purpose
    args = sys.argv[4:]
    MARGIN = 0.03
    if "--margin" in args:   # extra clearance kept while straightening (the sense nets keep their spacing)
        k = args.index("--margin"); MARGIN = float(args[k + 1]); args = args[:k] + args[k + 2:]
    only = set(args)
    tracks_all = [t for t in b.GetTracks() if t.GetClass() == "PCB_TRACK"]
    vias_all = [t for t in b.GetTracks() if t.GetClass() == "PCB_VIA"]
    pads_all = [(fp, p) for fp in b.GetFootprints() for p in fp.Pads()]
    zones_all = [z for z in b.Zones() if not z.GetIsRuleArea()]
    nets = sorted({t.GetNetname() for t in tracks_all if t.GetNetname()})
    log = []
    for net in nets:
        if (only and net not in only) or (not only and SKIP.fullmatch(net)):
            continue
        clr = 0.2 if kr.POWER_RE.fullmatch(net) else 0.15
        for layer in ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu"):
            lid = b.GetLayerID(layer)
            segs = [t for t in b.GetTracks() if t.GetClass() == "PCB_TRACK" and t.GetNetname() == net and t.GetLayer() == lid]
            if len(segs) < 2:
                continue
            key = lambda p: (round(p[0], 4), round(p[1], 4))
            ends = collections.defaultdict(list)
            for t in segs:
                ends[key(P(t.GetStart()))].append((t, 0))
                ends[key(P(t.GetEnd()))].append((t, 1))
            # points where something other than two chain segments meets: they stay
            fixed = set()
            for k, lst in ends.items():
                pt = (k[0], k[1])
                if len(lst) != 2 or TO(lst[0][0].GetWidth()) != TO(lst[1][0].GetWidth()):
                    fixed.add(k); continue
                if any(v.GetNetname() == net and math.dist(P(v.GetPosition()), pt) <= TO(v.GetWidth(lid)) / 2 + 0.05 for v in vias_all):
                    fixed.add(k); continue
                if any(p.GetNetname() == net and p.IsOnLayer(lid) and p.HitTest(V(pt), MM(0.1)) for fp, p in pads_all):
                    fixed.add(k); continue
                if any(z.GetNetname() == net and z.IsOnLayer(lid) and z.GetFilledPolysList(lid).Collide(V(pt), MM(0.3)) for z in zones_all):
                    fixed.add(k); continue
            # segments that carry a T-junction (another same-net end or via in their copper, away from their ends) stay whole
            anchors = [P(o.GetStart()) for o in segs] + [P(o.GetEnd()) for o in segs] + [P(v.GetPosition()) for v in vias_all if v.GetNetname() == net]
            def carries_t(t):
                a, c = P(t.GetStart()), P(t.GetEnd()); hw = TO(t.GetWidth()) / 2
                for q in anchors:
                    if math.dist(q, a) < 1e-3 or math.dist(q, c) < 1e-3:
                        continue
                    vx, vy = c[0] - a[0], c[1] - a[1]; L2 = vx * vx + vy * vy
                    tt = ((q[0] - a[0]) * vx + (q[1] - a[1]) * vy) / L2 if L2 else 0
                    if 0 < tt < 1 and math.dist(q, (a[0] + tt * vx, a[1] + tt * vy)) <= hw + 0.35:
                        return True
                return False
            pinned = {t.m_Uuid.AsString() for t in segs if carries_t(t)}
            for t in segs:
                if t.m_Uuid.AsString() in pinned:
                    fixed.add(key(P(t.GetStart()))); fixed.add(key(P(t.GetEnd())))
            # walk chains from fixed points
            used = set()
            for k0 in list(ends):
                if k0 not in fixed:
                    continue
                for t0, e0 in ends[k0]:
                    if t0.m_Uuid.AsString() in used or t0.m_Uuid.AsString() in pinned:
                        continue
                    chain, pts = [], [(k0[0], k0[1])]
                    t, e = t0, e0
                    while True:
                        chain.append(t); used.add(t.m_Uuid.AsString())
                        far = P(t.GetEnd()) if e == 0 else P(t.GetStart())
                        pts.append(far)
                        kf = key(far)
                        if kf in fixed:
                            break
                        nxt = [(o, oe) for o, oe in ends[kf] if o.m_Uuid.AsString() != t.m_Uuid.AsString()]
                        if len(nxt) != 1 or nxt[0][0].m_Uuid.AsString() in used or nxt[0][0].m_Uuid.AsString() in pinned:
                            break
                        t, e = nxt[0]
                    if len(chain) < 2 or key(pts[-1]) not in fixed:
                        continue
                    w = TO(chain[0].GetWidth())
                    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
                    R = kr.Router(b, dru, (min(xs) - 2, min(ys) - 2, max(xs) + 2, max(ys) + 2), step=0.025)
                    mask = R.blocked(layer, net, clr + w / 2 + MARGIN)
                    def free(p, q):
                        n = max(2, int(math.dist(p, q) / 0.0125))
                        for s_ in range(n + 1):
                            x = p[0] + (q[0] - p[0]) * s_ / n; y = p[1] + (q[1] - p[1]) * s_ / n
                            i, j = R.grid.ij(x, y)
                            if mask[j, i]:
                                return False
                        return True
                    new = [pts[0]]
                    i = 0
                    while i < len(pts) - 1:
                        j = len(pts) - 1
                        while j > i + 1 and not free(pts[i], pts[j]):
                            j -= 1
                        new.append(pts[j]); i = j
                    oldL = sum(math.dist(a, c) for a, c in zip(pts, pts[1:]))
                    newL = sum(math.dist(a, c) for a, c in zip(new, new[1:]))
                    if len(new) < len(pts) and newL <= oldL + 1e-6:
                        for o in chain:
                            b.Remove(o); keep.append(o)
                        for a, c in zip(new, new[1:]):
                            nt = pcbnew.PCB_TRACK(b)
                            nt.SetStart(V(a)); nt.SetEnd(V(c)); nt.SetWidth(MM(w)); nt.SetLayer(lid); nt.SetNet(chain[0].GetNet())
                            b.Add(nt); keep.append(nt)
                        log.append((net, layer, len(pts) - 1, len(new) - 1, oldL, newL, pts[0], pts[-1]))
    for net, layer, n0, n1, l0, l1, a, c in log:
        print("  %-16s %-6s %3d -> %2d segments, %7.2f -> %7.2f mm  (%.2f, %.2f)-(%.2f, %.2f)" % (net, layer, n0, n1, l0, l1, a[0], a[1], c[0], c[1]))
    print("pulled %d chains: %d -> %d segments, %.1f mm shorter" % (len(log), sum(x[2] for x in log), sum(x[3] for x in log), sum(x[4] - x[5] for x in log)))

pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(PCB, b)
