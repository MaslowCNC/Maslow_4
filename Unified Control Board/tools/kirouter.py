# kirouter.py - a small grid A* router and placement helper for one-off edits to a KiCad 10 board
# through pcbnew's Python API (Unified Control Board v1.1).  Obstacles are rasterised from every other
# net's copper (pads, tracks, vias, non-background zones) grown by clearance + half the track width,
# keep-out rule areas, the ISL_nn island areas of the board's custom rules, NPTH holes and the board edge.
# The background pours (GND on the outer layers, the inner planes) are left out: zone refill flows around
# whatever is routed.  DRC is the judge afterwards.
import math, heapq, re
import numpy as np
import pcbnew

MM, TO = pcbnew.FromMM, pcbnew.ToMM
ERR = MM(0.005)


def poly_outlines(ps):
    out = []
    for k in range(ps.OutlineCount()):
        ol = ps.Outline(k)
        out.append([(TO(ol.CPoint(i).x), TO(ol.CPoint(i).y)) for i in range(ol.PointCount())])
    return out


class Grid:
    def __init__(self, x0, y0, x1, y1, step):
        self.x0, self.y0, self.step = x0, y0, step
        self.nx = int(round((x1 - x0) / step)) + 1
        self.ny = int(round((y1 - y0) / step)) + 1

    def ij(self, x, y):
        return int(round((x - self.x0) / self.step)), int(round((y - self.y0) / self.step))

    def xy(self, i, j):
        return self.x0 + i * self.step, self.y0 + j * self.step

    def fill(self, mask, pts, value=True):
        """Scanline-fill a closed polygon (list of (x, y) mm) into mask[j, i]."""
        if len(pts) < 3:
            return
        ys = [p[1] for p in pts]
        j0 = max(0, int(math.ceil((min(ys) - self.y0) / self.step)))
        j1 = min(self.ny - 1, int(math.floor((max(ys) - self.y0) / self.step)))
        if j0 > j1:
            return
        n = len(pts)
        for j in range(j0, j1 + 1):
            y = self.y0 + j * self.step
            xs = []
            for k in range(n):
                xa, ya = pts[k]
                xb, yb = pts[(k + 1) % n]
                if (ya <= y < yb) or (yb <= y < ya):
                    xs.append(xa + (y - ya) * (xb - xa) / (yb - ya))
            xs.sort()
            for a, b in zip(xs[0::2], xs[1::2]):
                i0 = max(0, int(math.ceil((a - self.x0) / self.step)))
                i1 = min(self.nx - 1, int(math.floor((b - self.x0) / self.step)))
                if i0 <= i1:
                    mask[j, i0:i1 + 1] = value


def isl_rules(dru_text):
    """ISL_nn island rules: area name -> (layer, allowed nets)."""
    rules = {}
    for m in re.finditer(r'\(rule "(ISL_\d+)[^"]*"\s*\(layer ([^)]+)\)\s*\(constraint disallow track\)\s*\(condition "([^"]+)"\)', dru_text):
        rules[m.group(1)] = (m.group(2).strip(), re.findall(r"A\.NetName != '([^']+)'", m.group(3)))
    return rules


POWER_RE = re.compile(r"PH[ABC][12]|(TL|TR|BL|BR) OUT[12]|VCC|VSUP|USB5V|VBUS")   # Motor / Power net classes: 0.2 mm


def extra_for(net, grow):
    """Obstacle growth for copper of `net`: Power / Motor class copper keeps 0.2 mm, at least 0.05 more than Default."""
    return 0.05 if POWER_RE.fullmatch(net or "") else 0.0


