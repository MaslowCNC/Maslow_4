// Workspace accuracy model for Maslow 4 / 4.1 (prototype, not used by the UI yet).
//
// Replaces the legacy "colour map" (toolpath-displayer.js, drawMachineBelts) with an error estimate in mm built from
// the machine's geometry. For every point p on the frame it estimates how far a cut lands from where it should,
// relative to the frame centre (the usual home), as a 95% bound:
//
//   E95(p) = |bias(p)| + 2.45 * sqrt(largest eigenvalue of Cov(p)) [+ deflection under a side load, optional]
//
// Pieces (all linearised around the true geometry):
//   1. Sled equilibrium. Four belts are position-controlled; when their lengths disagree the sled settles where the
//      elastic energy sum k_i * stretch_i^2 is smallest, with k_i = EA / belt length. So a belt-length error vector dL
//      moves the sled by dp = G dL, G = (J'WJ)^-1 J'W, J rows = unit vectors anchor -> sled, W = diag(1 / belt length).
//   2. Anchor uncertainty left after calibration. The firmware solves a bundle adjustment
//      (Calibration.cpp recomputeAnchorsWithLevenbergMarquardt): 5 anchor parameters (tlX, tlY, trX, trY, brX; BL at
//      the origin, BR on y = 0) plus one (x, y) per grid point, residual = distance - measured length. With measurement
//      noise sigmaCal (the fit RMS the firmware logs as "Find Anchors fit: rms=") the anchor covariance is
//      sigmaCal^2 * (Schur complement of J'J)^-1. That is where the calibration grid's size and placement come in.
//   3. Systematic belt-length errors (spoil board thickness entered wrong, belt pitch / stretch scale, a constant arm
//      length error). These bias the calibration measurements too, so the solver absorbs part of them: the fitted
//      anchors shift by the least-squares response to the bias, and what's left shows up at each point.
//   4. Runtime belt noise sigmaRun per belt (encoder quantisation is 0.0107 mm, so in practice PID following error,
//      the BE field in the status report).
//   5. Optional: deflection under a side load F, from the belt stiffness K = sum EA/L u u' + T/L (I - u u').
//
// Run: node tools/workspace-accuracy/accuracy-model.mjs [--json out.json]

const ARM = 123.4, EXT = 30.0; // beltEndExtension + armLength (maslow-m4.yaml)
const MM_PER_REV = 1.9988 * 22; // beltToothSpacing * encoderTeeth
const ENCODER_STEP = MM_PER_REV / 4096; // AS5600, 12 bit

// ---------- small linear algebra ----------
const inv2 = ([[a, b], [c, d]]) => { const det = a * d - b * c; return [[d / det, -b / det], [-c / det, a / det]]; };
const mul = (A, B) => A.map((r) => B[0].map((_, j) => r.reduce((s, v, k) => s + v * B[k][j], 0)));
const tr = (A) => A[0].map((_, j) => A.map((r) => r[j]));
const sub = (A, B) => A.map((r, i) => r.map((v, j) => v - B[i][j]));
const add = (A, B) => A.map((r, i) => r.map((v, j) => v + B[i][j]));
const scale = (A, s) => A.map((r) => r.map((v) => v * s));
function invN(M) { // Gauss-Jordan, small matrices only
  const n = M.length, A = M.map((r, i) => [...r, ...Array.from({ length: n }, (_, j) => (i === j ? 1 : 0))]);
  for (let c = 0; c < n; c++) {
    let p = c; for (let r = c + 1; r < n; r++) if (Math.abs(A[r][c]) > Math.abs(A[p][c])) p = r;
    [A[c], A[p]] = [A[p], A[c]];
    const d = A[c][c]; for (let j = 0; j < 2 * n; j++) A[c][j] /= d;
    for (let r = 0; r < n; r++) if (r !== c) { const f = A[r][c]; for (let j = 0; j < 2 * n; j++) A[r][j] -= f * A[c][j]; }
  }
  return A.map((r) => r.slice(n));
}
const eigMax2 = ([[a, b], [, d]]) => (a + d) / 2 + Math.sqrt(((a - d) / 2) ** 2 + b * b);
const eigMin2 = ([[a, b], [, d]]) => (a + d) / 2 - Math.sqrt(((a - d) / 2) ** 2 + b * b);

