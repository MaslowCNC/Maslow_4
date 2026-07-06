/**
 * Imperial units utility functions for Maslow CNC UI.
 * Handles conversion between mm, decimal inches, and fractional inches.
 */

// Max fractional denominators by axis
const INCH_FRAC_MAX_DENOM_XY = 32;   // 1/32 for X and Y
const INCH_FRAC_MAX_DENOM_Z  = 1024; // 1/1024 for Z

/**
 * Check if n is a positive power of 2.
 * @param {number} n
 * @returns {boolean}
 */
function isPowerOf2(n) {
  return Number.isInteger(n) && n > 0 && (n & (n - 1)) === 0;
}

/**
 * Compute the greatest common divisor of two non-negative integers.
 * @param {number} a
 * @param {number} b
 * @returns {number}
 */
function gcdInt(a, b) {
  a = Math.abs(Math.round(a));
  b = Math.abs(Math.round(b));
  while (b !== 0) {
    const t = b;
    b = a % b;
    a = t;
  }
  return a;
}

/**
 * Format a decimal-inch value as a mixed-number fractional string.
 * Rounds to nearest 1/maxDenom.
 * Examples: 1.1875 → "1 3/16", 0.5 → "1/2", 2.0 → "2", -1.5 → "-1 1/2"
 *
 * @param {number} inches - decimal inch value (may be negative)
 * @param {number} maxDenom - maximum denominator (must be a power of 2)
 * @returns {string}
 */
function formatInchFraction(inches, maxDenom) {
  if (isNaN(inches)) return '0';

  const negative = inches < 0;
  const abs = Math.abs(inches);

  // Round to nearest 1/maxDenom
  const rounded = Math.round(abs * maxDenom) / maxDenom;

  const whole = Math.floor(rounded);
  const fracPart = rounded - whole;

  let numer = Math.round(fracPart * maxDenom);
  const denom = maxDenom;

  // Reduce fraction
  if (numer > 0) {
    const g = gcdInt(numer, denom);
    numer = numer / g;
    const reducedDenom = denom / g;

    let result;
    if (whole === 0) {
      result = `${numer}/${reducedDenom}`;
    } else {
      result = `${whole} ${numer}/${reducedDenom}`;
    }
    return negative ? `-${result}` : result;
  }

  return negative && whole > 0 ? `-${whole}` : String(whole);
}

/**
 * Parse a fractional or decimal inch string to a decimal inch value.
 * Accepts formats:
 *   "W N/D"  (mixed number, e.g. "1 3/16")
 *   "N/D"    (fraction only, e.g. "3/16")
 *   "W"      (whole number, e.g. "2")
 *   "W.F"    (decimal, e.g. "1.5")
 *   Negative versions prefixed with "-" are also accepted.
 *
 * @param {string} str
 * @returns {number|null} decimal inch value, or null if invalid
 */
function parseInchFraction(str) {
  if (str === null || str === undefined) return null;
  const s = String(str).trim();
  if (s === '') return null;

  // Pure decimal (no slash)
  if (!s.includes('/')) {
    const v = parseFloat(s);
    return isNaN(v) ? null : v;
  }

  // Handle negative prefix
  const neg = s.startsWith('-');
  const abs = neg ? s.slice(1).trim() : s;

  const spaceIdx = abs.indexOf(' ');
  let whole = 0;
  let fracStr;

  if (spaceIdx !== -1) {
    // "W N/D" format
    const wholeStr = abs.slice(0, spaceIdx);
    fracStr = abs.slice(spaceIdx + 1).trim();
    whole = parseInt(wholeStr, 10);
    if (isNaN(whole) || whole < 0) return null;
  } else {
    // "N/D" format (no whole part)
    fracStr = abs;
  }

  const slashIdx = fracStr.indexOf('/');
  if (slashIdx === -1) return null;

  const numer = parseInt(fracStr.slice(0, slashIdx), 10);
  const denom = parseInt(fracStr.slice(slashIdx + 1), 10);
  if (isNaN(numer) || isNaN(denom) || denom === 0) return null;
  if (numer < 0 || denom < 0) return null;

  const result = whole + numer / denom;
  return neg ? -result : result;
}

