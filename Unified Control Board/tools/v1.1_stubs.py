# v1.1_stubs.py <board dir> [--apply]: find track stubs that run past their last connection and go nowhere (KiCad
# DRC's "track_dangling"), and with --apply cut each one back to that connection.
#
# KiCad's JSON report gives a dangling track's *start* as its position, not the loose end, so the loose end is
# found here the way KiCad's own test finds it: the end that no other same-net item touches (within half the track
# width).  The cut goes to the nearest point along the segment where same-net copper already attaches (another
# track's end, a via centre or a pad centre inside this track's copper), so no connection is added or lost; a
# segment attached only at its far end is removed.  One pass only: whatever DRC reports afterwards is for a person
# to look at, because chasing it automatically can eat real routing.  Run with KiCad 10's Python:
#   /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 \
#       "Unified Control Board/tools/v1.1_stubs.py" "Unified Control Board" --apply
import sys, os, json, math, re, subprocess, tempfile
import pcbnew

TO, MM = pcbnew.ToMM, pcbnew.FromMM
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
D = sys.argv[1].rstrip("/") + "/"
APPLY = "--apply" in sys.argv
PCB = D + "Unified Control Board.kicad_pcb"
b = pcbnew.LoadBoard(PCB)
keep = []   # KiCad 10 SWIG: removed items must stay referenced


def P(v):
    return (TO(v.x), TO(v.y))


def V(p):
    return pcbnew.VECTOR2I(MM(p[0]), MM(p[1]))


def dist(p, q):
    return math.hypot(p[0] - q[0], p[1] - q[1])


def proj(p, a, c):
    vx, vy = c[0] - a[0], c[1] - a[1]
    L2 = vx * vx + vy * vy
    t = ((p[0] - a[0]) * vx + (p[1] - a[1]) * vy) / L2 if L2 else 0.0
    t = min(1.0, max(0.0, t))
    q = (a[0] + t * vx, a[1] + t * vy)
    return t, dist(p, q), q


