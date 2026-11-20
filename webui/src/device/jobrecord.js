// Job records: the belt motor currents and belt errors of each job, recorded by this page from the status report.
// Nothing is added to the firmware: MC, BE and SD are already in every report (docs/protocol.md), and the device
// (machine.js or the simulator) puts them in its state as motorCurrent, beltError and job. The recorder samples
// each new report while a job runs and reduces the samples to one row per second (about 3,600 rows an hour).
//
// It only records while this page is open: a sleeping phone or a background tab leaves gaps (rows are missing for
// those seconds), and the records live in this browser only (device/jobstore.js, IndexedDB).
//
// A job record, as stored (version 1). A job history lists these.
//   id        "<start ms>-<file>", unique per job
//   v         RECORD_VERSION
//   file      the job's file name, as the report gives it (no /sd/ prefix)
//   machine   the machine's name when the job started ("Maslow Mini"), or null
//   start     when the job started, ms since 1970 (Date.now())
//   end       when it ended, ms; null while it runs, or if the page closed before it ended
//   firstLine the file line it started from (0 from the start, more for Continue)
//   lastLine  the last file line seen
//   outcome   null while running, else { kind, line?, detail? }; kind is one of OUTCOMES' keys
//   rows      one array per second with samples, in COLUMNS' order:
//               t (seconds since start), line (last file line in that second), n (reports in that second),
//               then for TL, TR, BL, BR the min, average and max current (mA, whole numbers),
//               then for TL, TR, BL, BR the belt error with the largest size in that second (mm, signed; null if not reported)
//   log       the page's log lines ("HH:MM:SS text") from just before the job started until shortly after it ended:
//             the machine's messages and what this page sent. At most LOG_LINES, the oldest dropped first
//   logDropped how many lines were dropped from the start of log (0 or missing: none)

export const RECORD_VERSION = 1;
export const MOTORS = ["TL", "TR", "BL", "BR"];
export const COLUMNS = ["t", "line", "n",
  ...MOTORS.flatMap((m) => [`${m}min`, `${m}avg`, `${m}max`]),
  ...MOTORS.map((m) => `${m}err`)];
export const KEEP_JOBS = 10;
export const LOG_LINES = 5000;   // per job, so a long job with a chatty machine can't fill the browser's storage
const LOG_BEFORE = 10;          // lines of the log from before the job started, for context ($SD/Run and its answer)

// How a job ended. rank: a later, more specific reading replaces an earlier one (a stop that turns out to be a
// spindle fault), never the other way round.
export const OUTCOMES = {
  finished: { rank: 0, label: () => "Finished" },
  stopped: { rank: 1, label: (o) => `Stopped${o.line ? ` at line ${o.line.toLocaleString()}` : ""}` },
  error: { rank: 2, label: (o) => `Ended on an error${o.line ? ` at line ${o.line.toLocaleString()}` : ""}${o.detail ? `: ${o.detail}` : ""}` },
  spindleFault: { rank: 2, label: (o) => `Spindle fault${o.line ? ` at line ${o.line.toLocaleString()}` : ""}` },
  restarted: { rank: 2, label: () => "The machine restarted" },
  incomplete: { rank: 3, label: () => "Recording ended when this page closed" }
};
export const outcomeLabel = (o) => (o ? OUTCOMES[o.kind]?.label(o) ?? o.kind : "Running");

// MC is raw ADC 0–4095; the current UI converts it to mA this way (docs/protocol.md)
export const adcToMilliamps = (adc) => (adc == null ? null : 1000 * 3.3 * adc / 4095 / 1.5);

/** One status report's sample from the device state, or null when no job runs or there's no current reading. */
export function sampleOf(d) {
  if (!d?.job || !d.motorCurrent) return null;
  const mc = d.motorCurrent;
  const be = d.beltError;
  return {
    line: Number(d.job.line) || 0,
    mA: [mc.tl, mc.tr, mc.bl, mc.br].map(adcToMilliamps),
    err: [0, 1, 2, 3].map((i) => (be && Number.isFinite(be[i]) ? be[i] : null))
  };
}

/** How the job ended, from the device state just after its SD field went away. */
export function outcomeOf(d) {
  const inc = d?.incident?.kind;
  const lj = d?.lastJob;
  if (lj?.result === "fault" || inc === "spindleFault") return { kind: "spindleFault", line: lj?.line };
  if (inc === "restarted") return { kind: "restarted" };
  if (inc === "jobFailed") return { kind: "error", line: lj?.line, detail: String(d.incident.detail || "").replace(/\.$/, "") };
  if (d?.fluid === "Alarm" || d?.error) return { kind: "error", line: lj?.line, detail: d.error?.reason };
  if (lj?.result === "stopped") return { kind: "stopped", line: lj.line };
  return { kind: "finished" };
}

