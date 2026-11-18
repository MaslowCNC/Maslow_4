// Where on the frame cuts land within a tolerance: the overlay on the drawing (the Workspace toggle).
// Replaces the current UI's red→green squares (toolpath-displayer.js, computPositonGradient) with an error in mm worked
// out from the anchors. Plain JS with no UI code. The model, its evidence and the numbers behind the defaults are in
// tools/workspace-accuracy/REPORT.md (the prototype there is accuracy-model.mjs).
//
// For each point p it estimates the 95% error relative to the frame centre:
//   e95(p) = |systematic(p) − systematic(centre)| + 2.45 · σmax(p)
// - The sled settles where the four belts' stretch energy is smallest (belt stiffness EA / L), so a belt-length error
//   dL moves it by G·dL, G = (J'WJ)⁻¹J'W.
// - The anchors are only as good as calibration: their covariance comes from the same bundle adjustment the firmware
//   runs (Calibration.cpp), on the grid the firmware measures, with the fit RMS as the measurement noise.
// - Systematic belt errors (spoil board thickness, belt scale) bias calibration too, so the fit absorbs part of them.
//
// Estimates until the firmware saves them (see REPORT.md, "Inputs and firmware data"): the calibration fit RMS
// (CAL_RMS) and the grid that was measured (the firmware's auto rule is repeated here). Flat machines only: a vertical
// machine also needs the weight of the sled (REPORT.md, "Vertical machines").

export const TOLERANCE = 2;          // mm, the default tolerance (Settings → Frame and belts changes it)
const CAL_RMS = 0.5;                 // mm, calibration fit RMS ("Find Anchors fit: rms=")
const RUN_SIGMA = 0.1;               // mm, belt following error while cutting (the report's BE)
const SPOIL_ERROR = 3;               // mm, spoil board thickness entered wrong by this much
const BELT_SCALE = 0.0005;           // belt pitch / stretch scale error
const ARM = 153.4;                   // beltEndExtension + armLength, mm (defaults; replaced by the config's values)
const MIN_BELT = 100;                // mm of belt beyond the arm, closer than this to an anchor isn't usable
const STEP = 50;                     // mm between map points

const inv2 = ([[a, b], [c, d]]) => { const det = a * d - b * c; return [[d / det, -b / det], [-c / det, a / det]]; };
const mul = (A, B) => A.map((r) => B[0].map((_, j) => r.reduce((s, v, k) => s + v * B[k][j], 0)));
const tr = (A) => A[0].map((_, j) => A.map((r) => r[j]));
const sub = (A, B) => A.map((r, i) => r.map((v, j) => v - B[i][j]));
const add = (A, B) => A.map((r, i) => r.map((v, j) => v + B[i][j]));
const eigMax2 = ([[a, b], [, d]]) => (a + d) / 2 + Math.sqrt(((a - d) / 2) ** 2 + b * b);
function invN(M) {
  const n = M.length, A = M.map((r, i) => [...r, ...M.map((_, j) => (i === j ? 1 : 0))]);
  for (let c = 0; c < n; c++) {
    let p = c; for (let r = c + 1; r < n; r++) if (Math.abs(A[r][c]) > Math.abs(A[p][c])) p = r;
    [A[c], A[p]] = [A[p], A[c]];
    const d = A[c][c]; for (let j = 0; j < 2 * n; j++) A[c][j] /= d;
    for (let r = 0; r < n; r++) if (r !== c) { const f = A[r][c]; for (let j = 0; j < 2 * n; j++) A[r][j] -= f * A[c][j]; }
  }
  return A.map((r) => r.slice(n));
}

/**
 * The frame as the firmware's solver sees it, from the config.
 * @param {{ tl:[x,y], tr:[x,y], bl:[x,y], br:[x,y], z?: number[4], spoil?: number, arm?: number }} c
 *   anchors in mm (BL is normally 0,0), z = [tl, tr, bl, br] anchor heights, arm = beltEndExtension + armLength
 * @returns the frame, or null if the anchors don't make one
 */
export function makeFrame({ tl, tr: trA, bl = [0, 0], br, z = [0, 0, 0, 0], spoil = 0, arm = ARM }) {
  const width = Math.min(trA[0] - tl[0], br[0] - bl[0]), height = Math.min(tl[1] - bl[1], trA[1] - br[1]);
  if (!(width > 300 && height > 300)) return null;
  // centre = where the diagonals cross (MaslowKinematics::calculateCenter); machine X 0, Y 0 is here
  const A = (trA[1] - bl[1]) / (trA[0] - bl[0]), B = (br[1] - tl[1]) / (br[0] - tl[0]);
  const cx = (br[1] - B * br[0] + A * trA[0] - trA[1]) / (A - B), cy = A * (cx - trA[0]) + trA[1];
  return { anchors: [tl, trA, bl, br], z: z.map((v) => v + spoil), arm, center: [cx, cy], width, height };
}

