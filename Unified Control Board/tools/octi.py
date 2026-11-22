# octi.py <board dir> [--dry] [--nets RE] [--keep NET,NET] [--adaptive] [--corridor MM] [--bend MM] [--step S]
#         [--log FILE]: put track runs back on 45/90 degrees and take out small detours.  Run with KiCad 10's Python.
#
# A run is a stretch of same-layer, same-width track between fixed points: pads, vias, junctions of three or more
# tracks, width changes, track ends that only overlap, pour connections, and T-junctions.  A track end landing on the
# middle of another track splits that track there first, as does a via or same-net pad sitting on its middle (never
# within 0.15 mm or a track width of its ends: KiCad calls such a short piece dangling).
#
# A run is re-routed when one of its segments is off the 45/90 directions (by more than 0.5 degrees) or it is more
# than 0.4 mm longer than the 45/90 distance between its ends.  --keep names hand-routed nets: only their off-angle
# runs are touched.  The new route stays on the run's layer, inside a corridor (--corridor, default 2 mm) round the old
# one.  It comes from an A* search over 45/90 moves on a grid laid through the run's first end, charging --bend mm
# (default 0.6) for every 45 degrees of turn and never turning sharper than 90 degrees.  The last two non-parallel
# segments are then stretched so the route lands exactly on the run's other end.
#
# Clearance is the board's plus 0.05 mm, retried at the board's own when the old run sits right at the limit.  The
# sense nets, U9_SYNC and 3V3A keep 0.25 mm more; with --adaptive, they keep whatever extra spacing their old run
# already has at its tightest.  Routes of other nets keep off the analog nets as tools/reroute.py does.
#
# Kept: an off-angle run's new route if it is at most 12 % + 0.3 mm longer than the old one; a 45/90 run's new route
# only if it is at least 0.2 mm shorter.  Then a full DRC with schematic parity: every re-routed run near anything new
# (error, unconnected item, dangling track or via) is put back, and DRC runs again until nothing new is left or nothing
# can be traced to a run.  A pour that a run of another net cut shows up as an unconnected pad or zone; find the run by
# putting candidates back one at a time.  The board is saved with refilled zones.
import sys, os, re, math, json, heapq, subprocess, tempfile, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import pcbnew
import kirouter as kr

TO, MM = pcbnew.ToMM, pcbnew.FromMM
args = [a for a in sys.argv[1:]]
D = args[0].rstrip("/") + "/"
PCB = D + "Unified Control Board.kicad_pcb"
DRY = "--dry" in args
NETS = re.compile(args[args.index("--nets") + 1]) if "--nets" in args else None
LOG = open(args[args.index("--log") + 1], "a") if "--log" in args else None
STEP = float(args[args.index("--step") + 1]) if "--step" in args else 0.05
KEEPNETS = set(args[args.index("--keep") + 1].split(",")) if "--keep" in args else set()   # hand-routed: only off-angle runs
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
SKIP = re.compile(r"USBD[+-]")                         # a coupled pair: left as routed
SPACED = re.compile(r"IS[ABC][12]|S[ABC][12]|U9_SYNC|3V3A")   # spacing chosen on purpose: keep 0.25 mm more
ANALOG = r"IS[ABC][12]|S[ABC][12]|3V3A|TEMP[12]|VBUS_SENSE|FB|COMP|COMP_Z|RT"
OFF_DEG, MARGIN = 0.5, 0.05
CORRIDOR = float(args[args.index("--corridor") + 1]) if "--corridor" in args else 2.0
ADAPTIVE = "--adaptive" in args   # spaced nets: keep the spacing the old run has at its tightest, not a flat 0.25 mm
BEND = float(args[args.index("--bend") + 1]) if "--bend" in args else 0.6   # mm charged per 45 degrees of turn
b = pcbnew.LoadBoard(PCB)
dru = open(D + "Unified Control Board.kicad_dru").read()
KEEP = []   # every item added to or taken off the board stays referenced here, or SWIG may free it under the board


def P(v):
    return (TO(v.x), TO(v.y))


def V(p):
    return pcbnew.VECTOR2I(MM(p[0]), MM(p[1]))


def off_angle(a, c):
    if math.dist(a, c) < 0.06:
        return False
    ang = math.degrees(math.atan2(c[1] - a[1], c[0] - a[0])) % 45
    return min(ang, 45 - ang) > OFF_DEG


def octile(a, c):
    dx, dy = abs(c[0] - a[0]), abs(c[1] - a[1])
    return max(dx, dy) + (math.sqrt(2) - 1) * min(dx, dy)


