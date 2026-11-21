# reroute.py <board dir> <net> <end A> <end B> [--inner] [--force] [--dry] [--accept R] [--chain K] [--step S]
#            [--vias N] [--save MM] [--viacost C] [--fewer K] [--slack MM]
# Re-route the worst stretch of the copper path between two pads of a net.  An end can also be a point of the net's
# copper on a layer, "In2.Cu@-19.8,-12.6" (tools/netreview.py local_detours() reports them that way).
#
# On tools/netreview.py's point graph (dead copper peeled off first), the path between the pads is split at real
# junctions (a point where a third piece of copper joins, a pad or a zone).  The longest of those chains (or chain K,
# counted from pad A) is taken out, which leaves the net in two parts, and the two parts are joined again by the
# shortest route kirouter finds from any point of one to any point of the other: clearance plus 0.03 mm, top and
# bottom layers (--inner also allows In1/In2 at 1.5x cost), kept off the analog nets.  The new route is kept only if
# it is at least 2 mm shorter and no more than R (default 0.85) of the old chain; otherwise the original goes back
# exactly.  --vias N limits the extra vias the new route may add, --save MM sets the minimum saving (default 2 mm) and
# --viacost C the router's cost of a via in mm (default 2).  --fewer K accepts instead a route with at least K fewer
# vias that is no more than --slack MM (default 0.5) longer.  Run with KiCad 10's Python.
import sys, os, math, re, heapq, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import pcbnew
import kirouter as kr
import netreview as nr

TO, MM = pcbnew.ToMM, pcbnew.FromMM
args = [a for a in sys.argv[1:] if not a.startswith("--")]
D = args[0].rstrip("/") + "/"
NET, PA, PB = args[1], args[2], args[3]
flags = sys.argv
INNER, IN1, FORCE, DRY = "--inner" in flags, "--in1" in flags, "--force" in flags, "--dry" in flags
ACCEPT = float(flags[flags.index("--accept") + 1]) if "--accept" in flags else 0.85
CHAIN = int(flags[flags.index("--chain") + 1]) if "--chain" in flags else None
STEP = float(flags[flags.index("--step") + 1]) if "--step" in flags else 0.1
MAXVIAS = int(flags[flags.index("--vias") + 1]) if "--vias" in flags else 99   # extra vias allowed
MINSAVE = float(flags[flags.index("--save") + 1]) if "--save" in flags else 2.0
VIACOST = float(flags[flags.index("--viacost") + 1]) if "--viacost" in flags else 2.0
FEWER = int(flags[flags.index("--fewer") + 1]) if "--fewer" in flags else None
SLACK = float(flags[flags.index("--slack") + 1]) if "--slack" in flags else 0.5
PCB = D + "Unified Control Board.kicad_pcb"
b = pcbnew.LoadBoard(PCB)
dru = open(D + "Unified Control Board.kicad_dru").read()
keep = []


def P(v):
    return (TO(v.x), TO(v.y))


def V(p):
    return pcbnew.VECTOR2I(MM(p[0]), MM(p[1]))


its = nr.load(b)[NET]
by_uid = {it.uid: it for it in its}
g = nr.build(NET, its)
n = len(g.info)
parent = list(range(n))


def root(i):
    while parent[i] != i:
        parent[i] = parent[parent[i]]
        i = parent[i]
    return i


for u, v, w, tag in g.edges:
    if tag[0] in ("piece", "link") and w < nr.MERGE and not (u in g.term or v in g.term):
        ru, rv = root(u), root(v)
        if ru != rv:
            parent[ru] = rv
E = [(root(u), root(v), w, tag) for u, v, w, tag in g.edges if root(u) != root(v)]
terms = {root(t) for t in g.term}
adj = collections.defaultdict(set)
for k, (u, v, w, tag) in enumerate(E):
    adj[u].add((v, k)); adj[v].add((u, k))
alive = set(range(len(E)))
nbrs = lambda x: {v for v, k in adj[x] if k in alive}
queue = [x for x in list(adj) if x not in terms and len(nbrs(x)) <= 1]
while queue:
    x = queue.pop()
    if x in terms or len(nbrs(x)) > 1:
        continue
    for v, k in list(adj[x]):
        if k in alive:
            alive.discard(k)
            if v not in terms and len(nbrs(v)) <= 1:
                queue.append(v)


def end_node(spec):
    """a pad ("U2.1") or a point of the net's copper on a layer ("In2.Cu@-19.8,-12.6", as netreview.local_detours names it)"""
    if "@" in spec:
        ln, xy = spec.split("@")
        x, y = (float(c) for c in xy.split(","))
        return root(g.key[(ln, round(x, 4), round(y, 4))])
    return root(g.key[("pad", spec)])


TA, TB = end_node(PA), end_node(PB)
# shortest copper path A -> B
dist_, prev = {TA: 0.0}, {}
pq = [(0.0, TA)]
while pq:
    d, x = heapq.heappop(pq)
    if x == TB:
        break
    if d > dist_[x] + 1e-9:
        continue
    for v, k in adj[x]:
        if k in alive and d + E[k][2] < dist_.get(v, 1e18) - 1e-9:
            dist_[v] = d + E[k][2]; prev[v] = (x, k)
            heapq.heappush(pq, (dist_[v], v))
