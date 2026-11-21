import { beforeAll, describe, expect, test } from "bun:test";

const g = globalThis as any;

beforeAll(async () => {
  await import("../tablet.js");
});

describe("encoderTeeth check against the detected board", () => {
  const warn = (board: string, teeth: string, extendDist?: string) =>
    g.__tabletEncoderTeethTestApi.encoderTeethWarning(board, teeth, extendDist);

  test("no warning when the teeth match the board", () => {
    expect(warn("maslow41", "22.000000")).toBe("");
    expect(warn("unified", "35.000000")).toBe("");
    expect(warn("unified36v", "35")).toBe("");
  });

  test("warns a Maslow 4.1 set to 35 teeth", () => {
    const msg = warn("maslow41", "35.000000");
    expect(msg).toContain("Maslow 4.1");
    expect(msg).toContain("have 22 teeth");
    expect(msg).toContain("encoderTeeth=22");
  });

  test("warns a Mini set to 22 teeth", () => {
    for (const board of ["unified", "unified36v"]) {
      const msg = warn(board, "22.000000");
      expect(msg).toContain("Maslow Mini");
      expect(msg).toContain("have 35 teeth");
      expect(msg).toContain("encoderTeeth=35");
    }
  });

  test("scales Extend Dist so it pays out the same belt as before", () => {
    // 442 mm read at 22 teeth is 442 * 35 / 22 = 703.2 mm of belt: round up
    const mini = warn("unified", "22.000000", "442.000000");
    expect(mini).toContain("Maslow_Extend_Dist=704");
    expect(mini).toContain("it is 442 now");
    // a 4.1 set to 35 teeth paid out 22/35 of what it read
    expect(warn("maslow41", "35", "1100")).toContain("Maslow_Extend_Dist=692");
  });

  test("keeps the new Extend Dist within the firmware's 4250 mm limit", () => {
    expect(warn("unified", "22", "3000")).toContain("Maslow_Extend_Dist=4250");
  });

  test("explains how to scale Extend Dist when it couldn't be read", () => {
    for (const extendDist of [undefined, "", "0"]) {
      const msg = warn("unified", "22", extendDist);
      expect(msg).toContain("Maslow_Extend_Dist=N");
      expect(msg).toContain("times 35/22");
    }
  });

  test("says to save, restart and calibrate again", () => {
    const msg = warn("unified", "22", "442");
    expect(msg).toContain("$CO");
    expect(msg).toContain("restart the machine");
    expect(msg).toContain("Retract All, Extend All, Apply Tension, then Find Anchors");
  });

  test("says nothing for an unknown board or an unreadable value", () => {
    expect(warn("unknown", "22")).toBe("");
    expect(warn("", "35")).toBe("");
    expect(warn("maslow41", "")).toBe("");
  });
});