def log(msg):
    print(msg, flush=True)
    if LOG:
        LOG.write(msg + "\n"); LOG.flush()


def drc_bad():
    out = tempfile.mktemp(suffix=".json")
    subprocess.run([KICAD_CLI, "pcb", "drc", "--schematic-parity", "--severity-all", "--format", "json", "-o", out, PCB],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    d = json.load(open(out))
    os.unlink(out)
    bad = {}
    for sec in ("violations", "unconnected_items"):
        for v in d.get(sec, []):
            if v["severity"] == "error" or v["type"] in ("track_dangling", "via_dangling"):
                key = (v["type"], tuple(sorted(re.sub(r"\s+", " ", i["description"]) for i in v["items"])))
                bad[key] = [(i["pos"]["x"], i["pos"]["y"], i["description"]) for i in v["items"]]
    return bad


# ------------------------------------------------------------------ 1. split tracks at T-junctions, vias and pads
def split_points():
    """{track uid: [points on its middle where something of its net attaches]}"""
    tracks = [t for t in b.GetTracks() if t.GetClass() == "PCB_TRACK"]
    by_nl = collections.defaultdict(list)
    for t in tracks:
        by_nl[(t.GetNetname(), t.GetLayer())].append(t)
    vias = collections.defaultdict(list)
    for t in b.GetTracks():
        if t.GetClass() == "PCB_VIA":
            vias[t.GetNetname()].append(P(t.GetPosition()))
    pads = collections.defaultdict(list)
    for fp in b.GetFootprints():
        for p in fp.Pads():
            if p.GetNetname():
                pads[p.GetNetname()].append(p)
    out = collections.defaultdict(list)
    for (net, L), ts in by_nl.items():
        if not net or SKIP.fullmatch(net) or (NETS and not NETS.fullmatch(net)):
            continue
        for t in ts:
            a, c = P(t.GetStart()), P(t.GetEnd())
            Lt = math.dist(a, c)
            if Lt < 0.1:
                continue
            ux, uy = (c[0] - a[0]) / Lt, (c[1] - a[1]) / Lt
            cand = [e for o in ts if o is not t for e in (P(o.GetStart()), P(o.GetEnd()))] + vias[net]
            for p in pads[net]:
                if p.IsOnLayer(L):
                    cand.append(P(p.GetPosition()))
            # never within 0.15 mm (or a track width) of an end: KiCad counts an item that touches both ends of a short
            # piece for the nearer end only, so such a piece would show as dangling
            edge = max(0.15, TO(t.GetWidth()))
            for q in cand:
                s = (q[0] - a[0]) * ux + (q[1] - a[1]) * uy
                if not (edge < s < Lt - edge):
                    continue
                dperp = abs((q[0] - a[0]) * uy - (q[1] - a[1]) * ux)
                if dperp <= TO(t.GetWidth()) / 2 + 0.001:
                    out[t.m_Uuid.AsString()].append((a[0] + ux * s, a[1] + uy * s))
    return out


def apply_splits(sp):
    n = 0
    for t in list(b.GetTracks()):
        pts = sp.get(t.m_Uuid.AsString()) if t.GetClass() == "PCB_TRACK" else None
        if not pts:
            continue
        a, c = P(t.GetStart()), P(t.GetEnd())
        pts = sorted(set((round(x, 5), round(y, 5)) for x, y in pts), key=lambda q: math.dist(a, q))
        chain = [a] + pts + [c]
        for p, q in zip(chain, chain[1:]):
            if math.dist(p, q) < 1e-4:
                continue
            nt = pcbnew.PCB_TRACK(b)
            nt.SetStart(V(p)); nt.SetEnd(V(q)); nt.SetWidth(t.GetWidth()); nt.SetLayer(t.GetLayer()); nt.SetNet(t.GetNet())
            b.Add(nt); KEEP.append(nt)
        b.Remove(t); KEEP.append(t)
        n += 1
    return n


# ------------------------------------------------------------------ 2. runs
def runs_of(net, L):
    ts = [t for t in b.GetTracks() if t.GetClass() == "PCB_TRACK" and t.GetNetname() == net and t.GetLayer() == L]
    if not ts:
        return []
    key = lambda p: (round(p[0], 2), round(p[1], 2))
    ends = collections.defaultdict(list)
    for t in ts:
        ends[key(P(t.GetStart()))].append(t)
        ends[key(P(t.GetEnd()))].append(t)
    fixed = set()
    vias = [P(v.GetPosition()) + (TO(v.GetWidth(pcbnew.F_Cu)) / 2,) for v in b.GetTracks() if v.GetClass() == "PCB_VIA" and v.GetNetname() == net]
    pads = [p for fp in b.GetFootprints() for p in fp.Pads() if p.GetNetname() == net and p.IsOnLayer(L)]
    zones = [z for z in b.Zones() if not z.GetIsRuleArea() and z.GetNetname() == net and z.IsOnLayer(L)]
    for k, lst in ends.items():
        pt = (k[0], k[1])
        if len(lst) != 2 or abs(lst[0].GetWidth() - lst[1].GetWidth()) > MM(0.001):
            fixed.add(k); continue
        if any(math.dist(pt, (vx, vy)) <= vr + 0.01 for vx, vy, vr in vias):
            fixed.add(k); continue
        if any(p.HitTest(V(pt)) for p in pads):
            fixed.add(k); continue
        if any(z.GetFilledPolysList(L).Contains(V(pt)) for z in zones):
            fixed.add(k)
    seen, runs = set(), []
    for t0 in ts:
        u0 = t0.m_Uuid.AsString()
        if u0 in seen:
            continue
        # walk both ways from t0 to fixed points
        seq = [t0]; seen.add(u0)
        for side in (0, 1):
            cur = t0
            pt = key(P(cur.GetEnd())) if side == 1 else key(P(cur.GetStart()))
            while pt not in fixed:
                nxt = [o for o in ends[pt] if o.m_Uuid.AsString() not in seen]
                if not nxt:
                    break
                o = nxt[0]; seen.add(o.m_Uuid.AsString())
                if side == 1:
                    seq.append(o)
                else:
                    seq.insert(0, o)
                pt = key(P(o.GetEnd())) if key(P(o.GetStart())) == pt else key(P(o.GetStart()))
        # ordered points
        if len(seq) == 1:
            pts = [P(seq[0].GetStart()), P(seq[0].GetEnd())]
        else:
            s0, s1 = seq[0], seq[1]
            a0, c0 = P(s0.GetStart()), P(s0.GetEnd())
            shared = {key(P(s1.GetStart())), key(P(s1.GetEnd()))}
            pts = [c0, a0] if key(a0) in shared else [a0, c0]
            for o in seq[1:]:
                a, c = P(o.GetStart()), P(o.GetEnd())
                pts.append(c if key(a) == key(pts[-1]) else a)
        runs.append({"net": net, "layer": L, "w": TO(seq[0].GetWidth()), "tracks": seq, "pts": pts})
    return runs


# ------------------------------------------------------------------ 3. the 45/90 router
DIRS = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)]