// ---------- geometry ----------
// Frame coordinates as the solver uses them: BL at (0,0), BR on y=0. theta = [tlX, tlY, trX, trY, brX].
function makeFrame({ tl, tr: trA, bl = [0, 0], br, z = [100, 56, 34, 78], spoil = 19 }) {
  const anchors = [tl, trA, bl, br]; // TL, TR, BL, BR (firmware order)
  // centre = intersection of the diagonals (MaslowKinematics::calculateCenter)
  const A = (trA[1] - bl[1]) / (trA[0] - bl[0]), B = (br[1] - tl[1]) / (br[0] - tl[0]);
  const cx = (br[1] - B * br[0] + A * trA[0] - trA[1]) / (A - B), cy = A * (cx - trA[0]) + trA[1];
  const width = Math.min(trA[0] - tl[0], br[0] - bl[0]), height = Math.min(tl[1] - bl[1], trA[1] - br[1]);
  return { anchors, z: z.map((v) => v + spoil), center: [cx, cy], width, height };
}
const theta0 = (f) => [f.anchors[0][0], f.anchors[0][1], f.anchors[1][0], f.anchors[1][1], f.anchors[3][0]];

// d(distance_i)/d(theta) for sled at s: anchor i moves with these theta entries
function dDist_dTheta(f, s) {
  return f.anchors.map((a, i) => {
    const dx = s[0] - a[0], dy = s[1] - a[1], d = Math.hypot(dx, dy), u = [dx / d, dy / d], row = [0, 0, 0, 0, 0];
    if (i === 0) { row[0] = -u[0]; row[1] = -u[1]; }
    if (i === 1) { row[2] = -u[0]; row[3] = -u[1]; }
    if (i === 3) { row[4] = -u[0]; }
    return row;
  });
}
const units = (f, s) => f.anchors.map((a) => { const dx = s[0] - a[0], dy = s[1] - a[1], d = Math.hypot(dx, dy); return [dx / d, dy / d]; });
const dists = (f, s) => f.anchors.map((a) => Math.hypot(s[0] - a[0], s[1] - a[1]));
const beltLen = (d) => d - (ARM + EXT); // XY belt length beyond the arm

// ---------- systematic belt-length biases (measured minus true), per point ----------
function bias(f, s, sys) {
  const d = dists(f, s);
  return d.map((di, i) => {
    const L = beltLen(di);
    // XY length derived from the angled belt with a wrong Z: d_xy' = sqrt(len^2 - z^2) ~ L + z * dz / L
    const zTerm = sys.dz ? (f.z[i] * sys.dz) / Math.max(L, 1) : 0;
    return zTerm + (sys.scale || 0) * L + (sys.offset || 0);
  });
}

// ---------- calibration grid as the firmware builds it (Calibration::generate_calibration_grid) ----------
function calibrationGrid(f, { widthFrac = 0.4, heightFrac = 0.4, size = 0, maxSpacing = 260, ring = 150, w = 0, h = 0 } = {}) {
  const gw = w || f.width * widthFrac, gh = h || f.height * heightFrac;
  let n = size;
  if (!n) { n = 9; for (const t of [3, 5, 7, 9]) if (Math.max(gw / (t - 1), gh / (t - 1)) <= maxSpacing) { n = t; break; } }
  const pts = [], half = (n - 1) / 2, [cx, cy] = f.center;
  for (let i = -half; i <= half; i++) for (let j = -half; j <= half; j++) pts.push([cx + (i * gw) / (n - 1), cy + (j * gh) / (n - 1)]);
  // first six points: a ring around the start position (the centre)
  for (const [dx, dy] of [[0, 0], [ring, 0], [ring, ring], [0, ring], [-ring, ring], [-ring, 0]]) pts.push([cx + dx, cy + dy]);
  return { pts, n, gw, gh };
}

