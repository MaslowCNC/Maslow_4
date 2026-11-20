import { beforeAll, describe, expect, test } from "bun:test";

const g = globalThis as any;

beforeAll(async () => {
  await import("../tablet.js");
});

describe("tablet position display", () => {
  test("shows home-relative and machine-relative X, Y, and Z in display units", () => {
    const values: Record<string, string> = {};
    g.setTextContent = (id: string, value: string) => { values[id] = value; };

    g.__tabletPositionTestApi.updatePositionDisplay(
      [25.4, -50.8, 3.175],
      [254, 508, 76.2],
      1 / 25.4,
      4
    );

    expect(values).toEqual({
      "wpos-x": "1.0000",
      "wpos-y": "-2.0000",
      "wpos-z": "0.1250",
      "mpos-x": "10.0000",
      "mpos-y": "20.0000",
      "mpos-z": "3.0000",
    });
  });
});