const better = (a, b) => (OUTCOMES[b.kind]?.rank ?? 0) > (OUTCOMES[a.kind]?.rank ?? 0);

export function newRecord(d, now, machine = null) {
  const file = String(d.job.file || "").split("/").pop();
  return { id: `${now}-${file}`, v: RECORD_VERSION, file, machine, start: now, end: null,
    firstLine: Number(d.job.line) || 0, lastLine: Number(d.job.line) || 0, outcome: null, rows: [],
    log: (d.log || []).slice(-LOG_BEFORE), logDropped: 0 };
}

// The second being collected: { t, line, n, sum[4], min[4], max[4], err[4] }. Kept beside the record, not in it.
const openBucket = (t) => ({ t, line: 0, n: 0, sum: [0, 0, 0, 0], min: [Infinity, Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity, -Infinity], err: [null, null, null, null] });
function closeBucket(b) {
  const r = (v) => Math.round(v);
  return [b.t, b.line, b.n,
    ...[0, 1, 2, 3].flatMap((i) => [r(b.min[i]), r(b.sum[i] / b.n), r(b.max[i])]),
    ...b.err.map((e) => (e == null ? null : Math.round(e * 1000) / 1000))];
}

/** Add one sample to a record at time now (ms). Returns true when a row was completed. */
export function addSample(rec, bucketRef, sample, now) {
  const t = Math.max(0, Math.floor((now - rec.start) / 1000));
  let closed = false;
  if (bucketRef.b && bucketRef.b.t !== t) { rec.rows.push(closeBucket(bucketRef.b)); bucketRef.b = null; closed = true; }
  const b = bucketRef.b || (bucketRef.b = openBucket(t));
  b.n++;
  b.line = Math.max(b.line, sample.line);
  sample.mA.forEach((v, i) => { const x = v ?? 0; b.sum[i] += x; b.min[i] = Math.min(b.min[i], x); b.max[i] = Math.max(b.max[i], x); });
  sample.err.forEach((e, i) => { if (e != null && (b.err[i] == null || Math.abs(e) > Math.abs(b.err[i]))) b.err[i] = e; });
  rec.lastLine = Math.max(rec.lastLine, sample.line);
  return closed;
}

export function flushBucket(rec, bucketRef) {
  if (bucketRef.b?.n) rec.rows.push(closeBucket(bucketRef.b));
  bucketRef.b = null;
}

/**
 * The lines added to the page's log between two of its states. The log keeps its last 200 lines, so once full it
 * drops lines from the front as new ones come: find where the old log's tail lines up with the new log's head.
 */
export function newLogLines(prev = [], next = []) {
  if (next === prev) return [];
  for (let k = 0; k <= prev.length; k++) {
    const n = prev.length - k;
    if (n > next.length) continue;
    let same = true;
    for (let i = 0; i < n && same; i++) same = next[i] === prev[k + i];
    if (same) return next.slice(n);
  }
  return next;
}

export function addLog(rec, lines) {
  if (!lines.length) return;
  rec.log = [...(rec.log || []), ...lines];
  if (rec.log.length > LOG_LINES) { rec.logDropped = (rec.logDropped || 0) + rec.log.length - LOG_LINES; rec.log = rec.log.slice(-LOG_LINES); }
}

/** The records to keep: the newest `keep` by start time. Returns { keep, drop }. */
export function prune(records, keep = KEEP_JOBS) {
  const sorted = [...records].sort((a, b) => b.start - a.start);
  return { keep: sorted.slice(0, keep), drop: sorted.slice(keep) };
}

// --- CSV, for support and spreadsheets ---

