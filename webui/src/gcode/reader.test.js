// Tests for reader.js. Run with `npm test` (node --test, no dependencies).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readGcode, parseLine, arcPoints, boundaryPath } from "./reader.js";

const close = (a, b, msg) => assert.ok(Math.abs(a - b) < 1e-6, `${msg ?? ""} expected ${b}, got ${a}`);
const closeXYZ = (p, q, msg) => { close(p.x, q.x, `${msg} x`); close(p.y, q.y, `${msg} y`); close(p.z, q.z, `${msg} z`); };

test("an empty file has no lines, moves or extent", () => {
  for (const text of ["", undefined, null]) {
    const r = readGcode(text);
    assert.equal(r.lineCount, 0);
    assert.deepEqual(r.moves, []);
    assert.equal(r.extent, null);
    assert.equal(r.travelExtent, null);
  }
});

test("line count matches the machine's numbering, with or without a final newline", () => {
  assert.equal(readGcode("G0 X1\nG1 X2\n").lineCount, 2);
  assert.equal(readGcode("G0 X1\r\nG1 X2").lineCount, 2);
  assert.equal(readGcode("\n\n").lineCount, 2);
  const r = readGcode("(header)\n\nG1 X5");
  assert.equal(r.moves[0].line, 3);
});

test("comments, line numbers, checksums and % lines are ignored", () => {
  assert.deepEqual(parseLine("N10 G1 X1 (move X1 Y9) Y2 ; Z5"), [["G", 1], ["X", 1], ["Y", 2]]);
  assert.deepEqual(parseLine("n20g0x-1.5y.5*57"), [["G", 0], ["X", -1.5], ["Y", 0.5]]);
  assert.deepEqual(parseLine("%"), []);
  assert.deepEqual(parseLine("; G1 X100"), []);
  assert.deepEqual(parseLine("$H"), []);
  const r = readGcode("%\n(G1 X100 Y100)\nG1 X10 Y10 ; G1 X200\n%");
  assert.equal(r.moves.length, 1);
  assert.deepEqual(r.extent.max, { x: 10, y: 10, z: 0 });
});

test("rapids and cuts: rapids are kept but not in the extent", () => {
  const r = readGcode("G0 X-50 Y-50 Z5\nG1 Z-1 F500\nX10\nY20\nG0 Z5\nX100 Y100");
  assert.deepEqual(r.moves.map(m => m.rapid), [true, false, false, false, true, true]);
  assert.deepEqual(r.moves[2].from, { x: -50, y: -50, z: -1 });
  assert.deepEqual(r.moves[2].to, { x: 10, y: -50, z: -1 });
  assert.deepEqual(r.extent, { min: { x: -50, y: -50, z: -1 }, max: { x: 10, y: 20, z: 5 } });   // the plunge starts at Z5
  assert.deepEqual(r.travelExtent.max, { x: 100, y: 100, z: 5 });
});

test("G20 converts to mm, G21 switches back", () => {
  const r = readGcode("G20\nG1 X1 Y2 Z-0.5\nG21\nG1 X10");
  assert.deepEqual(r.moves[0].to, { x: 25.4, y: 50.8, z: -12.7 });
  assert.deepEqual(r.moves[1].to, { x: 10, y: 50.8, z: -12.7 });
});

test("units on the same line as the move apply to it", () => {
  const r = readGcode("G1 X1\nG20 G1 X1");
  close(r.moves[1].to.x, 25.4);
});

test("G91 moves are relative, G90 absolute again", () => {
  const r = readGcode("G1 X10 Y10\nG91\nG1 X5 Y-3\nX5\nG90\nX0");
  assert.deepEqual(r.moves.map(m => [m.to.x, m.to.y]), [[10, 10], [15, 7], [20, 7], [0, 7]]);
});

test("relative moves in inches", () => {
  const r = readGcode("G20 G91\nG1 X1\nX1");
  close(r.moves[1].to.x, 50.8);
});

test("a quarter arc crossing the Y axis reaches the top of its circle, not just its ends", () => {
  // Centre (0,0), radius 10, from (10,0)... no: from (7.07,7.07) counter-clockwise to (-7.07,7.07) crosses +Y
  const a = 10 / Math.SQRT2;
  const r = readGcode(`G1 X${a} Y${a}\nG3 X${-a} Y${a} I${-a} J${-a}`);
  const arc = r.moves[1];
  assert.equal(arc.arc.clockwise, false);
  closeXYZ(arc.arc.center, { x: 0, y: 0, z: 0 }, "centre");
  close(r.extent.max.y, 10, "top of the circle");
  close(r.extent.min.x, -a);
  close(r.extent.max.x, a);
});

