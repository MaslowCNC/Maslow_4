import { test } from "node:test";
import assert from "node:assert/strict";
import {
  parseFileList, parseBytes, filesQuery, fileUrl, nameError, crumbs, joinPath, parentPath, hasRoom, checkYaml,
  protectedWhy, isEditable, isConfig, statusFailed, flashActions
} from "./flashfiles.js";
import { createFlashFs, SAMPLE_YAML } from "./flashfiles.sample.js";

// As the firmware's file list response writes it (WebUIServer.cpp, create_file_list_response)
const ANSWER = `{"files":[{"name":"maslow.yaml","shortname":"maslow.yaml","size":6165,"datetime":""},
{"name":"backups","shortname":"backups","size":-1,"datetime":""},
{"name":"index.html.gz","shortname":"index.html.gz","size":145194,"datetime":""}],
"path":"","total":"1.88 MB","used":"176.00 KB","occupation":9,"status":"Ok"}`;

test("parseFileList reads the firmware's listing: folders first, sizes in bytes, free space", () => {
  const l = parseFileList(ANSWER);
  assert.equal(l.path, "/");
  assert.deepEqual(l.files.map((f) => [f.name, f.dir, f.size]),
    [["backups", true, null], ["index.html.gz", false, 145194], ["maslow.yaml", false, 6165]]);
  assert.equal(l.total, Math.round(1.88 * 1024 * 1024));
  assert.equal(l.used, 176 * 1024);
  assert.equal(l.free, l.total - l.used);
  assert.equal(l.failed, null);
  assert.equal(parseFileList({ path: "a/b", status: "Cannot delete x No such file", files: [] }).failed, "Cannot delete x No such file");
  assert.equal(parseFileList({ path: "a/b", files: [] }).path, "/a/b");
});

test("parseBytes reads formatBytes()'s forms", () => {
  assert.equal(parseBytes("512 B"), 512);
  assert.equal(parseBytes("2.00 KB"), 2048);
  assert.equal(parseBytes("2.00 MB"), 2097152);
  assert.equal(parseBytes("lots"), null);
});

test("statusFailed tells an action that didn't work from one that did", () => {
  for (const s of ["Cannot rename a File exists", "Upload failed", "Missing new filename", "No SD card"]) assert.ok(statusFailed(s), s);
  for (const s of ["Ok", "a deleted", "a renamed to b", "x created"]) assert.ok(!statusFailed(s), s);
});

test("filesQuery and fileUrl build the requests the firmware expects", () => {
  assert.equal(filesQuery("/"), "path=%2F");
  assert.equal(filesQuery("/backups", "delete", "a b.yaml"), "path=%2Fbackups%2F&action=delete&filename=a+b.yaml");
  assert.equal(filesQuery("/", "rename", "index.html.gz", "index-old.html.gz"),
    "path=%2F&action=rename&filename=index.html.gz&newname=index-old.html.gz");
  assert.equal(fileUrl("/backups/a b.yaml"), "/backups/a%20b.yaml");
  assert.equal(fileUrl("/maslow.yaml", "/flashfs"), "/flashfs/maslow.yaml");
});

test("paths and breadcrumbs", () => {
  assert.equal(joinPath("/", "a"), "/a");
  assert.equal(joinPath("/a", "b"), "/a/b");
  assert.equal(parentPath("/a/b"), "/a");
  assert.equal(parentPath("/a"), "/");
  assert.deepEqual(crumbs("/a/b"), [["/", "Flash"], ["/a", "a"], ["/a/b", "b"]]);
});

