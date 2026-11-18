import { STEPS } from "./flow/screens.js";
import { arcPoints } from "./gcode/reader.js";

// Step graphics and the frame drawing, carried over from the mockup. They return SVG markup as strings.
export function stepArt(i) {
  const frame = `<circle cx="20" cy="14" r="5" fill="var(--node)"/><circle cx="240" cy="14" r="5" fill="var(--node)"/><circle cx="20" cy="106" r="5" fill="var(--node)"/><circle cx="240" cy="106" r="5" fill="var(--node)"/>`;
  const sled = (x, y) => `<rect x="${x - 14}" y="${y - 14}" width="28" height="28" rx="6" fill="var(--panel)" stroke="var(--lilac)" stroke-width="2"/>`;
  const belts = (x, y, dash) => [[20, 14], [240, 14], [20, 106], [240, 106]].map(([a, b]) =>
    `<line x1="${a}" y1="${b}" x2="${x}" y2="${y}" stroke="var(--wire)" stroke-width="1.6" ${dash ? 'stroke-dasharray="5 4"' : ""}/>`).join("");
  const arrow = (x1, y1, x2, y2) => `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="#3c82dc" stroke-width="2.5" marker-end="url(#ah)"/>`;
  const defs = `<defs><marker id="ah" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="5" markerHeight="5" orient="auto"><path d="M0 0L10 5L0 10z" fill="#3c82dc"/></marker></defs>`;
  const art = [
    frame + belts(130, 60, true) + sled(130, 60) + arrow(70, 33, 105, 50) + arrow(190, 33, 155, 50) + arrow(70, 87, 105, 70) + arrow(190, 87, 155, 70),
    frame + belts(130, 60, true) + sled(130, 60) + arrow(105, 50, 60, 28) + arrow(155, 50, 200, 28) + arrow(105, 70, 60, 92) + arrow(155, 70, 200, 92),
    frame + belts(130, 60, false) + sled(130, 60) + `<text x="130" y="100" text-anchor="middle" fill="var(--muted)" font-size="11" font-family="monospace">belts tight, position checked</text>`,
    frame + belts(130, 60, false) + sled(130, 60) + [[20, 14], [240, 14], [20, 106], [240, 106]].map(([x, y]) => `<circle cx="${x}" cy="${y}" r="11" fill="none" stroke="#3c82dc" stroke-width="2"/>`).join("") +
      `<text x="130" y="100" text-anchor="middle" fill="var(--muted)" font-size="11" font-family="monospace">measuring the frame</text>`,
    `<rect x="110" y="12" width="40" height="44" rx="6" fill="var(--panel)" stroke="var(--wire)"/><rect x="124" y="56" width="12" height="16" fill="var(--wire)"/><path d="M127 72 h6 v24 l-3 6 l-3 -6z" fill="var(--lilac)"/>` +
      `<line x1="60" y1="84" x2="200" y2="84" stroke="#dc3c3c" stroke-width="2" stroke-dasharray="6 4"/><circle cx="60" cy="84" r="4" fill="#dc3c3c"/><text x="206" y="88" fill="var(--muted)" font-size="11" font-family="monospace">beam</text>` + arrow(90, 104, 90, 64),
    `<rect x="100" y="14" width="40" height="40" rx="6" fill="var(--panel)" stroke="var(--wire)"/><rect x="113" y="54" width="14" height="8" fill="var(--wire)"/><path d="M117 62 h6 v38 l-3 7 l-3 -7z" fill="var(--lilac)"/>` +
      `<line x1="150" y1="62" x2="170" y2="62" stroke="var(--muted)"/><line x1="150" y1="107" x2="170" y2="107" stroke="var(--muted)"/>` + arrow(162, 84, 162, 66) + arrow(162, 84, 162, 103) +
      `<text x="176" y="88" fill="var(--node)" font-size="12" font-family="monospace">31.5 mm</text>`,
    `<rect x="30" y="30" width="200" height="70" fill="var(--canvas2)" stroke="var(--wire)"/><rect x="50" y="42" width="160" height="46" fill="#5c4a3a" stroke="var(--lilac)"/>` +
      [[46, 38], [214, 38], [46, 92], [214, 92]].map(([x, y]) => `<rect x="${x - 7}" y="${y - 5}" width="14" height="10" rx="2" fill="#3c82dc"/>`).join(""),
    `<path d="M100 14 h44 l16 16 v76 h-60z" fill="var(--input)" stroke="var(--lilac)" stroke-width="1.5"/><path d="M144 14 v16 h16" fill="none" stroke="var(--lilac)" stroke-width="1.5"/>` +
      `<text x="130" y="66" text-anchor="middle" fill="var(--node)" font-size="12" font-family="monospace">.nc</text>` + arrow(190, 60, 166, 60)
  ][i];
  return `<div class="art"><svg viewBox="0 0 260 120" role="img" aria-label="${STEPS[i]}">${defs}${art}</svg></div>`;
}

