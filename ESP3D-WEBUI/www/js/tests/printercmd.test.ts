import { beforeAll, beforeEach, describe, expect, test } from "bun:test";

const g = globalThis as any;

const makeSocket = () => ({
  readyState: 1, // WebSocket.OPEN
  sent: [] as string[],
  send(text: string) {
    this.sent.push(text);
  },
});

let messages: string[];
let sendLineOrRealtime: (text: string, onOk?: Function, onError?: Function) => boolean;

beforeAll(async () => {
  g.WebSocket = g.WebSocket ?? { OPEN: 1 };
  await import("../wscommands.js");
  ({ sendLineOrRealtime } = await import("../printercmd.js"));
});

beforeEach(() => {
  messages = [];
  g.addMessage = (m: string) => messages.push(m);
  g.ws_source = makeSocket();
  g.wsCommands.reset();
  g.wsCommands.handleLine("<Idle|MPos:0,0,0>");
});

describe("sendLineOrRealtime", () => {
  test("a line goes over the socket and its ok runs the success callback", () => {
    let ok = 0;
    expect(sendLineOrRealtime("  $/kinematics/MaslowKinematics/fixedZ ", () => ok++)).toBe(true);
    expect(g.ws_source.sent).toEqual(["$/kinematics/MaslowKinematics/fixedZ\n"]);
    g.wsCommands.handleLine("$/kinematics/MaslowKinematics/fixedZ=false");
    g.wsCommands.handleLine("ok");
    expect(ok).toBe(1);
  });

  test("an error reply runs the error callback with the code", () => {
    const errors: string[] = [];
    sendLineOrRealtime("$BOGUS", undefined, (c: string) => errors.push(c));
    g.wsCommands.handleLine("error:3");
    expect(errors).toEqual(["3"]);
  });

  test("realtime characters are sent raw and not tracked", () => {
    sendLineOrRealtime("?");
    sendLineOrRealtime("!");
    sendLineOrRealtime(String.fromCharCode(0xa0)); // would be eaten by trim()
    expect(g.ws_source.sent).toEqual(["?", "!", "\xa0"]);
    expect(g.wsCommands.pendingCount()).toBe(0);
  });

  test("with the socket closed nothing is sent, the user is told, and onError runs", () => {
    g.ws_source.readyState = 3;
    const errors: string[] = [];
    expect(sendLineOrRealtime("G0 X1", undefined, (c: string) => errors.push(c))).toBe(false);
    expect(errors).toEqual(["not sent"]);
    expect(messages).toEqual(["Not sent: 'G0 X1' (connection to the machine is closed)"]);
  });

  test("a status poll while disconnected is not reported as a message", () => {
    g.ws_source.readyState = 3;
    expect(sendLineOrRealtime("?")).toBe(false);
    expect(messages).toEqual([]);
  });
});
