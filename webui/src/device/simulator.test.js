// Tests for the simulator's stand-in G-code. Run with `npm test`.
import { test } from "node:test";
import assert from "node:assert/strict";
import { createSimulator, sampleGcode } from "./simulator.js";
import { readGcode } from "../gcode/reader.js";

test("a demo file's sample covers its size and goes down to its depth", () => {
  const r = readGcode(sampleGcode({ name: "x.nc", w: 482, h: 300, depth: 12 }));
  assert.deepEqual(r.extent, { min: { x: 0, y: 0, z: -12 }, max: { x: 482, y: 300, z: 5 } });
});

test("choosing a file reads it into state.gcode; choosing none clears it", () => {
  const sim = createSimulator();
  const file = sim.getState().files[0];
  sim.send("chooseFile", { file: file.name });
  const g = sim.getState().gcode;
  assert.equal(g.file, file.name);
  assert.equal(g.status, "ready");
  assert.equal(g.extent.max.x, file.w);
  assert.ok(g.moves.length > 0 && g.lineCount > 0);
  sim.send("chooseFile", { file: null });
  assert.equal(sim.getState().gcode, null);
});

test("setSettings saves the changed machine settings, logs them with $CO, and refuses out-of-range values", () => {
  const sim = createSimulator();
  assert.equal(sim.getState().settings.workW, 0);   // automatic
  assert.equal(sim.getState().settings.retractLimit, 300);
  sim.send("setSettings", { values: { parkX: 100, parkY: -50, parkZ: 2 }, label: "Park position" });
  const s = sim.getState();
  assert.deepEqual([s.settings.parkX, s.settings.parkY, s.settings.parkZ], [100, -50, 2]);
  assert.ok(s.log.some(l => l.endsWith("$/Maslow_Park_X=100")));
  assert.ok(!s.log.some(l => l.includes("Maslow_Park_Z")));   // unchanged, not sent
  assert.ok(s.log.at(-1).endsWith("$CO"));
  sim.send("setSettings", { values: { scaleX: 1.5 }, label: "Scale" });
  assert.equal(sim.getState().settings.scaleX, 1);
  assert.match(sim.getState().log.at(-1), /Not sent: Scale/);
});