const pad = (n) => String(n).padStart(2, "0");
const day = (d) => `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
const clock = (d, sep = ":") => [d.getHours(), d.getMinutes(), d.getSeconds()].map(pad).join(sep);

/** maslow-currents-sign-panel-2026-11-20-14-05-09.csv: the file without its extension, and the job's start */
export function csvFileName(rec) {
  const name = rec.file.replace(/\.[^.]*$/, "").replace(/[^A-Za-z0-9_-]+/g, "-").replace(/^-+|-+$/g, "") || "job";
  const d = new Date(rec.start);
  return `maslow-currents-${name}-${day(d)}-${clock(d, "-")}.csv`;
}

const CSV_HEAD = ["time_s", "clock", "line", "reports",
  ...MOTORS.flatMap((m) => [`${m}_min_mA`, `${m}_avg_mA`, `${m}_max_mA`]),
  ...MOTORS.map((m) => `${m}_belt_error_mm`)];

/** maslow-log-sign-panel-2026-11-20-14-05-09.txt, named like the CSV */
export const logFileName = (rec) => csvFileName(rec).replace(/^maslow-currents-/, "maslow-log-").replace(/\.csv$/, ".txt");

export function logText(rec) {
  const d = new Date(rec.start);
  const head = [
    `Maslow job log: ${rec.file}`,
    `Started ${day(d)} ${clock(d)}${rec.end ? `, ended ${clock(new Date(rec.end))}` : ""}`,
    `Outcome: ${outcomeLabel(rec.outcome)}`,
    `Machine: ${rec.machine || "not known"}`,
    `Lines: ${(rec.log || []).length}${rec.logDropped ? ` (the first ${rec.logDropped} were dropped: a job keeps its last ${LOG_LINES})` : ""}. Recorded by the page while it was open, so a sleeping phone or background tab leaves gaps.`,
    ""
  ];
  return [...head, ...(rec.log || []), ""].join("\n");
}

export function csvText(rec) {
  const out = [CSV_HEAD.join(",")];
  for (const row of rec.rows) {
    const [t, ...rest] = row;
    out.push([t, clock(new Date(rec.start + t * 1000)), ...rest.map((v) => (v == null ? "" : v))].join(","));
  }
  return out.join("\n") + "\n";
}

// --- The recorder: watches the device and keeps the records ---

const SETTLE_MS = 10000;   // after a job ends, how long a later report can still say more about how (a spindle fault)
const SAVE_EVERY = 10;     // rows between saves while a job runs, so a reload keeps most of it

/**
 * device: { getState, subscribe }; store: { load, save, remove, persistent } (device/jobstore.js).
 * examples: records shown after the stored ones but never saved or pruned (the simulator's example job).
 * Returns { records(), current(), subscribe(fn), ready }. records() is newest first and includes the running job.
 */
export function createRecorder({ device, store, now = () => Date.now(), examples = [] }) {
  const listeners = new Set();
  let list = [];            // stored records, newest first, without the running one
  let cur = null;           // the running job's record
  const bucket = { b: null };
  let settling = null;      // { rec, until } just after a job ended
  let lastMc = null;
  let lastLog = device.getState().log || [];
  const notify = () => listeners.forEach((fn) => fn());
  const save = (rec) => Promise.resolve().then(() => store.save(rec)).catch(() => {});

  function keepList(rec) {
    const { keep, drop } = prune([rec, ...list.filter((r) => r.id !== rec.id)], KEEP_JOBS);
    list = keep;
    for (const r of drop) Promise.resolve().then(() => store.remove(r.id)).catch(() => {});
  }

  function finish(d, t) {
    flushBucket(cur, bucket);
    cur.end = t;
    cur.outcome = outcomeOf(d);
    const rec = cur;
    cur = null;
    keepList(rec);
    save(rec);
    settling = { rec, until: t + SETTLE_MS };
  }

  function onState(d) {
    const t = now();
    const added = newLogLines(lastLog, d.log || []);
    lastLog = d.log || [];
    if (cur) addLog(cur, added);
    else if (settling && added.length && t <= settling.until) { addLog(settling.rec, added); save(settling.rec); }
    if (settling) {
      if (t > settling.until || d.job) settling = null;
      else {
        const o = outcomeOf(d);
        if (better(settling.rec.outcome, o)) { settling.rec.outcome = o; save(settling.rec); notify(); }
      }
    }
    const file = d.job ? String(d.job.file || "").split("/").pop() : null;
    if (cur && (!d.job || file !== cur.file)) { finish(d, t); notify(); }
    if (d.job && !cur) { cur = newRecord(d, t, d.machine); bucket.b = null; lastMc = null; notify(); }
    if (!cur || d.motorCurrent === lastMc) return;
    lastMc = d.motorCurrent;
    const s = sampleOf(d);
    if (!s) return;
    if (addSample(cur, bucket, s, t)) {
      notify();
      if (cur.rows.length % SAVE_EVERY === 0) save(cur);
    }
  }

  // Earlier records from storage. One left without an end is from a page that closed while its job ran.
  const ready = Promise.resolve().then(() => store.load()).catch(() => []).then((loaded) => {
    for (const r of loaded || []) {
      if (r.v !== RECORD_VERSION || (cur && r.id === cur.id) || list.some((x) => x.id === r.id)) continue;
      if (!r.outcome) { r.outcome = { kind: "incomplete" }; r.end = r.end ?? (r.start + (r.rows.at(-1)?.[0] ?? 0) * 1000); save(r); }
      list.push(r);
    }
    const { keep, drop } = prune(list, KEEP_JOBS);
    list = keep;
    for (const r of drop) Promise.resolve().then(() => store.remove(r.id)).catch(() => {});
    notify();
  });

  onState(device.getState());
  device.subscribe(onState);

  return {
    ready,
    get persistent() { return !!store.persistent; },
    records: () => [...(cur ? [cur] : []), ...list, ...examples],
    current: () => cur,
    subscribe(fn) { listeners.add(fn); return () => listeners.delete(fn); }
  };
}
