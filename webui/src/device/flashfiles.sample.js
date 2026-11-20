// A stand-in for the board's flash filesystem, for the simulator and the tests. It answers /files requests the way
// the firmware does (device/flashfiles.js describes them), so the same parsing runs on both.

// The start of a Maslow Mini's maslow.yaml (firmware/FluidNC/data/maslow-mini.yaml), with the parts a tester edits
export const SAMPLE_YAML = `board: Maslow
name: Maslow S3 Board
meta:
Maslow_vertical: false
Maslow_calibration_grid_width_mm_X: 0.000000
Maslow_calibration_grid_height_mm_Y: 0.000000
Maslow_calibration_grid_size: 9
Maslow_Retract_Current_Threshold: 1300
Maslow_Acceptable_Calibration_Threshold: 0.450000
Maslow_Apply_Tension_Belt_Retraction_Limit: 300.000000
Maslow_Extend_Dist: 2600.000000
Maslow_Scale_X: 1.000000
Maslow_Scale_Y: 1.000000
Maslow_spoilboardThickness: 12.700000
Maslow_workThickness: 6.350000
Maslow_Park_Z: 2.000000
Maslow_Park_X: 0.000000
Maslow_Park_Y: 0.000000
stepping:
  engine: Timed
  idle_ms: 240
  pulse_us: 4
  segments: 12

sdcard:
  cs_pin: gpio.10
  card_detect_pin: NO_PIN
  frequency_hz: 8000000

kinematics:
  MaslowKinematics:
    tlX: 0.000000
    tlY: 2100.000000
    tlZ: 100.000000
    trX: 3000.000000
    trY: 2100.000000
    trZ: 56.000000
    blX: 0.000000
    blY: 0.000000
    blZ: 34.000000
    brX: 3000.000000
    brY: 0.000000
    brZ: 78.000000
    beltEndExtension: 30.000000
    armLength: 123.400002

SpindleBoard:
  phase_deg_per_mm: 45.000000
  max_rpm: 14000
  speed_map: 0=0.000% 14000=100.000%
  off_on_alarm: true

arc_tolerance_mm: 0.002000
junction_deviation_mm: 0.010000
report_inches: false
planner_blocks: 16
`;

const PREFERENCES = `{"enable_lock_UI":"false","enable_ping":"true","enable_DHT":"false","enable_camera":"false","language":"en","xy_feedrate":"2500","z_feedrate":"300"}
`;

const TOTAL = 2 * 1024 * 1024;   // max_littlefs.csv: a 2 MB partition
const BLOCK = 4096;
const fmt = (b) => (b < 1024 ? `${b} B` : b < 1024 * 1024 ? `${(b / 1024).toFixed(2)} KB` : `${(b / 1024 / 1024).toFixed(2)} MB`);

export function createFlashFs() {
  const files = new Map([
    ["/index.html.gz", { size: 145194 }],
    ["/maslow.yaml", { text: SAMPLE_YAML }],
    ["/preferences.json", { text: PREFERENCES }],
    ["/favicon.ico", { size: 1150 }]
  ]);
  const dirs = new Set(["/"]);
  const sizeOf = (f) => (f.text != null ? new TextEncoder().encode(f.text).length : f.size);
  const used = () => [...files.values()].reduce((n, f) => n + Math.ceil(sizeOf(f) / BLOCK) * BLOCK, 0) + 8 * BLOCK;
  const clean = (p) => (`/${String(p || "").replace(/^\/+|\/+$/g, "")}`);
  const inside = (dir, name) => `${dir === "/" ? "" : dir}/${name}`;

  function listing(dir, status) {
    const prefix = dir === "/" ? "/" : `${dir}/`;
    const kids = [];
    for (const [p, f] of files) if (p.startsWith(prefix) && !p.slice(prefix.length).includes("/")) kids.push({ name: p.slice(prefix.length), shortname: p.slice(prefix.length), size: sizeOf(f), datetime: "" });
    for (const p of dirs) if (p !== "/" && p.startsWith(prefix) && !p.slice(prefix.length).includes("/")) kids.push({ name: p.slice(prefix.length), shortname: p.slice(prefix.length), size: -1, datetime: "" });
    const u = used();
    return { files: kids, path: dir.slice(1), total: fmt(TOTAL), used: fmt(u), occupation: Math.round((u * 100) / TOTAL), status };
  }

  return {
    // A /files GET: the query as URLSearchParams or its text
    request(query) {
      const q = new URLSearchParams(query);
      const dir = clean(q.get("path"));
      if (!dirs.has(dir)) return { status: "No SD card" };   // what the firmware says when the path can't be opened
      const action = q.get("action"), name = q.get("filename");
      if (!action || !name) return listing(dir, "Ok");
      const p = inside(dir, name);
      if (action === "delete") return listing(dir, files.delete(p) ? `${name} deleted` : `Cannot delete ${name} No such file or directory`);
      if (action === "deletedir") {
        if (!dirs.has(p)) return listing(dir, `Cannot delete ${name} No such file or directory`);
        for (const k of [...files.keys()]) if (k.startsWith(`${p}/`)) files.delete(k);
        for (const k of [...dirs]) if (k === p || k.startsWith(`${p}/`)) dirs.delete(k);
        return listing(dir, `${name} deleted`);
      }
      if (action === "createdir") {
        if (dirs.has(p) || files.has(p)) return listing(dir, `Cannot create ${name} File exists`);
        dirs.add(p);
        return listing(dir, `${name} created`);
      }
      if (action === "rename") {
        const newname = q.get("newname");
        if (newname == null) return listing(dir, "Missing new filename");
        const to = inside(dir, newname);
        // A rename onto a taken name fails here, as it may on the board
        if (files.has(to) || dirs.has(to)) return listing(dir, `Cannot rename ${name} File exists`);
        if (files.has(p)) { files.set(to, files.get(p)); files.delete(p); }
        else if (dirs.has(p)) {
          dirs.delete(p); dirs.add(to);
          for (const k of [...files.keys()]) if (k.startsWith(`${p}/`)) { files.set(to + k.slice(p.length), files.get(k)); files.delete(k); }
        } else return listing(dir, `Cannot rename ${name} No such file or directory`);
        return listing(dir, `${name} renamed to ${newname}`);
      }
      return listing(dir, "Ok");
    },
    // An upload: replaces the file. content is the text, or { size } for a file the simulator doesn't keep.
    write(path, content) {
      const dir = clean(path.replace(/\/[^/]*$/, ""));
      if (!dirs.has(dir)) return { status: "Upload failed" };
      const f = typeof content === "string" ? { text: content } : { size: content.size, blob: content.blob };
      const old = files.get(path);
      if (used() - (old ? Math.ceil(sizeOf(old) / BLOCK) * BLOCK : 0) + Math.ceil(sizeOf(f) / BLOCK) * BLOCK > TOTAL) return listing(dir, "Upload failed");
      files.set(path, f);
      return listing(dir, "Ok");
    },
    // A file's contents: its text, a Blob for an uploaded file, or null if it isn't there
    read(path) {
      const f = files.get(path);
      return !f ? null : f.text != null ? f.text : f.blob || null;
    }
  };
}
