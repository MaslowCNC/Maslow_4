import { beforeAll, beforeEach, describe, expect, test } from "bun:test";

const g = globalThis as any;

// The 2026-11-12 Find Anchors result (anchor frame, BL at 0,0)
const ANCHORS = {
  tl: { x: 45.367, y: 1671.618 },
  tr: { x: 2964.85, y: 1829.713 },
  bl: { x: 0, y: 0 },
  br: { x: 2924.499, y: 0 },
};
// Its centre, where the diagonals cross (MaslowKinematics::calculateCenter)
const centre = () => {
  const a = ANCHORS;
  const A = (a.tr.y - a.bl.y) / (a.tr.x - a.bl.x);
  const B = (a.br.y - a.tl.y) / (a.br.x - a.tl.x);
  const x = (a.br.y - B * a.br.x + A * a.tr.x - a.tr.y) / (A - B);
  return { x, y: A * (x - a.tr.x) + a.tr.y };
};

/** Belt errors (encoder minus target) for a sled that is at mpos + d instead of mpos. */
const errorsFor = (mpos: { x: number; y: number }, d: { x: number; y: number }) => {
  const c = centre();
  const len = (p: { x: number; y: number }, a: { x: number; y: number }) => Math.hypot(p.x + c.x - a.x, p.y + c.y - a.y);
  const actual = { x: mpos.x + d.x, y: mpos.y + d.y };
  const e = (a: { x: number; y: number }) => len(actual, a) - len(mpos, a);
  return { etl: e(ANCHORS.tl), etr: e(ANCHORS.tr), ebl: e(ANCHORS.bl), ebr: e(ANCHORS.br) };
};

const report = (x: number, y: number, belts: object | null, job = true) => ({
  mpos: { x, y, z: -3 },
  belts,
  job: job ? { pct: 10, file: "/sd/test.nc" } : null,
});

beforeAll(async () => {
  await import("../job-trace.js");
});

beforeEach(() => {
  g.jobTrace.addSample(report(0, 0, null, false), ANCHORS); // no job running
  g.jobTrace.clear();
});

describe("actualPosition", () => {
  test("no belt error gives the commanded position", () => {
    const p = g.jobTrace.actualPosition({ x: 100, y: -50, z: 2 }, { etl: 0, etr: 0, ebl: 0, ebr: 0 }, ANCHORS);
    expect(p).toEqual({ x: 100, y: -50, z: 2 });
  });

  test.each([
    [{ x: 1, y: 0 }, { x: 0, y: 0 }],
    [{ x: 0, y: -0.7 }, { x: -500, y: -250 }],
    [{ x: -0.6, y: 0.55 }, { x: -493, y: -130 }], // near the bottom-left test spot
  ])("recovers an offset of %o at %o", (d, mpos) => {
    const p = g.jobTrace.actualPosition({ ...mpos, z: 0 }, errorsFor(mpos, d), ANCHORS);
    expect(p.x - mpos.x).toBeCloseTo(d.x, 3);
    expect(p.y - mpos.y).toBeCloseTo(d.y, 3);
  });

  test("missing belt errors give no position", () => {
    expect(g.jobTrace.actualPosition({ x: 0, y: 0, z: 0 }, { etl: 0, etr: 0 }, ANCHORS)).toBeNull();
    expect(g.jobTrace.actualPosition({ x: 0, y: 0, z: 0 }, null, ANCHORS)).toBeNull();
  });
});

describe("addSample", () => {
  const zero = { etl: 0, etr: 0, ebl: 0, ebr: 0 };

  test("records only while a job runs", () => {
    expect(g.jobTrace.addSample(report(0, 0, zero, false), ANCHORS)).toBe(false);
    expect(g.jobTrace.addSample(report(0, 0, zero), ANCHORS)).toBe(true);
    expect(g.jobTrace.addSample(report(5, 0, zero), ANCHORS)).toBe(true);
    expect(g.jobTrace.addSample(report(10, 0, zero, false), ANCHORS)).toBe(false);
    expect(g.jobTrace.points().map((p: any) => p.x)).toEqual([0, 5]);
  });

  test("the trace stays after the job and clears when the next one starts", () => {
    g.jobTrace.addSample(report(0, 0, zero), ANCHORS);
    g.jobTrace.addSample(report(5, 0, zero), ANCHORS);
    g.jobTrace.addSample(report(5, 0, zero, false), ANCHORS);
    expect(g.jobTrace.points().length).toBe(2);
    g.jobTrace.addSample(report(20, 0, zero), ANCHORS);
    expect(g.jobTrace.points().map((p: any) => p.x)).toEqual([20]);
  });

  test("skips reports that didn't move", () => {
    g.jobTrace.addSample(report(0, 0, zero), ANCHORS);
    expect(g.jobTrace.addSample(report(0.01, 0, zero), ANCHORS)).toBe(false);
    expect(g.jobTrace.points().length).toBe(1);
  });

  test("undrawn starts at the last drawn point", () => {
    g.jobTrace.addSample(report(0, 0, zero), ANCHORS);
    g.jobTrace.addSample(report(5, 0, zero), ANCHORS);
    g.jobTrace.markDrawn();
    g.jobTrace.addSample(report(10, 0, zero), ANCHORS);
    expect(g.jobTrace.undrawn().map((p: any) => p.x)).toEqual([5, 10]);
  });
});
