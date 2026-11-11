// Reads a G-code file's text into what the UI needs: its line count, its moves (for a preview to draw) and the
// job's extent. Plain JS with no UI or framework code, so it outlives whichever preview draws it. Tests:
// reader.test.js (npm test).
//
// Units: millimetres, always. G20 files are converted.
// Coordinates: work coordinates, the ones the machine reports as WPos (machine position − WCO). G92 offsets are
// applied, since FluidNC folds them into WCO. Moves in machine coordinates (G53) are kept but marked machine: true
// and left out of the extent.
//
// Handled: G0/G1/G2/G3 (arcs by I/J/K or R, in the G17/G18/G19 plane, helical Z), G20/G21, G90/G91, G90.1/G91.1,
// G92/G92.1, G53, comments in ( ) and after ;, line numbers (N), checksums (*), % lines, block delete (/) and $
// commands. Anything else (G28, G30, probing, canned cycles) doesn't move the reader's position.

const MM_PER_INCH = 25.4;
const AXES = ["x", "y", "z"];
// For each arc plane: the two axes the arc lies in, the axis it moves along linearly, and the centre offset words
const PLANES = {
  G17: { a: "x", b: "y", n: "z", ia: "I", ib: "J" },
  G18: { a: "z", b: "x", n: "y", ia: "K", ib: "I" },
  G19: { a: "y", b: "z", n: "x", ia: "J", ib: "K" }
};

const stripComments = (line) => line.replace(/\([^)]*\)/g, " ").replace(/;.*$/, "");
const WORD = /([A-Z])\s*([-+]?(?:\d+\.?\d*|\.\d+))/gi;

// One line's words as [[letter, number], ...], upper case, in order. Line numbers and checksums are dropped.
export function parseLine(raw) {
  let line = stripComments(String(raw)).trim();
  if (!line || line[0] === "%" || line[0] === "/" || line[0] === "$") return [];
  line = line.replace(/\*\d+\s*$/, "");
  const words = [];
  for (const [, letter, num] of line.matchAll(WORD)) {
    const L = letter.toUpperCase();
    if (L !== "N") words.push([L, Number(num)]);
  }
  return words;
}

const emptyExtent = () => ({ min: { x: Infinity, y: Infinity, z: Infinity }, max: { x: -Infinity, y: -Infinity, z: -Infinity } });
// Grows the extent by a point, on the axes whose position is known
function grow(e, p, known) {
  for (const k of AXES) {
    if (!known[k]) continue;
    if (p[k] < e.min[k]) e.min[k] = p[k];
    if (p[k] > e.max[k]) e.max[k] = p[k];
  }
}

// An axis the file never set has no extent: report 0..0 for it rather than Infinity
function fill(e) {
  for (const k of AXES) if (e.min[k] > e.max[k]) { e.min[k] = 0; e.max[k] = 0; }
  return e;
}

const TAU = 2 * Math.PI;
// The arc's sweep in radians: negative clockwise, positive counter-clockwise, a full turn when it ends where it began
function sweep(a0, a1, clockwise) {
  let d = a1 - a0;
  if (clockwise) { if (d >= -1e-9) d -= TAU; } else if (d <= 1e-9) d += TAU;
  return d;
}

// Every point of an arc move that sets its extent: both ends, plus each place it crosses an axis through its centre
function arcExtremes(move) {
  const { plane, center, clockwise } = move.arc;
  const P = PLANES[plane];
  const r = Math.hypot(move.from[P.a] - center[P.a], move.from[P.b] - center[P.b]);
  const a0 = Math.atan2(move.from[P.b] - center[P.b], move.from[P.a] - center[P.a]);
  const d = sweep(a0, Math.atan2(move.to[P.b] - center[P.b], move.to[P.a] - center[P.a]), clockwise);
  const pts = [move.from, move.to];
  for (let q = -8; q <= 8; q++) {
    const ang = (q * Math.PI) / 2;
    const t = (ang - a0) / d;   // how far along the arc this angle is, 0..1
    if (t > 0 && t < 1) {
      const p = { ...move.from };
      p[P.a] = center[P.a] + r * Math.cos(ang);
      p[P.b] = center[P.b] + r * Math.sin(ang);
      p[P.n] = move.from[P.n] + (move.to[P.n] - move.from[P.n]) * t;
      pts.push(p);
    }
  }
  return pts;
}

