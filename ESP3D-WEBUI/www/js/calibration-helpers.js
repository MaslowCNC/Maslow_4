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
 * Uses parametric curve sampling in (W, H) space along aspect ratios
 * 
 * @param {Object[]} measurements - Array of projected measurements
 * @param {Object} initialGuess - Initial guess for anchor positions
 * @param {Function} logFn - Optional logging function
 * @param {Array} testedPoints - Optional array to collect tested points for visualization
 * @returns {Promise<Object>} - Best guess configuration
 */
async function findBestRectangularStart(measurements, initialGuess, logFn = () => {}, testedPoints = null) {
    logFn('Computing anchor positions using parametric curve search...');
    logFn(`Evaluating against ${measurements.length} measurements`);
    
    let bestGuess = null;
    let bestFitness = Infinity;
    let testedCount = 0;
    
    // Estimate workspace center from initial guess
    const centerX = (initialGuess.tl.x + initialGuess.tr.x) / 2;
    const centerY = (initialGuess.tl.y + initialGuess.bl.y) / 2;
    
    logFn('Phase 1: Sampling along parametric curves (aspect ratios and diagonals)...');
    
    // Sample along different aspect ratios (width/height)
    const aspectRatios = [0.5, 0.67, 0.75, 1.0, 1.33, 1.5, 2.0, 2.5, 3.0];
    const diagonals = []; // Will sample from 500 to 6000mm
    
    for (let d = 500; d <= 6000; d += 100) {
        diagonals.push(d);
    }
    
    // Phase 1: Sample along each aspect ratio curve
    for (const aspectRatio of aspectRatios) {
        for (const diagonal of diagonals) {
            testedCount++;
            
            // For a rectangle with aspect ratio AR = W/H and diagonal D:
            // W² + H² = D²
            // W = AR * H
            // (AR * H)² + H² = D²
            // H² * (AR² + 1) = D²
            // H = D / sqrt(AR² + 1)
            // W = AR * H
            
            const height = diagonal / Math.sqrt(aspectRatio * aspectRatio + 1);
            const width = aspectRatio * height;
            
            const guess = {
                tl: { x: centerX - width/2, y: centerY + height/2 },
                tr: { x: centerX + width/2, y: centerY + height/2 },
                bl: { x: centerX - width/2, y: centerY - height/2 },
                br: { x: centerX + width/2, y: centerY - height/2 },
                fitness: 0
            };
            
            const testMeasurements = JSON.parse(JSON.stringify(measurements));
            const result = computeLinesFitness(testMeasurements, guess, true);
            
            // Store tested point for visualization
            if (testedPoints !== null) {
                testedPoints.push({
                    width: width,
                    height: height,
                    aspectRatio: aspectRatio,
                    diagonal: diagonal,
                    fitness: result.fitness,
                    guess: JSON.parse(JSON.stringify(guess))
                });
            }
            
            if (result.fitness < bestFitness) {
                bestFitness = result.fitness;
                bestGuess = JSON.parse(JSON.stringify(guess));
                bestGuess.fitness = result.fitness;
            }
            
            // Yield periodically
            if (testedCount % 50 === 0) {
                await new Promise(resolve => setTimeout(resolve, 0));
            }
        }
    }
    
    logFn(`Tested ${testedCount} configurations along parametric curves`);
    logFn(`Best: ${(bestGuess.tr.x - bestGuess.tl.x).toFixed(0)}mm x ${(bestGuess.tl.y - bestGuess.bl.y).toFixed(0)}mm, Fitness: ${(1/bestGuess.fitness).toFixed(4)}`);
    
    // Phase 2: Refine around best solution with local sampling
    logFn('Phase 2: Refining best solution (local sampling)...');
    
    const bestWidth = bestGuess.tr.x - bestGuess.tl.x;
    const bestHeight = bestGuess.tl.y - bestGuess.bl.y;
    const bestAspectRatio = bestWidth / bestHeight;
    
    // Sample around the best aspect ratio
    const refineAspectRatios = [];
    for (let ar = bestAspectRatio - 0.3; ar <= bestAspectRatio + 0.3; ar += 0.05) {
        if (ar > 0.1) refineAspectRatios.push(ar);
    }
    
    const bestDiagonal = Math.sqrt(bestWidth * bestWidth + bestHeight * bestHeight);
    const refineDiagonals = [];
    for (let d = bestDiagonal - 300; d <= bestDiagonal + 300; d += 25) {
        if (d > 0) refineDiagonals.push(d);
    }
    
    for (const aspectRatio of refineAspectRatios) {
        for (const diagonal of refineDiagonals) {
            testedCount++;
            
            const height = diagonal / Math.sqrt(aspectRatio * aspectRatio + 1);
            const width = aspectRatio * height;
            
            const guess = {
                tl: { x: centerX - width/2, y: centerY + height/2 },
                tr: { x: centerX + width/2, y: centerY + height/2 },
                bl: { x: centerX - width/2, y: centerY - height/2 },
                br: { x: centerX + width/2, y: centerY - height/2 },
                fitness: 0
            };
            
            const testMeasurements = JSON.parse(JSON.stringify(measurements));
            const result = computeLinesFitness(testMeasurements, guess, true);
            
            // Store tested point for visualization
            if (testedPoints !== null) {
                testedPoints.push({
                    width: width,
                    height: height,
                    aspectRatio: aspectRatio,
                    diagonal: diagonal,
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
