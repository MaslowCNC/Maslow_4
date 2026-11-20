// Tests for the job recorder (device/jobrecord.js). Run with `npm test`.
import { test } from "node:test";
import assert from "node:assert/strict";
import { createRecorder, newLogLines, logText, logFileName, sampleOf, outcomeOf, outcomeLabel, prune, csvText, csvFileName, adcToMilliamps, COLUMNS } from "./jobrecord.js";
import { memoryStore } from "./jobstore.js";

// A stand-in device whose state the test sets, and a clock the test moves
function fakeDevice(state = {}) {
  let s = { job: null, motorCurrent: null, ...state };
  const fns = new Set();
  return {
    getState: () => s,
    subscribe(fn) { fns.add(fn); return () => fns.delete(fn); },
    set(patch) { s = { ...s, ...patch }; fns.forEach((fn) => fn(s)); }
  };
}
const mc = (v) => ({ tl: v, tr: v, bl: v, br: v });

test("ADC to mA as the current UI converts it", () => {
  assert.equal(Math.round(adcToMilliamps(4095)), 2200);
  assert.equal(adcToMilliamps(null), null);
});

test("a sample needs a job and a current reading", () => {
  assert.equal(sampleOf({ job: null, motorCurrent: mc(1) }), null);
  assert.equal(sampleOf({ job: { line: 3 }, motorCurrent: null }), null);
  const s = sampleOf({ job: { line: 3 }, motorCurrent: mc(4095), beltError: [0.1, -0.2, 0, 0.5] });
  assert.equal(s.line, 3);
  assert.equal(Math.round(s.mA[0]), 2200);
  assert.deepEqual(s.err, [0.1, -0.2, 0, 0.5]);
});

test("how a job ended, from what the device knows", () => {
  assert.equal(outcomeOf({}).kind, "finished");
  assert.deepEqual(outcomeOf({ lastJob: { result: "stopped", line: 1240 } }), { kind: "stopped", line: 1240 });
  assert.equal(outcomeOf({ lastJob: { result: "fault", line: 9 } }).kind, "spindleFault");
  assert.equal(outcomeOf({ incident: { kind: "jobFailed", detail: "error:20." }, lastJob: { result: "stopped", line: 7 } }).detail, "error:20");
  assert.equal(outcomeOf({ fluid: "Alarm", error: { reason: "Position error" } }).detail, "Position error");
  assert.equal(outcomeLabel({ kind: "stopped", line: 1240 }), "Stopped at line 1,240");
  assert.equal(outcomeLabel(null), "Running");
});

test("reports are reduced to one row a second with min, average and max", async () => {
  let t = 1_000_000;
  const dev = fakeDevice({ machine: "Maslow Mini" });
  const store = memoryStore();
  const rec = createRecorder({ device: dev, store, now: () => t });
  await rec.ready;
  dev.set({ job: { file: "/sd/sign-panel.nc", line: 0 } });
  const r = rec.current();
  assert.equal(r.file, "sign-panel.nc");
  assert.equal(r.machine, "Maslow Mini");
  // Second 0: three reports; second 1: one report; the same MC object again is the same report, not a new sample
  for (const [dt, adc, line] of [[0, 0, 1], [50, 4095, 2], [100, 2047.5, 3], [1000, 100, 9]]) {
    t = 1_000_000 + dt;
    dev.set({ motorCurrent: mc(adc), beltError: [0.1, -0.4, 0, 0], job: { file: "/sd/sign-panel.nc", line } });
  }
  dev.set({ log: ["something else changed"] });
  assert.equal(r.rows.length, 1);
  const row = r.rows[0];
  assert.equal(row.length, COLUMNS.length);
  assert.deepEqual(row.slice(0, 6), [0, 3, 3, 0, 1100, 2200]);
  assert.equal(row[COLUMNS.indexOf("TRerr")], -0.4);
  // The job ends, stopped: the last second is kept, and the record is saved with its outcome
  t += 500;
  dev.set({ job: null, lastJob: { file: "sign-panel.nc", result: "stopped", line: 9 } });
  assert.equal(rec.current(), null);
  const done = rec.records()[0];
  assert.equal(done.rows.length, 2);
  assert.equal(done.lastLine, 9);
  assert.deepEqual(done.outcome, { kind: "stopped", line: 9 });
  await new Promise((r) => setTimeout(r, 0));
  assert.equal((await store.load()).length, 1);
  // A spindle fault reported just after the stop says more, so it replaces it
  t += 2000;
  dev.set({ incident: { kind: "spindleFault" } });
  assert.equal(done.outcome.kind, "spindleFault");
});

