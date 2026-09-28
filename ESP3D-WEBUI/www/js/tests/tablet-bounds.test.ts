import { beforeAll, beforeEach, describe, expect, test } from "bun:test";

const g = globalThis as any;

beforeAll(async () => {
  await import("../tablet.js");
});

describe("tablet work area bounds checks", () => {
  let messages: { textContent: string; scrollTop: number; scrollHeight: number };
  let alertCount = 0;

  beforeEach(() => {
    messages = { textContent: "", scrollTop: 0, scrollHeight: 0 };
    alertCount = 0;

    g.id = (name: string) => (name === "messages" ? messages : null);
    g.alertdlg = () => { alertCount += 1; };

    g.loadedValues = {
      workAreaX: "100",
      workAreaY: "100",
      workAreaCenterOffsetX: "0",
      workAreaCenterOffsetY: "0",
    };

    g.__tabletBoundsTestApi.resetLoadBoundsWarningState();
  });

  test("accepts in-bounds job", () => {
    g.getJobBoundingBox = () => ({
      min: { x: -40, y: -20, z: 0 },
      max: { x: 40, y: 20, z: 0 },
    });

    const ok = g.__tabletBoundsTestApi.checkLoadedJobWithinWorkArea(0, 0, "Load", false, true);
    expect(ok).toBe(true);
    expect(messages.textContent).toBe("");
    expect(alertCount).toBe(0);
  });

  test("rejects out-of-bounds job", () => {
    g.getJobBoundingBox = () => ({
      min: { x: -60, y: -20, z: 0 },
      max: { x: 40, y: 20, z: 0 },
    });

    const ok = g.__tabletBoundsTestApi.checkLoadedJobWithinWorkArea(0, 0, "Load", true, true);
    expect(ok).toBe(false);
    expect(messages.textContent).toContain("exceed work area");
    expect(alertCount).toBe(1);
  });

  test("suppresses duplicate warnings for repeated identical failures", () => {
    g.getJobBoundingBox = () => ({
      min: { x: -60, y: -20, z: 0 },
      max: { x: 40, y: 20, z: 0 },
    });

    const first = g.__tabletBoundsTestApi.checkLoadedJobWithinWorkArea(0, 0, "Load", true, true);
    const second = g.__tabletBoundsTestApi.checkLoadedJobWithinWorkArea(0, 0, "Load", true, true);

    expect(first).toBe(false);
    expect(second).toBe(false);
    expect(messages.textContent.match(/exceed work area/g)?.length).toBe(1);
    expect(alertCount).toBe(1);
  });

  test("derives home from MPOS-WPOS when available", () => {
    g.MPOS = [150, 80];
    g.WPOS = [10, 5];
    g.WCO = [999, 999];

    const home = g.__tabletBoundsTestApi.getCurrentHomeInMm();
    expect(home).toEqual({ x: 140, y: 75 });
  });
});
