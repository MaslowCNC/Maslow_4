import { beforeAll, beforeEach, describe, expect, test } from "bun:test";

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
  test("sends lines in order with a newline", () => {
    g.wsCommands.send("$J=G91G21F1000X10");
    g.wsCommands.send("$STOP\n");
    expect(g.ws_source.sent).toEqual(["$J=G91G21F1000X10\n", "$STOP\n"]);
    expect(g.wsCommands.pendingCount()).toBe(2);
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
    expect(messages).toEqual(["Machine rejected 'B' (probably): error 8"]);
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

  test("reset forgets pending lines", () => {
    g.wsCommands.send("A");
    g.wsCommands.reset();
    expect(g.wsCommands.pendingCount()).toBe(0);
  });
});
