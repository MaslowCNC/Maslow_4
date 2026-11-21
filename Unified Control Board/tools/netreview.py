# netreview.py <board.kicad_pcb> <out dir> [net ...]: trace every routed net and report what looks wrong with it.
# Run with KiCad 10's Python.  For each net it builds the copper the way KiCad connects it (a track end, via centre or
# pad centre inside another item, a track overlapping a pad, anything touching a zone fill) and reports:
#   dead   - tracks and vias that lead nowhere: peeled off as leaves, pads and zone islands never peeled;
#   loop   - copper that forms a closed loop (two paths between the same points; zone links not counted);
#   via1   - vias whose copper is all on one layer (they join nothing between layers);
#   detour - pad-to-pad copper paths much longer than the straight line between the pads (only for pairs whose
#            path passes no third pad), as path / straight, excess mm;
#   jog    - tiny segments (< 0.05 mm) and back-and-forth jogs.
# Writes <out>/report.json, <out>/report.txt and contact sheets <out>/sheet_NN.png (one tile per net, numbered in
# report.txt) plus <out>/net_<name>.png for each net.
import sys, os, json, math, re, heapq, collections
import numpy as np
import pcbnew

TO, MM = pcbnew.ToMM, pcbnew.FromMM
LAYERS = ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu"]
b, LID = None, {}


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


# ------------------------------------------------------------------ gather copper per net
class Item:
    __slots__ = ("kind", "obj", "layers", "pts", "w", "name", "polys", "uid")


def load(board):
    """read every net's copper from a loaded board; returns {net: [Item]}"""
    global b, LID
    b = board
    LID = {n: b.GetLayerID(n) for n in LAYERS}
    items_by_net = collections.defaultdict(list)
    for t in b.GetTracks():
        it = Item()
        it.obj, it.uid = t, t.m_Uuid.AsString()
        if t.GetClass() == "PCB_VIA":
            it.kind, it.layers, it.pts, it.w = "via", set(LAYERS), [P(t.GetPosition())], TO(t.GetWidth(pcbnew.F_Cu))
        else:
            it.kind, it.layers, it.pts, it.w = "track", {b.GetLayerName(t.GetLayer())}, [P(t.GetStart()), P(t.GetEnd())], TO(t.GetWidth())
        it.name = t.GetNetname()
        items_by_net[it.name].append(it)
    for fp in b.GetFootprints():
        for p in fp.Pads():
            n = p.GetNetname()
            if not n:
                continue
            it = Item()
            it.kind, it.obj, it.name, it.uid = "pad", p, n, "%s.%s" % (fp.GetReference(), p.GetNumber())
            it.layers = {b.GetLayerName(l) for l in p.GetLayerSet().CuStack() if b.GetLayerName(l) in LID}
            it.pts, it.w = [P(p.GetPosition())], 0
            it.polys = {}
            for ln in it.layers:
                ps = pcbnew.SHAPE_POLY_SET()
                p.TransformShapeToPolygon(ps, LID[ln], 0, MM(0.005), pcbnew.ERROR_INSIDE)
                it.polys[ln] = ps
            items_by_net[n].append(it)
    for z in b.Zones():
        if z.GetIsRuleArea() or not z.GetNetname():
            continue
        for l in z.GetLayerSet().Seq():
            ln = b.GetLayerName(l)
            if ln not in LID:
                continue
            fp_ = z.GetFilledPolysList(l)
            for k in range(fp_.OutlineCount()):
                one = pcbnew.SHAPE_POLY_SET()
                one.AddOutline(fp_.Outline(k))
                for h in range(fp_.HoleCount(k)):
                    one.AddHole(fp_.Hole(k, h))
                it = Item()
                it.kind, it.obj, it.name, it.uid = "zone", z, z.GetNetname(), "%s/%s/%d" % (z.GetZoneName(), ln, k)
                it.layers, it.pts, it.w, it.polys = {ln}, [], 0, {ln: one}
                items_by_net[it.name].append(it)


    return items_by_net


