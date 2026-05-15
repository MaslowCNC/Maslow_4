/**
 * Calibration Computation Library
 * 
 * This is a shared library that contains the core calibration computation logic.
 * It is used by both:
 * - ESP3D-WEBUI (the actual web interface running on the machine)
 * - The calibration simulator (for testing and development)
 * 
 * This eliminates code duplication between the simulator and the actual implementation.
 * 
 * NOTE: This file should be kept in sync with the logic in 
 * ESP3D-WEBUI/www/js/calculatesCalibrationStuff.js
 */

/**
 * Computes the distance between two points.
 */
function distanceBetweenPoints(a, b, c, d) {
    const dx = c - a;
    const dy = d - b;
    return Math.sqrt(dx * dx + dy * dy);
}

/**
 * Computes the end point of a line based on its starting point, angle, and length.
 */
function getEndPoint(startX, startY, angle, length) {
    const endX = startX + length * Math.cos(angle);
    const endY = startY + length * Math.sin(angle);
    return { x: endX, y: endY };
}

/**
 * Computes how close all of the line end points are to each other.
 */
function computeEndpointFitness(line1, line2, line3, line4) {
    const a = distanceBetweenPoints(line1.xEnd, line1.yEnd, line2.xEnd, line2.yEnd);
    const b = distanceBetweenPoints(line1.xEnd, line1.yEnd, line3.xEnd, line3.yEnd);
    const c = distanceBetweenPoints(line1.xEnd, line1.yEnd, line4.xEnd, line4.yEnd);
    const d = distanceBetweenPoints(line2.xEnd, line2.yEnd, line3.xEnd, line3.yEnd);
    const e = distanceBetweenPoints(line2.xEnd, line2.yEnd, line4.xEnd, line4.yEnd);
    const f = distanceBetweenPoints(line3.xEnd, line3.yEnd, line4.xEnd, line4.yEnd);

    return (a + b + c + d + e + f) / 6;
}

/**
 * Computes the end point of a line based on its starting point, angle, and length.
 */
function computeLineEndPoint(line) {
    const end = getEndPoint(line.xBegin, line.yBegin, line.theta, line.length);
    line.xEnd = end.x;
    line.yEnd = end.y;
    return line;
}

/**
 * Walks the four lines, adjusting their endpoints to minimize the distance between them.
 */
function walkLines(tlLine, trLine, blLine, brLine, stepSize) {
    let changeMade = true;
    let bestFitness = computeEndpointFitness(tlLine, trLine, blLine, brLine);

    while (changeMade) {
        changeMade = false;
        const lines = [tlLine, trLine, blLine, brLine];

        for (let i = 0; i < lines.length; i++) {
            const line = lines[i];

            for (let direction of [-1, 1]) {
                const newLine = computeLineEndPoint({
                    xBegin: line.xBegin,
                    yBegin: line.yBegin,
                    theta: line.theta + direction * stepSize,
                    length: line.length,
                });

                const newFitness = computeEndpointFitness(
                    i === 0 ? newLine : tlLine,
                    i === 1 ? newLine : trLine,
                    i === 2 ? newLine : blLine,
                    i === 3 ? newLine : brLine
                );

                if (newFitness < bestFitness) {
                    lines[i] = newLine;
                    bestFitness = newFitness;
                    changeMade = true;
                }
            }
        }

        tlLine = lines[0];
        trLine = lines[1];
        blLine = lines[2];
        brLine = lines[3];
    }

    return { tlLine, trLine, blLine, brLine };
}

/**
 * Fitness function that uses "magnetically attracted lines" approach
 */
function magneticallyAttractedLinesFitness(measurement, individual) {
    // Initialize theta values if not present
    if (typeof measurement.tlTheta === 'undefined') {
        measurement.tlTheta = -0.3;
        measurement.trTheta = 3.5;
        measurement.blTheta = 0.5;
        measurement.brTheta = 2.6;
    }

    // Create lines from anchor points with measured lengths
    let tlLine = computeLineEndPoint({
        xBegin: individual.tl.x,
        yBegin: individual.tl.y,
        theta: measurement.tlTheta,
        length: measurement.tl
    });
    let trLine = computeLineEndPoint({
        xBegin: individual.tr.x,
        yBegin: individual.tr.y,
        theta: measurement.trTheta,
        length: measurement.tr
    });
    let blLine = computeLineEndPoint({
        xBegin: individual.bl.x,
        yBegin: individual.bl.y,
        theta: measurement.blTheta,
        length: measurement.bl
    });
    let brLine = computeLineEndPoint({
        xBegin: individual.br.x,
        yBegin: individual.br.y,
        theta: measurement.brTheta,
        length: measurement.br
    });

    // Walk the lines with decreasing step sizes for progressive refinement
    const stepSizes = [0.1, 0.01, 0.001, 0.0001, 0.00001, 0.000001, 0.0000001, 0.00000001];
    for (const stepSize of stepSizes) {
        const walked = walkLines(tlLine, trLine, blLine, brLine, stepSize);
        tlLine = walked.tlLine;
        trLine = walked.trLine;
        blLine = walked.blLine;
        brLine = walked.brLine;
    }

    // Store updated theta values back in measurement
    measurement.tlTheta = tlLine.theta;
    measurement.trTheta = trLine.theta;
    measurement.blTheta = blLine.theta;
    measurement.brTheta = brLine.theta;

    const fitness = computeEndpointFitness(tlLine, trLine, blLine, brLine);

    return {
        fitness: fitness,
        lines: { tlLine, trLine, blLine, brLine }
    };
}

