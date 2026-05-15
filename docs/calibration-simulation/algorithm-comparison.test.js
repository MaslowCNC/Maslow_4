/**
 * Algorithm Comparison Test
 *
 * Automatically compares the original hill-climbing calibration algorithm with the
 * new Levenberg-Marquardt (LM) algorithm across multiple initial conditions.
 *
 * Outputs a table showing:
 * - Number of iterations to convergence
 * - Wall-clock time
 * - Final fitness (higher = better)
 * - Final anchor positions
 * - Agreement between the two algorithms
 *
 * Run with: node algorithm-comparison.test.js
 */

'use strict';

const path = require('path');
const {
    CalibrationComputer,
    LevenbergMarquardtCalibrationComputer,
    lmComputeResiduals,
    lmSSR
} = require(path.join(__dirname, '../../ESP3D-WEBUI/www/js/calibration-computation.js'));

// ─────────────────────────────────────────────────────────────────────────────
// Synthetic data generation helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * True anchor positions used to generate synthetic measurements.
 * Coordinates are relative to blX=0, blY=0 (bottom-left anchor fixed at origin).
 */
const TRUE_ANCHORS = {
    tl: { x: -1450, y: 1050 },
    tr: { x:  1520, y: 1035 },
    bl: { x:     0, y:    0 },
    br: { x:  2970, y:    0 }
};

/**
 * Compute the 3-D belt length from a sled position to an anchor, then project to XY.
 */
function beltLength(sledX, sledY, anchor, zHeight) {
    const d3 = Math.sqrt(
        (sledX - anchor.x) ** 2 +
        (sledY - anchor.y) ** 2 +
        zHeight ** 2
    );
    // Project to XY plane (matching measurementToXYPlane in machine-simulator.js)
    return Math.sqrt(d3 * d3 - zHeight * zHeight);
}

/**
 * Generate synthetic calibration measurements for a regular grid of sled positions.
 *
 * @param {Object} anchors - True anchor positions {tl, tr, bl, br}
 * @param {number} rows - Grid rows
 * @param {number} cols - Grid columns
 * @param {number} xSpacing - Spacing in mm along X
 * @param {number} ySpacing - Spacing in mm along Y
 * @param {number} noiseLevel - Random noise ±noiseLevel mm (0 = no noise)
 * @param {number} seed - Pseudo-random seed (0 = use Math.random)
 * @returns {Array} Array of {tl, tr, bl, br} measurement objects
 */
function generateMeasurements(anchors, rows, cols, xSpacing, ySpacing, noiseLevel, seed) {
    // Simple LCG for reproducible results when seed != 0
    let state = seed || 0;
    function rand() {
        if (!seed) return Math.random();
        state = (state * 1664525 + 1013904223) & 0xffffffff;
        return (state >>> 0) / 0xffffffff;
    }

    const zHeights = { tl: 100, tr: 56, bl: 34, br: 78 };
    const measurements = [];

    for (let r = 0; r < rows; r++) {
        for (let c = 0; c < cols; c++) {
            const sledX = (c - (cols - 1) / 2) * xSpacing;
            const sledY = (r - (rows - 1) / 2) * ySpacing;

            const noise = () => (rand() - 0.5) * 2 * noiseLevel;

            measurements.push({
                tl: beltLength(sledX, sledY, anchors.tl, zHeights.tl) + noise(),
                tr: beltLength(sledX, sledY, anchors.tr, zHeights.tr) + noise(),
                bl: beltLength(sledX, sledY, anchors.bl, zHeights.bl) + noise(),
                br: beltLength(sledX, sledY, anchors.br, zHeights.br) + noise()
            });
        }
    }

    return measurements;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initial guess configurations
// ─────────────────────────────────────────────────────────────────────────────

const INITIAL_GUESSES = [
    {
        name: 'Near-perfect (true values)',
        guess: {
            tl: { x: -1450, y: 1050 },
            tr: { x:  1520, y: 1035 },
            bl: { x:     0, y:    0 },
            br: { x:  2970, y:    0 }
        }
    },
    {
        name: 'Ideal rectangle (close)',
        guess: {
            tl: { x:     0, y: 2000 },
            tr: { x:  3000, y: 2000 },
            bl: { x:     0, y:    0 },
            br: { x:  3000, y:    0 }
        }
    },
    {
        name: 'Offset by ~300mm',
        guess: {
            tl: { x:  -800, y: 1400 },
            tr: { x:  2200, y: 1350 },
            bl: { x:     0, y:    0 },
            br: { x:  2700, y:    0 }
        }
    },
    {
        name: 'Scaled down (75%)',
        guess: {
            tl: { x: -1000, y:  750 },
            tr: { x:  1200, y:  750 },
            bl: { x:     0, y:    0 },
            br: { x:  2200, y:    0 }
        }
    },
    {
        name: 'Narrow (aspect ratio wrong)',
        guess: {
            tl: { x:  -500, y:  800 },
            tr: { x:   500, y:  800 },
            bl: { x:     0, y:    0 },
            br: { x:  1000, y:    0 }
        }
    },
    {
        name: 'Very large initial guess (far from truth)',
        guess: {
            tl: { x: -3000, y: 2500 },
            tr: { x:  3500, y: 2500 },
            bl: { x:     0, y:    0 },
            br: { x:  6000, y:    0 }
        }
    }
];

// ─────────────────────────────────────────────────────────────────────────────
// Comparison logic
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Compute the Euclidean distance between two 2-D points.
 */
function dist2D(a, b) {
    return Math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2);
}