def octi_route(run, margin=MARGIN):
    net, L, w = run["net"], run["layer"], run["w"]
    lname = b.GetLayerName(L)
    pts = run["pts"]
    A, Z = pts[0], pts[-1]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    pad_ = CORRIDOR + 0.5
    x0 = A[0] - math.ceil((A[0] - (min(xs) - pad_)) / STEP) * STEP
    y0 = A[1] - math.ceil((A[1] - (min(ys) - pad_)) / STEP) * STEP
    R = kr.Router(b, dru, (x0, y0, max(xs) + pad_, max(ys) + pad_), step=STEP)
    g = R.grid
    base = (0.2 if kr.POWER_RE.fullmatch(net) else 0.15) + margin
    extra = 0.25 if SPACED.fullmatch(net) else 0.0
    if SPACED.fullmatch(net) and ADAPTIVE:
        # the largest extra spacing the old run already keeps everywhere (its ends, inside pads, aside)
        samples = []
        for p, q in zip(pts, pts[1:]):
            n_ = max(1, int(math.dist(p, q) / 0.05))
            samples += [(p[0] + (q[0] - p[0]) * k / n_, p[1] + (q[1] - p[1]) * k / n_) for k in range(n_ + 1)]
        samples = [c for c in samples if min(math.dist(c, A), math.dist(c, Z)) > 0.6]
        extra = 0.0
        for e in (0.25, 0.2, 0.15, 0.1, 0.05):
            mm_ = R.blocked(lname, net, base + e + w / 2.0)
            if not any(mm_[min(g.ny - 1, max(0, g.ij(*c)[1])), min(g.nx - 1, max(0, g.ij(*c)[0]))] for c in samples):
                extra = e
                break
    clr = base + extra
    m = R.blocked(lname, net, clr + w / 2.0)
    # corridor: within CORRIDOR mm of the old run
    X = g.x0 + np.arange(g.nx)[None, :] * g.step
    Y = g.y0 + np.arange(g.ny)[:, None] * g.step
    dmin = np.full((g.ny, g.nx), 1e9)
    for p, q in zip(pts, pts[1:]):
        vx, vy = q[0] - p[0], q[1] - p[1]
        L2 = vx * vx + vy * vy
        t = np.zeros_like(dmin) if L2 == 0 else np.clip(((X - p[0]) * vx + (Y - p[1]) * vy) / L2, 0, 1)
        dmin = np.minimum(dmin, np.hypot(X - (p[0] + t * vx), Y - (p[1] + t * vy)))
    m = m | (dmin > CORRIDOR)
    noise = None
    if not re.fullmatch(ANALOG, net) and lname in ("F.Cu", "B.Cu"):
        noise = R.noise_field(ANALOG, 0.8).get(lname)
    si, sj = g.ij(*A)
    gi, gj = g.ij(*Z)
    for (ci, cj) in ((si, sj), (gi, gj)):
        r = 2
        m[max(0, cj - r):cj + r + 1, max(0, ci - r):ci + r + 1] = False
    st = g.step

    def h(i, j):
        dx, dy = abs(i - gi), abs(j - gj)
        return (max(dx, dy) + 0.41421356 * min(dx, dy)) * st
    best, prev = {}, {}
    pq = [(h(si, sj), 0.0, si, sj, -1)]
    best[(si, sj, -1)] = 0.0
    found = None
    while pq:
        f, c, i, j, d = heapq.heappop(pq)
        if c > best.get((i, j, d), 1e18) + 1e-9:
            continue
        if i == gi and j == gj:
            found = (i, j, d)
            break
        for nd in range(8):
            if d >= 0:
                turn = min((nd - d) % 8, (d - nd) % 8)
                if turn > 2:
                    continue
                pen = BEND * turn
            else:
                pen = 0.0
            di, dj = DIRS[nd]
            ni, nj = i + di, j + dj
            if not (0 <= ni < g.nx and 0 <= nj < g.ny) or m[nj, ni]:
                continue
            if di and dj and (m[j, ni] or m[nj, i]):
                continue
            sl = st * (1.41421356 if di and dj else 1.0)
            if noise is not None and noise[nj, ni]:
                sl *= 1.5
            nc = c + sl + pen
            k = (ni, nj, nd)
            if nc < best.get(k, 1e18) - 1e-12:
                best[k] = nc; prev[k] = (i, j, d)
                heapq.heappush(pq, (nc + h(ni, nj), nc, ni, nj, nd))
        if len(best) > 3_000_000:
            return None
    if not found:
        return None
    cells = [found]
    while cells[-1] in prev:
        cells.append(prev[cells[-1]])
    cells.reverse()
    corners = [cells[0]]
    for k in range(1, len(cells) - 1):
        if cells[k + 1][2] != cells[k][2]:
            corners.append(cells[k])
    corners.append(cells[-1])
    out = [list(g.xy(i, j)) for i, j, _ in corners]
    out[0] = [A[0], A[1]]
    # land exactly on Z: stretch the last two non-parallel segments
    dx, dy = Z[0] - out[-1][0], Z[1] - out[-1][1]
    if abs(dx) > 1e-7 or abs(dy) > 1e-7:
        segs = []
        for k in range(len(out) - 1):
            vx, vy = out[k + 1][0] - out[k][0], out[k + 1][1] - out[k][1]
            l = math.hypot(vx, vy)
            segs.append((k, vx / l, vy / l, l) if l > 0 else (k, 0, 0, 0))
        done = False
        for k2 in range(len(segs) - 1, -1, -1):
            for k1 in range(k2 - 1, -1, -1):
                _, u1x, u1y, l1 = segs[k1]
                _, u2x, u2y, l2 = segs[k2]
                det = u1x * u2y - u1y * u2x
                if abs(det) < 0.5:
                    continue
                a = (dx * u2y - dy * u2x) / det
                bb = (u1x * dy - u1y * dx) / det
                if l1 + a < 0.03 or l2 + bb < 0.03:
                    continue
                for q in range(k1 + 1, len(out)):
                    out[q][0] += a * u1x; out[q][1] += a * u1y
                for q in range(k2 + 1, len(out)):
                    out[q][0] += bb * u2x; out[q][1] += bb * u2y
                done = True
                break
            if done:
                break
        if not done:
            out[-1] = [Z[0], Z[1]]
    out[-1] = [Z[0], Z[1]]
    return [tuple(p) for p in out]


