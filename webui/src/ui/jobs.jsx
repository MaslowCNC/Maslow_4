import { useState, useEffect, useRef } from "preact/hooks";
import { MOTORS, COLUMNS, KEEP_JOBS, outcomeLabel, csvText, csvFileName, logText, logFileName } from "../device/jobrecord.js";
import "./jobs.css";

// Settings › Jobs: the jobs this browser recorded (device/jobrecord.js), newest first, each with when it ran, how
// long, and how it ended. A row opens to its motor current chart and a CSV download. A job history will build on it.

const C = Object.fromEntries(COLUMNS.map((c, i) => [c, i]));
const pad = (n) => String(n).padStart(2, "0");
const dur = (s) => (s >= 3600 ? `${Math.floor(s / 3600)}:${pad(Math.floor(s / 60) % 60)}:${pad(s % 60)}` : `${Math.floor(s / 60)}:${pad(s % 60)}`);
const when = (ms) => new Date(ms).toLocaleString([], { month: "short", day: "numeric", hour: "2-digit", minute: "2-digit" });
const lengthOf = (r) => Math.round(r.end ? (r.end - r.start) / 1000 : r.rows.length ? r.rows.at(-1)[C.t] : 0);
// How the row marks the outcome: done (finished), cut short (stopped, page closed), failed, or still running
const TONE = { finished: "ok", stopped: "warn", incomplete: "warn", error: "bad", spindleFault: "bad", restarted: "bad" };
const short = (o) => (!o ? "Running" : o.kind === "error" ? `Error${o.line ? ` at line ${o.line.toLocaleString()}` : ""}` : outcomeLabel(o).replace("Recording ended when this page closed", "Page closed"));

