/**
 * Calibration Helper Functions
 * 
 * Shared helper functions used by both the ESP3D-WEBUI and testing tools.
 * These functions are independent of the UI environment and can be safely shared.
 */

// Z-heights for belt anchor points (used in projection calculations)
var tlZ = 100;
var trZ = 56;
var blZ = 34;
var brZ = 78;

// Default initial guess for anchor positions
var defaultInitialGuess = {
    tl: { x: 0, y: 2000 },
    tr: { x: 3000, y: 2000 },
    bl: { x: 0, y: 0 },
    br: { x: 3000, y: 0 },
    fitness: 100000000,
};

/**
 * Projects a single measurement to the plane of the machine
 * @param {Object} measurement - Measurement object with tl, tr, bl, br
 * @returns {Object} - Projected measurements
 */
function projectMeasurement(measurement) {
    const tl = Math.sqrt(Math.pow(measurement.tl, 2) - Math.pow(tlZ, 2));
    const tr = Math.sqrt(Math.pow(measurement.tr, 2) - Math.pow(trZ, 2));
    const bl = Math.sqrt(Math.pow(measurement.bl, 2) - Math.pow(blZ, 2));
    const br = Math.sqrt(Math.pow(measurement.br, 2) - Math.pow(brZ, 2));
    return { tl: tl, tr: tr, bl: bl, br: br };
}

/**
 * Projects array of measurements
 * @param {Object[]} measurements - Array of measurements
 * @returns {Object[]} - Array of projected measurements
 */
function projectMeasurements(measurements) {
    return measurements.map(m => projectMeasurement(m));
}

/**
 * Searches for best rectangular starting configuration
 * Uses geometric locus computed from first measurement constraints
 * 
 * @param {Object[]} measurements - Array of projected measurements
 * @param {Object} initialGuess - Initial guess for anchor positions
 * @param {Function} logFn - Optional logging function
 * @param {Array} testedPoints - Optional array to collect tested points for visualization
 * @returns {Promise<Object>} - Best guess configuration
 */
