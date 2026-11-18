import { test } from "node:test";
import assert from "node:assert/strict";
import { settingError, effectiveWorkArea } from "./settings.js";

test("settingError checks numbers against the firmware's ranges", () => {
  assert.equal(settingError("workW", 0), null);
  assert.equal(settingError("parkZ", 101), "Between -100 and 100");
  assert.equal(settingError("scaleY", NaN), "Enter a number");
  assert.equal(settingError("retractForce", 1300.5), "Enter a whole number");
});

test("a work area of 0 is 80 % × 60 % of the frame, as the firmware works it out", () => {
  const frame = { tl: [0, 2100], tr: [3000, 2100], bl: [0, 0], br: [3000, 0] };
  assert.deepEqual(effectiveWorkArea({ workW: 0, workH: 1000 }, frame), { w: 2400, h: 1000, auto: { w: true, h: false } });
  assert.equal(effectiveWorkArea({ workW: 0, workH: 0 }, null).w, null);
});
