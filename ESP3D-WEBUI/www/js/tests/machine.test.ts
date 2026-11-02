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
    // MINFO merges into the same object (it also brings homed/extended)
    reply('MINFO: { "homed": true, "calibrationInProgress": false, "tl": 1810, "tr": 1801.5, "br": 1899, "bl": 1900.75, "etl": 0, "etr": 0, "ebr": 0, "ebl": 0, "extended": true }');
    expect(g.machine.state.belts).toMatchObject({ tl: 1810, homed: true, extended: true });
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
