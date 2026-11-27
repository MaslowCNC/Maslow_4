import { test } from "node:test";
import assert from "node:assert/strict";
import { prose, fmtProse, toMm, fromMm, fmt, fmtU, editText, parseLength, editedMm, JOG_STEPS, feedFromMm, feedToMm, fmtFeed } from "./units.js";

test("mm and inches convert both ways", () => {
  assert.equal(toMm(1, "in"), 25.4);
  assert.equal(fromMm(25.4, "in"), 1);
  assert.equal(toMm(12, "mm"), 12);
  assert.equal(fromMm(12, "mm"), 12);
});

test("lengths show 2 decimals in mm and 3 in inches; positions 3 in both", () => {
  assert.equal(fmt(12.7, "mm"), "12.70");
  assert.equal(fmt(12.7, "in"), "0.500");
  assert.equal(fmtU(12.7, "in"), "0.500 in");
  assert.equal(fmt(1.23456, "mm", "position"), "1.235");
  assert.equal(fmt(25.4, "in", "position"), "1.000");
  assert.equal(fmt(-0.0001, "mm", "position"), "0.000");
  assert.equal(fmt(1000, "mm", 0), "1000");
  assert.equal(fmt(null, "mm"), "–");
});

test("a box starts with the shown value, trailing zeros dropped", () => {
  assert.equal(editText(12.5, "mm"), "12.5");
  assert.equal(editText(12.7, "in"), "0.5");
  assert.equal(editText(300, "in"), "11.811");
  assert.equal(editText(null, "mm"), "");
});

test("typed values: decimals, fractions and unit suffixes", () => {
  assert.equal(parseLength("0.75", "in"), toMm(0.75, "in"));
  assert.equal(parseLength("3/4", "in"), toMm(0.75, "in"));
  assert.equal(parseLength("1 1/2", "in"), toMm(1.5, "in"));
  assert.equal(parseLength("1-1/2", "in"), toMm(1.5, "in"));
  assert.equal(parseLength("-1 1/2", "in"), -toMm(1.5, "in"));
  assert.equal(parseLength('3/4"', "in"), toMm(0.75, "in"));
  assert.equal(parseLength("2 in", "in"), 50.8);
  assert.equal(parseLength("12 mm", "in"), 12);
  assert.equal(parseLength('1/2"', "mm"), 12.7);
  assert.equal(parseLength("12.5", "mm"), 12.5);
  assert.equal(parseLength(".5", "mm"), 0.5);
  assert.equal(parseLength("-3", "mm"), -3);
  for (const bad of ["", "abc", "1/0", "1 2", "1.5 1/2", "--1", "1/2/3"]) assert.ok(Number.isNaN(parseLength(bad, "in")), bad);
});

test("an untouched box keeps the stored value, so switching units never drifts", () => {
  const stored = 300;
  assert.equal(editedMm(editText(stored, "in"), stored, "in"), 300);
  assert.equal(editedMm("11.811", stored, "in"), 300);
  assert.equal(editedMm("12", stored, "in"), 304.8);
  // round trips through both units return the stored value exactly
  let v = 123.456789;
  for (let i = 0; i < 10; i++) { v = editedMm(editText(v, "in"), v, "in"); v = editedMm(editText(v, "mm"), v, "mm"); }
  assert.equal(v, 123.456789);
});

test("jog step presets per unit", () => {
  assert.deepEqual(JOG_STEPS.in, [0.001, 0.01, 0.1, 1]);
  assert.deepEqual(JOG_STEPS.mm, [0.1, 1, 10, 100]);
});

test("feed rates: mm/min and in/min", () => {
  assert.equal(feedFromMm(2540, "in"), 100);
  assert.equal(feedToMm(100, "in"), 2540);
  assert.equal(fmtFeed(2540, "in"), "100 in/min");
  assert.equal(fmtFeed(1000, "in"), "39.4 in/min");
  assert.equal(fmtFeed(1000, "mm"), "1000 mm/min");
});

test("fixed text follows the unit", () => {
  assert.equal(prose("A belt wound in more than 300 mm.", "in"), "A belt wound in more than 11.8 in.");
  assert.equal(prose("at least 2 mm above work Z 0", "in"), "at least 0.079 in above work Z 0");
  assert.equal(prose("at least 2 mm above", "mm"), "at least 2 mm above");
  assert.equal(fmtProse(30, "in"), "1.18 in");
  assert.equal(fmt(1234.4, "mm", "size"), "1234");
  assert.equal(fmt(25.4 * 12.345, "in", "size"), "12.35");
});