// Belt lengths as labels on the belts: lengths = [tl, tr, bl, br] in mm, null where unknown. Each number lies along
// its belt, on the line, turned to read left to right, with a halo in the background colour instead of a box.
function beltLabels(lengths, target) {
  const anchors = [[30, 30], [380, 30], [30, 240], [380, 240]];
  return anchors.map(([ax, ay], i) => {
    const v = lengths[i];
    const text = v == null ? "?" : `${v.toFixed(1)} mm`;
    // Along the belt from its anchor toward the sled (or the frame's centre when the sled isn't drawn), far enough
    // in that the left-hand labels clear the log overlay
    const t = target.onBelt ? 0.65 : 0.6;
    const x = ax + (target.x - ax) * t, y = ay + (target.y - ay) * t;
    let angle = Math.atan2(target.y - ay, target.x - ax) * 180 / Math.PI;
    if (angle > 90) angle -= 180;
    if (angle < -90) angle += 180;
    return `<text x="${x.toFixed(1)}" y="${y.toFixed(1)}" transform="rotate(${angle.toFixed(1)} ${x.toFixed(1)} ${y.toFixed(1)})" ` +
      `text-anchor="middle" dominant-baseline="central" fill="var(--text)" stroke="var(--panel)" stroke-width="4" ` +
      `stroke-linejoin="round" paint-order="stroke" font-family="var(--font-mono)" font-size="11">${text}</text>`;
  }).join("");
}

// Motor current as a ring around each anchor, like the current UI's dials (tablet.js, debugging.js): the ring fills
// toward MAX_CURRENT_MA, coloured by how full it is. currents = [tl, tr, bl, br] in mA, null where there's no reading.
const MAX_CURRENT_MA = 2200;
const RING_R = 12, RING_C = 2 * Math.PI * RING_R;
const currentColour = (pct) => pct > 75 ? "#F44336" : pct > 50 ? "#FF9800" : pct > 25 ? "#2196F3" : "#4CAF50";
function currentRings(currents) {
  const names = ["Top left", "Top right", "Bottom left", "Bottom right"];
  return [[30, 30], [380, 30], [30, 240], [380, 240]].map(([x, y], i) => {
    const mA = currents[i];
    const pct = mA == null ? 0 : Math.min(100, (mA / MAX_CURRENT_MA) * 100);
    const title = mA == null ? `${names[i]} motor: no current reading` : `${names[i]} motor: ${Math.round(mA)} mA`;
    return `<g><title>${title}</title>` +
      `<circle cx="${x}" cy="${y}" r="${RING_R}" fill="none" stroke="var(--line2)" stroke-width="3"/>` +
      `<circle cx="${x}" cy="${y}" r="${RING_R}" fill="none" stroke="${currentColour(pct)}" stroke-width="3" stroke-linecap="round" ` +
      `stroke-dasharray="${RING_C.toFixed(2)}" stroke-dashoffset="${(RING_C * (1 - pct / 100)).toFixed(2)}" transform="rotate(-90 ${x} ${y})"/></g>`;
  }).join("");
}