/**
 * Validate a fractional-inch string for a given axis and return an error
 * message, or null when the value is valid.
 *
 * Rules:
 *  - Accepts decimal numbers, "N/D", or "W N/D".
 *  - Denominator must be a power of 2.
 *  - Denominator must not exceed INCH_FRAC_MAX_DENOM_Z  (1024) for Z axis.
 *  - Denominator must not exceed INCH_FRAC_MAX_DENOM_XY (32)   for X/Y axes.
 *  - Numerator must be less than denominator (proper fraction).
 *
 * @param {string} str   The input string.
 * @param {string} axis  Axis identifier: 'Z' | 'z' for Z; anything else for X/Y.
 * @returns {string|null} Error message, or null if valid.
 */
function validateInchFracInput(str, axis) {
  if (str === null || str === undefined || String(str).trim() === '') {
    return 'Value is required';
  }
  const s = String(str).trim();

  // Pure decimal — always accepted
  if (!s.includes('/')) {
    const v = parseFloat(s);
    if (isNaN(v)) return 'Invalid number';
    return null;
  }

  const isZAxis = axis && axis.toString().toUpperCase() === 'Z';
  const maxDenom = isZAxis ? INCH_FRAC_MAX_DENOM_Z : INCH_FRAC_MAX_DENOM_XY;

  const neg = s.startsWith('-');
  const abs = neg ? s.slice(1).trim() : s;
  const spaceIdx = abs.indexOf(' ');

  let fracStr;
  if (spaceIdx !== -1) {
    const wholeStr = abs.slice(0, spaceIdx);
    fracStr = abs.slice(spaceIdx + 1).trim();
    const whole = parseInt(wholeStr, 10);
    if (isNaN(whole) || whole < 0) return 'Invalid whole number part';
  } else {
    fracStr = abs;
  }

  const slashIdx = fracStr.indexOf('/');
  if (slashIdx === -1) return 'Invalid format. Use "W N/D" (e.g. "1 3/16")';

  const numer = parseInt(fracStr.slice(0, slashIdx), 10);
  const denom = parseInt(fracStr.slice(slashIdx + 1), 10);

  if (isNaN(numer) || numer < 0) return 'Numerator must be a non-negative integer';
  if (isNaN(denom) || denom <= 0) return 'Denominator must be a positive integer';
  if (!isPowerOf2(denom)) return 'Denominator must be a power of 2 (e.g. 2, 4, 8, 16, 32…)';
  if (denom > maxDenom) {
    return `Denominator too large. Maximum is 1/${maxDenom} for ${isZAxis ? 'Z' : 'X/Y'} axis`;
  }
  if (numer >= denom) return 'Numerator must be less than the denominator';

  return null;
}

/**
 * Return the max fraction denominator for a given axis.
 * @param {string} axis
 * @returns {number}
 */
function maxFracDenomForAxis(axis) {
  return (axis && axis.toString().toUpperCase() === 'Z')
    ? INCH_FRAC_MAX_DENOM_Z
    : INCH_FRAC_MAX_DENOM_XY;
}

/**
 * Parse a value that may be in mm (decimal) or fractional inches depending on
 * the supplied unit mode, and always return mm.
 *
 * @param {string|number} str        Raw input string or number.
 * @param {string}        unitMode   Current display mode: 'mm' | 'inch_frac' | 'inch_dec'.
 * @param {string}        [axis]     Axis hint for validation ('Z' etc.).
 * @returns {number|null}            Millimetre value, or null if parse fails.
 */
function parseToMm(str, unitMode, axis) {
  if (unitMode === 'mm') {
    const v = parseFloat(str);
    return isNaN(v) ? null : v;
  }
  // inch_frac or inch_dec
  const inches = parseInchFraction(str);
  if (inches === null) return null;
  return inches * 25.4;
}

/**
 * Format a millimetre value for display given the current unit mode.
 *
 * @param {number} mm         Value in millimetres.
 * @param {string} unitMode   'mm' | 'inch_frac' | 'inch_dec'
 * @param {string} [axis]     Axis hint ('Z' etc.) for fraction precision.
 * @returns {string}
 */
function mmToDisplay(mm, unitMode, axis) {
  switch (unitMode) {
    case 'inch_frac': {
      const inches = mm / 25.4;
      const maxDenom = maxFracDenomForAxis(axis);
      return formatInchFraction(inches, maxDenom);
    }
    case 'inch_dec':
      return (mm / 25.4).toFixed(4);
    default: // 'mm'
      return mm.toFixed(2);
  }
}