if TB not in dist_:
    sys.exit("%s: %s and %s are not connected" % (NET, PA, PB))
path, x = [], TB
while x != TA:
    x0, k = prev[x]
    path.append((x0, x, k)); x = x0
path.reverse()
# chains: split where the path passes a junction
chains, cur = [], []
for x0, x1, k in path:
    cur.append(k)
    if x1 in terms or len(nbrs(x1)) >= 3:
        chains.append(cur); cur = []
if cur:
    chains.append(cur)
clen = lambda ch: sum(E[k][2] for k in ch if E[k][3][0] == "piece")
pick = CHAIN if CHAIN is not None else max(range(len(chains)), key=lambda i: clen(chains[i]))
chain = set(chains[pick])
print("%s %s-%s: path %.1f mm in %d chains %s; re-routing chain %d (%.1f mm)" % (
    NET, PA, PB, sum(E[k][2] for _, _, k in path), len(chains), ["%.1f" % clen(c) for c in chains], pick, clen(chains[pick])))
# ------------------------------------------------------------------ take the chain out
# The chain's own pieces, plus everything sitting on its interior points: router fragments shorter than the merge
# distance were contracted into those points and would otherwise be left behind, disconnected.
seq = [x0 for x0, x1, k in path if k in chain] + [x1 for x0, x1, k in path if k in chain]
cstart = next(x0 for x0, x1, k in path if k in chain)
cend = [x1 for x0, x1, k in path if k in chain][-1]
interior = set(seq) - {cstart, cend}
pieces = collections.defaultdict(list)
via_out = set()
for k in chain:
    tag = E[k][3]
    if tag[0] == "piece":
        pieces[tag[1]].append((tag[2], tag[3]))
    elif tag[0] == "via":
        via_out.add(tag[1])
for u, v, w, tag in g.edges:
    if root(u) in interior or root(v) in interior:
        if tag[0] == "piece" and (tag[2], tag[3]) not in pieces[tag[1]]:
            pieces[tag[1]].append((tag[2], tag[3]))
        elif tag[0] == "via":
            via_out.add(tag[1])
pieces = {k: v for k, v in pieces.items() if v}
# a via whose layers the chain only touches on one side stays (it is the junction); a via the chain passes through goes
via_edges = collections.Counter(E[k][3][1] for k in chain if E[k][3][0] == "via")
old_len = clen(chains[pick])
old_vias = len(via_out)
removed, added = [], []
widths = []
for uid, lst in pieces.items():
    t = by_uid[uid].obj
    widths.append(TO(t.GetWidth()))
    a, c = P(t.GetStart()), P(t.GetEnd())
    vx, vy = c[0] - a[0], c[1] - a[1]
    L2 = vx * vx + vy * vy
    tt = lambda p: ((p[0] - a[0]) * vx + (p[1] - a[1]) * vy) / L2 if L2 else 0.0
    cut = sorted((min(tt(p), tt(q)), max(tt(p), tt(q))) for p, q in lst)
    keep_iv, pos = [], 0.0
    for lo, hi in cut:
        if lo > pos + 1e-9:
            keep_iv.append((pos, lo))
        pos = max(pos, hi)
    if pos < 1 - 1e-9:
        keep_iv.append((pos, 1.0))
    b.Remove(t); keep.append(t); removed.append(t)
    for lo, hi in keep_iv:
        if (hi - lo) * math.sqrt(L2) < 0.06:
            continue
        nt = pcbnew.PCB_TRACK(b)
        nt.SetStart(V((a[0] + vx * lo, a[1] + vy * lo))); nt.SetEnd(V((a[0] + vx * hi, a[1] + vy * hi)))
        nt.SetWidth(t.GetWidth()); nt.SetLayer(t.GetLayer()); nt.SetNet(t.GetNet())
        b.Add(nt); keep.append(nt); added.append(nt)
for it in its:
    if it.kind == "via" and it.uid in via_out:
        b.Remove(it.obj); keep.append(it.obj); removed.append(it.obj)
W = max(set(widths), key=widths.count) if widths else 0.15
# ------------------------------------------------------------------ the two parts left, from the graph minus the chain
live = alive - chain
cadj = collections.defaultdict(set)
for k in live:
    u, v = E[k][0], E[k][1]
    cadj[u].add(v); cadj[v].add(u)


def comp(s):
    seen, st = {s}, [s]
    while st:
        x = st.pop()
        for v in cadj[x]:
            if v not in seen:
                seen.add(v); st.append(v)
    return seen


CA, CB = comp(TA), comp(TB)
if CA & CB:
    sys.exit("%s: still joined after taking the chain out (a loop); not re-routing" % NET)
# In1 is the VCC plane that feeds the motor stages: new signal tracks stay off it unless --in1
layers = ("F.Cu",) + (("In1.Cu",) if IN1 else ()) + (("In2.Cu",) if INNER else ()) + ("B.Cu",)


