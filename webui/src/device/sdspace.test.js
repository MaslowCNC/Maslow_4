// The SD card's space from the firmware's listing (GET /upload, WebUIServer.cpp). Run with `npm test`.
import { test } from "node:test";
import assert from "node:assert/strict";
import { sdSpace } from "./machine.js";
import { simSd } from "./simulator.js";

test("total and used come as the firmware writes them, occupation as a percent", () => {
  assert.deepEqual(sdSpace({ files: [], path: "/", total: "29.72 GB", used: "1.21 MB", occupation: 0, status: "Ok" }),
    { total: "29.72 GB", used: "1.21 MB", percent: 0 });
  assert.equal(sdSpace({ total: "7.40 GB", used: "3.70 GB", occupation: "50" }).percent, 50);
});

test("no card (no capacity) or an old reply without the fields: nothing shown", () => {
  assert.equal(sdSpace({ total: "0 B", used: "0 B", occupation: 100 }), null);
  assert.equal(sdSpace({ files: [] }), null);
  assert.equal(sdSpace(null), null);
});

test("the simulator's card counts its files", () => {
  const a = simSd([]), b = simSd([{ size: "1024 KB" }]);
  assert.equal(a.total, "7.40 GB");
  assert.equal(parseFloat(b.used) - parseFloat(a.used), 1);
});
