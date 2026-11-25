// The Mini's tool state: the report's TS field through the shared machine.js (ESP3D-WEBUI/www/js), the adapter's
// mapping to the screens' fields, and the screens derive() picks from them. Run with `npm test`.
import { test, before } from "node:test";
import assert from "node:assert/strict";
import { toolFields, recoveryEnded } from "./machine.js";
import { derive, freshAnswers, helperZ } from "../flow/screens.js";

before(async () => {
  globalThis.WebSocket ??= { OPEN: 1 };
  globalThis.addMessage = () => {};
  globalThis.ws_source = { readyState: 1, send() {} };
  await import("../../../ESP3D-WEBUI/www/js/wscommands.js");
  await import("../../../ESP3D-WEBUI/www/js/machine.js");
});

test("machine.js reads TS into state.tool, and null without the field", () => {
  const m = globalThis.machine;
  m.handleLine("<Idle|MPos:0,0,0|FS:0,0|MS:7|TS:3>");
  assert.equal(m.state.tool, m.TOOL.LOADED);
  m.handleLine("<Hold:0|MPos:0,0,0|FS:0,0|MS:7|TS:7>");
  assert.equal(m.state.tool, m.TOOL.FAULT_RECOVERY);
  m.handleLine("<Idle|MPos:0,0,0|FS:0,0|MS:7>");
  assert.equal(m.state.tool, null);
});

test("TS maps to the screens' tool and spindle; no TS or 0 leaves the tool steps off", () => {
  assert.deepEqual(toolFields(3, 0), { toolSteps: true, toolCode: 3, tool: "loaded", spindle: "off" });
  assert.equal(toolFields(4, 0).tool, "none");
  assert.equal(toolFields(5, 0).tool, "loading");
  assert.equal(toolFields(6, 0).tool, "removing");
  assert.equal(toolFields(2, 0).tool, "checking");
  assert.equal(toolFields(7, 0).spindle, "recovering");
  assert.equal(toolFields(1, 0).spindle, "recoveryFailed");
  assert.equal(toolFields(3, 12000).spindle, "on");
  assert.equal(toolFields(null, 0).toolSteps, false);
  assert.equal(toolFields(0, 0).toolSteps, false);
  assert.equal(toolFields(null, 0).tool, "unknown");
});

test("a recovery ends when TS leaves 7", () => {
  assert.equal(recoveryEnded(7, 3), true);
  assert.equal(recoveryEnded(7, 1), true);
  assert.equal(recoveryEnded(7, 7), false);
  assert.equal(recoveryEnded(7, null), false);
  assert.equal(recoveryEnded(3, 4), false);
});

// A connected Mini that is ready to cut, with the given TS
const mini = (ts, extra = {}) => ({ connected: true, maslow: 7, fluid: "Idle",
  ...toolFields(ts, 0), ...extra });
const answers = (patch = {}) => ({ ...freshAnswers(), ...patch });

test("the tool steps follow TS", () => {
  assert.equal(derive(mini(2), answers()), "tcheck");
  assert.equal(derive(mini(3), answers()), "setz");
  assert.equal(derive(mini(4), answers()), "toolload");
  assert.equal(derive(mini(5), answers()), "toolseating");
  assert.equal(derive(mini(6), answers()), "toolfreeing");
  assert.equal(derive(mini(null), answers()), "tcheck");   // no TS: the Skip way out
  assert.equal(derive(mini(3), answers({ zDone: true })), "secure");
});

test("Set Z: the helper while a bit is loaded; removing the bit leads to Load the tool", () => {
  assert.equal(derive(mini(3), answers({ helper: true })), "zhelper");
  assert.equal(derive(mini(6), answers({ helper: true })), "toolfreeing");
  assert.equal(derive(mini(4), answers({ helper: true })), "toolload");   // no bit: nothing to measure
  assert.equal(derive(mini(5), answers()), "toolseating");
  assert.equal(derive(mini(4), answers({ zDone: true })), "secure");      // Continue with no tool
});

test("the helper's work Z 0: the seated socket face 20 mm above the work, less the stick-out", () => {
  assert.equal(helperZ(5), -15);
  assert.equal(helperZ(12.5), -7.5);
});

test("a spindle fault shows recovering with no Run, and a failed recovery asks for Home Z", () => {
  assert.equal(derive(mini(7, { job: { file: "a.nc" } }), answers({ zDone: true, secured: true, file: "a.nc" })), "spindle");
  assert.equal(derive(mini(7), answers({ zDone: true })), "spindle");
  assert.equal(derive(mini(1), answers({ zDone: true })), "recovery");
});
