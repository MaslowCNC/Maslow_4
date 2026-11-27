import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync, existsSync } from "node:fs";
import {
  parseVersion, compareVersions, isNewer, sameVersion, parseFwVersion, selectRelease, safeTag, mirrorUrl, manifestError,
  checkFirmware, checkPage, fileKind, statusRows, updateLock, updateActions, UPDATE_IDLE
} from "./update.js";
import { SIM_RELEASES, simFirmware, simManifest } from "./update.sample.js";

const v = (t) => parseVersion(t);
const cmp = (a, b) => compareVersions(v(a), v(b));

test("versions compare by semver, ignoring a local build's tail", () => {
  assert.deepEqual(v("FluidNC v1.27.0 (Maslow-Main-abc)"), { major: 1, minor: 27, patch: 0, pre: [] });
  assert.deepEqual(v("v1.28.0-exp.1-dirty").pre, ["exp", "1"]);
  assert.deepEqual(v("v1.27.0-12-g8700a9bf9").pre, []);
  assert.equal(cmp("v1.28.0-exp.2", "v1.28.0-exp.10"), -1);
  assert.equal(cmp("v1.28.0-exp.10", "v1.28.0"), -1);
  assert.equal(cmp("v1.28.0", "v1.27.9"), 1);
  assert.equal(cmp("v1.27.0-dirty", "v1.27.0"), 0);
  assert.equal(v("no version"), null);
});

test("newer: only by semver; an unreadable version counts if the strings differ", () => {
  assert.equal(isNewer("v1.27.0", "v1.28.0-exp.1"), true);
  assert.equal(isNewer("v1.28.0", "v1.28.0-exp.3"), false);
  assert.equal(isNewer("v1.28.0-exp.1-dirty", "v1.28.0-exp.1"), false);
  assert.equal(isNewer("", "nightly"), true);
  assert.equal(sameVersion("v1.28.0-pre.1-dirty", "v1.28.0-pre.1"), true);
});

test("[ESP800] gives the firmware version", () => {
  assert.equal(parseFwVersion("FW version: FluidNC v1.28.0-pre.1-dirty (Maslow-Main-43a8f6d) # FW target:grbl-embedded  # board:unified"), "v1.28.0-pre.1-dirty");
  assert.equal(parseFwVersion("nothing"), null);
});

test("each stream picks its release; drafts and releases without firmware.bin don't count", () => {
  const list = [
    { tag_name: "v1.29.0-exp.1", prerelease: true, draft: true, assets: [{ name: "firmware.bin" }] },
    { tag_name: "nightly", prerelease: true, assets: [{ name: "firmware.bin" }] },
    { tag_name: "v1.28.0-exp.2", prerelease: true, assets: [{ name: "firmware.bin" }], body: " notes " },
    { tag_name: "v1.28.0", prerelease: false, assets: [] },
    { tag_name: "v1.27.0", prerelease: false, assets: [{ name: "firmware.bin" }, { name: "index.html.gz" }] }
  ];
  assert.equal(selectRelease(list, "release").tag, "v1.27.0");
  assert.equal(selectRelease(list, "experimental").tag, "v1.28.0-exp.2");
  assert.equal(selectRelease(list, "experimental").notes, "notes");
  assert.equal(selectRelease(list, "nightly").tag, "nightly");
  assert.equal(selectRelease([], "nightly"), null);
  assert.equal(selectRelease(SIM_RELEASES, "release").tag, "v1.28.0");
});

test("the mirror's folder for a tag is release.yml's SAFE_TAG, and its manifest must be that tag", () => {
  assert.equal(safeTag("v1.28.0-exp.1"), "v1.28.0-exp.1");
  assert.equal(safeTag("a/b c+d"), "a-b-c_d");
  assert.match(mirrorUrl("v1.28.0", "firmware.bin"), /web-assets\/v1\.28\.0\/firmware\.bin$/);
  const m = { tag_name: "v1.28.0", files: { "firmware.bin": { size_bytes: 10 } } };
  assert.equal(manifestError(m, "v1.28.0"), null);
  assert.match(manifestError({ ...m, tag_name: "v1.27.0" }, "v1.28.0"), /for v1\.27\.0, not v1\.28\.0/);
  assert.match(manifestError({ tag_name: "v1.28.0", files: {} }, "v1.28.0"), /no firmware\.bin/);
});

