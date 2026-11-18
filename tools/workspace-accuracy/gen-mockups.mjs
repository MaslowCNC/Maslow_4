// Builds mockups.html: three ways to show the accuracy map on the new UI's frame drawing (webui/src/art.js,
// anchors at (30,30)...(380,240) in a 410 x 270 viewBox). Data comes from accuracy-model.mjs on a 3000 x 2100 frame.
// Run: node tools/workspace-accuracy/gen-mockups.mjs  (writes mockups.html next to this file)
import { writeFileSync } from "node:fs";
import { errorMap, largestRect } from "./accuracy-model.mjs";

const frameArgs = { tl: [0, 2100], tr: [3000, 2100], br: [3000, 0], bl: [0, 0], z: [100, 56, 34, 78], spoil: 19 };
const { tl, tr, br, z, spoil } = frameArgs;
// inline makeFrame equivalent (kept in the model file); re-create here to stay standalone
const f = { anchors: [tl, tr, [0, 0], br], z: z.map((v) => v + spoil), center: [1500, 1050], width: 3000, height: 2100 };

const data = {};
for (const [k, o] of Object.entries({ auto: {}, wide: { grid: { widthFrac: 0.8, heightFrac: 0.6 } } })) {
  const m = errorMap(f, { ...o, step: 50 });
  data[k] = {
    xs: m.xs, ys: m.ys, n: m.grid.n, gw: Math.round(m.grid.gw), gh: Math.round(m.grid.gh),
    e: m.cells.map((r) => r.map((c) => +c.e95.toFixed(3))),
    b: m.cells.map((r) => r.map((c) => +c.biasMm.toFixed(3))),
    s: m.cells.map((r) => r.map((c) => +(2.45 * c.sigma).toFixed(3))),
    rects: Object.fromEntries([0.5, 1, 2].map((t) => [t, largestRect(m, t, true)])),
  };
}