const units = (f, s) => f.anchors.map((a) => { const dx = s[0] - a[0], dy = s[1] - a[1], d = Math.hypot(dx, dy); return [dx / d, dy / d]; });
const dists = (f, s) => f.anchors.map((a) => Math.hypot(s[0] - a[0], s[1] - a[1]));
// d(distance)/d(tlX, tlY, trX, trY, brX): the solver keeps BL at the origin and BR on y = 0
const dTheta = (f, s) => units(f, s).map((u, i) => {
  const row = [0, 0, 0, 0, 0];
  if (i === 0) { row[0] = -u[0]; row[1] = -u[1]; } else if (i === 1) { row[2] = -u[0]; row[3] = -u[1]; } else if (i === 3) row[4] = -u[0];
  return row;
});
const bias = (f, s) => dists(f, s).map((d, i) => { const L = Math.max(d - f.arm, 1); return (f.z[i] * SPOIL_ERROR) / L + BELT_SCALE * L; });

// The firmware's auto grid (Calibration::generate_calibration_grid): 40% of the frame, 3/5/7/9 points a side with
// at most 260 mm between them, plus the 150 mm ring it starts with
function grid(f) {
  const gw = f.width * 0.4, gh = f.height * 0.4, pts = [], [cx, cy] = f.center;
  let n = 9; for (const t of [3, 5, 7, 9]) if (Math.max(gw, gh) / (t - 1) <= 260) { n = t; break; }
  const h = (n - 1) / 2;
  for (let i = -h; i <= h; i++) for (let j = -h; j <= h; j++) pts.push([cx + (i * gw) / (n - 1), cy + (j * gh) / (n - 1)]);
  for (const [dx, dy] of [[0, 0], [150, 0], [150, 150], [0, 150], [-150, 150], [-150, 0]]) pts.push([cx + dx, cy + dy]);
  return pts;
}

// Anchor covariance and the anchors' response to the systematic bias, by Schur complement over the sled positions
function calibrate(f) {
  let U = Array.from({ length: 5 }, () => Array(5).fill(0)); const g = Array(5).fill(0);
  for (const s of grid(f)) {
    const Ja = dTheta(f, s), Js = units(f, s), b = bias(f, s);
    const W = Array.from({ length: 5 }, (_, r) => [0, 1].map((c) => Ja.reduce((t, row, i) => t + row[r] * Js[i][c], 0)));
    const Vi = inv2([0, 1].map((r) => [0, 1].map((c) => Js.reduce((t, row) => t + row[r] * row[c], 0))));
    const WVi = mul(W, Vi), gs = [0, 1].map((r) => Js.reduce((t, row, i) => t + row[r] * b[i], 0));
    U = add(U, sub(mul(tr(Ja), Ja), mul(WVi, tr(W))));
    for (let r = 0; r < 5; r++) g[r] += Ja.reduce((t, row, i) => t + row[r] * b[i], 0) - (WVi[r][0] * gs[0] + WVi[r][1] * gs[1]);
  }
  const cov = invN(U);
  return { cov: cov.map((r) => r.map((v) => v * CAL_RMS ** 2)), shift: cov.map((r) => r.reduce((t, v, k) => t + v * g[k], 0)) };
}

/**
 * The accuracy map of a frame: e95 in mm at points STEP apart, in machine coordinates (X 0, Y 0 at the centre).
 * @returns {{ xs: number[], ys: number[], e95: number[][] }} e95[j][i] at (xs[i], ys[j]); Infinity where a belt
 *   would be shorter than MIN_BELT
 */
