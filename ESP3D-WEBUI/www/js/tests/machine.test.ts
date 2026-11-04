import { beforeAll, beforeEach, describe, expect, test } from "bun:test";

const g = globalThis as any;

const makeSocket = () => ({
  readyState: 1, // WebSocket.OPEN
  sent: [] as string[],
  send(text: string) {
    this.sent.push(text);
  },
});

/** Lines from the machine, routed the way socket.js does. */
const reply = (...lines: string[]) => {
  for (const line of lines) {
    g.wsCommands.handleLine(line);
    g.machine.handleLine(line);
  }
};
const tick = (ms = 0) => new Promise((r) => setTimeout(r, ms));
const lastSent = () => g.ws_source.sent[g.ws_source.sent.length - 1];
const BANNER = "Grbl 3.9 [FluidNC v1.26.0 (wifi) '$' for help]";

let messages: string[];

beforeAll(async () => {
  g.WebSocket = g.WebSocket ?? { OPEN: 1 };
  await import("../wscommands.js");
  await import("../machine.js");
});

beforeEach(() => {
  messages = [];
  g.addMessage = (m: string) => messages.push(m);
  g.ws_source = makeSocket();
  g.wsCommands.reset();
  // A calibrated machine sitting idle.
  reply("<Idle|MPos:0,0,0|FS:0,0|WCO:0,0,0|MS:7>");
  g.ws_source.sent.length = 0;
});

describe("state store", () => {
  test("status reports give state, positions and job progress", () => {
    reply("<Run|MPos:110.5,20,-3|FS:800,0|WCO:10,10,-1|SD:42.50,/sd/part.nc,120>");
    const s = g.machine.state;
    expect(s.grbl).toBe("Run");
    expect(s.mpos).toEqual({ x: 110.5, y: 20, z: -3 });
    expect(s.wpos).toEqual({ x: 100.5, y: 10, z: -2 });
    expect(s.job).toEqual({ pct: 42.5, file: "/sd/part.nc", line: 120 });
    reply("<Hold:0|MPos:110.5,20,-3|FS:0,0>");
    expect(g.machine.state.grbl).toBe("Hold");
    expect(g.machine.state.grblDetail).toBe("0");
    expect(g.machine.state.job).toBe(null);
  });

  test("Maslow state, MINFO and G-code units", () => {
    reply(
      "<Idle|MPos:0,0,0|FS:0,0|MS:4>",
      'MINFO: { "homed": false, "calibrationInProgress": false, "tl": 1800.5, "tr": 1801, "br": 1900, "bl": 1899, "etl": 0, "etr": 0, "ebr": 0, "ebl": 0, "extended": true }',
      "[GC:G0 G54 G17 G20 G90 G94 M5 M9 T0 F0 S0]",
    );
    const s = g.machine.state;
    expect(s.maslow).toBe(4);
    expect(s.belts.tl).toBe(1800.5);
    expect(s.units).toBe("inch");
  });

  test("MS gives the Maslow state from every report", () => {
    reply("<Idle|MPos:0,0,0|FS:0,0|MS:4>");
    expect(g.machine.state.maslow).toBe(4);
    reply("<Home|MPos:0,0,0|FS:0,0|MS:1>");
    expect(g.machine.state.maslow).toBe(1);
  });

  test("belt lengths and errors are kept between the reports that carry them", () => {
    reply("<Jog|MPos:0,0,0|FS:500,0|MS:7|BL:1800.25,1801.5,1900.75,1899|BE:0.012,-0.034,0,0.5>");
    const belts = { tl: 1800.25, tr: 1801.5, bl: 1900.75, br: 1899, etl: 0.012, etr: -0.034, ebl: 0, ebr: 0.5 };
    expect(g.machine.state.belts).toMatchObject(belts);
    reply("<Jog|MPos:1,0,0|FS:500,0|MS:7>", "<Jog|MPos:2,0,0|FS:500,0|MS:7|WCO:0,0,0>");
    expect(g.machine.state.belts).toMatchObject(belts);
    // MINFO merges in homed/extended, but doesn't override lengths the status report gave
    reply('MINFO: { "homed": true, "calibrationInProgress": false, "tl": 0.01, "tr": 1801.5, "br": 1899, "bl": 1900.75, "etl": 0, "etr": 0, "ebr": 0, "ebl": 0, "extended": true }');
    expect(g.machine.state.belts).toMatchObject({ tl: 1800.25, homed: true, extended: true });
  });

  test("MC gives the motor currents", () => {
    reply("<Idle|N:12|MPos:0,0,0|FS:0,0|MS:7|BL:1,2,3,4|BE:0,0,0,0|MC:120,98,0,4095>");
    expect(g.machine.state.motorCurrent).toEqual({ tl: 120, tr: 98, bl: 0, br: 4095 });
  });

  test("a '?' belt length is unknown (null)", () => {
    reply("<Idle|MPos:0,0,0|FS:0,0|MS:0|BL:1814.59,?,1790.66,?|BE:0,0,0,0>");
    expect(g.machine.state.belts).toMatchObject({ tl: 1814.59, tr: null, bl: 1790.66, br: null });
  });

  test("on('state') hears changes and can unsubscribe", () => {
    const seen: string[] = [];
    const off = g.machine.on("state", (s: any) => seen.push(s.grbl));
    reply("<Jog|MPos:0,0,0>");
    off();
    reply("<Idle|MPos:0,0,0>");
    expect(seen).toEqual(["Jog"]);
  });
});

