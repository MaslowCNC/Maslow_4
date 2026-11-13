import { STEPS } from "./flow/screens.js";

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

export function visSvg(k, s, pos = { x: 0, y: 0 }, home = { x: 0, y: 0 }) {
  // The sled follows the machine position, scaled to the drawing. The cross marks the user's home (work 0, 0);
  // it's drawn over the sled and reaches past it, so it still shows when the sled sits on home.
  const toX = (x) => Math.max(40, Math.min(370, 205 + x / 4)), toY = (y) => Math.max(40, Math.min(230, 140 - y / 4));
  const sledX = toX(pos.x), sledY = toY(pos.y), hx = toX(home.x), hy = toY(home.y), unk = s.nopos && !s.moving, done = ["cutting", "stopped", "spindle"].includes(k);
  const loose = s.nopos && s.moving;
  const belts = [[30, 30], [380, 30], [30, 240], [380, 240]].map(([x, y]) =>
    `<line x1="${x}" y1="${y}" x2="${sledX}" y2="${sledY}" stroke="${loose ? "var(--setup)" : "#8f8a97"}" stroke-width="1.4" ${loose ? 'stroke-dasharray="5 4"' : ""}/>`).join("");
  const anchors = [[30, 30], [380, 30], [30, 240], [380, 240]].map(([x, y]) => `<circle cx="${x}" cy="${y}" r="6" fill="var(--node)"/>`).join("");
  const path = "M130 90 H280 V190 H130 Z M170 120 H240 V160 H170 Z";
  return `<svg viewBox="0 0 410 270" role="img" aria-label="Device frame with the sled and toolpath" class="${s.stale ? "stale" : ""}">
    <rect x="30" y="30" width="350" height="210" fill="none" stroke="var(--line2)" stroke-dasharray="3 5"/>
    <rect x="110" y="72" width="190" height="136" fill="var(--input)" stroke="var(--line2)"/>
    <path d="${path}" fill="none" stroke="var(--line2)" stroke-width="2"/>
    ${done ? `<path d="M130 90 H280 V190 H210" fill="none" stroke="var(--lilac)" stroke-width="3"/>` : ""}
    ${k === "success" || k === "parking" ? `<path d="${path}" fill="none" stroke="var(--lilac)" stroke-width="3"/>` : ""}
    ${k === "tracing" ? `<path d="M126 86 H284 V194 H200" fill="none" stroke="var(--ready)" stroke-width="2.5" stroke-dasharray="6 4"/>` : ""}
    ${unk ? "" : belts}${anchors}
    ${unk ? `<text x="205" y="62" text-anchor="middle" fill="var(--muted)" font-family="var(--font-mono)" font-size="12">Sled position unknown</text>` : ""}
    <rect ${unk ? 'opacity="0"' : ""} x="${sledX - 13}" y="${sledY - 13}" width="26" height="26" rx="6" fill="var(--panel)" stroke="var(--lilac)" stroke-width="2"/>
    ${unk ? "" : `<circle cx="${sledX}" cy="${sledY}" r="3" fill="var(--lilac)"/>`}
    ${unk ? "" : `<g stroke="var(--ready)" stroke-width="1.6"><title>Your home (work 0, 0)</title><line x1="${hx - 20}" y1="${hy}" x2="${hx + 20}" y2="${hy}"/><line x1="${hx}" y1="${hy - 20}" x2="${hx}" y2="${hy + 20}"/></g>`}</svg>`;
}