def plen(pts):
    return sum(math.dist(p, q) for p, q in zip(pts, pts[1:]))


def kr_seg_dist(p, a, c):
    """distance from point p to segment a-c"""
    vx, vy = c[0] - a[0], c[1] - a[1]
    L2 = vx * vx + vy * vy
    t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((p[0] - a[0]) * vx + (p[1] - a[1]) * vy) / L2))
    return math.hypot(p[0] - (a[0] + t * vx), p[1] - (a[1] + t * vy))


# ------------------------------------------------------------------ 4. run it
base = drc_bad()
log("baseline: %d blocking DRC items" % len(base))
n_split = apply_splits(split_points())
log("tracks split at T-junctions, vias and pads: %d" % n_split)
nets = sorted({t.GetNetname() for t in b.GetTracks() if t.GetClass() == "PCB_TRACK" and t.GetNetname()})
done = []          # (run, old track objects, new track objects)
stats = collections.Counter()
for net in nets:
    if SKIP.fullmatch(net) or (NETS and not NETS.fullmatch(net)):
        continue
    for L in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu):
        for run in runs_of(net, L):
            pts = run["pts"]
            if len(pts) < 2 or math.dist(pts[0], pts[-1]) < 1e-3:
                continue
            old = plen(pts)
            off = any(off_angle(p, q) for p, q in zip(pts, pts[1:]))
            bumpy = old > octile(pts[0], pts[-1]) + 0.4
            if (not off and not bumpy) or (net in KEEPNETS and not off):
                continue
            stats["tried"] += 1
            new = octi_route(run)
            if new is None:
                new = octi_route(run, 0.0)   # the old run sits right at the clearance limit: let DRC judge
            if new is None:
                stats["no route"] += 1
                log("  %-16s %s %.2f mm%s: no route" % (net, b.GetLayerName(L), old, " off-angle" if off else ""))
                continue
            nl = plen(new)
            ok = (nl <= old * 1.12 + 0.3) if off else (nl <= old - 0.2)
            log("  %-16s %s %6.2f -> %6.2f mm, %d -> %d segments%s: %s" % (
                net, b.GetLayerName(L), old, nl, len(pts) - 1, len(new) - 1, " off-angle" if off else " bump", "kept" if ok else "longer, left"))
            if not ok:
                stats["longer"] += 1
                continue
            stats["re-routed"] += 1
            if DRY:
                continue
            olds = list(run["tracks"])
            for t in olds:
                b.Remove(t)
            KEEP.extend(olds)
            news = []
            for p, q in zip(new, new[1:]):
                if math.dist(p, q) < 1e-4:
                    continue
                nt = pcbnew.PCB_TRACK(b)
                nt.SetStart(V(p)); nt.SetEnd(V(q)); nt.SetWidth(MM(run["w"])); nt.SetLayer(L); nt.SetNet(b.FindNet(net))
                b.Add(nt); news.append(nt); KEEP.append(nt)
            done.append((run, olds, news))