/**
 * Compute distance from center of mass for a line
 */
function computeDistanceFromCenterOfMass(lineToCompare, line2, line3, line4) {
    // Compute the center of mass from the OTHER three lines (not including lineToCompare)
    const centerX = (line2.xEnd + line3.xEnd + line4.xEnd) / 3;
    const centerY = (line2.yEnd + line3.yEnd + line4.yEnd) / 3;

    // Return the distance vector from lineToCompare's endpoint to the center
    return {
        x: lineToCompare.xEnd - centerX,
        y: lineToCompare.yEnd - centerY
    };
}

/**
 * Generate tweaks for anchor positions based on distances from center of mass
 */
function generateTweaks(lines) {
    return {
        tlX: computeDistanceFromCenterOfMass(lines.tlLine, lines.trLine, lines.blLine, lines.brLine).x,
        tlY: computeDistanceFromCenterOfMass(lines.tlLine, lines.trLine, lines.blLine, lines.brLine).y,
        trX: computeDistanceFromCenterOfMass(lines.trLine, lines.tlLine, lines.blLine, lines.brLine).x,
        trY: computeDistanceFromCenterOfMass(lines.trLine, lines.tlLine, lines.blLine, lines.brLine).y,
        brX: computeDistanceFromCenterOfMass(lines.brLine, lines.tlLine, lines.trLine, lines.blLine).x
    };
}

/**
 * Compute furthest anchor from center of mass
 */
function computeFurthestFromCenterOfMass(allLines, lastGuess) {
    let tlX = 0, tlY = 0, trX = 0, trY = 0, brX = 0;

    // Accumulate tweaks from all lines
    allLines.forEach(lines => {
        const tweaks = generateTweaks(lines);
        tlX += tweaks.tlX;
        tlY += tweaks.tlY;
        trX += tweaks.trX;
        trY += tweaks.trY;
        brX += tweaks.brX;
    });

    // Average the tweaks
    const n = allLines.length;
    tlX /= n;
    tlY /= n;
    trX /= n;
    trY /= n;
    brX /= n;

    // Find the largest error
    const maxError = Math.max(
        Math.abs(tlX),
        Math.abs(tlY),
        Math.abs(trX),
        Math.abs(trY),
        Math.abs(brX)
    );

    // Apply the largest error as a correction
    const newGuess = JSON.parse(JSON.stringify(lastGuess));
    const scalor = -1;  // Move in opposite direction of error

    if (Math.abs(tlX) === maxError) {
        newGuess.tl.x += tlX * scalor;
    } else if (Math.abs(tlY) === maxError) {
        newGuess.tl.y += tlY * scalor;
    } else if (Math.abs(trX) === maxError) {
        newGuess.tr.x += trX * scalor;
    } else if (Math.abs(trY) === maxError) {
        newGuess.tr.y += trY * scalor;
    } else if (Math.abs(brX) === maxError) {
        newGuess.br.x += brX * scalor;
    }

    return newGuess;
}

/**
 * Compute overall fitness for a set of measurements
 */
function computeLinesFitness(measurements, lastGuess, skipThetaUpdates = false) {
    const fitnesses = [];
    const allLines = [];

    measurements.forEach(measurement => {
        const result = magneticallyAttractedLinesFitness(measurement, lastGuess);
        fitnesses.push(result.fitness);
        allLines.push(result.lines);
    });

    const avgFitness = fitnesses.reduce((a, b) => a + Math.abs(b), 0) / fitnesses.length;

    const updatedGuess = computeFurthestFromCenterOfMass(allLines, lastGuess);
    updatedGuess.fitness = avgFitness;

    return updatedGuess;
}

/**
 * CalibrationComputer class - Manages the calibration computation process
 */