describe("belt operations", () => {
  const idle = (ms: number) => `<Idle|MPos:0,0,0|FS:0,0|MS:${ms}>`;
  const home = (ms: number) => `<Home|MPos:0,0,0|FS:0,0|MS:${ms}>`;

  test("retract: accepted on ok, done when RETRACTED and Idle", async () => {
    reply(idle(4));
    const op = g.machine.retractBelts();
    let accepted = false;
    let done = false;
    op.accepted.then(() => (accepted = true));
    op.then(() => (done = true));
    expect(lastSent()).toBe("$ALL\n");
    reply("[MSG:INFO: Retracting all belts]", "ok");
    await tick();
    expect(accepted).toBe(true);
    expect(lastSent()).toBe("?"); // asks for a fresh report to see the operation start
    reply(home(1));
    await tick();
    reply(home(2));
    await tick();
    expect(done).toBe(false); // still Home
    reply(idle(2));
    await op;
    expect(done).toBe(true);
    expect(g.machine.runningCount()).toBe(0);
  });

  test("already retracted: a stale report doesn't finish it, the start state must show", async () => {
    reply(idle(2));
    const op = g.machine.retractBelts();
    let done = false;
    op.then(() => (done = true));
    reply("ok", idle(2)); // a report from before the command
    await tick();
    expect(done).toBe(false);
    reply(home(1), home(2), idle(2));
    await op;
  });

  test("a refused request fails after two fresh reports show no change", async () => {
    reply(idle(0));
    const op = g.machine.extendBelts();
    reply("[MSG:INFO: Cannot extend the belts until they have been retracted]", "ok");
    await tick();
    reply(idle(0));
    await tick();
    let settled = false;
    op.catch(() => (settled = true));
    await tick();
    expect(settled).toBe(false); // one report could be stale
    reply(idle(0));
    await expect(op).rejects.toMatchObject({ reason: "failed", detail: "extend belts didn't start: the machine stayed in UNKNOWN" });
    await expect(op.accepted).resolves.toBeUndefined();
  });

  test("apply tension falling back to EXTENDEDOUT fails it", async () => {
    reply(idle(4));
    const op = g.machine.applyTension();
    reply("ok", home(5));
    await tick();
    reply(idle(4));
    await expect(op).rejects.toMatchObject({ reason: "failed", detail: "apply tension failed: now EXTENDEDOUT" });
  });

  test("calibrate succeeds on READY_TO_CUT", async () => {
    reply(idle(4));
    const op = g.machine.calibrate();
    expect(lastSent()).toBe("$CAL\n");
    reply("ok", home(6));
    await tick();
    reply("[MSG:INFO: Calibration complete]", idle(7));
    await expect(op).resolves.toBeUndefined();
  });

  test("a firmware error rejects both promises with the code", async () => {
    const op = g.machine.retractBelts();
    reply("error:152");
    await expect(op).rejects.toMatchObject({ reason: "rejected", code: 152 });
    await expect(op.accepted).rejects.toMatchObject({ reason: "rejected" });
  });
});

