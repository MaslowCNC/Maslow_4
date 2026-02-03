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
 * Uses geometric locus arc computed from first measurement constraints
 * Based on the Shape2.html algorithm
 * 
 * @param {Object[]} measurements - Array of projected measurements
 * @param {Object} initialGuess - Initial guess for anchor positions
 * @param {Function} logFn - Optional logging function
 * @param {Array} testedPoints - Optional array to collect tested points for visualization
 * @returns {Promise<Object>} - Best guess configuration
 */
async function findBestRectangularStart(measurements, initialGuess, logFn = () => {}, testedPoints = null) {
    logFn('Computing anchor positions using geometric locus arc from first measurement...');
    logFn(`Evaluating against ${measurements.length} measurements`);
    
    if (!measurements || measurements.length === 0) {
        logFn('Error: No measurements provided');
        return initialGuess;
    }
    
    // Use first measurement to constrain the search
    const firstMeas = measurements[0];
    const dTL = firstMeas.tl;
    const dTR = firstMeas.tr;
    const dBL = firstMeas.bl;
    const dBR = firstMeas.br;
    
    // Assume first measurement is at center of workspace
    const x0 = (initialGuess.tl.x + initialGuess.tr.x) / 2;
    const y0 = (initialGuess.tl.y + initialGuess.bl.y) / 2;
    
    logFn(`First measurement point: (${x0.toFixed(1)}, ${y0.toFixed(1)})`);
    logFn(`Distances: TL=${dTL.toFixed(1)}, TR=${dTR.toFixed(1)}, BL=${dBL.toFixed(1)}, BR=${dBR.toFixed(1)}`);
    
    let bestGuess = null;
    let bestFitness = Infinity;
    let testedCount = 0;
    
    logFn('Phase 1: Computing valid solutions arc...');
    
    // Compute the locus of valid rectangular configurations using Shape2.html algorithm
    // This creates a parametric curve in (W, H) space, not a grid!
    
    const TL2 = dTL * dTL;
    const TR2 = dTR * dTR;
    const BR2 = dBR * dBR;
    const BL2 = dBL * dBL;
    const Kx = TL2 - TR2;
    const Ky = TR2 - BR2;
    
    const widthMin = 100;
    const widthMax = (dTL + dTR + dBR + dBL) * 0.75;  // Based on Shape2.html scan range
    const widthStep = 20;  // Finer sampling along the curve
    
    const validPoints = [];  // Store points on the arc
    
    // For each width w, solve for valid heights h that satisfy all distance constraints
    for (let w = widthMin; w <= widthMax; w += widthStep) {
        // From first measurement at (x0, y0), compute where rectangle's TL corner must be
        // if rectangle has width w
        const x = (w*w + Kx) / (2*w);  // X offset of TL corner from first measurement point
        const rem = TL2 - x*x;  // Remaining Y distance squared (dTL² - x²)
        
        if (rem >= 0) {
            const v = Math.sqrt(rem);  // Y distance from measurement to TL corner (can be ± v)
            
            // Try both +v and -v (measurement point could be above or below TL corner)
            [v, -v].forEach(yDistance => {
                // Now solve for height h using the BR constraint
                const disc = 4*yDistance*yDistance - 4*Ky;  // Discriminant of quadratic equation
                if (disc >= 0) {
                    const sqrtDisc = Math.sqrt(disc);  // Square root of discriminant
                    // Quadratic formula gives two solutions for h
                    [(-2*yDistance + sqrtDisc)/2, (-2*yDistance - sqrtDisc)/2].forEach(h => {
                        if (h > 100) {  // Only positive, reasonable heights
                            const y = (h*h - Ky) / (2*h);  // Y offset of TL corner from measurement
                            
                            // Ensure (x,y) offsets place TL corner within or near rectangle bounds (sanity check)
                            if (x >= -0.05 && x <= w+0.05 && y >= -0.05 && y <= h+0.05) {
                                validPoints.push({w, h, x, y});
                            }
                        }
                    });
                }
            });
        }
    }
    
    logFn(`Found ${validPoints.length} points on geometric locus arc`);
    logFn('Phase 2: Optimized search along arc using ternary search...');
    
    // Helper function to evaluate fitness at a specific index
    async function evaluatePointAtIndex(idx) {
        if (idx < 0 || idx >= validPoints.length) return Infinity;
        
        const point = validPoints[idx];
        const W = point.w;
        const H = point.h;
        
        testedCount++;
        
        // Transform to absolute coordinates
        const tl_x = x0 + point.x;
        const tl_y = y0 + point.y;
        
        // Construct rectangle
        const guess = {
            tl: { x: tl_x, y: tl_y },
            tr: { x: tl_x + W, y: tl_y },
            bl: { x: tl_x, y: tl_y - H },
            br: { x: tl_x + W, y: tl_y - H },
            fitness: 0
        };
        
        // Evaluate fitness
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
                onArc: true
            });
        }
        
        if (result.fitness < bestFitness) {
            bestFitness = result.fitness;
            bestGuess = JSON.parse(JSON.stringify(guess));
            bestGuess.fitness = result.fitness;
        }
        
        return result.fitness;
    }
    
    // Ternary search to find the minimum fitness along the arc
    // This reduces evaluations from O(n) to O(log n)
    const TERNARY_SEARCH_THRESHOLD = 5;  // Stop when range is this small
    let left = 0;
    let right = validPoints.length - 1;
    
    while (right - left > TERNARY_SEARCH_THRESHOLD) {
        const mid1 = left + Math.floor((right - left) / 3);
        const mid2 = left + Math.floor(2 * (right - left) / 3);
        
        const fitness1 = await evaluatePointAtIndex(mid1);
        const fitness2 = await evaluatePointAtIndex(mid2);
        
        logFn(`Ternary search: range [${left}, ${right}], tested indices ${mid1} (fitness: ${(1/fitness1).toFixed(4)}) and ${mid2} (fitness: ${(1/fitness2).toFixed(4)})`);
        
        if (fitness1 > fitness2) {
            // Minimum is in right 2/3
            left = mid1;
        } else {
            // Minimum is in left 2/3
            right = mid2;
        }
        
        await new Promise(resolve => setTimeout(resolve, 10));
    }
    
    // Evaluate remaining points in final range
    logFn(`Fine search in range [${left}, ${right}]...`);
    for (let i = left; i <= right; i++) {
        await evaluatePointAtIndex(i);
        if (testedCount % 5 === 0) {
            await new Promise(resolve => setTimeout(resolve, 10));
        }
    }
    
    if (!bestGuess) {
        logFn('Warning: No valid solution found on geometric locus arc, using initial guess');
        return initialGuess;
    }
    
    logFn(`Tested ${testedCount} configurations on geometric locus arc`);
    
    if (testedPoints !== null) {
        logFn(`Collected ${testedPoints.length} points for visualization`);
    }
    
    logFn(`Best: ${(bestGuess.tr.x - bestGuess.tl.x).toFixed(0)}mm x ${(bestGuess.tl.y - bestGuess.bl.y).toFixed(0)}mm, Fitness: ${(1/bestGuess.fitness).toFixed(4)}`);
    
    // Phase 3: Refine by sampling finer along the arc near best solution
    logFn('Phase 3: Refining best solution on arc with finer sampling...');
    
    const REFINEMENT_WIDTH_HALF_RANGE = 150;  // mm to search on each side of best width
    const bestWidth = bestGuess.tr.x - bestGuess.tl.x;
    const refineWidthMin = Math.max(widthMin, bestWidth - REFINEMENT_WIDTH_HALF_RANGE);
    const refineWidthMax = bestWidth + REFINEMENT_WIDTH_HALF_RANGE;
    const refineWidthStep = 5;  // Finer sampling for refinement
    
    let refinementCount = 0;
    const refinementPoints = [];
    
    // Build refined arc points near best solution
    for (let w = refineWidthMin; w <= refineWidthMax; w += refineWidthStep) {
        const x = (w*w + Kx) / (2*w);
        const rem = TL2 - x*x;
        
        if (rem >= 0) {
            const v = Math.sqrt(rem);
            
            [v, -v].forEach(yDistance => {
                const disc = 4*yDistance*yDistance - 4*Ky;
                if (disc >= 0) {
                    const sqrtDisc = Math.sqrt(disc);
                    [(-2*yDistance + sqrtDisc)/2, (-2*yDistance - sqrtDisc)/2].forEach(h => {
                        if (h > 100) {
                            const y = (h*h - Ky) / (2*h);
                            
                            if (x >= -0.05 && x <= w+0.05 && y >= -0.05 && y <= h+0.05) {
                                refinementPoints.push({w, h, x, y});
                            }
                        }
                    });
                }
            });
        }
    }
    
    logFn(`Generated ${refinementPoints.length} refinement points, using ternary search...`);
    
    // Add all refinement points to visualization (showing the search space)
    if (testedPoints !== null) {
        refinementPoints.forEach(point => {
            testedPoints.push({
                width: point.w,
                height: point.h,
                aspectRatio: point.w / point.h,
                fitness: null,  // Not evaluated yet
                guess: null,
                onArc: true,
                isRefinement: true,
                unevaluated: true  // Mark as not yet evaluated
            });
        });
    }
    
    // Helper function to evaluate refinement point at index
    async function evaluateRefinementPoint(idx) {
        if (idx < 0 || idx >= refinementPoints.length) return Infinity;
        
        const point = refinementPoints[idx];
        const w = point.w;
        const h = point.h;
        
        testedCount++;
        refinementCount++;
        
        const tl_x = x0 + point.x;
        const tl_y = y0 + point.y;
        
        const guess = {
            tl: { x: tl_x, y: tl_y },
            tr: { x: tl_x + w, y: tl_y },
            bl: { x: tl_x, y: tl_y - h },
            br: { x: tl_x + w, y: tl_y - h },
            fitness: 0
        };
        
        const testMeasurements = JSON.parse(JSON.stringify(measurements));
        const result = computeLinesFitness(testMeasurements, guess, true);
        
        // Update the visualization point that was added earlier
        if (testedPoints !== null) {
            // Find and update the unevaluated point
            const pointToUpdate = testedPoints.find(p => 
                p.unevaluated && 
                Math.abs(p.width - w) < 0.1 && 
                Math.abs(p.height - h) < 0.1
            );
            if (pointToUpdate) {
                pointToUpdate.fitness = result.fitness;
                pointToUpdate.guess = JSON.parse(JSON.stringify(guess));
                pointToUpdate.unevaluated = false;  // Mark as evaluated
            }
        }
        
        if (result.fitness < bestFitness) {
            bestFitness = result.fitness;
            bestGuess = JSON.parse(JSON.stringify(guess));
            bestGuess.fitness = result.fitness;
        }
        
        return result.fitness;
    }
    
    // Ternary search on refinement points
    // Use smaller threshold for refinement since points are already near optimum
    const REFINEMENT_SEARCH_THRESHOLD = 3;
    let refLeft = 0;
    let refRight = refinementPoints.length - 1;
    
    while (refRight - refLeft > REFINEMENT_SEARCH_THRESHOLD) {
        const refMid1 = refLeft + Math.floor((refRight - refLeft) / 3);
        const refMid2 = refLeft + Math.floor(2 * (refRight - refLeft) / 3);
        
        const refFitness1 = await evaluateRefinementPoint(refMid1);
        const refFitness2 = await evaluateRefinementPoint(refMid2);
        
        if (refFitness1 > refFitness2) {
            refLeft = refMid1;
        } else {
            refRight = refMid2;
        }
        
        await new Promise(resolve => setTimeout(resolve, 10));
    }
    
    // Evaluate remaining refinement points
    for (let i = refLeft; i <= refRight; i++) {
        await evaluateRefinementPoint(i);
    }
    
    logFn(`Refinement complete: tested ${refinementCount} points`);
    
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