class CalibrationComputer {
    constructor(initialGuess, config = {}) {
        this.initialGuess = {
            tl: { ...initialGuess.tl },
            tr: { ...initialGuess.tr },
            bl: { ...initialGuess.bl },
            br: { ...initialGuess.br },
            fitness: 100000000
        };
        this.currentGuess = null;
        this.bestGuess = null;
        this.totalIterations = 0;
        this.stagnantCounter = 0;
        this.acceptableThreshold = config.acceptableThreshold || 0.5;
        this.maxIterations = config.maxIterations || 200000;
        this.maxStagnant = config.maxStagnant || 1000;
    }

    /**
     * Process a chunk of measurement data
     */
    async processDataChunk(measurements, progressCallback) {
        // Initialize from initial guess if first computation
        if (!this.currentGuess) {
            this.currentGuess = JSON.parse(JSON.stringify(this.initialGuess));
            this.bestGuess = JSON.parse(JSON.stringify(this.initialGuess));
        }

        this.stagnantCounter = 0;
        this.totalIterations = 0;

        // Run optimization
        const result = await this.optimize(measurements, progressCallback);

        // Update initial guess for next stage
        if (1 / result.fitness > this.acceptableThreshold) {
            this.initialGuess = JSON.parse(JSON.stringify(result));
        }

        return result;
    }

    /**
     * Run the optimization algorithm
     */
    async optimize(measurements, progressCallback) {
        while (this.stagnantCounter < this.maxStagnant && this.totalIterations < this.maxIterations) {
            this.currentGuess = computeLinesFitness(measurements, this.currentGuess);

            if (1 / this.currentGuess.fitness > 1 / this.bestGuess.fitness) {
                this.bestGuess = JSON.parse(JSON.stringify(this.currentGuess));
                this.stagnantCounter = 0;
            } else {
                this.stagnantCounter++;
            }

            this.totalIterations++;

            // Yield periodically to prevent blocking and update progress
            if (this.totalIterations % 50 === 0) {
                if (progressCallback) {
                    progressCallback(this.totalIterations, 1 / this.bestGuess.fitness);
                }
                await new Promise(resolve => setTimeout(resolve, 0));
            }
        }

        return this.bestGuess;
    }

    /**
     * Get current status
     */
    getStatus() {
        return {
            bestGuess: this.bestGuess,
            bestFitness: this.bestGuess ? 1 / this.bestGuess.fitness : 0,
            totalIterations: this.totalIterations,
            stagnantCounter: this.stagnantCounter
        };
    }
}

// ============================================================
// Levenberg-Marquardt Calibration Algorithm
// ============================================================

/**
 * Convert a 5-parameter array to a guess object.
 * params = [tlX, tlY, trX, trY, brX]
 * bl is fixed at (0, 0), brY is fixed at 0.
 */
function lmParamsToGuess(params) {
    return {
        tl: { x: params[0], y: params[1] },
        tr: { x: params[2], y: params[3] },
        bl: { x: 0, y: 0 },
        br: { x: params[4], y: 0 }
    };
}

/**
 * Convert a guess object to a 5-parameter array.
 */
function lmGuessToParams(guess) {
    return [guess.tl.x, guess.tl.y, guess.tr.x, guess.tr.y, guess.br.x];
}

/**
 * Compute bundle-adjustment residuals.
 *
 * The bundle adjustment jointly optimises anchor positions AND sled positions.
 * For N calibration measurements the full parameter vector is:
 *
 *   params = [tlX, tlY, trX, trY, brX, s0x, s0y, s1x, s1y, ..., sN-1x, sN-1y]
 *             -------- 5 anchor params ---------  -------- 2N sled params -------
 *
 * Residuals: for each measurement i and each anchor j the residual is
 *
 *   r_{4i+j} = sqrt((si − anchorJ)²) − measured_belt_{i,j}
 *
 * giving 4N residuals total.  At the ideal solution all residuals are zero.
 *
 * This formulation has no iterative inner loop so each evaluation is pure
 * arithmetic (O(N)) and is orders of magnitude faster than line-walking for
 * Jacobian finite-difference computation.
 *
 * @param {Array} measurements - Array of {tl, tr, bl, br} objects
 * @param {Array} params       - Full bundle params [5 anchor + 2N sled]
 * @returns {Float64Array} Residuals (length 4N)
 */