test("a firmware file: an ESP32-S3 Maslow image, with its version read from it", () => {
  const ok = checkFirmware(simFirmware("v1.28.0"));
  assert.equal(ok.ok, true);
  assert.equal(ok.version, "v1.28.0");
  assert.deepEqual(ok.warnings, []);
  const page = checkFirmware(new Uint8Array([0x1f, 0x8b, ...new Array(100).fill(0)]));
  assert.match(page.errors[0], /gzipped/);
  const other = simFirmware("v1.28.0"); other[12] = 0;   // plain ESP32
  assert.match(checkFirmware(other).errors[0], /another ESP32 chip/);
  const plain = checkFirmware(simFirmware(null));
  assert.equal(plain.ok, true);
  assert.match(plain.warnings[0], /Maslow build/);
  assert.equal(fileKind("x.bin", simFirmware("v1")), "firmware");
  assert.equal(fileKind("index.html.gz", new Uint8Array([0x1f, 0x8b])), "page");
  assert.equal(checkPage(new Uint8Array([0x1f, 0x8b, 8])).ok, true);
  assert.equal(checkPage(new Uint8Array([0x3c, 0x68])).ok, false);
});

// The release files the repository mirrors, when this checkout has them
for (const dir of ["v1.27.0", "latest"]) {
  const f = new URL(`../../../web-assets/${dir}/firmware.bin`, import.meta.url);
  test(`the mirrored ${dir} firmware.bin passes the check`, { skip: !existsSync(f) }, () => {
    const c = checkFirmware(readFileSync(f));
    assert.equal(c.ok, true);
    assert.match(c.version, /^v1\.2\d\.\d/);
    const m = JSON.parse(readFileSync(new URL(`../../../web-assets/${dir}/manifest.json`, import.meta.url), "utf8"));
    assert.equal(sameVersion(c.version, m.tag_name), true);
  });
}

test("system status: [ESP420] and $wifi/status as labelled rows", () => {
  const esp420 = "[MSG:Chip ID: 4660]\n[MSG:CPU Frequency: 240Mhz]\n[MSG:Free memory: 101.23 KB]\n[MSG:Mode=STA:SSID=Shop:Status=Connected:IP=192.168.1.4:MAC=AA]\n[MSG:FW version: FluidNC v1.28.0 (x)]\nok";
  const wifi = "[MSG:Signal: 72%]\n[MSG:IP: 192.168.1.4]\n[MSG:Hostname: maslow]";
  const rows = Object.fromEntries(statusRows(esp420, wifi));
  assert.equal(rows.Firmware, "FluidNC v1.28.0 (x)");
  assert.equal(rows.Processor, "240 MHz");
  assert.equal(rows.Signal, "72%");
  assert.equal(rows["IP address"], "192.168.1.4");
  assert.equal(statusRows(esp420)[0][0], "Firmware");
});

test("updates are off during a job, while moving, disconnected or read-only", () => {
  const idle = { connected: true, maslow: 7, fluid: "Idle" };
  assert.equal(updateLock(idle), null);
  assert.match(updateLock({ ...idle, connected: false }), /Waiting/);
  assert.match(updateLock({ ...idle, job: { file: "a" } }), /job/);
  assert.match(updateLock({ ...idle, maslow: 3 }), /moves/);
  assert.match(updateLock({ ...idle, fluid: "Jog" }), /moves/);
  assert.match(updateLock({ ...idle, readOnly: true }), /Another tab/);
});

// A fake board and GitHub for the actions
function harness({ releases = SIM_RELEASES, offline = false, dev = { connected: true, maslow: 4, fluid: "Idle", fwVersion: "v1.27.0" }, mirror = {} } = {}) {
  let u = UPDATE_IDLE;
  const sent = [], logs = [], timers = [];
  const fw = simFirmware("v1.28.0"), page = new Uint8Array([0x1f, 0x8b, 1, 2, 3]);
  const files = { "manifest.json": simManifest("v1.28.0", fw, page), "firmware.bin": fw, "index.html.gz": page, ...mirror };
  const a = updateActions({
    get: () => u, set: (p) => { u = { ...u, ...p }; }, device: () => dev, log: (l) => logs.push(l),
    timer: (fn) => timers.push(fn),
    io: {
      releases: async () => { if (offline) throw { offline: true, message: "Failed to fetch" }; return releases; },
      fetchJson: async (url) => files[url.split("/").pop()],
      fetchBytes: async (url, on) => { const b = files[url.split("/").pop()]; on(b.length, b.length); return b; },
      sha256: async () => null,
      uploadPage: async (blob) => { sent.push(["page", blob.size]); },
      uploadFirmware: async (blob, on) => { on(blob.size, blob.size); sent.push(["firmware", blob.size]); }
    }
  });
  return { a, get: () => u, sent, logs, timers, dev, fw };
}