/**
 * Compute max anchor position error relative to true positions.
 */
function anchorError(result) {
    return {
        tl: dist2D(result.tl, TRUE_ANCHORS.tl),
        tr: dist2D(result.tr, TRUE_ANCHORS.tr),
        br: Math.abs(result.br.x - TRUE_ANCHORS.br.x)  // brY is always 0
    };
}

/**
 * Run one algorithm and return timing + result data.
 */
async function runAlgorithm(AlgoClass, algoConfig, measurements, guess, label) {
    const computer = new AlgoClass(JSON.parse(JSON.stringify(guess)), algoConfig);
    const t0 = Date.now();
    const result = await computer.processDataChunk(JSON.parse(JSON.stringify(measurements)), null);
    const elapsed = Date.now() - t0;
    const status = computer.getStatus();

    return {
        label,
        elapsed,
        iterations: status.totalIterations,
        fitness: status.bestFitness,
        result,
        error: anchorError(result)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Reporting helpers
// ─────────────────────────────────────────────────────────────────────────────

function fmt(n, d) { return Number(n).toFixed(d).padStart(8); }
function pad(s, w) { return String(s).padEnd(w); }

function printHeader() {
    console.log('');
    console.log('='.repeat(90));
    console.log('  Maslow CNC Calibration — Algorithm Comparison');
    console.log('  Original Hill-Climbing  vs  Levenberg-Marquardt');
    console.log('='.repeat(90));
    console.log('');
    console.log('  True anchor positions (reference):');
    Object.entries(TRUE_ANCHORS).forEach(([k, v]) => {
        console.log(`    ${k.toUpperCase()}: X=${v.x.toFixed(1)} mm, Y=${v.y.toFixed(1)} mm`);
    });
    console.log('');
}

function printGuessHeader(guessName) {
    console.log('-'.repeat(90));
    console.log(`  Initial guess: ${guessName}`);
    console.log('-'.repeat(90));
}

function printAlgoRow(data) {
    const e = data.error;
    const maxErr = Math.max(e.tl, e.tr, e.br);
    console.log(
        `  ${pad(data.label, 28)}` +
        `  iters=${pad(data.iterations, 6)}` +
        `  t=${pad(data.elapsed + 'ms', 8)}` +
        `  fitness=${fmt(data.fitness, 4)}` +
        `  err(tl/tr/br)=${fmt(e.tl, 1)}/${fmt(e.tr, 1)}/${fmt(e.br, 1)} mm` +
        `  max=${fmt(maxErr, 1)} mm`
    );
    console.log(
        `  ${pad('', 28)}` +
        `  tl=(${data.result.tl.x.toFixed(1)}, ${data.result.tl.y.toFixed(1)})` +
        `  tr=(${data.result.tr.x.toFixed(1)}, ${data.result.tr.y.toFixed(1)})` +
        `  br=(${data.result.br.x.toFixed(1)}, 0)`
    );
}

function printAgreement(origData, lmData) {
    const tlDiff = dist2D(origData.result.tl, lmData.result.tl);
    const trDiff = dist2D(origData.result.tr, lmData.result.tr);
    const brDiff = Math.abs(origData.result.br.x - lmData.result.br.x);
    const maxDiff = Math.max(tlDiff, trDiff, brDiff);
    const agree = maxDiff < 10.0;  // Within 10 mm is considered agreement

    const speedup = origData.elapsed > 0
        ? (origData.elapsed / lmData.elapsed).toFixed(1) + 'x'
        : 'n/a';

    console.log(
        `  Agreement: ${agree ? '✅  YES' : '❌  NO '}` +
        `  (max diff ${maxDiff.toFixed(1)} mm)` +
        `  LM speedup: ${speedup}` +
        `  (orig ${origData.elapsed} ms vs LM ${lmData.elapsed} ms)`
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

async function main() {
    printHeader();

    // Generate a representative set of measurements
    const measurements = generateMeasurements(
        TRUE_ANCHORS,
        5, 5,      // 5×5 grid
        260, 100,  // 260 mm x-spacing, 100 mm y-spacing
        0.5,       // ±0.5 mm random noise
        42         // Fixed seed for reproducibility
    );

    console.log(`  Measurement set: ${measurements.length} points, noise ±0.5 mm, seed=42`);
    console.log('');

    let totalAgreements = 0;
    let totalTests = 0;
    const summary = [];

    for (const { name, guess } of INITIAL_GUESSES) {
        printGuessHeader(name);

        const origData = await runAlgorithm(
            CalibrationComputer,
            { maxIterations: 2000, maxStagnant: 100 },
            measurements,
            guess,
            'Original (hill-climbing)'
        );

        const lmData = await runAlgorithm(
            LevenbergMarquardtCalibrationComputer,
            { maxIterations: 200 },
            measurements,
            guess,
            'Levenberg-Marquardt'
        );

        printAlgoRow(origData);
        printAlgoRow(lmData);
        printAgreement(origData, lmData);
        console.log('');

        const tlDiff = dist2D(origData.result.tl, lmData.result.tl);
        const trDiff = dist2D(origData.result.tr, lmData.result.tr);
        const brDiff = Math.abs(origData.result.br.x - lmData.result.br.x);
        const maxDiff = Math.max(tlDiff, trDiff, brDiff);
        const agree = maxDiff < 10.0;
        if (agree) totalAgreements++;
        totalTests++;

        summary.push({
            name,
            origFitness: origData.fitness,
            lmFitness: lmData.fitness,
            origIter: origData.iterations,
            lmIter: lmData.iterations,
            origTime: origData.elapsed,
            lmTime: lmData.elapsed,
            maxDiff,
            agree
        });
    }

    // ── Summary ──────────────────────────────────────────────────────────────
    console.log('='.repeat(90));
    console.log('  Summary');
    console.log('='.repeat(90));
    console.log(
        `  ${'Initial guess'.padEnd(38)}` +
        `  ${'Orig fit'.padStart(8)}` +
        `  ${'LM fit'.padStart(8)}` +
        `  ${'Orig itr'.padStart(9)}` +
        `  ${'LM itr'.padStart(7)}` +
        `  ${'Agree?'.padStart(7)}`
    );
    console.log('  ' + '-'.repeat(86));
    for (const row of summary) {
        console.log(
            `  ${row.name.padEnd(38)}` +
            `  ${row.origFitness.toFixed(4).padStart(8)}` +
            `  ${row.lmFitness.toFixed(4).padStart(8)}` +
            `  ${String(row.origIter).padStart(9)}` +
            `  ${String(row.lmIter).padStart(7)}` +
            `  ${row.agree ? '✅' : '❌'}  (${row.maxDiff.toFixed(1)} mm)`
        );
    }
    console.log('');

    const avgOrigTime = summary.reduce((s, r) => s + r.origTime, 0) / summary.length;
    const avgLMTime   = summary.reduce((s, r) => s + r.lmTime,   0) / summary.length;
    const avgOrigIter = summary.reduce((s, r) => s + r.origIter, 0) / summary.length;
    const avgLMIter   = summary.reduce((s, r) => s + r.lmIter,   0) / summary.length;

    console.log(`  Algorithms agreed on ${totalAgreements}/${totalTests} initial conditions.`);
    console.log(`  Avg time  — original: ${avgOrigTime.toFixed(0)} ms,  LM: ${avgLMTime.toFixed(0)} ms`);
    console.log(`  Avg iters — original: ${avgOrigIter.toFixed(0)},     LM: ${avgLMIter.toFixed(0)}`);
    console.log('');

    if (totalAgreements === totalTests) {
        console.log('  ✅  PASS: Both algorithms converge to the same solution regardless of starting conditions.');
    } else {
        console.log('  ⚠️  Some initial conditions led to different results between algorithms.');
        console.log('     This may indicate the original algorithm got stuck in a local optimum.');
    }

    console.log('='.repeat(90));
    console.log('');
}

main().catch(err => {
    console.error('Error running comparison:', err);
    process.exit(1);
});