function lmBundleResiduals(measurements, params) {
    const tlX = params[0], tlY = params[1];
    const trX = params[2], trY = params[3];
    const brX = params[4];
    const N = measurements.length;
    const residuals = new Float64Array(4 * N);

    for (let i = 0; i < N; i++) {
        const sx = params[5 + 2 * i];
        const sy = params[5 + 2 * i + 1];
        const m = measurements[i];

        const dTl = Math.sqrt((sx - tlX) * (sx - tlX) + (sy - tlY) * (sy - tlY));
        const dTr = Math.sqrt((sx - trX) * (sx - trX) + (sy - trY) * (sy - trY));
        const dBl = Math.sqrt(sx * sx + sy * sy);                        // bl at (0, 0)
        const dBr = Math.sqrt((sx - brX) * (sx - brX) + sy * sy);       // br at (brX, 0)

        residuals[4 * i]     = dTl - m.tl;
        residuals[4 * i + 1] = dTr - m.tr;
        residuals[4 * i + 2] = dBl - m.bl;
        residuals[4 * i + 3] = dBr - m.br;
    }

    return residuals;
}

/**
 * Compute Jacobian of lmBundleResiduals via forward finite differences.
 * J is (4N) × (5+2N) stored row-major in a Float64Array.
 *
 * Each column corresponds to one parameter perturbation.  Because the function
 * is pure arithmetic the evaluation is negligible in cost.
 *
 * @param {Array}        measurements - Measurement array
 * @param {Array}        params       - Full bundle params
 * @param {Float64Array} r0           - Residuals at current params
 * @param {number}       h            - Finite difference step size in mm
 * @returns {Float64Array} Jacobian (row-major, 4N × (5+2N))
 */
function lmBundleJacobian(measurements, params, r0, h) {
    const n = r0.length;          // 4 * N
    const m = params.length;      // 5 + 2*N
    const J = new Float64Array(n * m);

    for (let j = 0; j < m; j++) {
        const paramsPlus = params.slice();
        paramsPlus[j] += h;
        const rPlus = lmBundleResiduals(measurements, paramsPlus);

        for (let i = 0; i < n; i++) {
            J[i * m + j] = (rPlus[i] - r0[i]) / h;
        }
    }

    return J;
}

/**
 * Compute J^T * J (m x m) and J^T * r (m vector) for the LM normal equations.
 *
 * @param {Float64Array} J - Jacobian (n x m, row-major)
 * @param {Float64Array} r - Residual vector (length n)
 * @param {number}       m - Number of parameters
 * @returns {{ JtJ: Float64Array, Jtr: Float64Array }}
 */
function lmNormalEquations(J, r, m) {
    const n = r.length;
    const JtJ = new Float64Array(m * m);
    const Jtr = new Float64Array(m);

    for (let i = 0; i < n; i++) {
        for (let j = 0; j < m; j++) {
            Jtr[j] += J[i * m + j] * r[i];
            for (let k = 0; k < m; k++) {
                JtJ[j * m + k] += J[i * m + j] * J[i * m + k];
            }
        }
    }

    return { JtJ, Jtr };
}

/**
 * Solve a linear system A * x = b using Gaussian elimination with partial pivoting.
 *
 * @param {Float64Array} A - Coefficient matrix (m x m, row-major)
 * @param {Array}        b - Right-hand side vector (length m)
 * @param {number}       m - System size
 * @returns {Array} Solution vector x
 */
function lmSolveLinear(A, b, m) {
    // Build augmented matrix [A | b]
    const aug = [];
    for (let i = 0; i < m; i++) {
        const row = new Array(m + 1);
        for (let j = 0; j < m; j++) {
            row[j] = A[i * m + j];
        }
        row[m] = b[i];
        aug.push(row);
    }

    // Forward elimination with partial pivoting
    for (let col = 0; col < m; col++) {
        let maxRow = col;
        let maxVal = Math.abs(aug[col][col]);
        for (let row = col + 1; row < m; row++) {
            if (Math.abs(aug[row][col]) > maxVal) {
                maxVal = Math.abs(aug[row][col]);
                maxRow = row;
            }
        }
        const tmp = aug[col];
        aug[col] = aug[maxRow];
        aug[maxRow] = tmp;

        const pivot = aug[col][col];
        if (Math.abs(pivot) < 1e-15) continue;

        for (let row = col + 1; row < m; row++) {
            const factor = aug[row][col] / pivot;
            for (let k = col; k <= m; k++) {
                aug[row][k] -= factor * aug[col][k];
            }
        }
    }

    // Back substitution
    const x = new Array(m).fill(0);
    for (let i = m - 1; i >= 0; i--) {
        x[i] = aug[i][m];
        for (let j = i + 1; j < m; j++) {
            x[i] -= aug[i][j] * x[j];
        }
        if (Math.abs(aug[i][i]) > 1e-15) {
            x[i] /= aug[i][i];
        }
    }

    return x;
}

/**
 * Compute sum of squared residuals.
 */
function lmSSR(r) {
    let sum = 0;
    for (let i = 0; i < r.length; i++) sum += r[i] * r[i];
    return sum;
}

