# gndcheck.py <board.kicad_pcb> [net]: connected components of a net through its zone fills, vias and pads.
# Prints every component but the largest with its islands' bounding boxes (the islands DRC calls unconnected).
import sys
import pcbnew

TO = pcbnew.ToMM
b = pcbnew.LoadBoard(sys.argv[1])
net = sys.argv[2] if len(sys.argv) > 2 else "GND"
parent = {}


def find(a):
    while parent.setdefault(a, a) != a:
        parent[a] = parent[parent[a]]
        a = parent[a]
    return a


def union(a, c):
    parent[find(a)] = find(c)


islands = []   # (key, layer, SHAPE_POLY_SET with one outline, bbox)
for z in b.Zones():
    if z.GetIsRuleArea() or z.GetNetname() != net:
        continue
    for L in z.GetLayerSet().Seq():
        fp = z.GetFilledPolysList(L)
        for k in range(fp.OutlineCount()):
            one = pcbnew.SHAPE_POLY_SET()
            one.AddOutline(fp.Outline(k))
            for h in range(fp.HoleCount(k)):
                one.AddHole(fp.Hole(k, h))
            key = ("Z", z.GetZoneName(), L, k)
            find(key)
            islands.append((key, L, one, fp.Outline(k).BBox()))


def hits(L, pos):
    return [key for key, l, poly, bb in islands if l == L and bb.Contains(pos) and poly.Contains(pos)]


items = []
for t in b.GetTracks():
    if t.GetNetname() != net:
        continue
    key = ("T", id(t), TO(t.GetStart().x), TO(t.GetStart().y))
    find(key)
    if t.GetClass() == "PCB_VIA":
        for L in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu):
            for h in hits(L, t.GetPosition()):
                union(key, h)
    else:
        for pos in (t.GetStart(), t.GetEnd()):
            for h in hits(t.GetLayer(), pos):
                union(key, h)
    items.append((key, t))
# tracks touching tracks / vias at their ends
ends = {}
for key, t in items:
    pts = [t.GetPosition()] if t.GetClass() == "PCB_VIA" else [t.GetStart(), t.GetEnd()]
    for p in pts:
        ends.setdefault((round(TO(p.x), 3), round(TO(p.y), 3)), []).append(key)
for k, ks in ends.items():
    for a in ks[1:]:
        union(a, ks[0])
for fp in b.GetFootprints():
    for p in fp.Pads():
        if p.GetNetname() != net:
            continue
        key = ("P", fp.GetReference(), p.GetNumber())
        find(key)
        for L in p.GetLayerSet().CuStack():
            for h in hits(L, p.GetPosition()):
                union(key, h)
        e = ends.get((round(TO(p.GetPosition().x), 3), round(TO(p.GetPosition().y), 3)))
        if e:
            union(key, e[0])
comps = {}
for a in list(parent):
    comps.setdefault(find(a), []).append(a)
comps = sorted(comps.values(), key=len, reverse=True)
print("%s: %d components (largest %d members)" % (net, len(comps), len(comps[0])))
for c in comps[1:]:
    zs = [a for a in c if a[0] == "Z"]
    desc = []
    for key, l, poly, bb in islands:
        if key in zs:
            desc.append("%s %s x %.1f..%.1f y %.1f..%.1f" % (key[1], b.GetLayerName(l), TO(bb.GetLeft()), TO(bb.GetRight()), TO(bb.GetTop()), TO(bb.GetBottom())))
    others = [a for a in c if a[0] != "Z"]
    print("  component:", desc, "| items:", [(a[0], a[1] if a[0] == "P" else round(a[2], 2), a[2] if a[0] == "P" else round(a[3], 2)) for a in others][:6])