def seg_of(it):
    return pcbnew.SEG(V(it.pts[0]), V(it.pts[1]))


def bbox_of(its):
    xs, ys = [], []
    for it in its:
        if it.kind in ("track", "via", "pad"):
            for p in it.pts:
                xs.append(p[0]); ys.append(p[1])
    return (min(xs), min(ys), max(xs), max(ys)) if xs else None


# ------------------------------------------------------------------ per-net analysis on a point graph
# Nodes are points on one layer (track ends, the places other copper attaches along a track, via centres, pad
# centres) plus one terminal per pad and per zone island.  Edges are the track pieces between those points, the short
# links from an attaching point to the copper it lands in, and a via's links between layers.  Pieces shorter than
# MERGE are contracted, so clusters of tiny segments act as one point.
MERGE = 0.06


class Graph:
    def __init__(self):
        self.key = {}
        self.info = []          # per node: (layer or None, (x, y), label)
        self.edges = []         # (u, v, w, tag) tag: ("piece", track uid, p, q) | ("link",) | ("via", uid) | ("term",)
        self.term = set()

    def node(self, layer, p, label=None):
        k = (layer, round(p[0], 4), round(p[1], 4)) if label is None else label
        if k not in self.key:
            self.key[k] = len(self.info)
            self.info.append((layer, p, label))
        return self.key[k]

    def edge(self, u, v, w, tag):
        if u != v:
            self.edges.append((u, v, w, tag))


def build(net, its):
    g = Graph()
    tracks = [it for it in its if it.kind == "track"]
    vias = [it for it in its if it.kind == "via"]
    pads = [it for it in its if it.kind == "pad"]
    zones = [it for it in its if it.kind == "zone"]
    anchors = collections.defaultdict(list)   # layer -> [(point, owner uid)]
    for it in tracks:
        ln = next(iter(it.layers))
        for e in it.pts:
            anchors[ln].append((e, it.uid, it.w / 2))
    for it in vias:
        for ln in LAYERS:
            anchors[ln].append((it.pts[0], it.uid, it.w / 2))
    for it in pads:
        for ln in it.layers:
            anchors[ln].append((it.pts[0], it.uid, 0.0))
    # tracks: split at every anchor that lands in their copper
    for it in tracks:
        ln = next(iter(it.layers))
        a, c = it.pts
        hw = it.w / 2
        splits = [(0.0, a), (1.0, c)]
        for q, owner, reach in anchors[ln]:
            if owner == it.uid:
                continue
            t, d, qq = proj(q, a, c)
            if d <= hw + reach + 1e-4:   # generous, like KiCad: the anchor's own copper overlapping this track
                splits.append((t, qq))
                if d > 1e-4:
                    g.edge(g.node(ln, q), g.node(ln, qq), d, ("link",))
        # a pad the track only grazes (KiCad counts the overlap): attach at the point nearest the pad centre
        for pd in pads:
            if ln in pd.layers and pd.polys[ln].Collide(seg_of(it), MM(hw)):
                t, d, qq = proj(pd.pts[0], a, c)
                splits.append((t, qq))
                g.edge(g.node(ln, qq), g.node(ln, pd.pts[0]), d, ("link",))
        splits.sort(key=lambda x: x[0])
        for (t1, p1), (t2, p2) in zip(splits, splits[1:]):
            g.edge(g.node(ln, p1), g.node(ln, p2), dist(p1, p2), ("piece", it.uid, p1, p2))
    # vias: one node per layer, linked layer to layer; track ends inside the via land on it
    for k, it in enumerate(vias):
        for o in vias[k + 1:]:
            if dist(it.pts[0], o.pts[0]) <= (it.w + o.w) / 2:
                for ln in LAYERS:
                    g.edge(g.node(ln, it.pts[0]), g.node(ln, o.pts[0]), dist(it.pts[0], o.pts[0]), ("link",))
    for it in vias:
        c = it.pts[0]
        ns = [g.node(ln, c) for ln in LAYERS]
        for u, v in zip(ns, ns[1:]):
            g.edge(u, v, 0.0, ("via", it.uid))
        for t in tracks:
            ln = next(iter(t.layers))
            for e in t.pts:
                d = dist(e, c)
                if 1e-4 < d <= it.w / 2 + 1e-4:
                    g.edge(g.node(ln, e), g.node(ln, c), d, ("link",))
    # pads: a terminal joined to the pad centre on each of its layers; track ends and vias inside the pad land on it
    for pd in pads:
        T = g.node(None, pd.pts[0], ("pad", pd.uid))
        g.term.add(T)
        for ln in pd.layers:
            g.edge(T, g.node(ln, pd.pts[0]), 0.0, ("term",))
            for t in tracks:
                if ln in t.layers:
                    for e in t.pts:
                        if pd.polys[ln].Collide(V(e)) and dist(e, pd.pts[0]) > 1e-4:
                            g.edge(g.node(ln, e), g.node(ln, pd.pts[0]), dist(e, pd.pts[0]), ("link",))
            for v in vias:
                if pd.polys[ln].Collide(V(v.pts[0]), MM(v.w / 2)) and dist(v.pts[0], pd.pts[0]) > 1e-4:
                    g.edge(g.node(ln, v.pts[0]), g.node(ln, pd.pts[0]), dist(v.pts[0], pd.pts[0]), ("link",))
    # zones: a terminal per island; any anchor inside the fill lands on it
    for z in zones:
        ln = next(iter(z.layers))
        T = g.node(None, None, ("zone", z.uid))
        g.term.add(T)
        for q, owner, reach in anchors[ln]:
            if z.polys[ln].Collide(V(q)):
                g.edge(g.node(ln, q), T, 0.0, ("zterm",))
    return g