/**
 * Compute mean absolute residual (compatible with CalibrationComputer fitness format).
 * This matches computeEndpointFitness which returns the mean of 6 pairwise distances.
 */
function lmComputeFitness(residuals) {
    let sum = 0;
    for (let i = 0; i < residuals.length; i++) sum += Math.abs(residuals[i]);
    return sum / residuals.length;
}

/**
 * Compute residuals using the 2-parameter (anchor-only) form for external testing.
 * Provided for API compatibility; the optimiser uses lmBundleResiduals internally.
 *
 * @param {Array}  measurements - Array of {tl, tr, bl, br} objects
 * @param {Array}  anchorParams - [tlX, tlY, trX, trY, brX]
 * @returns {Float64Array} Bundle residuals (4N) with sled positions estimated via
 *                          2D Gauss-Newton from the supplied anchor params.
 */
function lmComputeResiduals(measurements, anchorParams) {
    const [tlX, tlY, trX, trY, brX] = anchorParams;
    const sledParams = [];
    for (const m of measurements) {
        const sl = lmEstimateSledPosition(m, tlX, tlY, trX, trY, brX);
        sledParams.push(sl.x, sl.y);
    }
    return lmBundleResiduals(measurements, [...anchorParams, ...sledParams]);
}

/**
 * Compute Jacobian for the anchor-only parameter space (exported for testing).
 */
function lmComputeJacobian(measurements, params, r0, h) {
    const n = r0.length;
    const m = params.length;
    const J = new Float64Array(n * m);
    for (let j = 0; j < m; j++) {
        const paramsPlus = params.slice();
        paramsPlus[j] += h;
        const rPlus = lmComputeResiduals(measurements, paramsPlus);
        for (let i = 0; i < n; i++) {
            J[i * m + j] = (rPlus[i] - r0[i]) / h;
        }
    }
    return J;
}

/**
 * Estimate sled XY position from all four belt lengths via 2D Gauss-Newton.
 *
 * Minimises sum_j (dist(sled, anchor_j) − belt_j)² with 10 Newton iterations.
 * This is independent of line-walking and robust to large errors in the anchor
 * initial guess, making bundle adjustment initialisation much more reliable.
 *
 * @param {Object} m     - Measurement {tl, tr, bl, br}
 * @param {number} tlX   - Current TL anchor X guess
 * @param {number} tlY   - Current TL anchor Y guess
 * @param {number} trX   - Current TR anchor X guess
 * @param {number} trY   - Current TR anchor Y guess
 * @param {number} brX   - Current BR anchor X guess (brY is always 0)
 * @returns {{ x: number, y: number }} Estimated sled position
 */
function lmEstimateSledPosition(m, tlX, tlY, trX, trY, brX) {
    const anchors = [
        { x: tlX, y: tlY },
        { x: trX, y: trY },
        { x: 0,   y: 0   },   // bl fixed
        { x: brX, y: 0   }    // br fixed y
    ];
    const belts = [m.tl, m.tr, m.bl, m.br];

    // Warm start: midpoint of the top anchor span, at one-quarter of their height
    let sx = (tlX + trX) / 2;
    let sy = (tlY + trY) / 4;

    for (let iter = 0; iter < 20; iter++) {
        let Hxx = 0, Hyy = 0, Hxy = 0, gx = 0, gy = 0;

        for (let j = 0; j < 4; j++) {
            const dx = sx - anchors[j].x;
            const dy = sy - anchors[j].y;
            const d = Math.sqrt(dx * dx + dy * dy) + 1e-10;
            const r = d - belts[j];
            const jx = dx / d;
            const jy = dy / d;

            Hxx += jx * jx;
            Hyy += jy * jy;
            Hxy += jx * jy;
            gx  += r * jx;
            gy  += r * jy;
        }

        const det = Hxx * Hyy - Hxy * Hxy + 1e-12;
        sx -= (Hyy * gx - Hxy * gy) / det;
        sy -= (Hxx * gy - Hxy * gx) / det;

        if (Math.abs(gx) + Math.abs(gy) < 1e-8) break;
    }

    return { x: sx, y: sy };
}

