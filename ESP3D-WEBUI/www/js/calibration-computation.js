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
 * NEW: Direct anchor point computation using geometric constraints.
 * This replaces the diagonal+arc ternary search with a more accurate method.
 * 
 * Given a reference point and distances to four anchors, directly compute
 * the anchor positions that form a valid rectangle.
 */

/**
 * Finds rectangular anchor configurations from distance measurements.
 * 
 * For a rectangle with corners TL(x1,y2), TR(x2,y2), BL(x1,y1), BR(x2,y1)
 * and reference point P0(x0,y0) with distances d_TL, d_TR, d_BL, d_BR:
 * 
 * We derive:
 *   d_TL² - d_TR² = 2*W*(xc - x0)  where W = x2-x1, xc = (x1+x2)/2
 *   d_TL² - d_BL² = 2*H*(yc - y0)  where H = y2-y1, yc = (y1+y2)/2
 * 
 * This allows direct computation of center position for any (W,H).
 */
function findRectangularSolutionsFromDistances(p0, distances) {
    const solutions = [];
    const { x: x0, y: y0 } = p0;
    const { tl: d_tl, tr: d_tr, bl: d_bl, br: d_br } = distances;
    
    const d_tl_sq = d_tl * d_tl;
    const d_tr_sq = d_tr * d_tr;
    const d_bl_sq = d_bl * d_bl;
    const d_br_sq = d_br * d_br;
    
    // Derived constraints from distance equations
    const delta_x_top = d_tl_sq - d_tr_sq;    // = 2*W*(xc - x0)
    const delta_x_bot = d_bl_sq - d_br_sq;    // = 2*W*(xc - x0) (should be equal)
    const delta_y_left = d_tl_sq - d_bl_sq;   // = 2*H*(yc - y0)
    const delta_y_right = d_tr_sq - d_br_sq;  // = 2*H*(yc - y0) (should be equal)
    
    // Sanity check: consistency of measurements
    const x_consistency = Math.abs(delta_x_top - delta_x_bot);
    const y_consistency = Math.abs(delta_y_left - delta_y_right);
    
    if (x_consistency > 10 || y_consistency > 10) {
        console.warn('Distance measurements may be inconsistent', {x_consistency, y_consistency});
    }
    
    // Search for solutions by testing different width and height combinations
    const minDim = 100;
    const maxDim = 5000;
    const step = 50;
    
    for (let W = minDim; W <= maxDim; W += step) {
        for (let H = minDim; H <= maxDim; H += step) {
            // Solve for center position from geometric constraints
            const xc = (delta_x_top / (2 * W)) + x0;
            const yc = (delta_y_left / (2 * H)) + y0;
            
            // Compute anchor positions
            const x1 = xc - W / 2;
            const x2 = xc + W / 2;
            const y1 = yc - H / 2;
            const y2 = yc + H / 2;
            
            const tl = { x: x1, y: y2 };
            const tr = { x: x2, y: y2 };
            const bl = { x: x1, y: y1 };
            const br = { x: x2, y: y1 };
            
            // Verify: compute distances and check error
            const calc_d_tl = Math.sqrt((x0 - x1) ** 2 + (y0 - y2) ** 2);
            const calc_d_tr = Math.sqrt((x0 - x2) ** 2 + (y0 - y2) ** 2);
            const calc_d_bl = Math.sqrt((x0 - x1) ** 2 + (y0 - y1) ** 2);
            const calc_d_br = Math.sqrt((x0 - x2) ** 2 + (y0 - y1) ** 2);
            
            const error_tl = Math.abs(calc_d_tl - d_tl);
            const error_tr = Math.abs(calc_d_tr - d_tr);
            const error_bl = Math.abs(calc_d_bl - d_bl);
            const error_br = Math.abs(calc_d_br - d_br);
            const totalError = error_tl + error_tr + error_bl + error_br;
            
            // Only keep solutions with low error
            if (totalError < 10) {
                const fitness = 1 / (totalError + 0.01);
                
                solutions.push({
                    tl, tr, bl, br,
                    width: W,
                    height: H,
                    center: { x: xc, y: yc },
                    error: totalError,
                    fitness: fitness
                });
            }
        }
    }
    
    // Sort by fitness (best first)
    solutions.sort((a, b) => b.fitness - a.fitness);
    
    return solutions;
}