log("runs: %s" % dict(stats))
if DRY:
    sys.exit(0)


def put_back(item):
    run, olds, news = item
    for t in news:
        b.Remove(t)
    for t in olds:
        b.Add(t)


filler = pcbnew.ZONE_FILLER(b)
for it in range(6):
    filler.Fill(b.Zones())
    pcbnew.SaveBoard(PCB, b)
    now = drc_bad()
    new = {k: v for k, v in now.items() if k not in base}
    log("DRC pass %d: %d new blocking items" % (it + 1, len(new)))
    if not new:
        break
    back = set()
    for k, items in new.items():
        nets_ = set(re.findall(r"\[([^\]]+)\]", " ".join(d for _, _, d in items)))
        for x, y, desc in items:
            for idx, (run, olds, news) in enumerate(done):
                if idx in back or run["net"] not in nets_:
                    continue
                if any(math.dist((x, y), P(t.GetStart())) < 1.5 or math.dist((x, y), P(t.GetEnd())) < 1.5 or
                       kr_seg_dist((x, y), P(t.GetStart()), P(t.GetEnd())) < 1.0 for t in news):
                    back.add(idx)
        log("   %s %s -> putting back %d runs" % (k[0], "; ".join(k[1])[:150], len(back)))
    if not back:
        # nothing matched by position: put back every run of the nets involved
        for k, items in new.items():
            nets_ = set(re.findall(r"\[([^\]]+)\]", " ".join(d for _, _, d in items)))
            back |= {idx for idx, (run, _, _) in enumerate(done) if run["net"] in nets_}
    if not back:
        log("   nothing new can be traced to a re-routed run; stopping")
        break
    for idx in sorted(back, reverse=True):
        put_back(done[idx])
        del done[idx]
log("kept %d re-routed runs" % len(done))