// Anchor covariance (per sigma^2) and bias response from a calibration grid, by Schur complement over the sled
// positions (the same block structure the firmware's LM uses).
function calibrate(f, grid, sys) {
  let U = Array.from({ length: 5 }, () => Array(5).fill(0)); const g = Array(5).fill(0);
  for (const s of grid.pts) {
    const Ja = dDist_dTheta(f, s), Js = units(f, s), b = bias(f, s, sys);
    // blocks: U += Ja'Ja, W = Ja'Js (5x2), V = Js'Js (2x2)
    const Wm = Array.from({ length: 5 }, (_, r) => [0, 1].map((c) => Ja.reduce((t, row, i) => t + row[r] * Js[i][c], 0)));
    const V = [0, 1].map((r) => [0, 1].map((c) => Js.reduce((t, row) => t + row[r] * row[c], 0)));
    const ga = Ja[0].map((_, r) => Ja.reduce((t, row, i) => t + row[r] * b[i], 0));
    const gs = [0, 1].map((r) => Js.reduce((t, row, i) => t + row[r] * b[i], 0));
    const Vi = inv2(V);
    U = add(U, sub(mul(tr(Ja), Ja), mul(mul(Wm, Vi), tr(Wm))));
    const corr = mul(mul(Wm, Vi), gs.map((v) => [v])).map((r) => r[0]);
    for (let r = 0; r < 5; r++) g[r] += ga[r] - corr[r];
  }
  const covPerSigma2 = invN(U);
  // least-squares response of the fitted anchors to the bias: theta_hat - theta = (J'J)^-1 J'b (reduced)
  const dTheta = covPerSigma2.map((r) => r.reduce((t, v, k) => t + v * g[k], 0));
  return { covPerSigma2, dTheta };
}

// sled response matrix G (2x4) at s: dp = G * (belt length error)
function sledGain(f, s) {
  const u = units(f, s), w = dists(f, s).map((d) => 1 / Math.max(beltLen(d), 50));
  const JtWJ = [0, 1].map((r) => [0, 1].map((c) => u.reduce((t, ui, i) => t + w[i] * ui[r] * ui[c], 0)));
  const Ji = inv2(JtWJ);
  return [0, 1].map((r) => u.map((ui, i) => w[i] * (Ji[r][0] * ui[0] + Ji[r][1] * ui[1])));
}

function stiffness(f, s, EA, T) { // N/mm, smallest over directions
  const u = units(f, s), L = dists(f, s).map((d) => Math.max(beltLen(d), 50));
  const K = [[0, 0], [0, 0]];
  u.forEach((ui, i) => { for (let r = 0; r < 2; r++) for (let c = 0; c < 2; c++) K[r][c] += (EA / L[i]) * ui[r] * ui[c] + (T / L[i]) * ((r === c ? 1 : 0) - ui[r] * ui[c]); });
  return eigMin2(K);
}

// ---------- the map ----------
export function errorMap(f, opts = {}) {
  const o = { sigmaCal: 0.5, sigmaRun: 0.1, sys: { dz: 3, scale: 0.0005, offset: 0 }, grid: {}, step: 25, minBelt: 100, load: 0, EA: 12000, T: 30, ...opts };
  const grid = calibrationGrid(f, o.grid), cal = calibrate(f, grid, o.sys);
  const Sa = scale(cal.covPerSigma2, o.sigmaCal ** 2);
  const at = (s) => {
    const G = sledGain(f, s), M = mul(G, dDist_dTheta(f, s));
    const b = bias(f, s, o.sys);
    const dp = [0, 1].map((r) => M[r].reduce((t, v, k) => t + v * cal.dTheta[k], 0) - G[r].reduce((t, v, i) => t + v * b[i], 0));
    return { G, M, dp };
  };
  const c = at(f.center);
  const point = (s) => {
    const p = at(s), Md = sub(p.M, c.M);
    let C = mul(mul(Md, Sa), tr(Md));
    C = add(C, scale(add(mul(p.G, tr(p.G)), mul(c.G, tr(c.G))), o.sigmaRun ** 2));
    const biasMm = Math.hypot(p.dp[0] - c.dp[0], p.dp[1] - c.dp[1]), sigma = Math.sqrt(eigMax2(C));
    const defl = o.load ? o.load / stiffness(f, s, o.EA, o.T) : 0;
    const shortest = Math.min(...dists(f, s).map(beltLen));
    return { e95: biasMm + 2.45 * sigma + defl, biasMm, sigma, defl, feasible: shortest >= o.minBelt };
  };
  // sample the anchor rectangle (frame coords)
  const xs = [], ys = [], [x0, x1] = [Math.max(f.anchors[0][0], f.anchors[2][0]), Math.min(f.anchors[1][0], f.anchors[3][0])];
  const [y0, y1] = [Math.max(f.anchors[2][1], f.anchors[3][1]), Math.min(f.anchors[0][1], f.anchors[1][1])];
  for (let x = x0 + o.step / 2; x < x1; x += o.step) xs.push(x);
  for (let y = y0 + o.step / 2; y < y1; y += o.step) ys.push(y);
  const cells = ys.map((y) => xs.map((x) => point([x, y])));
  return { f, o, grid, cal, Sa, xs, ys, cells, point };
}