// Points along an arc move, for drawing it: no chord longer than maxStep mm. Includes both ends.
export function arcPoints(move, maxStep = 1) {
  const { plane, center, clockwise } = move.arc;
  const P = PLANES[plane];
  const r = Math.hypot(move.from[P.a] - center[P.a], move.from[P.b] - center[P.b]);
  const a0 = Math.atan2(move.from[P.b] - center[P.b], move.from[P.a] - center[P.a]);
  const d = sweep(a0, Math.atan2(move.to[P.b] - center[P.b], move.to[P.a] - center[P.a]), clockwise);
  const n = Math.max(1, Math.ceil((Math.abs(d) * r) / maxStep));
  const pts = [];
  for (let i = 0; i <= n; i++) {
    if (i === n) { pts.push({ ...move.to }); break; }
    const t = i / n;
    const p = { ...move.from };
    p[P.a] = center[P.a] + r * Math.cos(a0 + d * t);
    p[P.b] = center[P.b] + r * Math.sin(a0 + d * t);
    p[P.n] = move.from[P.n] + (move.to[P.n] - move.from[P.n]) * t;
    pts.push(p);
  }
  return pts;
}

// The arc's centre for the R form: of the two circles through both ends, + R takes the shorter arc, − R the longer
function centerFromRadius(from, to, r, clockwise, P) {
  const dx = to[P.a] - from[P.a], dy = to[P.b] - from[P.b];
  const chord = Math.hypot(dx, dy);
  if (chord === 0) return null;
  const h2 = r * r - (chord / 2) ** 2;
  const h = Math.sqrt(Math.max(0, h2)) * (clockwise === r > 0 ? -1 : 1);
  const c = { ...from };
  c[P.a] = from[P.a] + dx / 2 - (h * dy) / chord;
  c[P.b] = from[P.b] + dy / 2 + (h * dx) / chord;
  return c;
}

/**
 * Reads a G-code file.
 * @param {string} text the file's contents
 * @returns {{
 *   lineCount: number,     lines in the file, as the machine numbers them when it runs it
 *   moves: Array<{ line, rapid, from: {x,y,z}, to: {x,y,z}, machine?: true,
 *                  arc?: { plane: "G17"|"G18"|"G19", center: {x,y,z}, clockwise } }>,
 *                          in file order; line is 1-based; rapid is G0; arcs also have arc (draw with arcPoints)
 *   extent: { min: {x,y,z}, max: {x,y,z} } | null,
 *                          what the cutting moves (G1/G2/G3) cover, arcs included; null if the file has none.
 *                          Rapids are left out, as in the current UI's job bounds. An axis the file never sets
 *                          (or only sets relative to an unknown start) reads 0..0.
 *   travelExtent: { min, max } | null   the same over every move, rapids included
 * }}
 */