// The workspace (webui/src/accuracy.js) as a dotted outline: the largest rectangle around the centre where cuts
// land within the tolerance. The job's outline is drawn over it, orange and dashed where it leaves, and one line
// under the frame says whether the job fits. Blue and orange are the Okabe-Ito pair, readable with colour blindness.
// a: { tol, rect: { w, h } | null, job: checkJob() result | null, note: string | null (shown instead),
//      hanging: the sled's mass in kg on a vertical frame, else null }
function accuracyOverlay(a, X, Y) {
  const OK = "#56b4e9", BAD = "#e69f00";
  // right-aligned under the frame, clear of the log, which sits over the drawing's left side
  const msg = (t, c = "var(--muted)") => `<text x="380" y="258" text-anchor="end" fill="${c}" stroke="var(--panel)" stroke-width="3" ` +
    `stroke-linejoin="round" paint-order="stroke" font-family="var(--font-mono)" font-size="9">${t}</text>`;
  if (a.note) return msg(a.note);
  const r = a.rect, rect = r && r.w ? `<rect x="${X(-r.w / 2).toFixed(1)}" y="${Y(r.h / 2).toFixed(1)}" width="${(X(r.w / 2) - X(-r.w / 2)).toFixed(1)}" ` +
    `height="${(Y(-r.h / 2) - Y(r.h / 2)).toFixed(1)}" fill="${OK}" fill-opacity=".06" stroke="${OK}" stroke-width="1.5" stroke-dasharray="2 4"><title>Cuts land within ±${a.tol} mm here (estimate)</title></rect>` : "";
  const area = r && r.w ? `${Math.round(r.w)} × ${Math.round(r.h)} mm` : "none";
  if (!a.job) return rect + msg(`Workspace at ±${a.tol} mm: ${area} (${a.hanging ? `est., ${a.hanging} kg sled` : "estimate"})`);
  const seg = (p) => p.points.map((q, i) => `${i ? "L" : "M"}${X(q.x).toFixed(1)} ${Y(q.y).toFixed(1)}`).join("");
  const path = (ok) => a.job.pieces.filter((p) => p.ok === ok).map(seg).join("");
  const job = `<path d="${path(true)}" fill="none" stroke="${OK}" stroke-width="1.5"/>` +
    `<path d="${path(false)}" fill="none" stroke="${BAD}" stroke-width="2.5" stroke-dasharray="5 3"/>`;
  const fits = a.job.outside === 0;
  const worst = Number.isFinite(a.job.worst) ? `worst ±${a.job.worst.toFixed(1)} mm` : "past the frame";
  const some = a.job.pieces.some((p) => p.ok) ? "partly " : "";
  return rect + job + msg(fits ? `Job fits the ±${a.tol} mm workspace` : `Job ${some}outside ±${a.tol} mm (${worst})`, fits ? OK : BAD);
}

// lengths: when given, each belt shows its live length (the Belts toggle).
// currents: when given, each motor shows its current as a ring at its anchor (the Motors toggle).
// accuracy: when given, the workspace and the job check (the Workspace toggle), see accuracyOverlay.
// frameSize: { w, h } in mm when the anchors are known; the drawn anchors then stand for the real ones, so the sled
// and the overlay are to scale. Without it the drawing assumes 4 mm per unit.
// Machine coordinates (mm, 0 0 at the frame centre) to drawing units
export function frameMap(frameSize = null) {
  const sx = frameSize ? 350 / frameSize.w : 1 / 4, sy = frameSize ? 210 / frameSize.h : 1 / 4, cy = frameSize ? 135 : 140;
  return { mapX: (x) => 205 + x * sx, mapY: (y) => cy - y * sy };
}