async function findBestRectangularStart(measurements, initialGuess, logFn = () => {}, testedPoints = null) {
    logFn('Computing anchor positions using geometric locus from first measurement...');
    logFn(`Evaluating against ${measurements.length} measurements`);
    
    if (!measurements || measurements.length === 0) {
        logFn('Error: No measurements provided');
        return initialGuess;
    }
    
    // Use first measurement to constrain the search
    const firstMeas = measurements[0];
    const d_tl = firstMeas.tl;
    const d_tr = firstMeas.tr;
    const d_bl = firstMeas.bl;
    const d_br = firstMeas.br;
    
    // Assume first measurement is at center of workspace
    const x0 = (initialGuess.tl.x + initialGuess.tr.x) / 2;
    const y0 = (initialGuess.tl.y + initialGuess.bl.y) / 2;
    
    logFn(`First measurement point: (${x0.toFixed(1)}, ${y0.toFixed(1)})`);
    logFn(`Distances: TL=${d_tl.toFixed(1)}, TR=${d_tr.toFixed(1)}, BL=${d_bl.toFixed(1)}, BR=${d_br.toFixed(1)}`);
    
    let bestGuess = null;
    let bestFitness = Infinity;
    let testedCount = 0;
    
    logFn('Phase 1: Sampling along geometric locus...');
    
    // Sample different frame sizes and compute where center must be
    // For each (W, H), we can compute center from distance constraints:
    // d_TL² - d_TR² = -2W(x0 - xc) => xc = x0 + (d_TL² - d_TR²)/(2W)
    // d_TL² - d_BL² = -2H(y0 - yc) => yc = y0 + (d_TL² - d_BL²)/(2H)
    
    const widthMin = 500;
    const widthMax = 6000;
    const widthStep = 50;
    
    for (let W = widthMin; W <= widthMax; W += widthStep) {
        // Compute required center X from left/right distances
        const xc = x0 + (d_tl * d_tl - d_tr * d_tr) / (2 * W);
        
        // For each width, sample different heights
        const heightMin = 500;
        const heightMax = 6000;
        const heightStep = 50;
        
        for (let H = heightMin; H <= heightMax; H += heightStep) {
            testedCount++;
            
            // Compute required center Y from top/bottom distances
            const yc = y0 + (d_tl * d_tl - d_bl * d_bl) / (2 * H);
            
            // Construct rectangle with this center and dimensions
            const guess = {
                tl: { x: xc - W/2, y: yc + H/2 },
                tr: { x: xc + W/2, y: yc + H/2 },
                bl: { x: xc - W/2, y: yc - H/2 },
                br: { x: xc + W/2, y: yc - H/2 },
                fitness: 0
            };
            
            // Verify this satisfies distance constraints (sanity check)
            const calc_d_tl = Math.sqrt((x0 - guess.tl.x) ** 2 + (y0 - guess.tl.y) ** 2);
            const calc_d_tr = Math.sqrt((x0 - guess.tr.x) ** 2 + (y0 - guess.tr.y) ** 2);
            const calc_d_bl = Math.sqrt((x0 - guess.bl.x) ** 2 + (y0 - guess.bl.y) ** 2);
            const calc_d_br = Math.sqrt((x0 - guess.br.x) ** 2 + (y0 - guess.br.y) ** 2);
            
            const errorSum = Math.abs(calc_d_tl - d_tl) + Math.abs(calc_d_tr - d_tr) + 
                           Math.abs(calc_d_bl - d_bl) + Math.abs(calc_d_br - d_br);
            
            // Evaluate fitness for all points (for better coverage)
            const testMeasurements = JSON.parse(JSON.stringify(measurements));
            const result = computeLinesFitness(testMeasurements, guess, true);
            
            // Store tested point for visualization
            if (testedPoints !== null) {
                testedPoints.push({
                    width: W,
                    height: H,
                    aspectRatio: W / H,
                    fitness: result.fitness,
                    guess: JSON.parse(JSON.stringify(guess)),
                    errorSum: errorSum
                });
            }
            
            // Only consider for best solution if distance constraints are reasonably satisfied
            if (errorSum < 200 && result.fitness < bestFitness) { // 200mm tolerance (relaxed)
                bestFitness = result.fitness;
                bestGuess = JSON.parse(JSON.stringify(guess));
                bestGuess.fitness = result.fitness;
            }
            
            // Yield periodically
            if (testedCount % 100 === 0) {
                await new Promise(resolve => setTimeout(resolve, 0));
            }
        }
    }
    
    if (!bestGuess) {
        logFn('Warning: No valid solution found on geometric locus, using initial guess');
        return initialGuess;
    }
    
    logFn(`Tested ${testedCount} configurations on geometric locus`);
    
    if (testedPoints !== null) {
        logFn(`Collected ${testedPoints.length} points for visualization`);
    }
    
    logFn(`Best: ${(bestGuess.tr.x - bestGuess.tl.x).toFixed(0)}mm x ${(bestGuess.tl.y - bestGuess.bl.y).toFixed(0)}mm, Fitness: ${(1/bestGuess.fitness).toFixed(4)}`);
    
    // Phase 2: Refine around best solution
    logFn('Phase 2: Refining best solution (local sampling)...');
    
    const bestWidth = bestGuess.tr.x - bestGuess.tl.x;
    const bestHeight = bestGuess.tl.y - bestGuess.bl.y;
    
    const refineWidthStep = 10;
    const refineHeightStep = 10;
    const refineRange = 100;
    
    for (let W = bestWidth - refineRange; W <= bestWidth + refineRange; W += refineWidthStep) {
        if (W < widthMin) continue;
        
        const xc = x0 + (d_tl * d_tl - d_tr * d_tr) / (2 * W);
        
        for (let H = bestHeight - refineRange; H <= bestHeight + refineRange; H += refineHeightStep) {
            if (H < heightMin) continue;
            
            testedCount++;
            
            const yc = y0 + (d_tl * d_tl - d_bl * d_bl) / (2 * H);
            
            const guess = {
                tl: { x: xc - W/2, y: yc + H/2 },
                tr: { x: xc + W/2, y: yc + H/2 },
                bl: { x: xc - W/2, y: yc - H/2 },
                br: { x: xc + W/2, y: yc - H/2 },
                fitness: 0
            };
            
            const testMeasurements = JSON.parse(JSON.stringify(measurements));
            const result = computeLinesFitness(testMeasurements, guess, true);
            
            // Store tested point for visualization
            if (testedPoints !== null) {
                testedPoints.push({
                    width: W,
                    height: H,
                    aspectRatio: W / H,
                    fitness: result.fitness,
                    guess: JSON.parse(JSON.stringify(guess)),
                    isRefinement: true
                });
            }
            
            if (result.fitness < bestFitness) {
                bestFitness = result.fitness;
                bestGuess = JSON.parse(JSON.stringify(guess));
                bestGuess.fitness = result.fitness;
            }
        }
    }
    
    logFn(`Total configurations tested: ${testedCount}`);
    logFn(`Final solution: ${(bestGuess.tr.x - bestGuess.tl.x).toFixed(1)}mm x ${(bestGuess.tl.y - bestGuess.bl.y).toFixed(1)}mm`);
    logFn(`Fitness: ${(1/bestGuess.fitness).toFixed(4)}`);
    
    return bestGuess;
}

// Export for use in other modules (works in both browser and Node.js)
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        tlZ,
        trZ,
        blZ,
        brZ,
        defaultInitialGuess,
        projectMeasurement,
        projectMeasurements,
        findBestRectangularStart
    };
}
