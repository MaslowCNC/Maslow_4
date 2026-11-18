// Tests for saving the log and for the command box's simulator replies. Run with `npm test`.
import { test } from "node:test";
import assert from "node:assert/strict";
import { logFileName, logFileText } from "./logfile.js";
import { createSimulator } from "../device/simulator.js";

test("the log file is named by date and time", () => {
  assert.equal(logFileName(new Date(2026, 10, 8, 9, 5, 3)), "maslow-log-2026-11-08-09-05-03.txt");
});

test("the log file has a header, then every line as the page shows it", () => {
  const text = logFileText(["09:05:01 Connected", "09:05:02 ok"], { date: new Date(2026, 10, 8, 9, 5, 3), machine: "Maslow Mini" });
  const lines = text.split("\n");
  assert.match(lines[0], /^Maslow log saved 2026-11-08 09:05:03/);
  assert.equal(lines[1], "Machine: Maslow Mini");
  assert.ok(text.includes("09:05:01 Connected\n09:05:02 ok\n"));
});

test("the simulator answers a typed command in the log", () => {
  const sim = createSimulator();
  sim.send("command", { line: "$I" });
  assert.match(sim.getState().log.at(-1), /Not sent: \$I \(not connected\)/);
  sim.send("connect");
  sim.answer(1);   // connected, belt lengths unknown
  const before = sim.getState().log.length;
  sim.send("command", { line: "$I" });
  const log = sim.getState().log.slice(before).map(l => l.slice(9));
  assert.equal(log[0], "> $I");
  assert.equal(log.at(-1), "ok");
  sim.send("command", { line: "$/Maslow_Park_X=12" });
  sim.send("command", { line: "$/Maslow_Park_X" });
  assert.ok(sim.getState().log.some(l => l.endsWith(" $/Maslow_Park_X=12")));
  sim.send("command", { line: "hello" });
  assert.match(sim.getState().log.at(-1), /error:1$/);
});