// The chosen file's cutting moves (readGcode() moves, work coordinates) as SVG paths, moved by the work offset.
// Rapids and G53 moves are left out. Moves on lines up to `done` (the running job's line) are drawn as cut, the rest
// fainter; done = null draws the whole file the same. Points closer than half a drawing unit are merged, so a big
// file stays a few thousand points.
export function toolpathSvg(moves, wco, X, Y, done = null) {
  const parts = { cut: [], todo: [] };
  let last = null, lastKey = null, px = 0, py = 0;
  for (const mv of moves) {
    if (mv.rapid || mv.machine) { last = null; continue; }
    const key = done != null && mv.line > done ? "todo" : "cut";
    const pts = mv.arc ? arcPoints(mv, 2) : [mv.from, mv.to];
    const d = parts[key];
    // A move that carries on from the previous one in the same style continues its path
    const goesOn = last && key === lastKey && last.x === mv.from.x && last.y === mv.from.y;
    pts.forEach((p, i) => {
      const x = X(p.x + wco.x), y = Y(p.y + wco.y);
      if (i === 0) { if (!goesOn) { d.push(`M${x.toFixed(1)} ${y.toFixed(1)}`); px = x; py = y; } return; }
      if (Math.abs(x - px) < 0.5 && Math.abs(y - py) < 0.5) return;
      d.push(`L${x.toFixed(1)} ${y.toFixed(1)}`); px = x; py = y;
    });
    last = mv.to; lastKey = key;
  }
  const path = (d, attrs) => (d.length ? `<path d="${d.join("")}" fill="none" stroke-linejoin="round" ${attrs}/>` : "");
  return `<g><title>The chosen file's cutting moves</title>` +
    path(parts.todo, 'stroke="var(--muted)" stroke-opacity=".55" stroke-width="1"') +
    path(parts.cut, `stroke="var(--lilac)" stroke-width="${done != null ? 1.8 : 1.3}"`) + "</g>";
}

// toolpath: an SVG string from toolpathSvg (drawn with frameMap(frameSize)), under the workspace overlay.
export function visSvg(k, s, pos = { x: 0, y: 0 }, home = { x: 0, y: 0 }, lengths = null, currents = null, accuracy = null, frameSize = null, toolpath = "") {
  // The sled follows the machine position, scaled to the drawing. The cross marks the user's home (work 0, 0);
  // it's drawn over the sled and reaches past it, so it still shows when the sled sits on home.
  const { mapX, mapY } = frameMap(frameSize);
  const toX = (x) => Math.max(40, Math.min(370, mapX(x))), toY = (y) => Math.max(40, Math.min(230, mapY(y)));
  const sledX = toX(pos.x), sledY = toY(pos.y), hx = toX(home.x), hy = toY(home.y), unk = s.nopos && !s.moving;
  const loose = s.nopos && s.moving;
  const belts = [[30, 30], [380, 30], [30, 240], [380, 240]].map(([x, y]) =>
    `<line x1="${x}" y1="${y}" x2="${sledX}" y2="${sledY}" stroke="${loose ? "var(--setup)" : "#8f8a97"}" stroke-width="1.4" ${loose ? 'stroke-dasharray="5 4"' : ""}/>`).join("");
  const anchors = [[30, 30], [380, 30], [30, 240], [380, 240]].map(([x, y]) => `<circle cx="${x}" cy="${y}" r="6" fill="var(--node)"/>`).join("");
  return `<svg viewBox="0 0 410 270" role="img" aria-label="Device frame with the sled and toolpath" class="${s.stale ? "stale" : ""}">
    <rect x="30" y="30" width="350" height="210" fill="none" stroke="var(--line2)" stroke-dasharray="3 5"/>
    ${toolpath}
    ${accuracy ? accuracyOverlay(accuracy, mapX, mapY) : ""}
    ${unk ? "" : belts}${anchors}${currents ? currentRings(currents) : ""}
    ${unk ? `<text x="205" y="62" text-anchor="middle" fill="var(--muted)" font-family="var(--font-mono)" font-size="12">Sled position unknown</text>` : ""}
    <rect ${unk ? 'opacity="0"' : ""} x="${sledX - 13}" y="${sledY - 13}" width="26" height="26" rx="6" fill="var(--panel)" stroke="var(--lilac)" stroke-width="2"/>
    ${unk ? "" : `<circle cx="${sledX}" cy="${sledY}" r="3" fill="var(--lilac)"/>`}
    ${unk ? "" : `<g stroke="var(--ready)" stroke-width="1.6"><title>Your home (work 0, 0)</title><line x1="${hx - 20}" y1="${hy}" x2="${hx + 20}" y2="${hy}"/><line x1="${hx}" y1="${hy - 20}" x2="${hx}" y2="${hy + 20}"/></g>`}
    ${lengths ? beltLabels(lengths, unk ? { x: 205, y: 135, onBelt: false } : { x: sledX, y: sledY, onBelt: true }) : ""}</svg>`;
}
