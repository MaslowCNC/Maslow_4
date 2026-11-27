// Does the chosen job fit? Its extent (work coordinates, from the G-code reader) is placed at the user's home (WCO)
// and compared, in machine coordinates, with:
//   - the work area: Settings › Frame and belts (0 = automatic, 80 % × 60 % of the frame), centred on the frame's
//     centre plus the centre offset, as the current UI's getWorkAreaBounds() does;
//   - the frame: the rectangle inside the four anchors, centred on machine 0, 0. Past it the sled can't go.
// Plain JS, no UI code. Tests: fit.test.js.

import { effectiveWorkArea } from "../device/settings.js";
import { fmt, fmtU } from "../units.js";

// Rounding slack, as the current UI's WORK_AREA_BOUNDS_EPSILON_MM
const EPS = 0.5;

// The rectangle inside the anchors, in machine coordinates (machine 0, 0 is the middle of the anchors)
export function frameBounds(frame) {
  if (!frame) return null;
  const xs = [frame.tl[0], frame.tr[0], frame.bl[0], frame.br[0]], ys = [frame.tl[1], frame.tr[1], frame.bl[1], frame.br[1]];
  const cx = (Math.min(...xs) + Math.max(...xs)) / 2, cy = (Math.min(...ys) + Math.max(...ys)) / 2;
  return {
    minX: Math.max(frame.tl[0], frame.bl[0]) - cx, maxX: Math.min(frame.tr[0], frame.br[0]) - cx,
    minY: Math.max(frame.bl[1], frame.br[1]) - cy, maxY: Math.min(frame.tl[1], frame.tr[1]) - cy,
  };
}

export function workAreaBounds(settings = {}, frame) {
  const a = effectiveWorkArea({ workW: settings.workW ?? 0, workH: settings.workH ?? 0 }, frame);
  if (!(a.w > 0) || !(a.h > 0)) return null;
  const ox = settings.offX ?? 0, oy = settings.offY ?? 0;
  return { minX: ox - a.w / 2, maxX: ox + a.w / 2, minY: oy - a.h / 2, maxY: oy + a.h / 2 };
}

const inside = (j, b) => j.minX >= b.minX - EPS && j.maxX <= b.maxX + EPS && j.minY >= b.minY - EPS && j.maxY <= b.maxY + EPS;

// How far the job goes past b, in mm, on its worst side
const past = (j, b) => Math.max(b.minX - j.minX, j.maxX - b.maxX, b.minY - j.minY, j.maxY - b.maxY, 0);

// extent: { min: {x, y}, max: {x, y} } in work coordinates; wco: the home's machine position.
// Returns null when there's nothing to check, else { level, text, job }:
//   level "ok": inside the work area; "outside": outside the work area but inside the frame (allowed, warned);
//   "pastFrame": past the frame, where the machine can't go (Run is blocked); "unknown": no anchors yet.
export function jobFit(extent, wco, settings, frame, unit = "mm") {
  if (!extent || !wco) return null;
  const job = { minX: extent.min.x + wco.x, maxX: extent.max.x + wco.x, minY: extent.min.y + wco.y, maxY: extent.max.y + wco.y };
  const fb = frameBounds(frame);
  if (!fb) return { level: "unknown", text: "", job };
  if (!inside(job, fb)) {
    return { level: "pastFrame", job,
      text: `The job goes ${fmtU(past(job, fb), unit, "size")} past the frame, where the machine can't reach.` };
  }
  const wa = workAreaBounds(settings, frame);
  if (wa && !inside(job, wa)) {
    return { level: "outside", job,
      text: `The job goes ${fmtU(past(job, wa), unit, "size")} outside the work area, where cuts are less accurate.` };
  }
  return { level: "ok", text: "", job };
}

// Where the job sits from home (work coordinates), for the File card: "X −150 to 150, Y −100 to 100"
export function jobPlacement(extent, unit = "mm") {
  if (!extent) return "";
  const n = (v) => fmt(v, unit, "size").replace("-", "−");
  return `X ${n(extent.min.x)} to ${n(extent.max.x)}, Y ${n(extent.min.y)} to ${n(extent.max.y)}`;
}