def analyse(net, its):
    g = build(net, its)
    n = len(g.info)
    # contract tiny pieces and links
    parent = list(range(n))

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for u, v, w, tag in g.edges:
        if tag[0] in ("piece", "link") and w < MERGE and not (u in g.term or v in g.term):
            ru, rv = root(u), root(v)
            if ru != rv:
                parent[ru] = rv
    E = []
    for k, (u, v, w, tag) in enumerate(g.edges):
        ru, rv = root(u), root(v)
        if ru != rv:
            E.append((ru, rv, w, tag, k))
    terms = {root(t) for t in g.term}
    adj = collections.defaultdict(set)
    for idx, (u, v, w, tag, k) in enumerate(E):
        adj[u].add((v, idx)); adj[v].add((u, idx))
    # peel: non-terminal nodes with one distinct neighbour (or none) go, with their edges
    alive_e = set(range(len(E)))
    nbrs = lambda x: {v for v, idx in adj[x] if idx in alive_e}
    queue = [x for x in list(adj) if x not in terms and len(nbrs(x)) <= 1]
    gone = set()
    while queue:
        x = queue.pop()
        if x in gone or x in terms or len(nbrs(x)) > 1:
            continue
        gone.add(x)
        for v, idx in list(adj[x]):
            if idx in alive_e:
                alive_e.discard(idx)
                if v not in terms and len(nbrs(v)) <= 1:
                    queue.append(v)
    dead_pieces = [E[i][3] for i in range(len(E)) if i not in alive_e and E[i][3][0] == "piece"]
    # pieces contracted into a dead node are dead too
    dead_nodes = gone
    for u, v, w, tag in g.edges:
        if tag[0] == "piece" and w < MERGE and root(u) in dead_nodes:
            dead_pieces.append(tag)
    via_live_layers = collections.defaultdict(set)
    for i in alive_e:
        u, v, w, tag, k = E[i]
        if tag[0] == "via":
            via_live_layers[tag[1]].add(i)
    dead_vias, via1 = [], []
    for it in its:
        if it.kind != "via":
            continue
        ns = {root(g.key[(ln, round(it.pts[0][0], 4), round(it.pts[0][1], 4))]) for ln in LAYERS}
        live = [x for x in ns if x not in gone]
        # layers on which something other than the via itself still attaches
        used = set()
        for x in live:
            for v, idx in adj[x]:
                if idx in alive_e and E[idx][3][0] != "via":
                    used.add(g.info[x][0])
        if not live or not used:
            dead_vias.append([round(c, 3) for c in it.pts[0]])
        elif len(used) == 1:
            via1.append([round(c, 3) for c in it.pts[0]])
    res = {"net": net, "segments": sum(1 for it in its if it.kind == "track"), "vias": sum(1 for it in its if it.kind == "via"),
           "pads": sum(1 for it in its if it.kind == "pad"), "zones": sum(1 for it in its if it.kind == "zone"),
           "length": round(sum(dist(*it.pts) for it in its if it.kind == "track"), 2)}
    dl = collections.defaultdict(float)
    for tag in dead_pieces:
        dl[tag[1]] += dist(tag[2], tag[3])
    res["dead"] = [{"uid": t[1], "p": [round(x, 3) for x in t[2]], "q": [round(x, 3) for x in t[3]], "len": round(dist(t[2], t[3]), 3)}
                   for t in dead_pieces]
    res["dead_len"] = round(sum(dl.values()), 2)
    res["dead_vias"] = dead_vias
    res["via1"] = via1
    # loops: bridges in the live graph without zone links; non-bridge track pieces lie on a loop
    live = [i for i in alive_e if E[i][3][0] != "zterm" and not (E[i][0] in terms and g.info[E[i][0]][2] and g.info[E[i][0]][2][0] == "zone")]
    ladj = collections.defaultdict(list)
    for i in live:
        u, v = E[i][0], E[i][1]
        ladj[u].append((v, i)); ladj[v].append((u, i))
    disc, low, bridges, timer = {}, {}, set(), [0]
    sys.setrecursionlimit(100000)

    def dfs(x, pe):
        disc[x] = low[x] = timer[0]; timer[0] += 1
        for v, i in ladj[x]:
            if i == pe:
                continue
            if v in disc:
                low[x] = min(low[x], disc[v])
            else:
                dfs(v, i)
                low[x] = min(low[x], low[v])
                if low[v] > disc[x]:
                    bridges.add(i)
    for x in list(ladj):
        if x not in disc:
            dfs(x, -1)
    loop_pieces = [E[i][3] for i in live if i not in bridges and E[i][3][0] == "piece"]
    res["loop_len"] = round(sum(dist(t[2], t[3]) for t in loop_pieces), 2)
    res["loops"] = [{"uid": t[1], "p": [round(x, 3) for x in t[2]], "q": [round(x, 3) for x in t[3]]} for t in loop_pieces]
    res["tiny"] = sum(1 for it in its if it.kind == "track" and dist(*it.pts) < MERGE)
    # detours between pads (pairs whose shortest copper path passes no third pad)
    pads = [(t, g.info[t][2][1]) for t in g.term if g.info[t][2][0] == "pad"]
    pad_roots = {root(t): uid for t, uid in pads}
    pos = {root(t): g.info[t][1] for t, uid in pads}
    out = []
    if res["zones"] == 0:
        for src in pad_roots:
            best, through = {src: 0.0}, {src: False}
            pq = [(0.0, src)]
            while pq:
                d, x = heapq.heappop(pq)
                if d > best.get(x, 1e18) + 1e-9:
                    continue
                for v, i in adj[x]:
                    if i not in alive_e:
                        continue
                    nd = d + E[i][2]
                    if nd < best.get(v, 1e18) - 1e-9:
                        best[v] = nd
                        through[v] = through[x] or (x in pad_roots and x != src)
                        heapq.heappush(pq, (nd, v))
            for dst, uid in pad_roots.items():
                if dst <= src or dst not in best or through.get(dst):
                    continue
                straight = dist(pos[src], pos[dst])
                path = best[dst]
                if straight > 0.5 and (path / straight > 1.5 and path - straight > 3.0 or path - straight > 12.0):
                    out.append({"a": pad_roots[src], "b": uid, "path": round(path, 1), "straight": round(straight, 1),
                                "ratio": round(path / straight, 2)})
    res["detours"] = sorted(out, key=lambda d: -(d["path"] - d["straight"]))
    return res


