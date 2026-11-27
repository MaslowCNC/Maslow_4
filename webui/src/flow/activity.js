import { SCREENS } from "./screens.js";

// What's running now, for the activity indicator in the top bar (and the phone's mode bar). It comes from the machine's
// state (the screen, the report's TS), so every connection shows it, not just the one that pressed the button. Only
// this page's own file transfers (uploading, reading a file) come from the page, since the firmware doesn't report them.
// null when nothing runs, else { label, frac, detail }: frac is 0..1 when the length is known (a bar), null when it
// isn't (a spinner).

// Z moves on the spindle board, from TS, shown wherever they start (the jog pad, another connection)
const TOOL_BUSY = { checking: "Homing Z", removing: "Removing the tool", loading: "Loading the tool" };
const frac = (n, total) => total > 0 ? Math.max(0, Math.min(1, n / total)) : null;

export function activity(k, d) {
  if (!d.connected) return null;
  if (d.upload) return { label: `Uploading ${d.upload.file}`, frac: frac(d.upload.loaded, d.upload.total), detail: null };
  const s = SCREENS[k];
  // A running job has its own progress (the File card): only its other moves (parking after, a spindle fault) show here
  if (s.moving && k !== "cutting") {
    // Find anchors' point n of total, once the firmware reports it (d.calPoint: { n, total }; not reported yet)
    const p = k === "anchors" ? d.calPoint : null;
    return { label: s.sub, frac: p ? frac(p.n, p.total) : null, detail: p?.total ? `point ${p.n} of ${p.total}` : null };
  }
  if (TOOL_BUSY[d.tool] && s.mode !== "Job") return { label: TOOL_BUSY[d.tool], frac: null, detail: null };
  const g = d.gcode;
  if (g?.status === "reading") return { label: `Reading ${g.file}`, frac: frac(g.loaded, g.total), detail: null };
  return null;
}