test("the same ends clockwise go the long way round and cross three axes", () => {
  const a = 10 / Math.SQRT2;
  const r = readGcode(`G1 X${a} Y${a}\nG2 X${-a} Y${a} I${-a} J${-a}`);
  close(r.extent.min.x, -10);
  close(r.extent.max.x, 10);
  close(r.extent.min.y, -10);
  close(r.extent.max.y, a);
});

test("a full circle (end = start) covers the whole circle", () => {
  const r = readGcode("G0 X10 Y0\nG1 Z-1\nG2 X10 Y0 I-10 J0");
  assert.deepEqual(
    [r.extent.min.x, r.extent.min.y, r.extent.max.x, r.extent.max.y].map(v => Math.round(v * 1e6) / 1e6),
    [-10, -10, 10, 10]);
});

test("R-form arcs: + takes the short way, − the long way", () => {
  const short = readGcode("G1 X0 Y0\nG2 X10 Y10 R10");
  closeXYZ(short.moves[1].arc.center, { x: 10, y: 0, z: 0 }, "short centre");
  close(short.extent.max.y, 10);
  close(short.extent.min.y, 0);
  const long = readGcode("G1 X0 Y0\nG2 X10 Y10 R-10");
  closeXYZ(long.moves[1].arc.center, { x: 0, y: 10, z: 0 }, "long centre");
  close(long.extent.min.x, -10);
  close(long.extent.max.y, 20);
});

test("arc offsets in inches", () => {
  const r = readGcode("G20\nG1 X1 Y0\nG3 X-1 Y0 I-1 J0");
  close(r.extent.max.y, 25.4);
  closeXYZ(r.moves[1].arc.center, { x: 0, y: 0, z: 0 }, "centre");
});

test("helical arcs move Z along the way", () => {
  const r = readGcode("G1 X10 Y0 Z0\nG3 X-10 Y0 Z-2 I-10 J0");
  close(r.extent.min.z, -2);
  const pts = arcPoints(r.moves[1], 1);
  close(pts[0].x, 10);
  closeXYZ(pts[pts.length - 1], { x: -10, y: 0, z: -2 }, "end");
  const mid = pts[Math.floor(pts.length / 2)];
  assert.ok(mid.y > 9.9 && mid.z < -0.9 && mid.z > -1.1, `midpoint ${JSON.stringify(mid)}`);
});

test("G92 shifts the program's coordinates into work coordinates", () => {
  const r = readGcode("G1 X100\nG92 X0\nG1 X10\nG92.1\nG1 X10");
  assert.deepEqual(r.moves.map(m => m.to.x), [100, 110, 10]);
});

test("G53 moves are marked as machine moves and left out of the extent", () => {
  const r = readGcode("G1 X10 Y10\nG53 G0 Z-5\nG1 X20");
  assert.equal(r.moves[1].machine, true);
  // After it the reader doesn't know Z in work coordinates, so Z drops out of the extent until it's set again
  assert.deepEqual(r.extent, { min: { x: 10, y: 10, z: 0 }, max: { x: 20, y: 10, z: 0 } });
  const back = readGcode("G1 X10 Y10 Z-1\nG53 G0 Z-5\nG1 X20 Z-2");
  assert.deepEqual([back.extent.min.z, back.extent.max.z], [-2, -1]);
});

test("a move with no axis words isn't a move", () => {
  const r = readGcode("G1 F1000\nG0\nM3 S12000\nG4 P1\nG28");
  assert.equal(r.moves.length, 0);
  assert.equal(r.lineCount, 5);
});

test("the assumed start (0,0,0) isn't part of the extent until the file goes there", () => {
  const r = readGcode("G1 X-10 Y5\nX40 Y25");
  assert.deepEqual(r.extent, { min: { x: -10, y: 5, z: 0 }, max: { x: 40, y: 25, z: 0 } });
  // relative moves from an unknown start don't make the axis known
  assert.equal(readGcode("G91\nG1 X5\nX5").extent, null);
});

test("boundaryPath closes the rectangle, then returns to where the machine was", () => {
  const ext = readGcode("G1 X-10 Y5\nX40 Y25").extent;
  assert.deepEqual(boundaryPath(ext, { x: 1, y: 2, z: 3 }), [
    { x: -10, y: 5 }, { x: 40, y: 5 }, { x: 40, y: 25 }, { x: -10, y: 25 }, { x: -10, y: 5 }, { x: 1, y: 2 }
  ]);
});