test("nameError refuses empty, nested, odd and taken names, and explains a taken one", () => {
  const files = parseFileList(ANSWER).files;
  assert.equal(nameError("index-old.html.gz", files), null);
  assert.match(nameError("", files), /Type a name/);
  assert.match(nameError("a/b", files), /can't contain/);
  assert.match(nameError(" a", files), /spaces/);
  assert.match(nameError("..", files), /Not a usable/);
  assert.match(nameError("maslow.yaml", files), /already exists.*may fail/);
  assert.equal(nameError("maslow.yaml", files, { except: "maslow.yaml" }), null);
});

test("hasRoom leaves a margin, counts the file being replaced, and lets an unknown size through", () => {
  const l = { free: 20000 };
  assert.ok(hasRoom(l, 10000));
  assert.ok(!hasRoom(l, 15000));
  assert.ok(hasRoom(l, 15000, 6000));
  assert.ok(hasRoom({ free: null }, 1e9));
});

test("protected files, editable files and the config", () => {
  assert.match(protectedWhy("/index.html.gz"), /page/);
  assert.match(protectedWhy("/maslow.yaml"), /configuration/);
  assert.equal(protectedWhy("/backups/maslow.yaml"), null);
  assert.ok(isEditable({ name: "maslow.yaml", size: 6000 }));
  assert.ok(isEditable({ name: "maslow.yaml.bak", size: 6000 }));
  assert.ok(!isEditable({ name: "index.html.gz", size: 145194 }));
  assert.ok(!isEditable({ name: "big.yaml", size: 200000 }));
  assert.ok(!isEditable({ name: "conf", dir: true }));
  assert.ok(isConfig("/maslow.yaml"));
  assert.ok(!isConfig("/maslow.yaml.bak"));
  assert.ok(!isConfig("/preferences.json"));
});

test("checkYaml blocks an empty file and tabs, and warns about lines that aren't name: value", () => {
  assert.deepEqual(checkYaml(SAMPLE_YAML), { errors: [], warnings: [] });
  assert.match(checkYaml("  \n").errors[0], /empty/);
  assert.match(checkYaml("a:\n\tb: 1\n").errors[0], /Tabs.*line 2/);
  const r = checkYaml("a: 1\nthis is wrong\n# a comment\nb:\n  - x\n");
  assert.deepEqual(r.errors, []);
  assert.match(r.warnings[0], /Line 2 doesn't/);
});

test("the stand-in filesystem answers like the firmware", () => {
  const fs = createFlashFs();
  let l = parseFileList(fs.request(filesQuery("/")));
  assert.deepEqual(l.files.map((f) => f.name), ["favicon.ico", "index.html.gz", "maslow.yaml", "preferences.json"]);
  assert.equal(l.total, 2 * 1024 * 1024);
  l = parseFileList(fs.request(filesQuery("/", "createdir", "backups")));
  assert.equal(l.files[0].name, "backups");
  fs.write("/backups/old.yaml", "a: 1\n");
  assert.deepEqual(parseFileList(fs.request(filesQuery("/backups"))).files.map((f) => f.name), ["old.yaml"]);
  // A rename onto a taken name fails, and says so
  l = parseFileList(fs.request(filesQuery("/", "rename", "index.html.gz", "maslow.yaml")));
  assert.match(l.failed, /Cannot rename/);
  l = parseFileList(fs.request(filesQuery("/", "rename", "index.html.gz", "index-old.html.gz")));
  assert.equal(l.failed, null);
  assert.ok(l.files.some((f) => f.name === "index-old.html.gz"));
  l = parseFileList(fs.request(filesQuery("/", "deletedir", "backups")));
  assert.ok(!l.files.some((f) => f.name === "backups"));
  assert.equal(fs.read("/maslow.yaml"), SAMPLE_YAML);
  assert.equal(parseFileList(fs.write("/huge.bin", { size: 3e6 })).failed, "Upload failed");
});

// flashActions over the stand-in filesystem, as the simulator wires it
function harness({ locked = null } = {}) {
  const fs = createFlashFs();
  let flash = { status: "idle", path: "/", files: [], free: null, total: null, busy: null, notice: null, editor: null };
  const logs = [];
  const a = flashActions({
    get: () => flash, set: (p) => { flash = { ...flash, ...p }; }, log: (l) => logs.push(l), lock: () => locked,
    io: {
      list: async (dir) => fs.request(filesQuery(dir)), act: async (q) => fs.request(q),
      upload: async (path, c) => fs.write(path, typeof c === "string" ? c : await c.text()), read: async (p) => fs.read(p),
      download: () => {}
    }
  });
  return { a, fs, logs, get: () => flash };
}

test("flashActions: list, folder, rename (refusing a taken name), delete", async () => {
  const { a, get } = harness();
  await a.listFlash();
  assert.equal(get().status, "ready");
  assert.equal(get().files.length, 4);
  await a.flashAction({ action: "createdir", name: "backups" });
  assert.equal(get().files[0].name, "backups");
  await a.flashAction({ action: "rename", name: "index.html.gz", newname: "maslow.yaml" });
  assert.match(get().notice.text, /already exists/);
  assert.ok(get().files.some((f) => f.name === "index.html.gz"));
  await a.flashAction({ action: "rename", name: "index.html.gz", newname: "index-old.html.gz" });
  assert.ok(get().files.some((f) => f.name === "index-old.html.gz"));
  await a.flashAction({ action: "delete", name: "favicon.ico" });
  assert.ok(!get().files.some((f) => f.name === "favicon.ico"));
  await a.listFlash({ path: "/backups" });
  assert.equal(get().path, "/backups");
  assert.deepEqual(get().files, []);
});

test("flashActions: saving maslow.yaml keeps a .bak first, offers a restart, and Undo puts the original back", async () => {
  const { a, fs, get } = harness();
  await a.listFlash();
  await a.openFlashFile({ name: "maslow.yaml" });
  assert.equal(get().editor.status, "ready");
  const edited = SAMPLE_YAML.replace("Maslow_Park_Z: 2.000000", "Maslow_Park_Z: 5.000000");
  await a.saveFlashFile({ text: edited });
  assert.equal(get().editor.status, "saved");
  assert.equal(get().editor.backup, "maslow.yaml.bak");
  assert.equal(get().editor.restart, true);
  assert.equal(fs.read("/maslow.yaml.bak"), SAMPLE_YAML);
  assert.equal(fs.read("/maslow.yaml"), edited);
  assert.ok(get().files.some((f) => f.name === "maslow.yaml.bak"));
  // A second save keeps the first backup (the original), not the first save
  await a.saveFlashFile({ text: edited + "# again\n" });
  assert.equal(fs.read("/maslow.yaml.bak"), SAMPLE_YAML);
  await a.restoreFlashFile();
  assert.equal(get().editor.status, "restored");
  assert.equal(fs.read("/maslow.yaml"), SAMPLE_YAML);
  // Tabs are refused before anything is sent
  await a.saveFlashFile({ text: "a:\n\tb: 1\n" });
  assert.equal(get().editor.status, "failed");
  assert.equal(fs.read("/maslow.yaml"), SAMPLE_YAML);
});

test("flashActions: nothing changes while locked", async () => {
  const { a, get, fs } = harness({ locked: "Not while a job runs." });
  await a.listFlash();
  await a.flashAction({ action: "delete", name: "maslow.yaml" });
  assert.equal(get().notice.text, "Not while a job runs.");
  assert.equal(fs.read("/maslow.yaml"), SAMPLE_YAML);
});