const html = `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Workspace accuracy mockups</title>
<style>
:root{color-scheme:dark;--page:#2b2e33;--canvas:#3f4243;--panel:#232832;--sep:#31343b;--input:#242834;--text:#e0e5ef;--muted:#aaa4b2;
--lilac:#c4a3d5;--node:#e0e5ef;--line2:#545252;--ok:#56b4e9;--bad:#e69f00;--font-sans:"Work Sans",system-ui,sans-serif;--font-mono:ui-monospace,SFMono-Regular,Menlo,monospace}
*{box-sizing:border-box}body{margin:0;background:var(--page);color:var(--text);font:14px/1.45 var(--font-sans);padding:16px}
h1{font-size:20px;margin:0 0 4px}p{margin:0 0 8px;color:var(--muted);max-width:75ch}
.bar{display:flex;flex-wrap:wrap;gap:16px;align-items:center;margin:12px 0 16px}
.seg{display:inline-flex;border:1px solid var(--sep);border-radius:8px;overflow:hidden}
.seg button{font:600 13px var(--font-mono);color:var(--text);background:var(--panel);border:0;padding:6px 10px;cursor:pointer}
.seg button[aria-pressed=true]{background:var(--lilac);color:var(--panel)}
.seg button:focus-visible{outline:2px solid var(--ok);outline-offset:-2px}
.lbl{font-size:12px;font-weight:600;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);margin-right:6px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,420px),1fr));gap:16px}
.card{background:var(--panel);border-radius:12px;padding:12px}
.card h2{font-size:15px;margin:0 0 2px}.card p{font-size:13px}
.screen{background:var(--canvas);border-radius:8px;padding:6px}
svg{display:block;width:100%;height:auto}
.readout{font:12px var(--font-mono);color:var(--muted);min-height:2.6em;margin-top:6px}
.legend{display:flex;align-items:center;gap:6px;font:11px var(--font-mono);color:var(--muted);margin-top:6px}
.legend i{display:block;height:10px;flex:1;border-radius:3px}
.chip{display:inline-block;font:600 12px var(--font-mono);padding:3px 8px;border-radius:6px;margin-top:6px}
</style></head><body>
<h1>Workspace accuracy: display options</h1>
<p>Expected cut error (95%, mm, relative to the frame centre) on a 3000 × 2100 mm frame, from <code>accuracy-model.mjs</code>:
calibration fit RMS 0.5 mm, belt noise 0.1 mm, spoil board 3 mm off, belt scale 0.05%. Switch the calibration grid to see how much it matters.</p>
<div class="bar">
  <div><span class="lbl">Tolerance</span><span class="seg" id="tol"><button data-v="0.5">±0.5</button><button data-v="1" aria-pressed="true">±1</button><button data-v="2">±2 mm</button></span></div>
  <div><span class="lbl">Calibration grid</span><span class="seg" id="grid"><button data-v="auto" aria-pressed="true">auto (${data.auto.n}×${data.auto.n}, ${data.auto.gw}×${data.auto.gh})</button><button data-v="wide">work area (${data.wide.n}×${data.wide.n}, ${data.wide.gw}×${data.wide.gh})</button></span></div>
</div>
<div class="grid">
  <section class="card"><h2>A. Tolerance contour + safe rectangle</h2>
    <p>One line where the error crosses the chosen tolerance, and the largest centred rectangle inside it (a work-area suggestion). The job outline turns orange and dashed where it leaves.</p>
    <div class="screen"><svg id="a" viewBox="0 0 410 270"></svg></div><div class="readout" id="ra"></div></section>
  <section class="card"><h2>B. Error heatmap in mm + cursor readout</h2>
    <p>Smooth map in a colour-blind-safe sequential scale (cividis), with the tolerance as a contour. Hover for the error at that point and what it's made of.</p>
    <div class="screen"><svg id="b" viewBox="0 0 410 270"></svg></div>
    <div class="legend" id="lg"></div><div class="readout" id="rb">Hover the frame</div></section>
  <section class="card"><h2>C. Job check only</h2>
    <p>No map at all: the safe rectangle as a faint outline, and a sentence about the loaded job. Least to read, fits the Run screen.</p>
    <div class="screen"><svg id="c" viewBox="0 0 410 270"></svg></div><div id="rc"></div></section>
</div>
<script>
const D = ${JSON.stringify(data)};
const W = 3000, H = 2100, toX = (x) => 30 + (350 * x) / W, toY = (y) => 240 - (210 * y) / H;
const fromX = (px) => ((px - 30) * W) / 350, fromY = (py) => ((240 - py) * H) / 210;
const job = { x: 1500 + 250, y: 1050 + 150, w: 1100, h: 700 }; // a job placed right of centre, frame mm (centre of job)
let tol = 1, g = "auto";
// cividis stops (colour-blind safe, monotone lightness)
const CIV = [[0,32,77],[39,64,107],[87,92,109],[124,123,120],[166,157,117],[210,193,98],[255,234,70]];
const civ = (t) => { t = Math.max(0, Math.min(1, t)) * (CIV.length - 1); const i = Math.min(CIV.length - 2, Math.floor(t)), u = t - i;
  return CIV[i].map((v, k) => Math.round(v + (CIV[i + 1][k] - v) * u)); };
const EMAX = 3;
function sample(d, x, y) { // bilinear on cell centres
  const s = 50, fx = (x - d.xs[0]) / s, fy = (y - d.ys[0]) / s, i = Math.max(0, Math.min(d.xs.length - 2, Math.floor(fx))), j = Math.max(0, Math.min(d.ys.length - 2, Math.floor(fy)));
  const u = Math.max(0, Math.min(1, fx - i)), v = Math.max(0, Math.min(1, fy - j)), L = (k) => (d[k][j][i] * (1 - u) + d[k][j][i + 1] * u) * (1 - v) + (d[k][j + 1][i] * (1 - u) + d[k][j + 1][i + 1] * u) * v;
  return { e: L("e"), b: L("b"), s: L("s") };
}
function contour(d, t) { // marching squares on cell centres -> svg path
  let p = ""; const { xs, ys, e } = d, lerp = (a, b, va, vb) => a + ((t - va) / (vb - va)) * (b - a);
  for (let j = 0; j < ys.length - 1; j++) for (let i = 0; i < xs.length - 1; i++) {
    const v = [e[j][i], e[j][i + 1], e[j + 1][i + 1], e[j + 1][i]], c = [[xs[i], ys[j]], [xs[i + 1], ys[j]], [xs[i + 1], ys[j + 1]], [xs[i], ys[j + 1]]];
    const pts = []; for (let k = 0; k < 4; k++) { const a = k, b = (k + 1) % 4; if ((v[a] > t) !== (v[b] > t)) pts.push([lerp(c[a][0], c[b][0], v[a], v[b]), lerp(c[a][1], c[b][1], v[a], v[b])]); }
    for (let k = 0; k + 1 < pts.length; k += 2) p += "M" + toX(pts[k][0]).toFixed(1) + " " + toY(pts[k][1]).toFixed(1) + "L" + toX(pts[k + 1][0]).toFixed(1) + " " + toY(pts[k + 1][1]).toFixed(1);
  }
  return p;
}
function heat(d) {
  const c = document.createElement("canvas"); c.width = 350; c.height = 210; const x = c.getContext("2d"), im = x.createImageData(350, 210);
  for (let py = 0; py < 210; py++) for (let px = 0; px < 350; px++) { const s = sample(d, (px * W) / 350, H - (py * H) / 210), [r, gg, b] = civ(s.e / EMAX), o = (py * 350 + px) * 4;
    im.data[o] = r; im.data[o + 1] = gg; im.data[o + 2] = b; im.data[o + 3] = 255; }
  x.putImageData(im, 0, 0); return c.toDataURL();
}
const frame = () => '<rect x="30" y="30" width="350" height="210" fill="none" stroke="var(--line2)" stroke-dasharray="3 5"/>';
const anchors = () => [[30,30],[380,30],[30,240],[380,240]].map(([x,y]) => '<circle cx="'+x+'" cy="'+y+'" r="6" fill="var(--node)"/>').join("");
const gridDots = (d) => { let s = ""; const n = d.n, h = (n - 1) / 2; for (let i = -h; i <= h; i++) for (let j = -h; j <= h; j++) s += '<circle cx="'+toX(1500 + i * d.gw / (n - 1))+'" cy="'+toY(1050 + j * d.gh / (n - 1))+'" r="1.6" fill="var(--muted)"/>'; return s; };
function rectSvg(r, fill) { if (!r.w) return ""; return '<rect x="'+toX(1500 - r.w / 2)+'" y="'+toY(1050 + r.h / 2)+'" width="'+(350 * r.w / W)+'" height="'+(210 * r.h / H)+'" '+fill+'/>'; }
function jobSvg(d, t) { // job outline split into in/out pieces along its edges
  const x0 = job.x - job.w / 2, x1 = job.x + job.w / 2, y0 = job.y - job.h / 2, y1 = job.y + job.h / 2, pts = [];
  const edges = [[x0,y0,x1,y0],[x1,y0,x1,y1],[x1,y1,x0,y1],[x0,y1,x0,y0]]; let ok = "", bad = "", worst = 0, outLen = 0;
  for (const [ax, ay, bx, by] of edges) { const n = 60; for (let k = 0; k < n; k++) { const u = k / n, v = (k + 1) / n, sx = ax + (bx - ax) * u, sy = ay + (by - ay) * u, ex = ax + (bx - ax) * v, ey = ay + (by - ay) * v;
    const e = sample(d, (sx + ex) / 2, (sy + ey) / 2).e; worst = Math.max(worst, e); const seg = "M"+toX(sx).toFixed(1)+" "+toY(sy).toFixed(1)+"L"+toX(ex).toFixed(1)+" "+toY(ey).toFixed(1);
    if (e > t) { bad += seg; outLen += Math.hypot(ex - sx, ey - sy); } else ok += seg; } }
  return { svg: '<path d="'+ok+'" stroke="var(--lilac)" stroke-width="2.5" fill="none"/><path d="'+bad+'" stroke="var(--bad)" stroke-width="3" stroke-dasharray="5 3" fill="none"/>', worst, outLen };
}
function render() {
  const d = D[g], r = d.rects[tol], c = contour(d, tol), j = jobSvg(d, tol), rlab = r.w ? Math.round(r.w) + " × " + Math.round(r.h) + " mm" : "none";
  document.getElementById("a").innerHTML = frame() + rectSvg(r, 'fill="var(--ok)" fill-opacity=".14" stroke="var(--ok)" stroke-width="1.5"') +
    '<path d="'+c+'" stroke="var(--ok)" stroke-width="2" fill="none"/>' + gridDots(d) + j.svg + anchors() +
    '<text x="205" y="258" text-anchor="middle" fill="var(--muted)" font-family="var(--font-mono)" font-size="11">±'+tol+' mm area · suggested work area '+rlab+'</text>';
  document.getElementById("ra").textContent = "Dots: calibration points. Job worst point: ±" + j.worst.toFixed(1) + " mm.";
  document.getElementById("b").innerHTML = '<image href="'+heat(d)+'" x="30" y="30" width="350" height="210" preserveAspectRatio="none"/>' +
    '<path d="'+c+'" stroke="#fff" stroke-width="1.5" fill="none" stroke-dasharray="4 3"/>' + gridDots(d) + anchors() +
    '<rect id="hit" x="30" y="30" width="350" height="210" fill="transparent"/><circle id="cur" r="4" fill="none" stroke="#fff" stroke-width="1.5" visibility="hidden"/>';
  const hit = document.getElementById("hit"), cur = document.getElementById("cur"), svg = document.getElementById("b");
  hit.onmousemove = (ev) => { const pt = svg.createSVGPoint(); pt.x = ev.clientX; pt.y = ev.clientY; const p = pt.matrixTransform(svg.getScreenCTM().inverse());
    const x = fromX(p.x), y = fromY(p.y), s = sample(d, x, y); cur.setAttribute("cx", p.x); cur.setAttribute("cy", p.y); cur.setAttribute("visibility", "visible");
    document.getElementById("rb").textContent = "X " + Math.round(x - 1500) + "  Y " + Math.round(y - 1050) + " mm: ±" + s.e.toFixed(2) + " mm (systematic " + s.b.toFixed(2) + " + random " + s.s.toFixed(2) + ")"; };
  hit.onmouseleave = () => { cur.setAttribute("visibility", "hidden"); };
  document.getElementById("c").innerHTML = frame() + rectSvg(r, 'fill="none" stroke="var(--ok)" stroke-width="1.5" stroke-dasharray="2 4"') + j.svg + anchors();
  const fits = j.outLen === 0;
  document.getElementById("rc").innerHTML = '<span class="chip" style="background:' + (fits ? "var(--ok)" : "var(--bad)") + ';color:var(--panel)">' +
    (fits ? "Job fits the ±" + tol + " mm area" : Math.round(j.outLen) + " mm of the outline is outside ±" + tol + " mm (worst ±" + j.worst.toFixed(1) + " mm)") + "</span>";
}
const lg = document.getElementById("lg"); lg.innerHTML = "0 mm<i style=\\"background:linear-gradient(90deg," + [0,.17,.33,.5,.67,.83,1].map((t) => "rgb(" + civ(t) + ")").join(",") + ")\\"></i>" + EMAX + "+ mm";
for (const [id, set] of [["tol", (v) => (tol = +v)], ["grid", (v) => (g = v)]]) document.getElementById(id).onclick = (ev) => {
  const b = ev.target.closest("button"); if (!b) return; set(b.dataset.v); for (const o of b.parentNode.children) o.setAttribute("aria-pressed", o === b); render(); };
render();
</script></body></html>`;
writeFileSync(new URL("./mockups.html", import.meta.url), html);
console.log("wrote mockups.html", Object.fromEntries(Object.entries(data).map(([k, v]) => [k, v.rects])));
