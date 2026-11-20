import { test } from "node:test";
import assert from "node:assert/strict";
import { frameBounds, workAreaBounds, jobFit, jobPlacement } from "./fit.js";

// A 3000 × 2100 mm frame: machine 0, 0 is its middle, so it reaches ±1500, ±1050
const frame = { tl: [0, 2100], tr: [3000, 2100], bl: [0, 0], br: [3000, 0] };
const home = { x: 0, y: 0, z: 0 };
const box = (x1, y1, x2, y2) => ({ min: { x: x1, y: y1, z: -3 }, max: { x: x2, y: y2, z: 0 } });

test("the frame is the rectangle inside the anchors, centred on machine 0, 0", () => {
  assert.deepEqual(frameBounds(frame), { minX: -1500, maxX: 1500, minY: -1050, maxY: 1050 });
  assert.equal(frameBounds(null), null);
});

test("an automatic work area is 80 % × 60 % of the frame, moved by the centre offset", () => {
  assert.deepEqual(workAreaBounds({}, frame), { minX: -1200, maxX: 1200, minY: -630, maxY: 630 });
  assert.deepEqual(workAreaBounds({ workW: 1000, workH: 500, offX: 100 }, frame), { minX: -400, maxX: 600, minY: -250, maxY: 250 });
});

test("a job inside the work area is ok", () => {
  assert.equal(jobFit(box(-200, -100, 200, 100), home, {}, frame).level, "ok");
});

test("a job outside the work area but inside the frame is a warning", () => {
  const f = jobFit(box(0, 0, 1300, 100), home, {}, frame);
  assert.equal(f.level, "outside");
  assert.match(f.text, /100 mm outside the work area/);
});

test("a job past the frame can't run, and home moves it", () => {
  assert.equal(jobFit(box(0, 0, 1600, 100), home, {}, frame).level, "pastFrame");
  assert.match(jobFit(box(0, 0, 1600, 100), home, {}, frame).text, /100 mm past the frame/);
  // From a home 500 mm to the left it fits the frame; a taller one fits the frame but not the work area
  assert.equal(jobFit(box(0, 0, 1600, 100), { x: -500, y: 0 }, {}, frame).level, "ok");
  assert.equal(jobFit(box(0, 0, 1600, 700), { x: -500, y: 0 }, {}, frame).level, "outside");
});

test("no anchors: nothing to compare with; no file: nothing to check", () => {
  assert.equal(jobFit(box(0, 0, 10, 10), home, {}, null).level, "unknown");
  assert.equal(jobFit(null, home, {}, frame), null);
});

test("placement reads from home", () => {
  assert.equal(jobPlacement(box(-150, -100, 150, 100)), "X −150 to 150, Y −100 to 100");
  assert.equal(jobPlacement(box(0, 0, 300, 200)), "X 0 to 300, Y 0 to 200");
});