/**
 * LevenbergMarquardtCalibrationComputer
 *
 * Uses the Levenberg-Marquardt nonlinear least squares algorithm to find anchor
 * point positions that best explain the calibration belt-length measurements.
 *
 * Internally uses **bundle adjustment**: anchor positions and sled positions are
 * optimised jointly.  This gives:
 *
 * - Robustness: works from any initial guess (no line-walking in the inner loop)
 * - Speed: each iteration is O(N) arithmetic — typically 5-50× faster than the
 *   original hill-climbing algorithm
 * - Gradient-based convergence: directed updates converge in far fewer iterations
 *
 * Compared to CalibrationComputer (iterative hill-climbing):
 * - Uses gradient information (Jacobian) for more directed convergence
 * - Adaptively blends Gauss-Newton and gradient descent via damping parameter λ
 * - Typically converges in far fewer iterations
 * - Is more robust to poor initial guesses due to the damping mechanism
 *
 * Parameters optimised: [tlX, tlY, trX, trY, brX]
 * Fixed: blX=0, blY=0, brY=0
 *
 * The API is intentionally identical to CalibrationComputer so either class can
 * be used as a drop-in replacement.
 */
class LevenbergMarquardtCalibrationComputer {
    /**
     * @param {Object} initialGuess - Initial anchor positions { tl, tr, bl, br }
     * @param {Object} [config] - Optional configuration
     * @param {number} [config.initialLambda=0.001] - Initial LM damping factor
     * @param {number} [config.lambdaIncrease=10] - Multiplier to increase λ on step rejection
     * @param {number} [config.lambdaDecrease=0.1] - Multiplier to decrease λ on step acceptance
     * @param {number} [config.maxIterations=500] - Maximum LM iterations per processDataChunk call
     * @param {number} [config.stepSize=10.0] - Finite difference step size in mm
     * @param {number} [config.convergenceThreshold=1e-4] - Stop when anchor step norm < this (mm)
     * @param {number} [config.acceptableThreshold=0.5] - Fitness threshold to update initial guess
     */
    constructor(initialGuess, config = {}) {
        this.initialGuess = {
            tl: { ...initialGuess.tl },
            tr: { ...initialGuess.tr },
            bl: { x: 0, y: 0 },
            br: { x: initialGuess.br ? initialGuess.br.x : 0, y: 0 },
            fitness: 100000000
        };
        this.bestGuess = null;
        this.bestFitness = 0;
        this.totalIterations = 0;
        this.lambda = config.initialLambda !== undefined ? config.initialLambda : 0.001;
        this.lambdaIncrease = config.lambdaIncrease || 10;
        this.lambdaDecrease = config.lambdaDecrease || 0.1;
        this.maxIterations = config.maxIterations || 500;
        this.stepSize = config.stepSize || 10.0;
        this.convergenceThreshold = config.convergenceThreshold || 1e-4;
        this.acceptableThreshold = config.acceptableThreshold || 0.5;
        // Optional logging callback: fn(message) — defaults to no-op so the class
        // stays environment-agnostic (works in browser and Node.js tests).
        this.logFn = config.logFn || null;
    }

    _log(msg) {
        if (this.logFn) this.logFn(msg);
    }

    /**
     * Process a chunk of measurement data and optimise anchor positions.
     * Drop-in replacement for CalibrationComputer.processDataChunk().
     *
     * @param {Array} measurements - Measurement array [{tl, tr, bl, br}, ...]
     * @param {Function} [progressCallback] - Called periodically with (iterations, fitness)
     * @returns {Object} Best guess { tl, tr, bl, br, fitness }
     */
    async processDataChunk(measurements, progressCallback) {
        const result = await this.optimize(measurements, progressCallback);
        if (1 / result.fitness > this.acceptableThreshold) {
            this.initialGuess = JSON.parse(JSON.stringify(result));
        }
        return result;
    }

