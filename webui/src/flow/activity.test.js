// The activity indicator follows the machine's state (the screen, TS) and this page's own transfers. Run with `npm test`.
import { test } from "node:test";
import assert from "node:assert/strict";
import { activity } from "./activity.js";

const d = (extra = {}) => ({ connected: true, tool: "unknown", gcode: null, upload: null, ...extra });

test("nothing running: no indicator; disconnected: none either", () => {
  assert.equal(activity("ready", d()), null);
  assert.equal(activity("tensioning", d({ connected: false })), null);
});

test("a moving screen shows a spinner with its name, whoever started it", () => {
  assert.deepEqual(activity("tensioning", d()), { label: "Applying tension", frac: null, detail: null });
  assert.equal(activity("retracting", d()).label, "Retracting belts");
  assert.equal(activity("toolfreeing", d()).label, "Removing the tool");
});

test("a running job has its own progress, but parking after it shows", () => {
  assert.equal(activity("cutting", d()), null);
  assert.ok(activity("success", d()));
});

test("Find anchors shows point n of total as a bar once the firmware reports it", () => {
  assert.equal(activity("anchors", d()).frac, null);
  const a = activity("anchors", d({ calPoint: { n: 3, total: 12 } }));
  assert.equal(a.frac, 0.25);
  assert.equal(a.detail, "point 3 of 12");
});

test("a Z move from TS shows on any screen but a job's", () => {
  assert.equal(activity("ready", d({ tool: "checking" })).label, "Homing Z");
  assert.equal(activity("ready", d({ tool: "removing" })).label, "Removing the tool");
  assert.equal(activity("cutting", d({ tool: "checking" })), null);
});

test("uploads and file reads: a bar when the size is known, a spinner when not", () => {
  assert.deepEqual(activity("ready", d({ upload: { file: "a.nc", loaded: 50, total: 200 } })), { label: "Uploading a.nc", frac: 0.25, detail: null });
  assert.equal(activity("ready", d({ gcode: { file: "a.nc", status: "reading" } })).frac, null);
  assert.equal(activity("ready", d({ gcode: { file: "a.nc", status: "reading", loaded: 300, total: 200 } })).frac, 1);
  assert.equal(activity("ready", d({ gcode: { file: "a.nc", status: "ready" } })), null);
});
