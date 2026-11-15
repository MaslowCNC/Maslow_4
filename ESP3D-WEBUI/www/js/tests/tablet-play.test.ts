import { beforeAll, describe, expect, test } from "bun:test";

const g = globalThis as any;

beforeAll(async () => {
  g.MASLOW_STATE_READY_TO_CUT = 7;
  await import("../tablet.js");
});

const mode = (s: object, opts: { loaded?: boolean; starting?: boolean } = {}) =>
  g.__tabletJobTestApi.playButtonMode(s, { loaded: true, starting: false, ...opts });

const job = { pct: 12.5, file: "/part.nc", line: 40 };

describe("Start button mode", () => {
  test("ready when idle, a file is loaded and Maslow is ready to cut", () => {
    expect(mode({ grbl: "Idle", maslow: 7, job: null })).toBe("ready");
  });

  test("off without a loaded file or when not ready to cut", () => {
    expect(mode({ grbl: "Idle", maslow: 7, job: null }, { loaded: false })).toBe("off");
    expect(mode({ grbl: "Idle", maslow: 3, job: null })).toBe("off");
    expect(mode({ grbl: "Alarm", maslow: 7, job: null })).toBe("off");
  });

  test("stays running while the job is reported, even when the machine is Idle for a moment", () => {
    expect(mode({ grbl: "Run", maslow: 7, job })).toBe("running");
    expect(mode({ grbl: "Idle", maslow: 7, job })).toBe("running");
  });

  test("running while the last moves finish after the file has been read", () => {
    expect(mode({ grbl: "Run", maslow: 7, job: null })).toBe("running");
  });

  test("ready again once the job is over", () => {
    expect(mode({ grbl: "Idle", maslow: 7, job: null, stopped: { line: 40, file: "/part.nc" } })).toBe("ready");
  });

  test("starting from the click until the job shows up, whatever the state", () => {
    expect(mode({ grbl: "Idle", maslow: 7, job: null }, { starting: true })).toBe("starting");
    expect(mode({ grbl: "Run", maslow: 7, job }, { starting: true })).toBe("starting");
  });

  test("Resume in a feed hold", () => {
    expect(mode({ grbl: "Hold", maslow: 7, job })).toBe("resume");
  });
});
