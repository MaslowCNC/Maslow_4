import { afterEach, beforeAll, beforeEach, describe, expect, setSystemTime, test } from "bun:test";

const g = globalThis as any;

// Minimal stand-in for the browser WebSocket the UI keeps in `ws_source`.
const makeSocket = () => ({
  readyState: 1, // WebSocket.OPEN
  sent: [] as string[],
  send(text: string) {
    this.sent.push(text);
  },
});

let messages: string[];

beforeAll(async () => {
  g.WebSocket = g.WebSocket ?? { OPEN: 1 };
  await import("../wscommands.js");
});

beforeEach(() => {
  messages = [];
  g.addMessage = (m: string) => messages.push(m);
  g.ws_source = makeSocket();
  g.wsCommands.reset();
  g.wsCommands.handleLine("<Idle|MPos:0,0,0>");
});

describe("wsCommands", () => {
  test("sends lines one at a time, each after the previous reply", () => {
    g.wsCommands.send("$J=G91G21F1000X10");
    g.wsCommands.send("$STOP\n");
    expect(g.ws_source.sent).toEqual(["$J=G91G21F1000X10\n"]);
    expect(g.wsCommands.pendingCount()).toBe(2);
    g.wsCommands.handleLine("ok");
    expect(g.ws_source.sent).toEqual(["$J=G91G21F1000X10\n", "$STOP\n"]);
    expect(g.wsCommands.pendingCount()).toBe(1);
  });

  test("an error reply also lets the next line go", () => {
    g.wsCommands.send("A");
    g.wsCommands.send("B");
    g.wsCommands.handleLine("error:3");
    expect(g.ws_source.sent).toEqual(["A\n", "B\n"]);
  });

  test("returns false and tracks nothing when the socket is closed", () => {
    g.ws_source.readyState = 3;
    expect(g.wsCommands.send("$J=G91X1")).toBe(false);
    expect(g.wsCommands.pendingCount()).toBe(0);
  });

  test("matches ok and error replies in order", () => {
    const seen: string[] = [];
    g.wsCommands.send("A", { onOk: () => seen.push("A ok") });
    g.wsCommands.send("B", { onError: (c: string) => seen.push(`B err ${c}`) });
    g.wsCommands.handleLine("ok");
    g.wsCommands.handleLine("error:8");
    expect(seen).toEqual(["A ok", "B err 8"]);
    expect(messages).toEqual(["Machine rejected 'B': error 8"]);
    expect(g.wsCommands.pendingCount()).toBe(0);
  });

  test("ignores an ok with nothing pending (reply to an HTTP-sent command)", () => {
    g.wsCommands.handleLine("ok");
    expect(g.wsCommands.pendingCount()).toBe(0);
    expect(messages).toEqual([]);
  });

  test("still reports an error with nothing pending", () => {
    g.wsCommands.handleLine("error:9");
    expect(messages).toEqual(["Machine reported error 9"]);
  });

  test("realtime bytes are sent raw and expect no reply", () => {
    g.wsCommands.sendRealtime(0x85);
    expect(g.ws_source.sent).toEqual(["\x85"]);
    expect(g.wsCommands.pendingCount()).toBe(0);
  });

  test("jog cancel during a jog drops queued lines but keeps the running one", () => {
    const flushed: string[] = [];
    g.wsCommands.handleLine("<Jog|MPos:0,0,0>");
    g.wsCommands.send("$J=X10");
    g.wsCommands.send("$J=Y10", { onError: (c: string) => flushed.push(`Y ${c}`) });
    g.wsCommands.send("$J=X-10", { onError: (c: string) => flushed.push(`X- ${c}`) });
    g.wsCommands.sendRealtime(0x85);
    expect(flushed).toEqual(["Y flushed", "X- flushed"]);
    expect(g.wsCommands.pendingCount()).toBe(1);
  });

  test("jog cancel outside a jog drops nothing (firmware ignores it)", () => {
    g.wsCommands.send("G0 X1");
    g.wsCommands.send("G0 X2");
    g.wsCommands.sendRealtime(0x85);
    expect(g.wsCommands.pendingCount()).toBe(2);
  });

  test("tracks the machine state from status reports", () => {
    g.wsCommands.handleLine("<Jog|MPos:1,2,3|FS:0,0>");
    expect(g.wsCommands.lastState()).toBe("Jog");
    g.wsCommands.handleLine("<Hold:0|MPos:1,2,3>");
    expect(g.wsCommands.lastState()).toBe("Hold");
  });

  test("debug mode logs each send and the reply matched to it", () => {
    const logged: string[] = [];
    const origLog = console.log;
    console.log = (m: string) => logged.push(m);
    try {
      g.wsCommands.setDebug(true);
      g.wsCommands.send("$G");
      g.wsCommands.send("$BOGUS");
      g.wsCommands.handleLine("ok");
      g.wsCommands.handleLine("error:3");
      g.wsCommands.setDebug(false);
      g.wsCommands.send("$G");
    } finally {
      console.log = origLog;
    }
    expect(logged.length).toBe(4);
    expect(logged[0]).toContain("→ '$G'");
    expect(logged[1]).toContain("← ok for '$G'");
    expect(logged[2]).toContain("→ '$BOGUS'");
    expect(logged[3]).toContain("← error 3 for '$BOGUS'");
  });

  test("soft reset (0x18) drops every waiting line", () => {
    const dropped: string[] = [];
    g.wsCommands.send("G0 X1", { onError: (c: string) => dropped.push(`X1 ${c}`) });
    g.wsCommands.send("G0 X2", { onError: (c: string) => dropped.push(`X2 ${c}`) });
    g.wsCommands.sendRealtime(0x18);
    expect(dropped).toEqual(["X1 reset", "X2 reset"]);
    expect(g.wsCommands.pendingCount()).toBe(0);
  });

  test("isRealtime picks out one-byte realtime commands", () => {
    for (const c of ["?", "!", "~", "\x18", "\x85", "\xa0"]) {
      expect(g.wsCommands.isRealtime(c)).toBe(true);
    }
    for (const c of ["$G", "G0", "x", "??", ""]) {
      expect(g.wsCommands.isRealtime(c)).toBe(false);
    }
  });

  test("urgent lines go ahead of queued ones, not the one on the wire", () => {
    g.wsCommands.send("A");
    g.wsCommands.send("B");
    g.wsCommands.send("$STOP", { urgent: true });
    g.wsCommands.handleLine("ok");
    expect(g.ws_source.sent).toEqual(["A\n", "$STOP\n"]);
  });

  test("quiet lines don't show 'Machine rejected' but still call onError", () => {
    const errors: string[] = [];
    g.wsCommands.send("$STOP", { quiet: true, onError: (c: string) => errors.push(c) });
    expect(g.wsCommands.handleLine("error:120")).toBe(true); // caller skips fault handling
    expect(errors).toEqual(["120"]);
    expect(messages).toEqual([]);
  });

  test("other replies are passed on for normal processing", () => {
    g.wsCommands.send("A");
    expect(g.wsCommands.handleLine("error:3")).toBe(false);
    expect(g.wsCommands.handleLine("ok")).toBe(false);
    expect(g.wsCommands.handleLine("<Idle|MPos:0,0,0>")).toBe(false);
  });

  describe("stop()", () => {
    const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
    const BANNER = "Grbl 3.9 [FluidNC v1.26.0 (wifi) '$' for help]";

    test("sends 0x18 at once, then $STOP after the restart banner", async () => {
      g.wsCommands.handleLine("<Run|MPos:0,0,0|SD:10.00,/sd/job.nc>");
      expect(g.wsCommands.stop()).toBe(true);
      expect(g.ws_source.sent).toEqual(["\x18"]);
      await sleep(60);
      expect(g.ws_source.sent).toEqual(["\x18"]); // still waiting for the restart
      g.wsCommands.handleLine("");
      g.wsCommands.handleLine(BANNER);
      await sleep(60);
      expect(g.ws_source.sent).toEqual(["\x18", "$STOP\n"]);
    });

    test("the same in every state, jog included (no 0x85)", async () => {
      g.wsCommands.handleLine("<Jog|MPos:0,0,0>");
      g.wsCommands.stop();
      g.wsCommands.handleLine(BANNER);
      await sleep(60);
      expect(g.ws_source.sent).toEqual(["\x18", "$STOP\n"]);
    });

    test("lines waiting for a reply are dropped by the reset", async () => {
      const dropped: string[] = [];
      g.wsCommands.send("G0 X10", { onError: (c: string) => dropped.push(c) });
      g.wsCommands.send("G0 X20", { onError: (c: string) => dropped.push(c) });
      g.wsCommands.stop();
      expect(dropped).toEqual(["reset", "reset"]);
      g.wsCommands.handleLine(BANNER); // let this stop finish before the next test
      await sleep(60);
    });

    test("sends $STOP anyway if no banner comes", async () => {
      g.wsCommands.stop();
      await sleep(1600);
      // (the 1 s status probe "?" also fires in this window)
      expect(g.ws_source.sent.filter((t: string) => t !== "?")).toEqual(["\x18", "$STOP\n"]);
    });

    test("a rejected $STOP is reported, except 120", async () => {
      const errors: string[] = [];
      g.wsCommands.stop({ onError: (c: string) => errors.push(c) });
      g.wsCommands.handleLine(BANNER);
      await sleep(60);
      expect(g.wsCommands.handleLine("error:152")).toBe(true);
      expect(errors).toEqual(["152"]);
      expect(messages).toEqual(["Machine rejected '$STOP': error 152"]);
    });

    test("returns false with the socket closed", () => {
      g.ws_source.readyState = 3;
      expect(g.wsCommands.stop()).toBe(false);
    });
  });

  describe("reply timeout", () => {
    afterEach(() => setSystemTime());

    test("a line with no reply is given up on and the next one goes", () => {
      const start = new Date("2026-10-30T12:00:00Z").getTime();
      setSystemTime(start);
      const errors: string[] = [];
      g.wsCommands.send("$/lost", { onError: (c: string) => errors.push(c) });
      g.wsCommands.send("$G");
      setSystemTime(start + 4000);
      g.wsCommands.checkTimeout();
      expect(errors).toEqual([]);
      setSystemTime(start + 6000);
      g.wsCommands.checkTimeout();
      expect(errors).toEqual(["timeout"]);
      expect(g.ws_source.sent).toEqual(["$/lost\n", "$G\n"]);
      expect(messages[0]).toContain("No reply from the machine to '$/lost'");
    });

    test("the clock doesn't run while the machine is moving", () => {
      const start = new Date("2026-10-30T12:00:00Z").getTime();
      setSystemTime(start);
      const errors: string[] = [];
      g.wsCommands.send("$MINFO", { onError: (c: string) => errors.push(c) });
      setSystemTime(start + 4000);
      g.wsCommands.handleLine("<Jog|MPos:0,0,0>");
      setSystemTime(start + 8000);
      g.wsCommands.checkTimeout();
      expect(errors).toEqual([]);
      setSystemTime(start + 9500);
      g.wsCommands.checkTimeout();
      expect(errors).toEqual(["timeout"]);
    });
  });

  test("reset forgets pending lines", () => {
    g.wsCommands.send("A");
    g.wsCommands.reset();
    expect(g.wsCommands.pendingCount()).toBe(0);
  });
});