if __name__ == "__main__":
    PCB, OUT = sys.argv[1], sys.argv[2]
    ONLY = set(sys.argv[3:])
    os.makedirs(OUT, exist_ok=True)
    items_by_net = load(pcbnew.LoadBoard(PCB))
    # ------------------------------------------------------------------ rendering (one tile per net)
    COL = {"F.Cu": (215, 35, 35), "In1.Cu": (150, 60, 190), "In2.Cu": (175, 115, 35), "B.Cu": (45, 105, 225)}
    outline = pcbnew.SHAPE_POLY_SET()
    b.GetBoardPolygonOutlines(outline, True)
    edge_pts = [[P(outline.Outline(k).CPoint(i)) for i in range(outline.Outline(k).PointCount())] for k in range(outline.OutlineCount())]
    all_tracks = [(b.GetLayerName(t.GetLayer()), P(t.GetStart()), P(t.GetEnd()), TO(t.GetWidth())) for t in b.GetTracks() if t.GetClass() == "PCB_TRACK"]
    all_pads = []
    for fp in b.GetFootprints():
        for p in fp.Pads():
            bb = p.GetBoundingBox()
            all_pads.append((TO(bb.GetLeft()), TO(bb.GetTop()), TO(bb.GetRight()), TO(bb.GetBottom()), p.GetNetname()))


    def render(net, its, res, size=320):
        bb = bbox_of(its)
        if not bb:
            return None
        x0, y0, x1, y1 = bb
        m = 2.0
        x0, y0, x1, y1 = x0 - m, y0 - m, x1 + m, y1 + m
        span = max(x1 - x0, y1 - y0)
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        x0, y0 = cx - span / 2, cy - span / 2
        s = size / span
        img = np.full((size, size, 3), 255, np.uint8)
        yy, xx = np.mgrid[0:size, 0:size]
        X = x0 + (xx + 0.5) / s
        Y = y0 + (yy + 0.5) / s

        def disc(x, y, r, col):
            i0, i1 = int((x - r - x0) * s) - 1, int((x + r - x0) * s) + 2
            j0, j1 = int((y - r - y0) * s) - 1, int((y + r - y0) * s) + 2
            i0, i1, j0, j1 = max(0, i0), min(size, i1), max(0, j0), min(size, j1)
            if i0 >= i1 or j0 >= j1:
                return
            mm = (X[j0:j1, i0:i1] - x) ** 2 + (Y[j0:j1, i0:i1] - y) ** 2 <= max(r, 0.6 / s) ** 2
            img[j0:j1, i0:i1][mm] = col

        def seg(a, c, w, col):
            L = dist(a, c)
            k = max(1, int(L * s / 1.2))
            for q in range(k + 1):
                disc(a[0] + (c[0] - a[0]) * q / k, a[1] + (c[1] - a[1]) * q / k, w / 2, col)

        def box(l, t, r, btm, col):
            i0, i1 = max(0, int((l - x0) * s)), min(size, int((r - x0) * s) + 1)
            j0, j1 = max(0, int((t - y0) * s)), min(size, int((btm - y0) * s) + 1)
            if i0 < i1 and j0 < j1:
                img[j0:j1, i0:i1] = col
        for l, t, r, btm, pn in all_pads:
            if r >= x0 and l <= x0 + span and btm >= y0 and t <= y0 + span:
                box(l, t, r, btm, (228, 228, 228) if pn != net else (255, 170, 60))
        for ln, a, c, w in all_tracks:
            if max(a[0], c[0]) >= x0 and min(a[0], c[0]) <= x0 + span and max(a[1], c[1]) >= y0 and min(a[1], c[1]) <= y0 + span:
                seg(a, c, max(w, 0.08), (222, 222, 222))
        for pts in edge_pts:
            for a, c in zip(pts, pts[1:] + pts[:1]):
                if max(a[0], c[0]) >= x0 and min(a[0], c[0]) <= x0 + span and max(a[1], c[1]) >= y0 and min(a[1], c[1]) <= y0 + span:
                    seg(a, c, 0.15, (120, 120, 120))
        for order in ("B.Cu", "In2.Cu", "In1.Cu", "F.Cu"):
            for it in its:
                if it.kind == "track" and order in it.layers:
                    seg(it.pts[0], it.pts[1], max(it.w, 1.6 / s), COL[order])
        for it in its:
            if it.kind == "via":
                disc(it.pts[0][0], it.pts[0][1], max(it.w / 2, 2.5 / s), (0, 0, 0))
        for d in res["loops"]:
            seg(d["p"], d["q"], max(0.2, 3.0 / s), (230, 0, 230))
        for d in res["dead"]:
            seg(d["p"], d["q"], max(0.25, 3.5 / s), (0, 200, 0))
        for v in res["dead_vias"] + res["via1"]:
            disc(v[0], v[1], max(0.4, 5.0 / s), (0, 200, 0))
        return img


    def png(path, arr):
        import zlib, struct
        h, w, _ = arr.shape
        raw = b"".join(b"\x00" + arr[y].tobytes() for y in range(h))

        def chunk(t, d):
            return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
        open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                               + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


    # ------------------------------------------------------------------ run
    nets = sorted(n for n, its in items_by_net.items() if n and any(i.kind in ("track", "via") for i in its) and (not ONLY or n in ONLY))
    report = []
    tiles = []
    for k, net in enumerate(nets):
        its = items_by_net[net]
        res = analyse(net, its)
        res["tile"] = k + 1
        report.append(res)
        img = render(net, its, res)
        if img is not None:
            png(os.path.join(OUT, "net_%s.png" % re.sub(r"[^A-Za-z0-9_.+-]", "_", net)), img)
            tiles.append(img)
        print("%3d %-22s seg %3d via %2d pad %2d zone %2d  %6.1f mm  dead %5.2f mm %d vias  via1 %d  loop %5.2f mm  tiny %2d  detours %s" % (
            k + 1, net, res["segments"], res["vias"], res["pads"], res["zones"], res["length"], res["dead_len"], len(res["dead_vias"]),
            len(res["via1"]), res["loop_len"], res["tiny"], ", ".join("%s-%s %.0f/%.0f" % (d["a"], d["b"], d["path"], d["straight"]) for d in res["detours"][:3])),
            flush=True)
    json.dump(report, open(os.path.join(OUT, "report.json"), "w"), indent=1)
    # contact sheets: 4 x 3 tiles of 320 px with 8 px gaps
    per, cols, ts, gap = 12, 4, 320, 8
    for s0 in range(0, len(tiles), per):
        chunk_ = tiles[s0:s0 + per]
        rows_ = (len(chunk_) + cols - 1) // cols
        sheet = np.full((rows_ * (ts + gap), cols * (ts + gap), 3), 255, np.uint8)
        for q, tile in enumerate(chunk_):
            r, c = divmod(q, cols)
            sheet[r * (ts + gap):r * (ts + gap) + ts, c * (ts + gap):c * (ts + gap) + ts] = tile
            sheet[r * (ts + gap):r * (ts + gap) + ts, c * (ts + gap) + ts:c * (ts + gap) + ts + 2] = 90
        png(os.path.join(OUT, "sheet_%02d.png" % (s0 // per + 1)), sheet)
    print("sheets:", (len(tiles) + per - 1) // per)