describe("motion", () => {
  test("jog sends mm with G21 and is done after the machine is Idle again", async () => {
    const op = g.machine.jog({ x: -10, y: 2.5 }, { feed: 1500 });
    expect(lastSent()).toBe("$J=G91 G21 F1500 X-10 Y2.5\n");
    reply("ok");
    await op.accepted;
    let done = false;
    op.then(() => (done = true));
    reply("<Idle|MPos:0,0,0>"); // stale report from before the jog started
    await tick();
    expect(done).toBe(false);
    reply("<Jog|MPos:-5,1,0>", "<Idle|MPos:-10,2.5,0>");
    await op;
    expect(done).toBe(true);
  });

  test("an XY jog needs known belt lengths; a Z jog doesn't", async () => {
    reply("<Idle|MPos:0,0,0|FS:0,0|MS:4>");
    const xy = g.machine.jog({ x: 5 }, { feed: 1000 });
    await expect(xy).rejects.toMatchObject({ reason: "not-ready" });
    expect(g.ws_source.sent).toEqual([]);
    const z = g.machine.jog({ z: -1 }, { feed: 300 });
    expect(lastSent()).toBe("$J=G91 G21 F300 Z-1\n");
    reply("ok", "<Jog|MPos:0,0,-1>", "<Idle|MPos:0,0,-1>");
    await z;
  });

  test("a rejected jog reports the firmware error", async () => {
    const op = g.machine.jog({ x: 1 }, { feed: 1000 });
    reply("error:8");
    await expect(op).rejects.toMatchObject({ reason: "rejected", code: 8 });
  });

  test("moveTo builds an absolute move", async () => {
    const op = g.machine.moveTo({ x: 0, y: 0 }, { machineCoords: true });
    expect(lastSent()).toBe("G90 G21 G53 G0 X0 Y0\n");
    reply("ok", "<Run|MPos:1,1,0>", "<Idle|MPos:0,0,0>");
    await op;
    const op2 = g.machine.moveTo({ z: 70 }, { feed: 400 });
    expect(lastSent()).toBe("G90 G21 G1 F400 Z70\n");
    reply("ok", "<Run|MPos:0,0,10>", "<Idle|MPos:0,0,70>");
    await op2;
  });

  test("ok and the status reports arriving together still finish the move", async () => {
    const op = g.machine.jog({ x: 1 }, { feed: 1000 });
    reply("ok", "<Jog|MPos:0.5,0,0>", "<Idle|MPos:1,0,0>"); // one frame, before any await
    await op;
  });

  test("bad arguments throw before anything is sent", () => {
    expect(() => g.machine.jog({}, { feed: 100 })).toThrow();
    expect(() => g.machine.jog({ x: 1 }, {})).toThrow();
    expect(() => g.machine.moveTo({ x: Number.NaN })).toThrow();
    expect(g.ws_source.sent).toEqual([]);
  });
});

describe("stop, pause, resume", () => {
  test("stop ends running operations with reset, sends 0x18, then $STOP", async () => {
    const jog = g.machine.jog({ x: 100 }, { feed: 1000 });
    reply("ok", "<Jog|MPos:5,0,0>");
    await jog.accepted;
    const stop = g.machine.stop();
    await expect(jog).rejects.toMatchObject({ reason: "reset" });
    expect(lastSent()).toBe("\x18");
    reply("", BANNER);
    await tick(60);
    expect(lastSent()).toBe("$STOP\n");
    reply("ok");
    await expect(stop).resolves.toBeUndefined();
    expect(g.machine.runningCount()).toBe(0);
  });

  test("pause only while running; done when the hold completes", async () => {
    await expect(g.machine.pause()).rejects.toMatchObject({ reason: "not-ready" });
    reply("<Run|MPos:0,0,0|SD:10,/sd/a.nc>");
    const op = g.machine.pause();
    expect(lastSent()).toBe("!");
    reply("<Hold:1|MPos:0,0,0>");
    await tick();
    let done = false;
    op.then(() => (done = true));
    await tick();
    expect(done).toBe(false);
    reply("<Hold:0|MPos:0,0,0>");
    await op;
    const r = g.machine.resume();
    expect(lastSent()).toBe("~");
    reply("<Run|MPos:0,0,0>");
    await r;
  });
});