    /**
     * Run the Levenberg-Marquardt bundle adjustment optimisation.
     *
     * Step 1: Initialise sled positions using 2D Gauss-Newton (independent of
     *         line-walking) so that bad initial anchor guesses don't derail the
     *         bundle adjustment.
     * Step 2: Build the full parameter vector [anchors (5) | sled XY (2N)].
     * Step 3: Iterate LM updates, solving a (5+2N)×(5+2N) linear system each time.
     * Step 4: Return the converged anchor positions.
     */
    async optimize(measurements, progressCallback) {
        const N = measurements.length;
        const h = this.stepSize;
        const [tlX0, tlY0, trX0, trY0, brX0] = lmGuessToParams(this.initialGuess);

        this._log(`[LM] Starting optimization: ${N} measurements, maxIterations=${this.maxIterations}, h=${h}mm`);
        this._log(`[LM] Initial anchor guess — tl=(${tlX0.toFixed(1)}, ${tlY0.toFixed(1)}) tr=(${trX0.toFixed(1)}, ${trY0.toFixed(1)}) br=(${brX0.toFixed(1)}, 0)`);

        // ── Step 1: Initialise sled positions via 2D Gauss-Newton ─────────────
        // This is independent of line-walking and robust to large anchor errors.
        const initSleds = [];
        for (const m of measurements) {
            const sl = lmEstimateSledPosition(m, tlX0, tlY0, trX0, trY0, brX0);
            initSleds.push(sl.x, sl.y);
        }

        this._log(`[LM] Phase 1: sled positions initialised via 2D Gauss-Newton`);

        // ── Step 2: Build full parameter vector ────────────────────────────────
        let params = [tlX0, tlY0, trX0, trY0, brX0, ...initSleds];
        const mTotal = params.length;  // 5 + 2*N

        let r = lmBundleResiduals(measurements, params);
        let ssr = lmSSR(r);

        let bestParams = params.slice();
        let bestSSR = ssr;
        let consecutiveRejections = 0;
        let lambda = this.lambda;
        this.totalIterations = 0;

        this._log(`[LM] Phase 1 initial SSR: ${ssr.toFixed(4)} (${5 + 2 * N} parameters, ${4 * N} residuals)`);

        // ── Step 3: LM iterations ──────────────────────────────────────────────
        while (this.totalIterations < this.maxIterations) {
            // Jacobian via forward finite differences (pure arithmetic, very fast)
            const J = lmBundleJacobian(measurements, params, r, h);

            // Normal equations: (J^T J + λ diag(J^T J)) Δθ = -J^T r
            const { JtJ, Jtr } = lmNormalEquations(J, r, mTotal);

            const A = new Float64Array(JtJ);
            for (let i = 0; i < mTotal; i++) {
                A[i * mTotal + i] += lambda * Math.max(JtJ[i * mTotal + i], 1e-10);
            }

            const step = lmSolveLinear(A, Array.from(Jtr).map(x => -x), mTotal);

            const newParams = params.map((p, i) => p + step[i]);
            const newR = lmBundleResiduals(measurements, newParams);
            const newSSR = lmSSR(newR);

            if (newSSR < ssr) {
                params = newParams;
                r = newR;
                ssr = newSSR;
                lambda = Math.max(lambda * this.lambdaDecrease, 1e-12);
                consecutiveRejections = 0;

                if (ssr < bestSSR) {
                    bestSSR = ssr;
                    bestParams = params.slice();
                }

                // Only check convergence after an accepted step — a rejected step
                // with small norm just means lambda is large, not that we've converged.
                const anchorStepNorm = Math.sqrt(
                    step.slice(0, 5).reduce((s, v) => s + v * v, 0)
                );
                if (anchorStepNorm < this.convergenceThreshold) {
                    this._log(`[LM] Converged: anchor step norm ${anchorStepNorm.toExponential(2)} < threshold ${this.convergenceThreshold}`);
                    break;
                }
            } else {
                lambda *= this.lambdaIncrease;
                consecutiveRejections++;
                if (consecutiveRejections > 20 || lambda > 1e12) {
                    this._log(`[LM] Stopping: ${consecutiveRejections} consecutive rejections, lambda=${lambda.toExponential(2)}`);
                    break;
                }
            }

            this.totalIterations++;

            // Yield to keep UI responsive and report progress with current best anchor positions
            if (this.totalIterations % 50 === 0) {
                const currentFitness = 1 / (lmComputeFitness(r) + 1e-10);
                this._log(`[LM] Iter ${this.totalIterations}: SSR=${ssr.toFixed(4)}, fitness=${currentFitness.toFixed(4)}, lambda=${lambda.toExponential(2)}`);
                if (progressCallback) {
                    // Pass current best anchor positions so the UI can show live progress
                    progressCallback(this.totalIterations, currentFitness, lmParamsToGuess(bestParams));
                }
                await new Promise(resolve => setTimeout(resolve, 0));
            }
        }

        this.lambda = lambda;

        this._log(`[LM] Phase 1 complete: ${this.totalIterations} iterations, best SSR=${bestSSR.toFixed(4)}`);
        this._log(`[LM] Phase 1 best anchors — tl=(${bestParams[0].toFixed(1)}, ${bestParams[1].toFixed(1)}) tr=(${bestParams[2].toFixed(1)}, ${bestParams[3].toFixed(1)}) br=(${bestParams[4].toFixed(1)}, 0)`);

        // ── Phase 2 refinement: Re-initialise sleds from the converged anchors ──
        // This escapes local minima caused by poor initial sled estimates when the
        // anchor initial guess was far from the truth.
        const phase2Sleds = [];
        for (const m of measurements) {
            const sl = lmEstimateSledPosition(m, bestParams[0], bestParams[1], bestParams[2], bestParams[3], bestParams[4]);
            phase2Sleds.push(sl.x, sl.y);
        }
        const params2 = [...bestParams.slice(0, 5), ...phase2Sleds];
        const r2 = lmBundleResiduals(measurements, params2);
        const ssr2 = lmSSR(r2);

        this._log(`[LM] Phase 2: re-initialised sled positions, SSR=${ssr2.toFixed(4)} (was ${bestSSR.toFixed(4)})`);

        if (ssr2 < bestSSR) {
            // Phase 2 starting point is already an improvement — capture it immediately
            // so we never return a worse Phase 1 result even if the Phase 2 loop makes no progress.
            bestSSR = ssr2;
            bestParams = params2.slice();
            // Run a second LM pass to try to improve further from this starting point
            this._log(`[LM] Phase 2 starting point is better — running second LM pass`);
            let p2 = params2.slice();
            let rp2 = r2;
            let ssr2Cur = ssr2;
            let lam2 = 0.001;
            let rej2 = 0;
            const maxIter2 = Math.min(this.maxIterations, 100);

            for (let iter2 = 0; iter2 < maxIter2; iter2++) {
                const J2 = lmBundleJacobian(measurements, p2, rp2, h);
                const { JtJ: JtJ2, Jtr: Jtr2 } = lmNormalEquations(J2, rp2, mTotal);
                const A2 = new Float64Array(JtJ2);
                for (let i = 0; i < mTotal; i++) {
                    A2[i * mTotal + i] += lam2 * Math.max(JtJ2[i * mTotal + i], 1e-10);
                }
                const step2 = lmSolveLinear(A2, Array.from(Jtr2).map(x => -x), mTotal);
                const np2 = p2.map((p, i) => p + step2[i]);
                const nr2 = lmBundleResiduals(measurements, np2);
                const nssr2 = lmSSR(nr2);

                if (nssr2 < ssr2Cur) {
                    p2 = np2; rp2 = nr2; ssr2Cur = nssr2;
                    lam2 = Math.max(lam2 * this.lambdaDecrease, 1e-12);
                    rej2 = 0;
                    if (ssr2Cur < bestSSR) { bestSSR = ssr2Cur; bestParams = p2.slice(); }
                } else {
                    lam2 *= this.lambdaIncrease; rej2++;
                    if (rej2 > 20 || lam2 > 1e12) break;
                }
                this.totalIterations++;
                const s2norm = Math.sqrt(step2.slice(0, 5).reduce((s, v) => s + v * v, 0));
                if (s2norm < this.convergenceThreshold) break;
            }
        }

        // ── Step 4: Report results ─────────────────────────────────────────────
        // Compute fitness using the same metric as CalibrationComputer for comparability
        this._log(`[LM] Optimization complete: ${this.totalIterations} total iterations, best SSR=${bestSSR.toFixed(4)}`);
        const bestAnchorGuess = lmParamsToGuess(bestParams);
        const finalFitness = computeLinesFitness(
            JSON.parse(JSON.stringify(measurements)),
            bestAnchorGuess
        );

        this.bestGuess = {
            tl: { x: bestParams[0], y: bestParams[1] },
            tr: { x: bestParams[2], y: bestParams[3] },
            bl: { x: 0, y: 0 },
            br: { x: bestParams[4], y: 0 },
            fitness: finalFitness.fitness
        };
        this.bestFitness = 1 / finalFitness.fitness;

        this._log(`[LM] Final result — tl=(${this.bestGuess.tl.x.toFixed(1)}, ${this.bestGuess.tl.y.toFixed(1)}) tr=(${this.bestGuess.tr.x.toFixed(1)}, ${this.bestGuess.tr.y.toFixed(1)}) br=(${this.bestGuess.br.x.toFixed(1)}, 0) fitness=${this.bestFitness.toFixed(6)}`);

        return JSON.parse(JSON.stringify(this.bestGuess));
    }

    /**
     * Get current optimisation status.
     * Drop-in replacement for CalibrationComputer.getStatus().
     */
    getStatus() {
        return {
            bestGuess: this.bestGuess,
            bestFitness: this.bestFitness,
            totalIterations: this.totalIterations
        };
    }
}

// Export for use in other modules (works in both browser and Node.js)
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        CalibrationComputer,
        LevenbergMarquardtCalibrationComputer,
        computeLinesFitness,
        magneticallyAttractedLinesFitness,
        computeFurthestFromCenterOfMass,
        distanceBetweenPoints,
        getEndPoint,
        computeEndpointFitness,
        computeLineEndPoint,
        walkLines,
        computeDistanceFromCenterOfMass,
        generateTweaks,
        lmParamsToGuess,
        lmGuessToParams,
        lmEstimateSledPosition,
        lmBundleResiduals,
        lmBundleJacobian,
        lmComputeResiduals,
        lmComputeJacobian,
        lmNormalEquations,
        lmSolveLinear,
        lmSSR,
        lmComputeFitness
    };
}
