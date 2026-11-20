import { beforeAll, describe, expect, test } from "bun:test";

const g = globalThis as any;

beforeAll(async () => {
  await import("../tablet.js");
});

describe("tablet position readout formatting", () => {
  test("formats home-relative and machine-relative coordinates", () => {
    expect(g.__tabletPositionTestApi.formatPositionCoordinates(
      { x: 123.456, y: -7.89, z: 2 },
      1,
      2,
    )).toEqual({ x: "123.46", y: "-7.89", z: "2.00" });
  });

  test("converts machine millimeters to displayed inches and marks unknown positions", () => {
    expect(g.__tabletPositionTestApi.formatPositionCoordinates(
      { x: 25.4, y: 0, z: null },
      1 / 25.4,
      4,
    )).toEqual({ x: "1.0000", y: "0.0000", z: "?" });
  });
});
