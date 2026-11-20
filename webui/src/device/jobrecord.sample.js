// Example job records for the simulator, so Settings › Advanced › Jobs shows what each kind of job looks like before
// one has been run. Made up but shaped like real ones (device/jobrecord.js): the load swings between the top and
// bottom motors as the sled moves, with spikes (a hard plunge, a snag) and their belt errors, and gaps where the
// page slept. One of each outcome. The same every time (a seeded random), and never saved: the simulator shows
// them after the stored records.

import { RECORD_VERSION, adcToMilliamps } from "./jobrecord.js";

// file, hours before now, seconds recorded, total lines, the line it ended at, outcome, gaps [from, to) s, spikes [second, arm, seconds]
// Log lines for an example: what the page logs around a job (time offset s, text). end: lines at the job's end.
const LOG_START = (file) => [[-2, `$SD/Run=/${file}`], [-2, "ok"], [0, "[MSG:INFO: Spindle on at 12000 RPM]"]];
const LOG_END = {
  finished: [[0, "[MSG:INFO: Job finished]"], [1, "[MSG:INFO: Parking]"]],
  stopped: [[-1, "$STOP"], [-1, "ok"], [0, "[MSG:INFO: Job stopped]"]],
  spindleFault: [[-2, "[MSG:WARN: SpindleBoard: driver over-current (nFAULT retries) - spindle stopped]"],
    [0, "[MSG:INFO: SpindleBoard: machine stopped after the spindle fault - ending the job ($STOP)]"],
    [8, "[MSG:INFO: SpindleBoard: Z re-homed after the spindle fault; work Z zero restored - ready to cut]"]],
  error: [[0, "[MSG:ERR: Emergency stop! Stopping all motors]"], [0, "[MSG:ERR: Position error over 15 mm on the top right belt]"]],
  incomplete: []
};

const EXAMPLES = [
  { file: "sign-panel.nc", ago: 2, length: 18 * 60, lines: 3260, outcome: { kind: "finished" },
    gaps: [[610, 655]], spikes: [[245, 2, 6], [902, 1, 4]] },
  { file: "coaster-set.nc", ago: 5, length: 6 * 60 + 40, lines: 2100, stopAt: 1388, outcome: { kind: "stopped", line: 1388 },
    gaps: [], spikes: [] },
  { file: "shelf-bracket.nc", ago: 26, length: 11 * 60 + 12, lines: 5400, stopAt: 3012, outcome: { kind: "spindleFault", line: 3012 },
    gaps: [], spikes: [[660, 0, 3], [668, 3, 4]] },
  { file: "big-tabletop.nc", ago: 50, length: 84 * 60, lines: 18400, outcome: { kind: "finished" },
    gaps: [[1500, 1520], [3900, 4140]], spikes: [[2210, 2, 5], [4400, 0, 3], [4720, 1, 6]] },
  { file: "shelf-bracket.nc", ago: 52, length: 3 * 60 + 5, lines: 5400, stopAt: 940, outcome: { kind: "error", line: 940, detail: "Position error over 15 mm on the top right belt" },
    gaps: [], spikes: [[178, 1, 7]] },
  { file: "logo-inlay.nc", ago: 75, length: 9 * 60, lines: 4100, stopAt: 2460, outcome: { kind: "incomplete" },
    gaps: [], spikes: [] }
];

function exampleRecord(ex, i, now) {
  let seed = 7 + i * 101;
  const rand = () => ((seed = (seed * 16807) % 2147483647) - 1) / 2147483646;
  const start = now - ex.ago * 3600 * 1000;
  const lastLine = ex.stopAt ?? ex.lines;
  const mA = (adc) => Math.round(adcToMilliamps(adc));
  const rows = [];
  for (let t = 0; t <= ex.length; t++) {
    if (ex.gaps.some(([a, b]) => t >= a && t < b)) continue;
    const line = Math.round((t / ex.length) * lastLine);
    const ph = line / 90, up = 0.5 + 0.5 * Math.sin(ph), side = 0.5 + 0.5 * Math.sin(ph * 0.63 + 1 + i);
    const load = [up * (1 - side), up * side, (1 - up) * (1 - side), (1 - up) * side].map((x) => 500 + 1100 * x);
    const spike = ex.spikes.find(([s, , n]) => t >= s && t < s + n);
    const motor = (v, m) => {
      const extra = spike?.[1] === m ? 1600 : 0;
      const lo = v - 60 - rand() * 80, hi = v + 60 + rand() * 80 + extra;
      return [mA(Math.max(0, lo)), mA(v + extra * 0.5 + (rand() - 0.5) * 40), mA(Math.min(4095, hi))];
    };
    const err = load.map((_, m) => +((rand() - 0.5) * 0.06 + (spike?.[1] === m ? 0.6 + rand() * 0.5 : 0)).toFixed(3));
    rows.push([t, line, 19 + Math.round(rand() * 2), ...load.flatMap(motor), ...err]);
  }
  const hms = (s) => new Date(start + s * 1000).toTimeString().slice(0, 8);
  const log = [...LOG_START(ex.file), ...ex.spikes.map(([s, arm]) => [s, `[MSG:WARN: ${["Top left", "Top right", "Bottom left", "Bottom right"][arm]} belt position error 0.9 mm]`]),
    ...(LOG_END[ex.outcome.kind] || []).map(([s, text]) => [ex.length + s, text])].map(([s, text]) => `${hms(s)} ${text}`);
  return {
    log, logDropped: 0,
    id: `example-${i}`, v: RECORD_VERSION, example: true, file: ex.file, machine: "Maslow Mini", start,
    end: start + ex.length * 1000, firstLine: 0, lastLine, outcome: ex.outcome, rows
  };
}

/** The example jobs, newest first, placed relative to now */
export const exampleJobs = (now = Date.now()) => EXAMPLES.map((ex, i) => exampleRecord(ex, i, now));
