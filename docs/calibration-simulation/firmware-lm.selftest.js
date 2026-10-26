/*
 * Self-test for the firmware-exact Levenberg-Marquardt anchor recompute port.
 *
 * Validates that:
 *  1. The analytic Jacobian (fwMeasurementJacobiansAndResiduals) matches a
 *     central finite-difference of lmBundleResiduals to < 1e-6.
 *  2. fwSolve5x5 solves a known linear system.
 *  3. fwInvertDamped2x2 matches a direct 2x2 inverse when lambda = 0.
 *  4. recomputeAnchorsLM is deterministic and self-consistent
 *     (reported rms == sqrt(SSR / (4N))).
 *
 * Run with: node docs/calibration-simulation/firmware-lm.selftest.js
 */
const lib = require('./calibration-computation.js');
const {
    recomputeAnchorsLM,
    fwMeasurementJacobiansAndResiduals,
    fwSolve5x5,
    fwInvertDamped2x2,
    lmBundleResiduals,
    lmEstimateSledPosition,
    lmSSR
} = lib;

let failures = 0;
function check(name, cond, detail) {
    if (cond) {
        console.log('  PASS ' + name);
    } else {
        failures++;
        console.log('  FAIL ' + name + (detail ? ' — ' + detail : ''));
    }
}

// Sample CLBM data (from the data-parser.html placeholder example).
const measurements = [
    { bl: 2960.58, br: 3150.08, tr: 3067.72, tl: 3049.85 },
    { bl: 3066.96, br: 3042.59, tr: 2957.53, tl: 3158.38 }
].map(m => ({ tl: m.tl, tr: m.tr, bl: m.bl, br: m.br }));

const initialAnchors = {
    tl: { x: 0,    y: 2978.4 },
    tr: { x: 3400, y: 2978.4 },
    bl: { x: 0,    y: 0 },
    br: { x: 3400, y: 0 }
};

// ── Test 1: analytic Jacobian vs central finite differences ────────────────
console.log('Test 1: analytic Jacobian vs finite differences');
{
    const tlX = 10, tlY = 2980, trX = 3390, trY = 2975, brX = 3395;
    const sx = 1700, sy = 1500;
    const m = measurements[0];
    const { jia, jis } = fwMeasurementJacobiansAndResiduals(m, tlX, tlY, trX, trY, brX, sx, sy);

    // Full 5-anchor + this-sled param vector for a single measurement.
    const params = [tlX, tlY, trX, trY, brX, sx, sy];
    const single = [m];
    const h = 1e-4;
    let maxErr = 0;

    // Anchor columns 0..4 → jia rows.
    for (let col = 0; col < 5; col++) {
        const pp = params.slice(); pp[col] += h;
        const pm = params.slice(); pm[col] -= h;
        const rp = lmBundleResiduals(single, pp);
        const rm = lmBundleResiduals(single, pm);
        for (let row = 0; row < 4; row++) {
            const fd = (rp[row] - rm[row]) / (2 * h);
            maxErr = Math.max(maxErr, Math.abs(fd - jia[row][col]));
        }
    }
    // Sled columns 5,6 → jis rows.
    for (let sc = 0; sc < 2; sc++) {
        const col = 5 + sc;
        const pp = params.slice(); pp[col] += h;
        const pm = params.slice(); pm[col] -= h;
        const rp = lmBundleResiduals(single, pp);
        const rm = lmBundleResiduals(single, pm);
        for (let row = 0; row < 4; row++) {
            const fd = (rp[row] - rm[row]) / (2 * h);
            maxErr = Math.max(maxErr, Math.abs(fd - jis[row][sc]));
        }
    }
    check('Jacobian max abs error < 1e-6', maxErr < 1e-6, 'maxErr=' + maxErr.toExponential(3));
}

// ── Test 2: fwSolve5x5 on a known system ──────────────────────────────────
console.log('Test 2: fwSolve5x5');
{
    const A = [
        [4, 1, 0, 0, 0],
        [1, 3, 1, 0, 0],
        [0, 1, 2, 1, 0],
        [0, 0, 1, 5, 1],
        [0, 0, 0, 1, 2]
    ];
    const xTrue = [1, -2, 3, -4, 5];
    const b = A.map(row => row.reduce((s, v, j) => s + v * xTrue[j], 0));
    const Acopy = A.map(r => r.slice());
    const { ok, solution } = fwSolve5x5(Acopy, b);
    let maxErr = 0;
    if (ok) for (let i = 0; i < 5; i++) maxErr = Math.max(maxErr, Math.abs(solution[i] - xTrue[i]));
    check('solve5x5 recovers known solution', ok && maxErr < 1e-9, 'maxErr=' + maxErr.toExponential(3));
}

// ── Test 3: fwInvertDamped2x2 with lambda = 0 ─────────────────────────────
console.log('Test 3: fwInvertDamped2x2 (lambda = 0)');
{
    const v00 = 3, v01 = 1, v11 = 2;
    const det = v00 * v11 - v01 * v01;
    const exp = [[v11 / det, -v01 / det], [-v01 / det, v00 / det]];
    const { ok, inv } = fwInvertDamped2x2(v00, v01, v11, 0);
    let maxErr = 0;
    if (ok) for (let i = 0; i < 2; i++) for (let j = 0; j < 2; j++) maxErr = Math.max(maxErr, Math.abs(inv[i][j] - exp[i][j]));
    check('invertDamped2x2 matches direct inverse', ok && maxErr < 1e-12, 'maxErr=' + maxErr.toExponential(3));
}

// ── Test 4: recomputeAnchorsLM determinism + self-consistency ─────────────
console.log('Test 4: recomputeAnchorsLM');
{
    const r1 = recomputeAnchorsLM(measurements, initialAnchors);
    const r2 = recomputeAnchorsLM(measurements, initialAnchors);
    check('result is deterministic',
        JSON.stringify(r1.anchors) === JSON.stringify(r2.anchors) && r1.ssr === r2.ssr);

    // Reported rms must equal sqrt(SSR / (4N)) — the firmware definition.
    const N = measurements.length;
    const expectedRms = Math.sqrt(r1.ssr / (4 * N));
    check('reported rms == sqrt(SSR/(4N))', Math.abs(r1.fitness.rms - expectedRms) < 1e-9,
        'rms=' + r1.fitness.rms + ' expected=' + expectedRms);

    // SSR of returned anchors (with freshly estimated sleds) should be near ssr.
    console.log('    anchors: tl=(' + r1.anchors.tl.x.toFixed(2) + ',' + r1.anchors.tl.y.toFixed(2) +
        ') tr=(' + r1.anchors.tr.x.toFixed(2) + ',' + r1.anchors.tr.y.toFixed(2) +
        ') brX=' + r1.anchors.br.x.toFixed(2));
    console.log('    rms=' + r1.fitness.rms.toFixed(5) + 'mm max=' + r1.fitness.maxResidual.toFixed(5) +
        'mm converged=' + r1.fitness.converged + ' passed=' + r1.passed +
        ' iters=' + r1.totalIterations);
}

console.log('');
if (failures === 0) {
    console.log('ALL SELF-TESTS PASSED');
    process.exit(0);
} else {
    console.log(failures + ' SELF-TEST(S) FAILED');
    process.exit(1);
}