def drc_dangling():
    out = tempfile.mktemp(suffix=".json")
    subprocess.run([KICAD_CLI, "pcb", "drc", "--severity-all", "--format", "json", "-o", out, PCB],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    d = json.load(open(out))
    os.unlink(out)
    return [v for v in d.get("violations", []) if v["type"] in ("track_dangling", "via_dangling")]


def same_net(seg):
    net, lay = seg.GetNetname(), seg.GetLayer()
    me = seg.m_Uuid.AsString()   # SWIG makes a new wrapper per lookup, so compare the items' UUIDs
    tracks = [t for t in b.GetTracks() if t.m_Uuid.AsString() != me and t.GetNetname() == net and t.GetClass() == "PCB_TRACK" and t.GetLayer() == lay]
    vias = [t for t in b.GetTracks() if t.GetNetname() == net and t.GetClass() == "PCB_VIA" and t.IsOnLayer(lay)]
    pads = [p for fp in b.GetFootprints() for p in fp.Pads() if p.GetNetname() == net and p.IsOnLayer(lay)]
    zones = [z for z in b.Zones() if not z.GetIsRuleArea() and z.GetNetname() == net and z.IsOnLayer(lay)]
    return tracks, vias, pads, zones


def end_counts(seg):
    """how many other same-net items touch the start and the end (KiCad: within half this track's width).  As in
    KiCad's own test, an item touching both ends of a short segment counts only for the nearer end."""
    acc = TO(seg.GetWidth()) / 2
    tracks, vias, pads, zones = same_net(seg)
    lay = seg.GetLayer()
    a, c = P(seg.GetStart()), P(seg.GetEnd())
    hits = []   # (touches start, touches end, distance to start, distance to end)
    for t in tracks:
        da, dc = (proj(p, P(t.GetStart()), P(t.GetEnd()))[1] - TO(t.GetWidth()) / 2 for p in (a, c))
        hits.append((da <= acc, dc <= acc, da, dc))
    for v in vias:
        da, dc = (dist(p, P(v.GetPosition())) - TO(v.GetWidth(lay)) / 2 for p in (a, c))
        hits.append((da <= acc, dc <= acc, da, dc))
    for p in pads:
        ha, hc = p.HitTest(V(a), MM(acc)), p.HitTest(V(c), MM(acc))
        hits.append((ha, hc, dist(a, P(p.GetPosition())), dist(c, P(p.GetPosition()))))
    for z in zones:
        polys = z.GetFilledPolysList(lay)
        hits.append((polys.Collide(V(a), MM(acc)), polys.Collide(V(c), MM(acc)), 0.0, 0.0))
    ns = ne = 0
    for ha, hc, da, dc in hits:
        if ha and hc:
            ns, ne = (ns + 1, ne) if da <= dc else (ns, ne + 1)
        else:
            ns, ne = ns + ha, ne + hc
    return ns, ne


def touched(seg, pt):
    ns, ne = end_counts(seg)
    return (ns if dist(pt, P(seg.GetStart())) < 1e-6 else ne) > 0


def first_attachment(seg, E, S):
    """nearest point from E towards S where same-net copper already connects to this segment (its anchor lies in
    this segment's copper), as (distance from E, what, point); S itself counts when something touches it there"""
    hw = TO(seg.GetWidth()) / 2
    tracks, vias, pads, zones = same_net(seg)
    L = dist(E, S)
    cand = []
    for t in tracks:
        for X in (P(t.GetStart()), P(t.GetEnd())):
            tt, d, q = proj(X, E, S)
            if d <= hw + 1e-4 and tt * L > 1e-3:
                cand.append((tt * L, "track end", q))
    for v in vias:
        tt, d, q = proj(P(v.GetPosition()), E, S)
        if d <= hw + 1e-4 and tt * L > 1e-3:
            cand.append((tt * L, "via", q))
    for p in pads:
        tt, d, q = proj(P(p.GetPosition()), E, S)
        if d <= hw + 1e-4 and tt * L > 1e-3:
            cand.append((tt * L, "pad %s.%s" % (p.GetParentFootprint().GetReference(), p.GetNumber()), q))
    if touched(seg, S):
        cand.append((L, "far end", S))
    return min(cand, key=lambda c: c[0]) if cand else None


items = drc_dangling()
print("%d dangling items" % len(items))
actions = []
for v in items:
    it = v["items"][0]
    pos = (it["pos"]["x"], it["pos"]["y"])
    m = re.match(r"(Track|Via) \[(.*?)\] on ([^, ]+)(?:, length ([0-9.]+) mm)?", it["description"])
    kind, net, layname, length = m.group(1), m.group(2), m.group(3), m.group(4)
    if kind == "Via":
        via = [t for t in b.GetTracks() if t.GetClass() == "PCB_VIA" and t.GetNetname() == net and dist(P(t.GetPosition()), pos) < 0.005]
        if len(via) != 1:
            print("  %-11s via at (%.2f, %.2f): %d matching vias - look at it" % (net, pos[0], pos[1], len(via)))
            continue
        via, r = via[0], TO(via[0].GetWidth(pcbnew.F_Cu)) / 2
        c = P(via.GetPosition())
        hits = [t for t in b.GetTracks() if t.GetClass() == "PCB_TRACK" and t.GetNetname() == net
                and (dist(P(t.GetStart()), c) <= r or dist(P(t.GetEnd()), c) <= r or proj(c, P(t.GetStart()), P(t.GetEnd()))[1] <= TO(t.GetWidth()) / 2)]
        pads = [p for fp in b.GetFootprints() for p in fp.Pads() if p.GetNetname() == net and p.HitTest(via.GetPosition())]
        layers = {t.GetLayer() for t in hits}
        # redundant: it touches no pad, only tracks on one layer, and those tracks meet each other without it
        grp = list(range(len(hits)))   # union-find over the tracks it touches, joined track to track
        def root(i):
            while grp[i] != i:
                i = grp[i]
            return i
        for i, ti in enumerate(hits):
            for j, tj in enumerate(hits):
                if i < j and any(proj(e, P(tj.GetStart()), P(tj.GetEnd()))[1] <= TO(tj.GetWidth()) / 2 + 1e-4 for e in (P(ti.GetStart()), P(ti.GetEnd()))) \
                        or i < j and any(proj(e, P(ti.GetStart()), P(ti.GetEnd()))[1] <= TO(ti.GetWidth()) / 2 + 1e-4 for e in (P(tj.GetStart()), P(tj.GetEnd()))):
                    grp[root(i)] = root(j)
        meet = len({root(i) for i in range(len(hits))}) <= 1
        if not pads and len(layers) <= 1 and meet:
            print("  %-11s via at (%.2f, %.2f): joins %d track(s) on one layer that meet without it - remove" % (net, c[0], c[1], len(hits)))
            actions.append(("remove", via, None))
        else:
            print("  %-11s via at (%.2f, %.2f): connects on one layer only - look at it" % (net, pos[0], pos[1]))
        continue
    lid = b.GetLayerID(layname)
    segs = [t for t in b.GetTracks() if t.GetClass() == "PCB_TRACK" and t.GetNetname() == net and t.GetLayer() == lid
            and dist(P(t.GetStart()), pos) < 0.005 and abs(TO(t.GetLength()) - float(length)) < 0.002]
    if len(segs) != 1:
        print("  %-11s %-6s at (%.2f, %.2f): %d matching segments - look at it" % (net, layname, pos[0], pos[1], len(segs)))
        continue
    seg = segs[0]
    a, c = P(seg.GetStart()), P(seg.GetEnd())
    ta, tc = (n > 0 for n in end_counts(seg))
    if ta and tc:
        print("  %-11s %-6s at (%.2f, %.2f): both ends touch something - look at it" % (net, layname, pos[0], pos[1]))
        continue
    if not ta and not tc:
        print("  %-11s %-6s at (%.2f, %.2f): touches nothing at either end - look at it" % (net, layname, pos[0], pos[1]))
        continue
    E, S, start_is_E = (a, c, True) if not ta else (c, a, False)
    att = first_attachment(seg, E, S)
    if att is None:
        print("  %-11s %-6s loose end (%.2f, %.2f): nothing attaches along it - look at it" % (net, layname, E[0], E[1]))
        continue
    cut, what, q = att
    if what == "via":
        vr = min((TO(v.GetWidth(seg.GetLayer())) / 2 for v in same_net(seg)[1] if dist(P(v.GetPosition()), q) <= TO(seg.GetWidth()) / 2 + 1e-4), default=0)
        if dist(q, S) <= vr:
            what = "far end"   # what would be left lies inside that via's pad: the whole segment is the stub
    if cut >= dist(E, S) - 1e-3:
        what = "far end"       # the first attachment is the far end itself
    if what == "far end":
        print("  %-11s %-6s loose end (%.2f, %.2f): whole %.2f mm segment hangs off its far end - remove" % (net, layname, E[0], E[1], cut))
        actions.append(("remove", seg, None))
    else:
        print("  %-11s %-6s loose end (%.2f, %.2f): cut %.2f mm back to a %s at (%.3f, %.3f)" % (net, layname, E[0], E[1], cut, what, q[0], q[1]))
        actions.append(("cut", seg, (start_is_E, q)))
if APPLY:
    for act, seg, arg in actions:
        if act == "remove":
            b.Remove(seg)
            keep.append(seg)
        else:
            (seg.SetStart if arg[0] else seg.SetEnd)(V(arg[1]))
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(PCB, b)
    left = drc_dangling()
    print("applied %d; dangling items left: %d" % (len(actions), len(left)))
    for v in left:
        print("   ", v["items"][0]["description"], "@", (v["items"][0]["pos"]["x"], v["items"][0]["pos"]["y"]))