describe("settings and failures", () => {
  test("getSetting returns the value from the reply", async () => {
    const op = g.machine.getSetting("kinematics/MaslowKinematics/fixedZ");
    expect(lastSent()).toBe("$/kinematics/MaslowKinematics/fixedZ\n");
    reply("$/kinematics/MaslowKinematics/fixedZ=false", "ok");
    await expect(op).resolves.toBe("false");
  });

  test("setSetting sends path=value", async () => {
    const op = g.machine.setSetting("Maslow_Park_Z", 2);
    expect(lastSent()).toBe("$/Maslow_Park_Z=2\n");
    reply("ok");
    await op;
  });

  test("not-sent when the socket is closed", async () => {
    g.ws_source.readyState = 3;
    await expect(g.machine.retractBelts()).rejects.toMatchObject({ reason: "not-sent" });
  });

  test("disconnected when the socket reopens before the reply", async () => {
    const op = g.machine.getSetting("Maslow_Park_X");
    g.wsCommands.reset();
    await expect(op).rejects.toMatchObject({ reason: "disconnected" });
  });
});

describe("runFile", () => {
  const run = (line: number) => `<Run|MPos:0,0,0|FS:800,0|MS:7|SD:${line}.00,/part.nc,${line}>`;

  test("accepted on ok; done when the job has gone and the machine is Idle", async () => {
    const op = g.machine.runFile("/part.nc");
    let done = false;
    op.then(() => (done = true));
    expect(lastSent()).toBe("$SD/Run=/part.nc\n");
    reply("ok");
    await op.accepted;
    reply(run(10), "<Hold:0|MPos:0,0,0|MS:7|SD:50.00,/part.nc,50>");
    await tick();
    expect(done).toBe(false); // paused mid-job
    reply(run(90), "<Idle|MPos:0,0,0|FS:0,0|MS:7>");
    await op;
    expect(g.machine.runningCount()).toBe(0);
  });

  test("a file that finishes before any report shows it is done after two fresh reports", async () => {
    const op = g.machine.runFile("/tiny.nc");
    reply("ok");
    await op.accepted;
    reply("<Idle|MPos:0,0,0|MS:7>");
    await tick();
    expect(g.machine.runningCount()).toBe(1);
    reply("<Idle|MPos:0,0,0|MS:7>");
    await op;
  });

  test("a rejected line in the file fails it with that error", async () => {
    const op = g.machine.runFile("/part.nc");
    reply("ok", run(3), "[MSG:ERR: 20 (Unsupported or invalid g-code command) in SD at line 4]");
    await expect(op).rejects.toMatchObject({ reason: "failed", code: 20, detail: "/part.nc line 4: Unsupported or invalid g-code command" });
  });

  test("an alarm fails it, stop() resets it", async () => {
    const a = g.machine.runFile("/part.nc");
    reply("ok", run(3), "<Alarm|MPos:0,0,0|MS:7>");
    await expect(a).rejects.toMatchObject({ reason: "failed" });
    reply("<Idle|MPos:0,0,0|MS:7>");
    const b = g.machine.runFile("/part.nc");
    reply("ok", run(3));
    g.machine.stop();
    await expect(b).rejects.toMatchObject({ reason: "reset" });
  });

  test("needs known belt lengths; a firmware error rejects it", async () => {
    reply("<Idle|MPos:0,0,0|MS:4>");
    await expect(g.machine.runFile("/part.nc")).rejects.toMatchObject({ reason: "not-ready" });
    expect(g.ws_source.sent).toEqual([]);
    reply("<Idle|MPos:0,0,0|MS:7>");
    const op = g.machine.runFile("/missing.nc");
    op.catch(() => {});
    reply("error:60");
    await expect(op.accepted).rejects.toMatchObject({ reason: "rejected", code: 60 });
  });
});

