import { test } from "node:test";
import assert from "node:assert/strict";
import { encoderTeethCheck, encoderTeethCommands, EXTEND_DIST_MAX } from "./encoderteeth.js";
import { wrongEncoderTeeth, encoderTeethSaved } from "../flow/incidents.js";

test("the right teeth for the board, or an unknown board, need no warning", () => {
  assert.equal(encoderTeethCheck("maslow41", "22"), null);
  assert.equal(encoderTeethCheck("unified", "35.000"), null);
  assert.equal(encoderTeethCheck("unified36v", 35), null);
  assert.equal(encoderTeethCheck("somethingelse", "22"), null);
  assert.equal(encoderTeethCheck(undefined, "22"), null);
  assert.equal(encoderTeethCheck("unified", "error:3"), null);   // couldn't be read
});

test("a Mini with the 4.1's 22 teeth: expected 35, about 37 % off, Extend distance scaled up", () => {
  const c = encoderTeethCheck("unified", "22", "1000");
  assert.equal(c.machine, "Maslow Mini");
  assert.equal(c.expected, 35);
  assert.equal(c.teeth, 22);
  assert.equal(c.errorPct, 37);
  assert.equal(c.newExtendDist, Math.ceil(1000 * 35 / 22));   // 1591
  assert.deepEqual(encoderTeethCommands(c).map(([cmd]) => cmd),
    ["$/kinematics/MaslowKinematics/encoderTeeth=35", "$/Maslow_Extend_Dist=1591", "$CO"]);
});

test("a 4.1 with the Mini's 35 teeth scales the Extend distance down; the result stays under the firmware's limit", () => {
  assert.equal(encoderTeethCheck("maslow41", 35, 2000).newExtendDist, Math.ceil(2000 * 22 / 35));
  assert.equal(encoderTeethCheck("unified", 22, 4000).newExtendDist, EXTEND_DIST_MAX);
});

test("without the Extend distance the commands say how to work it out", () => {
  const c = encoderTeethCheck("maslow41", "35", "");
  assert.equal(c.newExtendDist, 0);
  assert.match(encoderTeethCommands(c)[1][0], /=N$/);
  assert.match(wrongEncoderTeeth(c).next, /35|22\/35/);
});

test("the incident gives the facts, a Fix, and the restart and set-up steps", () => {
  const i = wrongEncoderTeeth(encoderTeethCheck("unified", "22", "1000"));
  assert.equal(i.fix.action, "fixEncoderTeeth");
  assert.match(i.detail, /Maslow Mini.*35 teeth.*set to 22/);
  assert.match(i.next, /1591 mm/);
  assert.match(i.next, /restart the machine.*Retract, Extend, Apply tension, then Find anchors/);
  const saved = encoderTeethSaved(i.check);
  assert.equal(saved.fix.action, "reboot");
  assert.match(encoderTeethSaved(i.check, "error:3").detail, /Couldn't save the fix: error:3/);
});