export function accuracyMap(f) {
  const cal = calibrate(f);
  const at = (s) => {
    const u = units(f, s), w = dists(f, s).map((d) => 1 / Math.max(d - f.arm, 50));
    const Ji = inv2([0, 1].map((r) => [0, 1].map((c) => u.reduce((t, ui, i) => t + w[i] * ui[r] * ui[c], 0))));
    const G = [0, 1].map((r) => u.map((ui, i) => w[i] * (Ji[r][0] * ui[0] + Ji[r][1] * ui[1])));
    const M = mul(G, dTheta(f, s)), b = bias(f, s);
    const dp = [0, 1].map((r) => M[r].reduce((t, v, k) => t + v * cal.shift[k], 0) - G[r].reduce((t, v, i) => t + v * b[i], 0));
    return { G, M, dp };
  };
  const c = at(f.center), GGc = mul(c.G, tr(c.G));
  const point = (s) => {
    if (Math.min(...dists(f, s)) - f.arm < MIN_BELT) return Infinity;
    const p = at(s), Md = sub(p.M, c.M);
    const C = add(mul(mul(Md, cal.cov), tr(Md)), add(mul(p.G, tr(p.G)), GGc).map((r) => r.map((v) => v * RUN_SIGMA ** 2)));
    return Math.hypot(p.dp[0] - c.dp[0], p.dp[1] - c.dp[1]) + 2.45 * Math.sqrt(eigMax2(C));
  };
  const [x0, x1] = [Math.max(f.anchors[0][0], f.anchors[2][0]), Math.min(f.anchors[1][0], f.anchors[3][0])];
  const [y0, y1] = [Math.max(f.anchors[2][1], f.anchors[3][1]), Math.min(f.anchors[0][1], f.anchors[1][1])];
  const fx = [], fy = [];
  for (let x = x0 + STEP / 2; x < x1; x += STEP) fx.push(x);
  for (let y = y0 + STEP / 2; y < y1; y += STEP) fy.push(y);
  return { xs: fx.map((x) => x - f.center[0]), ys: fy.map((y) => y - f.center[1]), e95: fy.map((y) => fx.map((x) => point([x, y]))) };
}

// e95 at a machine position, interpolated between map points (Infinity outside the map)
export function errorAt(map, x, y) {
  const { xs, ys, e95 } = map, fx = (x - xs[0]) / STEP, fy = (y - ys[0]) / STEP;
  if (fx < -0.5 || fy < -0.5 || fx > xs.length - 0.5 || fy > ys.length - 0.5) return Infinity;
  const i = Math.max(0, Math.min(xs.length - 2, Math.floor(fx))), j = Math.max(0, Math.min(ys.length - 2, Math.floor(fy)));
  const u = Math.max(0, Math.min(1, fx - i)), v = Math.max(0, Math.min(1, fy - j));
  return (e95[j][i] * (1 - u) + e95[j][i + 1] * u) * (1 - v) + (e95[j + 1][i] * (1 - u) + e95[j + 1][i + 1] * u) * v;
}

/** The largest rectangle centred on the frame centre where every map point is within tol: { w, h } in mm. */
export function safeRect(map, tol = TOLERANCE) {
  const { xs, ys, e95 } = map, ok = (hw, hh) => e95.every((r, j) => r.every((e, i) => Math.abs(xs[i]) > hw || Math.abs(ys[j]) > hh || e <= tol));
  let best = { w: 0, h: 0 };
  for (let hh = STEP; hh < -ys[0]; hh += STEP) {
    if (!ok(0, hh)) break;
    let hw = 0; while (hw + STEP < -xs[0] && ok(hw + STEP, hh)) hw += STEP;
    if (hw * hh * 4 > best.w * best.h) best = { w: 2 * hw, h: 2 * hh };
  }
  return best;
}

/**
 * Checks a job's outline (its extent, in machine coordinates) against the tolerance.
 * @returns {{ outside: number, worst: number, pieces: Array<{ ok: boolean, points: Array<{x,y}> }> }} outside = mm of
 *   the outline beyond the tolerance, worst = the largest e95 on it, pieces = the outline as runs that are all in or all out,
 *   { ok, points: [{x,y}, ...] }, for drawing
 */
export function checkJob(map, box, tol = TOLERANCE) {
  const { min: a, max: b } = box, corners = [[a.x, a.y], [b.x, a.y], [b.x, b.y], [a.x, b.y], [a.x, a.y]];
  const pieces = []; let outside = 0, worst = 0;
  for (let k = 0; k < 4; k++) {
    const [p, q] = [corners[k], corners[k + 1]], len = Math.hypot(q[0] - p[0], q[1] - p[1]), n = Math.max(1, Math.ceil(len / 25));
    for (let t = 0; t < n; t++) {
      const from = { x: p[0] + ((q[0] - p[0]) * t) / n, y: p[1] + ((q[1] - p[1]) * t) / n };
      const to = { x: p[0] + ((q[0] - p[0]) * (t + 1)) / n, y: p[1] + ((q[1] - p[1]) * (t + 1)) / n };
      const e = errorAt(map, (from.x + to.x) / 2, (from.y + to.y) / 2), ok = e <= tol;
      worst = Math.max(worst, e);
      if (!ok) outside += len / n;
      const last = pieces[pieces.length - 1];
      if (last && last.ok === ok) last.points.push(to); else pieces.push({ ok, points: [from, to] });
    }
  }
  return { outside, worst, pieces };
}