class Router:
    BACKGROUND = {"F.Cu": {"GND"}, "B.Cu": {"GND"}, "In1.Cu": None, "In2.Cu": None}   # None: every zone is a plane

    def __init__(self, board, dru_text, region, step=0.05):
        self.b = board
        self.region = region
        self.grid = Grid(*region, step)
        self.isl = isl_rules(dru_text)
        self.L = {name: board.GetLayerID(name) for name in ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu")}
        x0, y0, x1, y1 = region
        self.bbox = pcbnew.BOX2I(pcbnew.VECTOR2I(MM(x0 - 3), MM(y0 - 3)), pcbnew.VECTOR2I(MM(x1 - x0 + 6), MM(y1 - y0 + 6)))

    def _near(self, item):
        return item.GetBoundingBox().Intersects(self.bbox)

    def _grow(self, item, layer, amount):
        ps = pcbnew.SHAPE_POLY_SET()
        item.TransformShapeToPolygon(ps, layer, MM(amount), ERR, pcbnew.ERROR_OUTSIDE)
        return poly_outlines(ps)

    def blocked(self, layer_name, net, grow, rule_grow=0.0, extra_keep=(), for_via=False):
        """Cells where copper of net `net` reaching `grow` mm around the cell centre would hit another net,
        a keep-out or the board edge on this layer."""
        g, L = self.grid, self.L[layer_name]
        m = np.zeros((g.ny, g.nx), dtype=bool)
        for fp in self.b.GetFootprints():
            if not self._near(fp):
                continue
            for p in fp.Pads():
                if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                    hole = pcbnew.SHAPE_POLY_SET()
                    p.TransformHoleToPolygon(hole, MM(0.25 + grow), ERR, pcbnew.ERROR_OUTSIDE)
                    for pts in poly_outlines(hole):
                        g.fill(m, pts)
                    continue
                if p.IsOnLayer(L) and p.GetNetname() != net:
                    for pts in self._grow(p, L, grow + extra_for(p.GetNetname(), grow)):
                        g.fill(m, pts)
        for t in self.b.GetTracks():
            if not self._near(t) or t.GetNetname() == net:
                continue
            if t.GetClass() == "PCB_VIA" or t.GetLayer() == L:
                for pts in self._grow(t, L, grow + extra_for(t.GetNetname(), grow)):
                    g.fill(m, pts)
        bg = self.BACKGROUND[layer_name]
        for z in self.b.Zones():
            if not z.IsOnLayer(L) or not self._near(z):
                continue
            if z.GetIsRuleArea():
                name = z.GetZoneName()
                isl = self.isl.get(name)
                if for_via:
                    continue   # track-only rules (keep-outs that ban tracks, the ISL rules); vias: see route()
                if (z.GetDoNotAllowTracks() and not isl) or (isl and isl[0] == layer_name and net not in isl[1]):
                    ps = pcbnew.SHAPE_POLY_SET(z.Outline())
                    ps.Inflate(MM(grow if isl else max(grow, rule_grow)), pcbnew.CORNER_STRATEGY_ROUND_ALL_CORNERS, ERR)
                    for pts in poly_outlines(ps):
                        g.fill(m, pts)
                continue
            if z.GetNetname() == net or bg is None or z.GetNetname() in bg:
                continue
            ps = pcbnew.SHAPE_POLY_SET(z.GetFilledPolysList(L))
            ps.Inflate(MM(grow + extra_for(z.GetNetname(), grow)), pcbnew.CORNER_STRATEGY_ROUND_ALL_CORNERS, ERR)
            for pts in poly_outlines(ps):
                g.fill(m, pts)
        # board edge and cut-outs
        inside = np.zeros_like(m)
        outline = pcbnew.SHAPE_POLY_SET()
        self.b.GetBoardPolygonOutlines(outline, True)
        shrunk = pcbnew.SHAPE_POLY_SET(outline)
        shrunk.Inflate(-MM(0.5 + grow), pcbnew.CORNER_STRATEGY_ROUND_ALL_CORNERS, ERR)
        for k in range(shrunk.OutlineCount()):
            ol = shrunk.Outline(k)
            g.fill(inside, [(TO(ol.CPoint(i).x), TO(ol.CPoint(i).y)) for i in range(ol.PointCount())])
            for h in range(shrunk.HoleCount(k)):
                hl = shrunk.Hole(k, h)
                g.fill(inside, [(TO(hl.CPoint(i).x), TO(hl.CPoint(i).y)) for i in range(hl.PointCount())], False)
        m |= ~inside
        for pts in extra_keep:
            g.fill(m, pts)
        return m

    def noise_field(self, nets_re, margin):
        """Cells within `margin` mm of copper of nets matching nets_re, on each outer layer."""
        g = self.grid
        out = {}
        rx = re.compile(nets_re)
        for ln in ("F.Cu", "B.Cu"):
            L = self.L[ln]
            m = np.zeros((g.ny, g.nx), dtype=bool)
            for fp in self.b.GetFootprints():
                if not self._near(fp):
                    continue
                for p in fp.Pads():
                    if p.IsOnLayer(L) and rx.fullmatch(p.GetNetname() or ""):
                        for pts in self._grow(p, L, margin):
                            g.fill(m, pts)
            for t in self.b.GetTracks():
                if self._near(t) and rx.fullmatch(t.GetNetname() or "") and (t.GetClass() == "PCB_VIA" or t.GetLayer() == L):
                    for pts in self._grow(t, L, margin):
                        g.fill(m, pts)
            for z in self.b.Zones():
                if not z.GetIsRuleArea() and z.IsOnLayer(L) and self._near(z) and rx.fullmatch(z.GetNetname() or ""):
                    ps = pcbnew.SHAPE_POLY_SET(z.GetFilledPolysList(L))
                    ps.Inflate(MM(margin), pcbnew.CORNER_STRATEGY_ROUND_ALL_CORNERS, ERR)
                    for pts in poly_outlines(ps):
                        g.fill(m, pts)
            out[ln] = m
        return out

    def route(self, net, starts, goals, width, clearance, layers=("F.Cu", "B.Cu"), via=(0.6, 0.3), via_cost=3.0,
              noise=None, noise_cost=4.0, clear_radius=0.0, extra_keep=(), allow=None, heuristic=None):
        """starts/goals: lists of (x, y, layer_name).  Returns [(layer, [(x, y), ...]), ...] runs with vias between.
        heuristic: optional h(i, j) -> mm lower bound to the nearest goal (replaces the default, which scans every
        goal and gets slow with hundreds of them)."""
        g = self.grid
        blk = {}
        for ln in layers:
            keeps = extra_keep.get(ln, ()) if isinstance(extra_keep, dict) else extra_keep
            m = self.blocked(ln, net, clearance + width / 2.0)
            if keeps:
                km = np.zeros_like(m)
                for pts in keeps:
                    g.fill(km, pts)
                for pts in (allow or {}).get(ln, ()):
                    g.fill(km, pts, False)   # the keep-out does not apply here (real obstacles still do)
                m |= km
            blk[ln] = m
        vb = None
        for ln in ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu"):
            m = self.blocked(ln, net, clearance + via[0] / 2.0, for_via=True)
            vb = m if vb is None else (vb | m)
        # rule areas that ban vias
        for z in self.b.Zones():
            if z.GetIsRuleArea() and z.GetDoNotAllowVias() and self._near(z):
                ps = pcbnew.SHAPE_POLY_SET(z.Outline())
                ps.Inflate(MM(via[0] / 2.0), pcbnew.CORNER_STRATEGY_ROUND_ALL_CORNERS, ERR)
                for pts in poly_outlines(ps):
                    g.fill(vb, pts)
        # the start/goal pads themselves: free their immediate surroundings on their own layer
        for (x, y, ln) in list(starts) + list(goals):
            i, j = g.ij(x, y)
            r = max(1, int(round(clear_radius / g.step)))
            blk[ln][max(0, j - r):j + r + 1, max(0, i - r):i + r + 1] = False
        li = {ln: k for k, ln in enumerate(layers)}
        goal_set = {(li[ln],) + g.ij(x, y) for (x, y, ln) in goals}
        gx = [g.ij(x, y) for (x, y, ln) in goals]

        def h(i, j):
            return min(math.hypot(i - a, j - b) for a, b in gx) * g.step
        if heuristic is not None:
            h = heuristic

        dirs = [(1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)]
        best, prev, pq = {}, {}, []
        for (x, y, ln) in starts:
            s = (li[ln],) + g.ij(x, y)
            best[s] = 0.0
            heapq.heappush(pq, (h(s[1], s[2]), 0.0, s))
        nz = noise or {}
        found = None
        while pq:
            f, c, s = heapq.heappop(pq)
            if c > best.get(s, 1e18) + 1e-9:
                continue
            if s in goal_set:
                found = s
                break
            l, i, j = s
            ln = layers[l]
            for di, dj in dirs:
                ni, nj = i + di, j + dj
                if not (0 <= ni < g.nx and 0 <= nj < g.ny) or blk[ln][nj, ni]:
                    continue
                if di and dj and (blk[ln][j, ni] or blk[ln][nj, i]):
                    continue
                step = g.step * (1.41421356 if di and dj else 1.0)
                if ln in nz and nz[ln][nj, ni]:
                    step *= noise_cost
                nc = c + step
                t = (l, ni, nj)
                if nc < best.get(t, 1e18):
                    best[t], prev[t] = nc, s
                    heapq.heappush(pq, (nc + h(ni, nj), nc, t))
            if not vb[j, i]:
                for l2 in range(len(layers)):
                    if l2 != l and not blk[layers[l2]][j, i]:
                        t = (l2, i, j)
                        nc = c + via_cost
                        if nc < best.get(t, 1e18):
                            best[t], prev[t] = nc, s
                            heapq.heappush(pq, (nc + h(i, j), nc, t))
        if not found:
            return None
        path = [found]
        while path[-1] in prev:
            path.append(prev[path[-1]])
        path.reverse()
        runs, cur = [], None
        for (l, i, j) in path:
            if cur is None or cur[0] != layers[l]:
                cur = (layers[l], [])
                runs.append(cur)
            cur[1].append((i, j))

        def los(mask, a, c):
            n = max(1, int(math.hypot(c[0] - a[0], c[1] - a[1]) * 3))
            for k in range(n + 1):
                fi = a[0] + (c[0] - a[0]) * k / n
                fj = a[1] + (c[1] - a[1]) * k / n
                for ii in (int(math.floor(fi)), int(math.ceil(fi))):
                    for jj in (int(math.floor(fj)), int(math.ceil(fj))):
                        if mask[jj, ii]:
                            return False
            return True

        out = []
        for ln, cells in runs:
            mask = blk[ln]
            pts, k = [cells[0]], 0
            while k < len(cells) - 1:
                m = len(cells) - 1
                while m > k + 1 and not los(mask, cells[k], cells[m]):
                    m -= 1
                pts.append(cells[m])
                k = m
            out.append((ln, [g.xy(i, j) for (i, j) in pts]))
        return out


def simplify(pts):
    """Collapse a cell path into its corner points."""
    if len(pts) < 3:
        return pts
    out = [pts[0]]
    for k in range(1, len(pts) - 1):
        ax, ay = pts[k][0] - out[-1][0], pts[k][1] - out[-1][1]
        bx, by = pts[k + 1][0] - pts[k][0], pts[k + 1][1] - pts[k][1]
        if abs(ax * by - ay * bx) > 1e-9:
            out.append(pts[k])
    out.append(pts[-1])
    return out


def add_runs(board, net, runs, width, via=(0.6, 0.3), snap_start=None, snap_end=None, keep=None):
    """Turn router runs into PCB_TRACK / PCB_VIA items.  snap_* replace the first/last point exactly."""
    ni = board.FindNet(net)
    made = []
    # a run of one point between two layer changes is a stacked via: keep one via there, not two
    runs = [r for k, r in enumerate(runs) if k in (0, len(runs) - 1) or len(simplify(r[1])) > 1]
    for k, (ln, pts) in enumerate(runs):
        pts = simplify(pts)
        if k == 0 and snap_start:
            pts = [snap_start] + pts[1:] if len(pts) > 1 else [snap_start, pts[0]]
        if k == len(runs) - 1 and snap_end:
            pts = pts[:-1] + [snap_end] if len(pts) > 1 else [pts[0], snap_end]
        for a, b2 in zip(pts, pts[1:]):
            if abs(a[0] - b2[0]) < 1e-9 and abs(a[1] - b2[1]) < 1e-9:
                continue
            t = pcbnew.PCB_TRACK(board)
            t.SetStart(pcbnew.VECTOR2I(MM(a[0]), MM(a[1])))
            t.SetEnd(pcbnew.VECTOR2I(MM(b2[0]), MM(b2[1])))
            t.SetWidth(MM(width))
            t.SetLayer(board.GetLayerID(ln))
            t.SetNet(ni)
            board.Add(t)
            made.append(t)
        if k < len(runs) - 1:
            x, y = pts[-1]
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
            v.SetWidth(MM(via[0]))
            v.SetDrill(MM(via[1]))
            v.SetViaType(pcbnew.VIATYPE_THROUGH)
            v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            v.SetNet(ni)
            board.Add(v)
            made.append(v)
    if keep is not None:
        keep.extend(made)
    return made


def free_spot(router, side, anchor, radius, fp_w, fp_h, rot_options=(0, 90), avoid_nets_ok=("GND",), step=0.1,
              pad_specs=None, net_of_pad=None):
    """Find a position near `anchor` where a footprint of courtyard fp_w x fp_h fits on `side` ('F' or 'B'):
    no overlap with other courtyards on that side, and every pad (pad_specs: [(dx, dy, w, h, net)] at rot 0)
    clear of other nets' copper by 0.2 mm.  Returns (x, y, rot) closest to the anchor, or None."""
    b = router.b
    crt = b.GetLayerID("F.CrtYd" if side == "F" else "B.CrtYd")
    cu = "F.Cu" if side == "F" else "B.Cu"
    boxes = []
    for fp in b.GetFootprints():
        if not router._near(fp):
            continue
        cy = fp.GetCourtyard(crt)
        if cy.OutlineCount():
            bb = cy.BBox()
            boxes.append((TO(bb.GetLeft()), TO(bb.GetTop()), TO(bb.GetRight()), TO(bb.GetBottom())))
    nets = {spec[4] for spec in pad_specs}
    blocks = {n: router.blocked(cu, n, 0.2) for n in nets}
    g = router.grid
    best = None
    ax, ay = anchor
    n = int(radius / step)
    for di in range(-n, n + 1):
        for dj in range(-n, n + 1):
            x, y = ax + di * step, ay + dj * step
            d = math.hypot(x - ax, y - ay)
            if d > radius or (best and d >= best[0]):
                continue
            for rot in rot_options:
                w, hh = (fp_w, fp_h) if rot in (0, 180) else (fp_h, fp_w)
                x0, y0, x1, y1 = x - w / 2, y - hh / 2, x + w / 2, y + hh / 2
                if any(a < x1 and c > x0 and b_ < y1 and e > y0 for (a, b_, c, e) in boxes):
                    continue
                ok = True
                for (dx, dy, pw, ph, net) in pad_specs:
                    r = math.radians(rot)
                    px, py = x + dx * math.cos(r) + dy * math.sin(r), y - dx * math.sin(r) + dy * math.cos(r)
                    pw2, ph2 = (pw, ph) if rot in (0, 180) else (ph, pw)
                    i0, j0 = g.ij(px - pw2 / 2, py - ph2 / 2)
                    i1, j1 = g.ij(px + pw2 / 2, py + ph2 / 2)
                    if i0 < 0 or j0 < 0 or i1 >= g.nx or j1 >= g.ny or blocks[net][j0:j1 + 1, i0:i1 + 1].any():
                        ok = False
                        break
                if ok:
                    best = (d, x, y, rot)
    return best[1:] if best else None
