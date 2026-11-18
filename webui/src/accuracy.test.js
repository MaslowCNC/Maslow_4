import { test } from "node:test";
import assert from "node:assert/strict";
import { makeFrame, accuracyMap, errorAt, safeRect, checkJob, startRing } from "./accuracy.js";

// A 3000 x 2100 mm frame with the anchor heights from maslow-m4.yaml and a 19 mm spoil board, as in
// tools/workspace-accuracy/REPORT.md
const frame = makeFrame({ tl: [0, 2100], tr: [3000, 2100], bl: [0, 0], br: [3000, 0], z: [100, 56, 34, 78], spoil: 19 });
const map = accuracyMap(frame);

test("anchors that don't make a frame give no frame", () => {
  assert.equal(makeFrame({ tl: [0, 0], tr: [0, 0], br: [0, 0] }), null);
});

test("the centre is the most accurate place and errors grow toward the anchors", () => {
  assert.ok(errorAt(map, 0, 0) < 0.4);
  assert.ok(errorAt(map, 1000, 600) > errorAt(map, 400, 200));
  assert.equal(errorAt(map, 5000, 0), Infinity);
});

test("the ±1 mm area matches the prototype (accuracy-model.mjs, 50 mm steps)", () => {
  assert.deepEqual(safeRect(map, 1), { w: 1000, h: 700 });
  assert.ok(safeRect(map, 2).w > safeRect(map, 1).w);
  assert.deepEqual(safeRect(map), safeRect(map, 3)); // the default tolerance is ±3 mm
});

test("at ±1 mm, a job inside the area fits; one reaching the corner doesn't", () => {
  const small = checkJob(map, { min: { x: -300, y: -200 }, max: { x: 300, y: 200 } }, 1);
  assert.equal(small.outside, 0);
  assert.ok(small.pieces.every((p) => p.ok));
  const big = checkJob(map, { min: { x: -1200, y: -700 }, max: { x: 1200, y: 700 } }, 1);
  assert.ok(big.outside > 1000 && big.worst > 1);
  assert.ok(big.pieces.some((p) => !p.ok));
});

test("the starting ring follows the board, as in the firmware", () => {
  assert.equal(startRing("Maslow 4.1"), 150);
  assert.equal(startRing("Maslow Mini"), 50);
  assert.equal(startRing("Maslow Mini 36 V"), 50);
  assert.equal(startRing(null), 150);   // not known yet
  // On a 2000 x 1400 frame the Mini's smaller ring changes the ±1 mm area (README.md, "Maslow 4.1 and Maslow Mini")
  const c = { tl: [0, 1400], tr: [2000, 1400], bl: [0, 0], br: [2000, 0], z: [100, 56, 34, 78], spoil: 19 };
  assert.deepEqual(safeRect(accuracyMap(makeFrame({ ...c, ring: 150 })), 1), { w: 600, h: 500 });
  assert.deepEqual(safeRect(accuracyMap(makeFrame({ ...c, ring: 50 })), 1), { w: 700, h: 400 });
});

test("a vertical frame: the weight costs accuracy, the top edge is out of reach, a heavier sled is worse", () => {
  const c = { tl: [0, 2100], tr: [3000, 2100], bl: [0, 0], br: [3000, 0], z: [100, 56, 34, 78], spoil: 19 };
  const flat = accuracyMap(makeFrame(c)), hung = accuracyMap(makeFrame({ ...c, vertical: true, mass: 8 }));
  assert.ok(errorAt(hung, 0, 0) < 0.6);
  // below the top anchors, between them, the top belts would need more than a motor holds
  assert.equal(errorAt(hung, 0, 900), Infinity);
  assert.ok(Number.isFinite(errorAt(flat, 0, 900)));
  const area = (m) => { const r = safeRect(m, 2); return r.w * r.h; };
  assert.ok(area(hung) < area(flat));
  assert.ok(area(accuracyMap(makeFrame({ ...c, vertical: true, mass: 12 }))) < area(hung));
  // the mass makes no difference to a flat frame
  assert.deepEqual(safeRect(accuracyMap(makeFrame({ ...c, mass: 12 })), 2), safeRect(flat, 2));
});
