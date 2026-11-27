import { test } from "node:test";
import assert from "node:assert/strict";
import { handleMessage, isAllowedOrigin, safeFileName, refusal } from "./jobreceiver.js";

// The current UI's receiver (a plain script that publishes its test API), to check the two agree
await import("../../../ESP3D-WEBUI/www/js/jobreceiver.js");
const old = globalThis.__jobReceiverTestApi;

const ABUNDANCE = "https://abundance.maslowcnc.com";
function event(data, origin = ABUNDANCE) {
  const posted = [];
  return { data, origin, source: { postMessage: (m, o) => posted.push([m, o]) }, posted };
}
const idle = { ready: () => true, state: () => ({ job: null, grbl: "Idle" }) };

test("only Abundance and localhost may send, as in the current UI", () => {
  for (const o of [ABUNDANCE, "http://localhost:5173", "http://127.0.0.1", "https://evil.example", "http://abundance.maslowcnc.com",
    "http://localhost.evil.example", null]) {
    assert.equal(isAllowedOrigin(o), old.isAllowedOrigin(o), String(o));
  }
  assert.equal(isAllowedOrigin(ABUNDANCE), true);
  assert.equal(isAllowedOrigin("https://evil.example"), false);
});

test("file names are cleaned the same way as the current UI does it", () => {
  for (const n of ["sign.nc", "../../etc/passwd", "C:\\jobs\\shelf.gcode", "my part (v2).NC", "", null, ".hidden", "café.txt", `${"x".repeat(80)}.gcode`, "plain"]) {
    assert.equal(safeFileName(n), old.safeFileName(n), String(n));
  }
  assert.equal(safeFileName("../../etc/passwd"), "passwd.nc");
  assert.equal(safeFileName(`${"x".repeat(80)}.gcode`).length, 60);
});

test("refusals match: still connecting, or a job or motion in progress", () => {
  for (const [st, ready] of [[{ grbl: "Idle" }, true], [{ grbl: "Idle" }, false], [{ job: { file: "a" }, grbl: "Idle" }, true],
    [{ grbl: "Run" }, true], [{ grbl: "Jog" }, true], [{ grbl: "Alarm" }, true]]) {
    assert.deepEqual(refusal(st, ready), old.refusal(st, ready));
  }
});

test("messages from other pages are ignored without a reply", async () => {
  const e = event({ type: "maslow:gcode", gcode: "G0 X0" }, "https://evil.example");
  assert.equal(await handleMessage(e, { ...idle, upload: () => assert.fail("uploaded") }), null);
  assert.equal(e.posted.length, 0);
});

test("hello is answered only once the page can take a file", async () => {
  const e = event({ type: "maslow:hello" });
  assert.equal(await handleMessage(e, { ...idle, ready: () => false }), null);
  assert.deepEqual(await handleMessage(e, idle), { version: 1, type: "maslow:ready" });
  assert.deepEqual(e.posted[0], [{ version: 1, type: "maslow:ready" }, ABUNDANCE]);
});

test("a file is uploaded under its clean name and the result echoes the id; nothing runs it", async () => {
  const e = event({ type: "maslow:gcode", id: 7, name: "jobs/shelf part", gcode: "G0 X0\nG1 X10\n" });
  let got = null, received = null;
  const r = await handleMessage(e, {
    ...idle,
    upload: async (f) => { got = [f.name, await f.text()]; return { name: f.name, path: `/${f.name}` }; },
    received: (u) => { received = u; }
  });
  assert.deepEqual(got, ["shelf part.nc", "G0 X0\nG1 X10\n"]);
  assert.deepEqual(received, { name: "shelf part.nc", path: "/shelf part.nc" });
  assert.deepEqual(r, { version: 1, type: "maslow:result", id: 7, ok: true, name: "shelf part.nc", path: "/shelf part.nc" });
});

test("mid-job, disconnected, empty or a failed upload: refused with the current UI's replies", async () => {
  const notified = [];
  const deps = { upload: () => assert.fail("uploaded"), notify: (n, why) => notified.push([n, why.reason]) };
  const gcode = { type: "maslow:gcode", id: "a", name: "x.nc", gcode: "G0 X0" };
  assert.deepEqual(await handleMessage(event(gcode), { ...deps, ready: () => true, state: () => ({ job: { file: "y" }, grbl: "Run" }) }),
    { version: 1, type: "maslow:result", id: "a", ok: false, reason: "busy", detail: "The machine is running" });
  assert.equal((await handleMessage(event(gcode), { ...deps, ready: () => false, state: () => ({}) })).reason, "not-ready");
  assert.deepEqual(notified, [["x.nc", "busy"], ["x.nc", "not-ready"]]);
  assert.equal((await handleMessage(event({ ...gcode, gcode: "" }), { ...idle, ...deps })).reason, "invalid");
  const failed = await handleMessage(event(gcode), { ...idle, upload: () => Promise.reject({ reason: "rejected", detail: "HTTP 500" }) });
  assert.deepEqual([failed.ok, failed.reason, failed.detail], [false, "rejected", "HTTP 500"]);
});