test("check, then install the release: page first, then firmware, then the restart and the version check", async () => {
  const h = harness();
  await h.a.checkUpdates({ stream: "release" });
  assert.equal(h.get().check.release.tag, "v1.28.0");
  assert.equal(h.get().check.newer, true);
  await h.a.installUpdate({ source: "release" });
  assert.deepEqual(h.sent.map(([k]) => k), ["page", "firmware"]);
  assert.equal(h.get().install.phase, "restarting");
  assert.equal(h.get().install.expect, "v1.28.0");
  h.a.updateConnected("v1.28.0-dirty");
  assert.equal(h.get().install.phase, "done");
  assert.equal(h.get().install.match, true);
  assert.equal(h.get().check.newer, false);
});

test("the board coming back on another version is reported, not hidden", async () => {
  const h = harness();
  await h.a.checkUpdates({ stream: "release" });
  await h.a.installUpdate({ source: "release" });
  h.a.updateConnected("v1.27.0");
  assert.equal(h.get().install.match, false);
});

test("no internet: the check says so", async () => {
  const h = harness({ offline: true });
  await h.a.checkUpdates({ stream: "release" });
  assert.equal(h.get().check.state, "offline");
});

test("a mirror holding another release's files stops the install before anything is sent", async () => {
  const h = harness({ mirror: { "manifest.json": { tag_name: "v1.27.0", files: { "firmware.bin": { size_bytes: 1 } } } } });
  await h.a.checkUpdates({ stream: "release" });
  await h.a.installUpdate({ source: "release" });
  assert.equal(h.get().install.phase, "failed");
  assert.match(h.get().install.detail, /for v1\.27\.0/);
  assert.deepEqual(h.sent, []);
});

test("a download of the wrong size stops the install", async () => {
  const fw = simFirmware("v1.28.0");
  const h = harness({ mirror: { "firmware.bin": fw.subarray(0, 100) } });
  await h.a.checkUpdates({ stream: "release" });
  await h.a.installUpdate({ source: "release" });
  assert.match(h.get().install.detail, /arrived with 100 bytes/);
  assert.deepEqual(h.sent, []);
});

test("files from this computer: checked when chosen, refused when wrong, installed when right", async () => {
  const h = harness();
  const file = (name, bytes) => ({ name, size: bytes.length, arrayBuffer: async () => bytes.buffer.slice(0), [Symbol.toStringTag]: "Blob" });
  await h.a.chooseUpdateFiles({ files: [file("notes.txt", new Uint8Array([65, 66, 67]))] });
  assert.equal(h.get().files[0].ok, false);
  await h.a.installUpdate({ source: "files" });
  assert.equal(h.get().install.phase, "failed");
  assert.deepEqual(h.sent, []);
  // A firmware replaces the bad pick of the same kind
  await h.a.chooseUpdateFiles({ files: [file("firmware.bin", h.fw)] });
  assert.equal(h.get().files.length, 1);
  assert.equal(h.get().files[0].version, "v1.28.0");
  h.dev.fwVersion = "v1.27.0";
  await h.a.installUpdate({ source: "files" });
  assert.deepEqual(h.sent.map(([k]) => k), ["firmware"]);
  assert.equal(h.get().install.expect, "v1.28.0");
});

test("no update while a job runs, even if the button was pressed", async () => {
  const h = harness({ dev: { connected: true, maslow: 7, fluid: "Run", job: { file: "a.nc" }, fwVersion: "v1.27.0" } });
  await h.a.checkUpdates({ stream: "release" });
  await h.a.installUpdate({ source: "release" });
  assert.equal(h.get().install.phase, "failed");
  assert.deepEqual(h.sent, []);
});

test("a board that isn't back after a while is flagged as slow", async () => {
  const h = harness();
  await h.a.checkUpdates({ stream: "release" });
  await h.a.installUpdate({ source: "release" });
  h.timers.forEach((fn) => fn());
  assert.equal(h.get().install.slow, true);
});

test("the check on connect (quiet) logs a newer release, not a missing internet", async () => {
  const off = harness({ offline: true });
  await off.a.checkUpdates({ stream: "release", quiet: true });
  assert.equal(off.get().check.state, "offline");
  assert.deepEqual(off.logs, []);
  const on = harness();
  await on.a.checkUpdates({ stream: "release", quiet: true });
  assert.match(on.logs[0], /v1\.28\.0/);
});