describe("files", () => {
  // http.js and httpCmdBuilders.js stand-ins: each request waits in `requests` for a reply.
  let requests: { url: string; data?: any; progress?: any; ok: (t: string) => void; fail: (s: number, t: string) => void }[];
  const listing = (status = "Ok", files = [{ name: "part.nc", size: "1234", datetime: "" }, { name: "jobs", size: "-1", datetime: "" }]) =>
    JSON.stringify({ files, path: "/", total: "8 GB", used: "1 GB", occupation: "12", mode: "direct", status });
  let pingPaused: boolean[];

  beforeEach(() => {
    requests = [];
    pingPaused = [];
    g.httpCmd = { fileUpload: "/upload" };
    g.buildHttpFileCmd = (p: any) =>
      `/upload?path=${encodeURIComponent(p.path)}` +
      ["action", "filename", "newname"].filter((k) => p[k]).map((k) => `&${k}=${encodeURIComponent(p[k])}`).join("");
    g.SendGetHttp = (url: string, ok: any, fail: any) => requests.push({ url, ok, fail });
    g.SendFileHttp = (url: string, data: any, progress: any, ok: any, fail: any) => requests.push({ url, data, progress, ok, fail });
    g.BuildFileUploadFormData = (dir: string, files: File[]) => ({ dir, names: files.map((f) => f.name) });
    g.disablePingForUpload = () => pingPaused.push(true);
    g.restorePingAfterUpload = () => pingPaused.push(false);
  });

  test("list parses the listing", async () => {
    const p = g.machine.files.list("/");
    expect(requests[0].url).toBe("/upload?path=%2F");
    requests[0].ok(listing());
    const l = await p;
    expect(l.status).toBe("Ok");
    expect(l.total).toBe("8 GB");
    expect(l.files).toEqual([
      { name: "part.nc", size: 1234, isdir: false, datetime: "" },
      { name: "jobs", size: null, isdir: true, datetime: "" },
    ]);
  });

  test("delete splits the path and pauses ping checks while it runs", async () => {
    const p = g.machine.files.delete("/jobs/old.nc");
    await tick();
    expect(requests[0].url).toBe("/upload?path=%2Fjobs%2F&action=delete&filename=old.nc");
    expect(pingPaused).toEqual([true]);
    requests[0].ok(listing("old.nc deleted"));
    await p;
    expect(pingPaused).toEqual([true, false]);
    g.machine.files.delete("/jobs", { dir: true });
    await tick();
    expect(requests[1].url).toBe("/upload?path=%2F&action=deletedir&filename=jobs");
  });

  test("a refused action rejects with the firmware's status and the listing", async () => {
    const p = g.machine.files.delete("/part.nc");
    await tick();
    requests[0].ok(listing("Cannot delete part.nc No such file"));
    const err = await p.catch((e: any) => e);
    expect(err).toMatchObject({ reason: "failed", detail: "Cannot delete part.nc No such file" });
    expect(err.listing.files.length).toBe(2);
  });

  test("upload sends the files to the directory and reports progress", async () => {
    const onProgress = () => {};
    const file = new File(["G0 X0"], "a.nc");
    const p = g.machine.files.upload("/jobs/", file, { onProgress });
    await tick();
    expect(requests[0]).toMatchObject({ url: "/upload", data: { dir: "/jobs/", names: ["a.nc"] }, progress: onProgress });
    requests[0].ok(listing());
    await p;
    expect(pingPaused).toEqual([true, false]);
  });

  test("HTTP errors, unreadable replies and a busy upload channel", async () => {
    const a = g.machine.files.list("/");
    requests[0].fail(500, "oops");
    await expect(a).rejects.toMatchObject({ reason: "rejected", code: 500 });
    const b = g.machine.files.list("/");
    requests[1].ok("<html>");
    await expect(b).rejects.toMatchObject({ reason: "bad-reply" });
    g.http_communication_locked = true;
    try {
      await expect(g.machine.files.upload("/", [new File([""], "x.nc")])).rejects.toMatchObject({ reason: "busy" });
    } finally {
      delete g.http_communication_locked;
    }
    expect(requests.length).toBe(2);
  });

  test("rename and createDir", async () => {
    g.machine.files.rename("/jobs/a.nc", "b.nc");
    g.machine.files.createDir("/jobs/new");
    expect(requests.map((r) => r.url)).toEqual([
      "/upload?path=%2Fjobs%2F&action=rename&filename=a.nc&newname=b.nc",
      "/upload?path=%2Fjobs%2F&action=createdir&filename=new",
    ]);
  });
});