export function readGcode(text) {
  const src = String(text ?? "");
  const lines = src.split(/\r?\n/);
  if (lines.length && lines[lines.length - 1] === "") lines.pop();   // a final newline doesn't start a line

  const pos = { x: 0, y: 0, z: 0 };       // where the tool is, work coordinates, mm
  // Which axes of pos the file has set. Until a move sets an axis the reader doesn't know where the tool is on it
  // (pos says 0), and after a G53 move it doesn't know the work position, so the extent leaves those out.
  const known = { x: false, y: false, z: false };
  const offset = { x: 0, y: 0, z: 0 };    // G92 offset, mm
  let motion = null;                       // the modal motion: G0..G3
  let inches = false, relative = false, arcRelative = true, plane = "G17";
  const moves = [];
  const cut = emptyExtent(), all = emptyExtent();

  lines.forEach((raw, i) => {
    const words = parseLine(raw);
    if (!words.length) return;
    const val = {};
    let lineMotion = null, nonModal = null, machineCoords = false;
    for (const [L, n] of words) {
      if (L === "G") {
        const g = Math.round(n * 10) / 10;
        if (g === 0 || g === 1 || g === 2 || g === 3) lineMotion = g;
        else if (g === 20) inches = true;
        else if (g === 21) inches = false;
        else if (g === 90) relative = false;
        else if (g === 91) relative = true;
        else if (g === 90.1) arcRelative = false;
        else if (g === 91.1) arcRelative = true;
        else if (g === 17 || g === 18 || g === 19) plane = `G${g}`;
        else if (g === 53) machineCoords = true;
        else if ([4, 10, 28, 30, 92, 92.1].includes(g) || (g >= 38 && g < 39)) nonModal = g;
        else if (g === 80) motion = null;
      } else if (val[L] === undefined) {
        val[L] = n;
      }
    }
    const mm = (v) => (inches ? v * MM_PER_INCH : v);

    if (nonModal === 92.1) { AXES.forEach(k => { offset[k] = 0; }); return; }
    if (nonModal === 92) {
      // G92 X..: the current point is now called X.. in the program; the work position doesn't change
      for (const k of AXES) {
        const L = k.toUpperCase();
        if (val[L] !== undefined) offset[k] = pos[k] - mm(val[L]);
      }
      return;
    }
    if (nonModal != null) return;   // dwell, G10, G28/G30, probing: no move the reader can follow

    if (lineMotion != null) motion = lineMotion;
    const hasAxis = AXES.some(k => val[k.toUpperCase()] !== undefined);
    if (!hasAxis || motion == null) return;

    const to = { ...pos };
    for (const k of AXES) {
      const v = val[k.toUpperCase()];
      if (v === undefined) continue;
      if (machineCoords) to[k] = mm(v);   // machine coordinates; the reader doesn't know the WCO, so taken as is
      else to[k] = relative ? pos[k] + mm(v) : mm(v) + offset[k];
    }
    const move = { line: i + 1, rapid: motion === 0, from: { ...pos }, to };
    if (machineCoords) move.machine = true;

    if ((motion === 2 || motion === 3) && !machineCoords) {
      const P = PLANES[plane];
      const clockwise = motion === 2;
      let center = null;
      if (val.R !== undefined) {
        center = centerFromRadius(move.from, to, mm(val.R), clockwise, P);
      } else if (val[P.ia] !== undefined || val[P.ib] !== undefined) {
        center = { ...move.from };
        const ca = mm(val[P.ia] ?? 0), cb = mm(val[P.ib] ?? 0);
        center[P.a] = arcRelative ? move.from[P.a] + ca : ca + offset[P.a];
        center[P.b] = arcRelative ? move.from[P.b] + cb : cb + offset[P.b];
      }
      if (center) move.arc = { plane, center, clockwise };   // no centre: an invalid arc, drawn as a straight line
    }

    moves.push(move);
    Object.assign(pos, to);
    const before = { ...known };
    for (const k of AXES) {
      if (val[k.toUpperCase()] !== undefined) known[k] = machineCoords ? false : known[k] || !relative;
    }
    if (move.machine) return;
    // An arc's sides come from where it starts, so they count only if that is known on both of its axes
    const P = move.arc && PLANES[move.arc.plane];
    const sides = move.arc && before[P.a] && before[P.b] ? arcExtremes(move).slice(2) : [];
    const grows = [[move.from, before], [move.to, known], ...sides.map(p => [p, { ...known, [P.a]: true, [P.b]: true }])];
    const add = (e) => grows.forEach(([p, k]) => grow(e, p, k));
    add(all);
    if (!move.rapid) add(cut);
  });

  const found = (e) => (AXES.some(k => e.min[k] <= e.max[k]) ? fill(e) : null);
  return { lineCount: lines.length, moves, extent: found(cut), travelExtent: found(all) };
}

// The points Trace boundary visits: the extent as a closed rectangle (from its lower-left corner, counter-clockwise),
// then back to `back`, where the machine was. Work coordinates, mm.
export function boundaryPath(extent, back) {
  const { min, max } = extent;
  const pts = [
    { x: min.x, y: min.y }, { x: max.x, y: min.y }, { x: max.x, y: max.y }, { x: min.x, y: max.y }, { x: min.x, y: min.y }
  ];
  if (back) pts.push({ x: back.x, y: back.y });
  return pts;
}