// Hand the browser a file to save, as Save log does (ui/logfile.js)
function download(name, text, type = "text/csv") {
  const url = URL.createObjectURL(new Blob([text], { type: `${type};charset=utf-8` }));
  const a = Object.assign(document.createElement("a"), { href: url, download: name });
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export function JobsSection({ recorder }) {
  const [, setTick] = useState(0);
  const [open, setOpen] = useState(null);   // the id of the job whose chart is open
  useEffect(() => recorder.subscribe(() => setTick((n) => n + 1)), [recorder]);
  const recs = recorder.records();
  return <>
    <h4>Jobs</h4>
    <p class="note">The last {KEEP_JOBS} jobs run with this page open, kept in this browser only{recorder.persistent ? "" : " (storage is blocked here, so they go when the page closes)"}.
      Open one to see its belt motor currents, or to download its log. A sleeping phone or a tab in the background leaves gaps.</p>
    {!recs.length ? <p class="note">No jobs yet. Run a job with this page open and it shows here.</p>
      : <ul class="joblist">{recs.map((r) => <JobRow rec={r} open={open === r.id} toggle={() => setOpen(open === r.id ? null : r.id)} />)}</ul>}
  </>;
}

function JobRow({ rec, open, toggle }) {
  const tone = rec.outcome ? TONE[rec.outcome.kind] || "warn" : "run";
  const id = `job-${rec.id.replace(/[^A-Za-z0-9-]/g, "-")}`;
  return <li class={`jobrow${open ? " open" : ""}`}>
    <button type="button" class="jobhead" aria-expanded={open} aria-controls={id} onClick={toggle}>
      <span class="jobfile">{rec.file}{rec.example && <small class="jobex">example</small>}</span>
      <span class="jobmeta">{when(rec.start)} · {dur(lengthOf(rec))}</span>
      <span class={`jobout ${tone}`}><i aria-hidden="true" />{short(rec.outcome)}</span>
      <svg class="chev" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><path d="M6 9l6 6 6-6" /></svg>
    </button>
    {open && <div class="jobbody" id={id}>
      <JobSummary rec={rec} />
      {rec.rows.length ? <CurrentChart rec={rec} /> : <p class="note">No readings yet{rec.outcome ? "" : ": they start with the next status report"}.</p>}
      <div class="jobdl">
        <button class="btn ghost" type="button" disabled={!rec.rows.length} onClick={() => download(csvFileName(rec), csvText(rec))}
          title="A spreadsheet of every second: time, file line, each motor's lowest, average and highest current, and belt errors">Download CSV</button>
        <button class="btn ghost" type="button" disabled={!rec.log?.length} onClick={() => download(logFileName(rec), logText(rec), "text/plain")}
          title={rec.log?.length ? `The ${rec.log.length.toLocaleString()} log lines from this job: the machine's messages and what this page sent` : "No log lines were recorded for this job"}>Download log</button>
      </div>
    </div>}
  </li>;
}

function JobSummary({ rec }) {
  const peak = MOTORS.map((m) => Math.max(0, ...rec.rows.map((r) => r[C[`${m}max`]])));
  const gaps = rec.rows.reduce((n, r, i) => n + (i && r[C.t] - rec.rows[i - 1][C.t] > 3 ? 1 : 0), 0);
  return <p class="jobsum">
    {rec.outcome?.detail && <span>{outcomeLabel(rec.outcome)}</span>}
    <span>Lines {rec.firstLine.toLocaleString()}–{rec.lastLine.toLocaleString()}{gaps ? ` · ${gaps} gap${gaps === 1 ? "" : "s"} in the recording` : ""}</span>
    <span>Peak current {MOTORS.map((m, i) => `${m} ${peak[i].toLocaleString()}`).join(" · ")} mA</span>
  </p>;
}

// The chart: one strip per motor on a shared time axis and a shared mA scale, the band from the lowest to the highest
// current in each second and the average as a line. Points are merged to about one per 2 px. A gap of more than 3 s
// (the page asleep or disconnected) breaks the line. Hover or touch shows the readout above.
const STRIP = 54, GAP = 8, AXIS = 34, LEFT = 34, RIGHT = 8;

function bins(rows, cols) {
  if (rows.length <= cols) return rows.map((r) => ({ t: r[C.t], line: r[C.line], rows: [r] }));
  const t0 = rows[0][C.t], span = Math.max(1, rows.at(-1)[C.t] - t0), out = [];
  for (const r of rows) {
    const i = Math.min(cols - 1, Math.floor(((r[C.t] - t0) / span) * cols));
    if (!out[i]) out[i] = { t: r[C.t], line: r[C.line], rows: [] };
    out[i].rows.push(r);
  }
  return out.filter(Boolean);
}

function CurrentChart({ rec }) {
  const box = useRef(null);
  const [w, setW] = useState(600);
  const [at, setAt] = useState(null);   // the hovered bin's index
  useEffect(() => {
    const el = box.current;
    if (!el || typeof ResizeObserver === "undefined") return;
    const ro = new ResizeObserver(([e]) => setW(Math.max(260, Math.round(e.contentRect.width))));
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const rows = rec.rows;
  const plotW = w - LEFT - RIGHT;
  const t0 = rows[0][C.t], t1 = Math.max(t0 + 1, rows.at(-1)[C.t]);
  const x = (t) => LEFT + ((t - t0) / (t1 - t0)) * plotW;
  const top = Math.max(500, ...rows.map((r) => Math.max(...MOTORS.map((m) => r[C[`${m}max`]]))));
  const yMax = Math.ceil(top / 500) * 500;
  const h = MOTORS.length * (STRIP + GAP) + AXIS;
  const bs = bins(rows, Math.max(20, Math.floor(plotW / 2))).map((b) => ({
    ...b,
    min: MOTORS.map((m) => Math.min(...b.rows.map((r) => r[C[`${m}min`]]))),
    max: MOTORS.map((m) => Math.max(...b.rows.map((r) => r[C[`${m}max`]]))),
    avg: MOTORS.map((m) => Math.round(b.rows.reduce((s, r) => s + r[C[`${m}avg`]] * r[C.n], 0) / b.rows.reduce((s, r) => s + r[C.n], 0))),
    err: MOTORS.map((m) => b.rows.reduce((e, r) => { const v = r[C[`${m}err`]]; return v != null && (e == null || Math.abs(v) > Math.abs(e)) ? v : e; }, null)),
    line: b.rows.at(-1)[C.line]
  }));
  // Runs of bins without a gap of more than 3 s
  const runs = [];
  bs.forEach((b, i) => { if (!i || b.t - bs[i - 1].t > Math.max(3, ((t1 - t0) / plotW) * 6)) runs.push([]); runs.at(-1).push(b); });

  const ticks = [];
  const step = [10, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200].find((s) => (t1 - t0) / s <= Math.max(2, Math.floor(plotW / 90))) || 7200;
  for (let t = Math.ceil(t0 / step) * step; t <= t1; t += step) ticks.push(t);
  const lineAt = (t) => { let best = bs[0]; for (const b of bs) { if (b.t <= t) best = b; else break; } return best.line; };

  const move = (e) => {
    const r = e.currentTarget.getBoundingClientRect();
    const px = ((e.clientX - r.left) / r.width) * w;
    let i = 0, d = Infinity;
    bs.forEach((b, j) => { const dd = Math.abs(x(b.t) - px); if (dd < d) { d = dd; i = j; } });
    setAt(i);
  };
  const hov = at != null ? bs[at] : null;

  return <div class="jobchart" ref={box}>
    <div class="jobread" aria-live="off">{hov
      ? <><b>{dur(hov.t)}</b><span>line {hov.line.toLocaleString()}</span>{MOTORS.map((m, i) =>
          <span><i class={`sw m${i}`} />{m} {hov.avg[i].toLocaleString()} mA <small>({hov.min[i]}–{hov.max[i]}{hov.err[i] != null ? `, belt ${hov.err[i] > 0 ? "+" : ""}${hov.err[i]} mm` : ""})</small></span>)}</>
      : <span class="muted">Hover or touch the chart for the values at a moment.</span>}</div>
    <svg width={w} height={h} viewBox={`0 0 ${w} ${h}`} role="img" onPointerMove={move} onPointerDown={move} onPointerLeave={() => setAt(null)}
      aria-label={`Motor currents for ${rec.file} over ${dur(t1 - t0)}, 0 to ${yMax} mA. Download the CSV for the values.`}>
      {MOTORS.map((m, i) => {
        const y0 = i * (STRIP + GAP), y = (v) => y0 + STRIP - (v / yMax) * STRIP;
        return <g class={`strip m${i}`}>
          <rect class="sbg" x={LEFT} y={y0} width={plotW} height={STRIP} />
          <line class="grid" x1={LEFT} x2={LEFT + plotW} y1={y(yMax / 2)} y2={y(yMax / 2)} />
          {runs.map((run) => <>
            <path class="band" d={run.map((b, j) => `${j ? "L" : "M"}${x(b.t).toFixed(1)},${y(b.max[i]).toFixed(1)}`).join("") +
              [...run].reverse().map((b) => `L${x(b.t).toFixed(1)},${y(b.min[i]).toFixed(1)}`).join("") + "Z"} />
            <path class="avg" d={run.map((b, j) => `${j ? "L" : "M"}${x(b.t).toFixed(1)},${y(b.avg[i]).toFixed(1)}`).join("")} />
          </>)}
          <text class="mname" x={LEFT - 6} y={y0 + 16} text-anchor="end">{m}</text>
          <text class="mscale" x={LEFT - 6} y={y0 + STRIP} text-anchor="end">0</text>
        </g>;
      })}
      <text class="mscale" x={LEFT + plotW - 4} y={11} text-anchor="end">{yMax.toLocaleString()} mA</text>
      {ticks.map((t) => <g class="tick">
        <line x1={x(t)} x2={x(t)} y1={0} y2={h - AXIS + 4} />
        <text x={x(t)} y={h - AXIS + 16} text-anchor="middle">{dur(t)}</text>
        <text class="tline" x={x(t)} y={h - AXIS + 30} text-anchor="middle">ln {lineAt(t).toLocaleString()}</text>
      </g>)}
      {hov && <line class="cross" x1={x(hov.t)} x2={x(hov.t)} y1={0} y2={h - AXIS + 4} />}
    </svg>
  </div>;
}