/**
 * Refines a coarse solution with finer step size.
 */
function refineSolution(p0, distances, coarseSolution, searchRange = 100, step = 1) {
    const { x: x0, y: y0 } = p0;
    const { tl: d_tl, tr: d_tr, bl: d_bl, br: d_br } = distances;
    const { width: W0, height: H0 } = coarseSolution;
    
    let bestSolution = coarseSolution;
    let bestError = coarseSolution.error;
    
    const d_tl_sq = d_tl * d_tl;
    const d_tr_sq = d_tr * d_tr;
    const d_bl_sq = d_bl * d_bl;
    const delta_x_top = d_tl_sq - d_tr_sq;
    const delta_y_left = d_tl_sq - d_bl_sq;
    
    for (let W = W0 - searchRange; W <= W0 + searchRange; W += step) {
        for (let H = H0 - searchRange; H <= H0 + searchRange; H += step) {
            if (W <= 0 || H <= 0) continue;
            
            const xc = (delta_x_top / (2 * W)) + x0;
            const yc = (delta_y_left / (2 * H)) + y0;
            
            const x1 = xc - W / 2;
            const x2 = xc + W / 2;
            const y1 = yc - H / 2;
            const y2 = yc + H / 2;
            
            const calc_d_tl = Math.sqrt((x0 - x1) ** 2 + (y0 - y2) ** 2);
            const calc_d_tr = Math.sqrt((x0 - x2) ** 2 + (y0 - y2) ** 2);
            const calc_d_bl = Math.sqrt((x0 - x1) ** 2 + (y0 - y1) ** 2);
            const calc_d_br = Math.sqrt((x0 - x2) ** 2 + (y0 - y1) ** 2);
            
            const totalError = Math.abs(calc_d_tl - d_tl) + 
                             Math.abs(calc_d_tr - d_tr) +
                             Math.abs(calc_d_bl - d_bl) + 
                             Math.abs(calc_d_br - d_br);
            
            if (totalError < bestError) {
                bestError = totalError;
                bestSolution = {
                    tl: { x: x1, y: y2 },
                    tr: { x: x2, y: y2 },
                    bl: { x: x1, y: y1 },
                    br: { x: x2, y: y1 },
                    width: W,
                    height: H,
                    center: { x: xc, y: yc },
                    error: totalError,
                    fitness: 1 / (totalError + 0.01)
                };
            }
        }
    }
    
    return bestSolution;
}

/**
 * Main function to compute anchor positions from first measurement.
 * Replaces the diagonal+arc ternary search with direct geometric computation.
 */
function computeAnchorsFromFirstMeasurement(firstMeasurement, referencePoint) {
    console.log('Computing anchor positions using direct geometric method...');
    
    // Phase 1: Coarse search (50mm steps)
    const coarseSolutions = findRectangularSolutionsFromDistances(referencePoint, firstMeasurement);
    
    if (coarseSolutions.length === 0) {
        console.error('No valid solutions found in coarse search');
        return null;
    }
    
    console.log(`Found ${coarseSolutions.length} coarse solutions, refining best one...`);
    
    // Phase 2: Refine the best solution (1mm steps)
    const refinedSolution = refineSolution(
        referencePoint, 
        firstMeasurement, 
        coarseSolutions[0],
        100,  // Search within ±100mm
        1     // 1mm step size
    );
    
    console.log('Refined solution error:', refinedSolution.error.toFixed(3), 'mm');
    
    // Convert to expected format
    return {
        tl: refinedSolution.tl,
        tr: refinedSolution.tr,
        bl: refinedSolution.bl,
        br: refinedSolution.br,
        fitness: refinedSolution.error < 1 ? 100000000 : 1 / refinedSolution.error
    };
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

// Export for use in other modules (works in both browser and Node.js)
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        CalibrationComputer,
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
        // New anchor point solver functions
        findRectangularSolutionsFromDistances,
        refineSolution,
        computeAnchorsFromFirstMeasurement
    };
}