// largest axis-aligned rectangle of cells with e95 <= tol (maximal rectangle in a binary matrix)
export function largestRect(map, tol, centered = false) {
  const { xs, ys, cells, o, f } = map, ok = cells.map((r) => r.map((c) => c.feasible && c.e95 <= tol));
  if (centered) { // grow a rectangle centred on the frame centre with the frame's aspect, then widen each axis
    const [cx, cy] = f.center; let best = { w: 0, h: 0 };
    const fits = (hw, hh) => cells.every((r, j) => r.every((c, i) => Math.abs(xs[i] - cx) > hw || Math.abs(ys[j] - cy) > hh || ok[j][i]));
    for (let hh = o.step; hh < f.height / 2; hh += o.step) { let hw = 0; while (hw + o.step < f.width / 2 && fits(hw + o.step, hh)) hw += o.step; if (!fits(hw, hh)) break; if (4 * hw * hh > best.w * best.h) best = { w: 2 * hw, h: 2 * hh }; }
    return best;
  }
  const H = Array(xs.length).fill(0); let best = { area: 0 };
  ok.forEach((row, j) => {
    row.forEach((v, i) => (H[i] = v ? H[i] + 1 : 0));
    const st = [];
    for (let i = 0; i <= H.length; i++) {
      const h = i < H.length ? H[i] : 0;
      while (st.length && H[st[st.length - 1]] >= h) {
        const top = st.pop(), left = st.length ? st[st.length - 1] + 1 : 0, w = i - left, area = w * H[top];
        if (area > best.area) best = { area, w: w * o.step, h: H[top] * o.step, x: xs[left] - o.step / 2, y: ys[j - H[top] + 1] - o.step / 2 };
      }
      st.push(i);
    }
  });
  return best;
}

const pctOk = (map, tol) => { let n = 0, k = 0; for (const r of map.cells) for (const c of r) { n++; if (c.feasible && c.e95 <= tol) k++; } return (100 * k) / n; };