test("only the last ten jobs are kept", () => {
  const recs = Array.from({ length: 12 }, (_, i) => ({ id: String(i), start: i }));
  const { keep, drop } = prune(recs);
  assert.equal(keep.length, 10);
  assert.deepEqual(keep[0].id, "11");
  assert.deepEqual(drop.map((r) => r.id).sort(), ["0", "1"]);
});

test("a record left running by a closed page loads as incomplete", async () => {
  const store = memoryStore();
  await store.save({ id: "a", v: 1, file: "x.nc", start: 5000, end: null, outcome: null, rows: [[0, 1, 1], [4, 2, 1]] });
  const rec = createRecorder({ device: fakeDevice(), store });
  await rec.ready;
  const [r] = rec.records();
  assert.equal(r.outcome.kind, "incomplete");
  assert.equal(r.end, 9000);
});

test("the CSV has a header, the clock time and empty cells for missing belt errors", () => {
  const start = new Date(2026, 10, 20, 14, 5, 9).getTime();
  const rec = { file: "sign panel.nc", start, rows: [[0, 3, 2, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3, 0.1, null, null, -0.2]] };
  const [head, line] = csvText(rec).trim().split("\n");
  assert.ok(head.startsWith("time_s,clock,line,reports,TL_min_mA"));
  assert.equal(line, "0,14:05:09,3,2,1,2,3,1,2,3,1,2,3,1,2,3,0.1,,,-0.2");
  assert.equal(csvFileName(rec), "maslow-currents-sign-panel-2026-11-20-14-05-09.csv");
});

test("the simulator's example jobs cover each outcome and aren't saved", async () => {
  const { exampleJobs } = await import("./jobrecord.sample.js");
  const exs = exampleJobs(500_000_000);
  assert.deepEqual([...new Set(exs.map((e) => e.outcome.kind))].sort(), ["error", "finished", "incomplete", "spindleFault", "stopped"]);
  assert.equal(exs[0].rows.length, 18 * 60 + 1 - 45);
  assert.ok(exs.every((e) => e.rows.every((r) => r.length === COLUMNS.length)));
  assert.ok(exs.every((e, i) => !i || e.start < exs[i - 1].start), "newest first");
  const store = memoryStore();
  const rec = createRecorder({ device: fakeDevice(), store, examples: exs });
  await rec.ready;
  assert.equal(rec.records().length, exs.length);
  assert.equal((await store.load()).length, 0);
});

test("new log lines are found even when the 200-line log drops its oldest", () => {
  assert.deepEqual(newLogLines(["a", "b"], ["a", "b", "c"]), ["c"]);
  assert.deepEqual(newLogLines(["a", "b", "c"], ["b", "c", "d", "e"]), ["d", "e"]);
  assert.deepEqual(newLogLines(["ok", "ok"], ["ok", "ok", "ok"]), ["ok"]);
  const p = ["x"], n = p;
  assert.deepEqual(newLogLines(p, n), []);
});

test("a job keeps the log from just before it started until shortly after it ended", async () => {
  let t = 0;
  const dev = fakeDevice({ log: ["10:00:00 Connected", "10:00:01 $SD/Run=/a.nc"] });
  const rec = createRecorder({ device: dev, store: memoryStore(), now: () => t });
  await rec.ready;
  dev.set({ job: { file: "a.nc", line: 0 } });
  dev.set({ log: [...dev.getState().log, "10:00:05 [MSG:WARN: something]"] });
  t = 3000;
  dev.set({ job: null, log: [...dev.getState().log, "10:00:08 [MSG:INFO: Job finished]"] });
  t = 5000;
  dev.set({ log: [...dev.getState().log, "10:00:10 [MSG:INFO: Parking]"] });
  t = 60000;
  dev.set({ log: [...dev.getState().log, "10:01:00 unrelated"] });
  const r = rec.records()[0];
  assert.deepEqual(r.log.map((l) => l.slice(9)), ["Connected", "$SD/Run=/a.nc", "[MSG:WARN: something]", "[MSG:INFO: Job finished]", "[MSG:INFO: Parking]"]);
  assert.match(logText(r), /^Maslow job log: a\.nc\n/);
  assert.match(logFileName(r), /^maslow-log-a-\d{4}-\d\d-\d\d-\d\d-\d\d-\d\d\.txt$/);
});
