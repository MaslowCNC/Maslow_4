// mm / inch for what the page shows and what's typed. The firmware always works in mm: every value in device state
// is mm, and every value sent is mm. Only the screen converts. Values are stored as converted, never rounded; rounding
// is for display only, so switching units back and forth doesn't drift.
//
// unit: "mm" or "in" (Settings › Display › Units).

export const MM_PER_INCH = 25.4;
export const UNITS = ["mm", "in"];
export const unitOf = (u) => (u === "in" ? "in" : "mm");
// For labels read aloud and messages: "mm" or "inches"
export const unitName = (u) => (u === "in" ? "inches" : "mm");

// To a millionth of a mm, so 12 in is 304.8 mm and not 304.79999999999995 when it's saved
export const toMm = (v, unit) => (unit === "in" ? Math.round(v * MM_PER_INCH * 1e6) / 1e6 : v);
export const fromMm = (mm, unit) => (unit === "in" ? mm / MM_PER_INCH : mm);

// Decimals shown: a length in 2 (mm) or 3 (in); a position 3 in both, as a DRO does; a size (a file's or the work
// area's width) 0 or 2; a depth or a coordinate in a sentence 1 or 3
const DECIMALS = { length: { mm: 2, in: 3 }, position: { mm: 3, in: 3 }, size: { mm: 0, in: 2 }, fine: { mm: 1, in: 3 } };
const decimalsFor = (unit, kind) => (typeof kind === "number" ? kind : (DECIMALS[kind] || DECIMALS.length)[unitOf(unit)]);

/**
 * A distance in mm as the number shown in `unit`, without the unit.
 * kind: "length" (default), "position", or a number of decimals. -0 shows as 0.
 */
export function fmt(mm, unit, kind = "length") {
  if (mm == null || !Number.isFinite(mm)) return "–";
  const n = decimalsFor(unit, kind), v = fromMm(mm, unit);
  const s = v.toFixed(n);
  return Number(s) === 0 ? (0).toFixed(n) : s;
}

/** The same with the unit: "12.70 mm", "0.500 in" */
export const fmtU = (mm, unit, kind = "length") => `${fmt(mm, unit, kind)} ${unitOf(unit)}`;

/**
 * A distance in a sentence: in mm as given, in inches to about 3 significant figures ("11.8 in", "1.18 in", "0.079 in")
 */
export function fmtProse(mm, unit) {
  if (unitOf(unit) === "mm") return `${mm} mm`;
  const v = fromMm(mm, "in"), a = Math.abs(v);
  return `${Number(v.toFixed(a >= 10 ? 1 : a >= 1 ? 2 : 3))} in`;
}

/** Fixed text with "N mm" in it, in the chosen unit: "more than 300 mm" → "more than 11.8 in" */
export const prose = (text, unit) =>
  unitOf(unit) === "mm" || typeof text !== "string" ? text : text.replace(/(\d+(?:\.\d+)?) mm\b/g, (_, n) => fmtProse(+n, unit));

/** The keyboard for a length box: inches can be typed as fractions ("1 1/2"), which a phone's decimal pad can't */
export const numMode = (unit) => (unitOf(unit) === "in" ? "text" : "decimal");

/** Text to start an input box with: the value in `unit`, rounded as shown, with trailing zeros dropped ("12.5", "0.5") */
export function editText(mm, unit, kind = "length") {
  if (mm == null || !Number.isFinite(mm)) return "";
  return String(Number(fmt(mm, unit, kind)));
}

// "1 1/2", "1-1/2", "3/4", "-1 1/2", "0.75", "2", with an optional unit: " in ", '"', "mm"
const NUM = String.raw`\d+(?:\.\d*)?|\.\d+`;
const RE = new RegExp(String.raw`^([+-]?)\s*(?:(\d+)(?:\s+|\s*-\s*)(\d+)\s*/\s*(\d+)|(\d+)\s*/\s*(\d+)|(${NUM}))\s*(in|inch|inches|"|mm)?$`, "i");

/**
 * A typed distance, in mm, or NaN. The number is in `unit` unless the text says otherwise ("12 mm" while in inches,
 * '1/2"' while in mm). Fractions are accepted in either ("3/4", "1 1/2"), as tape measures read.
 */
export function parseLength(text, unit) {
  const m = RE.exec(String(text ?? "").trim());
  if (!m) return NaN;
  const [, sign, whole, num, den, fnum, fden, plain, suffix] = m;
  let v;
  if (whole != null) { if (+den === 0) return NaN; v = +whole + +num / +den; }
  else if (fnum != null) { if (+fden === 0) return NaN; v = +fnum / +fden; }
  else v = +plain;
  if (sign === "-") v = -v;
  const u = !suffix ? unitOf(unit) : suffix.toLowerCase() === "mm" ? "mm" : "in";
  return toMm(v, u);
}

/**
 * What an edited box holds, in mm: the stored value while the text still shows it as it was filled in (so an untouched,
 * rounded box never saves its rounding), else what was typed.
 */
export function editedMm(text, storedMm, unit, kind = "length") {
  if (storedMm != null && Number.isFinite(storedMm) && String(text).trim() === editText(storedMm, unit, kind)) return storedMm;
  return parseLength(text, unit);
}

// Jog step presets, in the unit shown
export const JOG_STEPS = { mm: [0.1, 1, 10, 100], in: [0.001, 0.01, 0.1, 1] };
// The steps the jog pad starts with
export const JOG_DEFAULT = { mm: { xy: 10, z: 5 }, in: { xy: 0.5, z: 0.2 } };

// Feed rates: mm/min ↔ in/min
export const feedFromMm = (mmPerMin, unit) => fromMm(mmPerMin, unit);
export const feedToMm = (v, unit) => toMm(v, unit);
export const feedUnit = (unit) => `${unitOf(unit)}/min`;
export const fmtFeed = (mmPerMin, unit) =>
  mmPerMin == null || !Number.isFinite(mmPerMin) ? "–" : `${Math.round(feedFromMm(mmPerMin, unit) * (unit === "in" ? 10 : 1)) / (unit === "in" ? 10 : 1)} ${feedUnit(unit)}`;
