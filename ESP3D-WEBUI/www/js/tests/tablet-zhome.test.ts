import { beforeAll, describe, expect, test } from "bun:test";

const g = globalThis as any;

beforeAll(async () => {
  await import("../tablet.js");
});

describe("Set Z Home value check", () => {
  test("accepts a Z home at or above the z-stop", () => {
    expect(g.__tabletZHomeTestApi.checkZHome("62.08")).toEqual({ zHome: 62.08 });
    expect(g.__tabletZHomeTestApi.checkZHome("0")).toEqual({ zHome: 0 });
    expect(g.__tabletZHomeTestApi.checkZHome("1,5")).toEqual({ zHome: 1.5 });
  });

  test("refuses a Z home below the z-stop", () => {
    const { zHome, error } = g.__tabletZHomeTestApi.checkZHome("-0.5");
    expect(zHome).toBeUndefined();
    expect(error).toContain("below the z-stop");
    expect(error).toContain("0.500 mm under it");
  });

  test("refuses a Z home above the Z travel, and text that isn't a number", () => {
    expect(g.__tabletZHomeTestApi.checkZHome("80").error).toContain("72 mm above the z-stop");
    expect(g.__tabletZHomeTestApi.checkZHome("abc").error).toContain("Enter the Z home");
  });

  test("the Mini only needs a number", () => {
    expect(g.__tabletZHomeTestApi.checkZHome("-3", true)).toEqual({ zHome: -3 });
  });
});
