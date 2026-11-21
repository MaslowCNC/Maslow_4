import { beforeAll, describe, expect, test } from "bun:test";

const g = globalThis as any;

beforeAll(async () => {
  await import("../tablet.js");
});

describe("encoderTeeth check against the detected board", () => {
  const warn = (board: string, teeth: string) => g.__tabletEncoderTeethTestApi.encoderTeethWarning(board, teeth);

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

  test("says nothing for an unknown board or an unreadable value", () => {
    expect(warn("unknown", "22")).toBe("");
    expect(warn("", "35")).toBe("");
    expect(warn("maslow41", "")).toBe("");
  });
});