def points(C):
    out = []
    for k in live:
        u, v, w, tag = E[k]
        if u in C and v in C and tag[0] == "piece":
            p, q = tag[2], tag[3]
            ln = next(iter(by_uid[tag[1]].layers))
            if ln in layers:
                m = max(1, int(math.dist(p, q) / 0.5))
                out += [(p[0] + (q[0] - p[0]) * s / m, p[1] + (q[1] - p[1]) * s / m, ln) for s in range(m + 1)]
    for x in C:
        lab = g.info[x][2] if x < len(g.info) else None
    for it in its:
        if it.kind == "via" and it.uid not in via_out:
            if any(root(g.key[(ln, round(it.pts[0][0], 4), round(it.pts[0][1], 4))]) in C for ln in nr.LAYERS):
                out += [(it.pts[0][0], it.pts[0][1], ln) for ln in layers]
        elif it.kind == "pad" and root(g.key[("pad", it.uid)]) in C:
            out += [(it.pts[0][0], it.pts[0][1], ln) for ln in it.layers if ln in layers]
    return out


starts, goals = points(CA), points(CB)
if not starts or not goals:
    sys.exit("%s: nothing to join on %s" % (NET, layers))
pts = [(x, y) for x, y, _ in starts + goals] + [p for lst in pieces.values() for pq in lst for p in pq]
xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
region = (max(min(xs) - 6, -52), max(min(ys) - 6, -45), min(max(xs) + 6, 52), min(max(ys) + 6, 78))
clr = (0.2 if kr.POWER_RE.fullmatch(NET) else 0.15) + 0.03
R = kr.Router(b, dru, region, step=STEP)
SENS = r"IS[ABC][12]|S[ABC][12]|3V3A|TEMP[12]|VBUS_SENSE|FB|COMP|COMP_Z|RT"
noise = R.noise_field(SENS, 0.8) if not re.fullmatch(SENS, NET) else {}
for ln in layers:
    if ln.startswith("In"):
        noise[ln] = np.ones((R.grid.ny, R.grid.nx), dtype=bool)
# fast heuristic: distance to the nearest goal on a 0.5 mm coarse grid, minus that grid's half diagonal
cg = 0.5
gxs = np.array([x for x, y, _ in goals]); gys = np.array([y for x, y, _ in goals])
nx_, ny_ = int((region[2] - region[0]) / cg) + 2, int((region[3] - region[1]) / cg) + 2
cx = region[0] + (np.arange(nx_) + 0.5) * cg; cy = region[1] + (np.arange(ny_) + 0.5) * cg
H = np.full((ny_, nx_), 1e9)
for k0 in range(0, len(gxs), 200):
    dx = cx[None, :, None] - gxs[None, None, k0:k0 + 200]; dy = cy[:, None, None] - gys[None, None, k0:k0 + 200]
    H = np.minimum(H, np.sqrt(dx * dx + dy * dy).min(axis=2))
H = np.maximum(H - cg * 0.71, 0.0)
x0g, y0g, st = R.grid.x0, R.grid.y0, R.grid.step


def heur(i, j):
    x = x0g + i * st; y = y0g + j * st
    return H[min(ny_ - 1, max(0, int((y - region[1]) / cg))), min(nx_ - 1, max(0, int((x - region[0]) / cg)))]


runs = R.route(NET, starts, goals, W, clr, layers=layers, via_cost=VIACOST, noise=noise, noise_cost=1.5, clear_radius=0.3,
               heuristic=heur)
ok = False
if runs:
    new_len = sum(math.dist(p, q) for _, ps in runs for p, q in zip(kr.simplify(ps), kr.simplify(ps)[1:]))
    new_vias = len(runs) - 1
    s0, e0 = runs[0][1][0], runs[-1][1][-1]
    sa = min(starts, key=lambda q: (q[0] - s0[0]) ** 2 + (q[1] - s0[1]) ** 2)
    gb = min(goals, key=lambda q: (q[0] - e0[0]) ** 2 + (q[1] - e0[1]) ** 2)
    print("  chain %.1f mm, %d vias -> new %.1f mm, %d vias (%s), from (%.2f, %.2f) %s to (%.2f, %.2f) %s" % (
        old_len, old_vias, new_len, new_vias, "/".join(r[0][:-3] for r in runs), sa[0], sa[1], sa[2], gb[0], gb[1], gb[2]))
    if FEWER is not None:
        ok = FORCE or (new_vias <= old_vias - FEWER and new_len <= old_len + SLACK)
    else:
        ok = FORCE or (new_len <= ACCEPT * old_len and old_len - new_len >= MINSAVE and new_vias <= old_vias + MAXVIAS)
    if ok and not DRY:
        kr.add_runs(b, NET, runs, W, snap_start=(sa[0], sa[1]), snap_end=(gb[0], gb[1]), keep=keep)
else:
    print("  chain %.1f mm, %d vias -> no route found" % (old_len, old_vias))
if not ok or DRY:
    for o in added:
        b.Remove(o)
    for o in removed:
        b.Add(o)
    print("  kept the original")
else:
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(PCB, b)
    print("  re-routed")