// ---------- CLI ----------
if (import.meta.url === `file://${process.argv[1]}`) {
  const frames = {
    "maslow-m4.yaml (4340 x 3230)": makeFrame({ tl: [139.2, 3230.6], tr: [4308.9, 3230.7], br: [4340.4, 0] }),
    "standard 3000 x 2100": makeFrame({ tl: [0, 2100], tr: [3000, 2100], br: [3000, 0] }),
  };
  const tols = [0.25, 0.5, 1, 2];
  const r0 = (v) => Math.round(v);
  console.log(`encoder step: ${ENCODER_STEP.toFixed(4)} mm per count (${MM_PER_REV.toFixed(2)} mm per rev / 4096)\n`);
  for (const [name, f] of Object.entries(frames)) {
    const m = errorMap(f);
    const sd = m.Sa.map((r, i) => Math.sqrt(r[i]).toFixed(2));
    console.log(`== ${name}: grid ${m.grid.n}x${m.grid.n}, ${r0(m.grid.gw)} x ${r0(m.grid.gh)} mm; anchor sd [tlX tlY trX trY brX] = ${sd.join(" ")} mm`);
    console.log(`   defaults: sigmaCal 0.5, sigmaRun 0.1, spoil error 3 mm, belt scale 0.05%; auto work area ${r0(f.width * 0.8)} x ${r0(f.height * 0.6)}`);
    for (const t of tols) {
      const a = largestRect(m, t), c = largestRect(m, t, true);
      console.log(`   ±${t} mm: ${pctOk(m, t).toFixed(0).padStart(3)}% of anchor rect | largest rect ${r0(a.w || 0)} x ${r0(a.h || 0)} | centred rect ${r0(c.w)} x ${r0(c.h)}`);
    }
    // where the error comes from at a few points
    const [cx, cy] = f.center;
    for (const [lbl, dx, dy] of [["centre+600,0", 600, 0], ["grid corner", m.grid.gw / 2, m.grid.gh / 2], ["auto WA corner", f.width * 0.4, f.height * 0.3], ["near BR", f.width / 2 - 250, -f.height / 2 + 250]]) {
      const p = m.point([cx + dx, cy + dy]);
      console.log(`   ${lbl.padEnd(15)} e95 ${p.e95.toFixed(2)} mm = bias ${p.biasMm.toFixed(2)} + 2.45*sd ${p.sigma.toFixed(2)}`);
    }
    // grid comparison
    console.log("   grid size / placement vs ±1 mm area (centred rect) and corner-of-work-area error:");
    for (const g of [{ widthFrac: 0.4, heightFrac: 0.4 }, { widthFrac: 0.6, heightFrac: 0.6 }, { widthFrac: 0.8, heightFrac: 0.6 }, { size: 3, widthFrac: 0.8, heightFrac: 0.6 }, { size: 9, widthFrac: 0.4, heightFrac: 0.4 }]) {
      const mg = errorMap(f, { grid: g }), c = largestRect(mg, 1, true), e = mg.point([cx + f.width * 0.4, cy + f.height * 0.3]);
      console.log(`     ${String(mg.grid.n).padStart(1)}x${mg.grid.n} ${r0(mg.grid.gw)} x ${r0(mg.grid.gh)} mm: ±1 mm centred ${r0(c.w)} x ${r0(c.h)}; WA corner e95 ${e.e95.toFixed(2)} (bias ${e.biasMm.toFixed(2)}, sd ${e.sigma.toFixed(2)})`);
    }
    // sensitivity to each input
    console.log("   one input at a time (centred ±1 mm rect):");
    for (const [lbl, o] of [["noise only", { sys: {} }], ["sigmaCal 1.0", { sigmaCal: 1, sys: {} }], ["spoil error 3 mm", { sigmaCal: 0, sigmaRun: 0, sys: { dz: 3 } }], ["belt scale 0.05%", { sigmaCal: 0, sigmaRun: 0, sys: { scale: 0.0005 } }], ["arm offset 1 mm", { sigmaCal: 0, sigmaRun: 0, sys: { offset: 1 } }], ["defaults + 10 N load", { load: 10 }]]) {
      const mo = errorMap(f, o), c = largestRect(mo, 1, true);
      console.log(`     ${lbl.padEnd(20)} ${r0(c.w)} x ${r0(c.h)}`);
    }
    console.log("");
  }
  const out = process.argv.indexOf("--json");
  if (out > 0) {
    const fs = await import("node:fs");
    const f = frames["standard 3000 x 2100"], res = {};
    for (const [k, o] of Object.entries({ default: {}, wideGrid: { grid: { widthFrac: 0.8, heightFrac: 0.6 } } })) {
      const m = errorMap(f, { ...o, step: 50 });
      res[k] = { center: f.center, width: f.width, height: f.height, step: 50, xs: m.xs, ys: m.ys, grid: m.grid, e95: m.cells.map((r) => r.map((c) => +c.e95.toFixed(3))),
        rects: Object.fromEntries([0.5, 1, 2].map((t) => [t, { any: largestRect(m, t), centred: largestRect(m, t, true) }])) };
    }
    fs.writeFileSync(process.argv[out + 1], JSON.stringify(res));
    console.log("wrote", process.argv[out + 1]);
  }
}
