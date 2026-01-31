/**
 * Anchor Point Solver
 * 
 * This module implements the geometric approach to finding anchor positions
 * from a single measurement point and distances to each anchor.
 * 
 * Given:
 * - A point P0 at (x0, y0) with known distances d_TL, d_TR, d_BL, d_BR to four anchors
 * - Constraint: Anchors form a rectangle
 * 
 * Find: The anchor positions that satisfy these constraints
 * 
 * This replaces the diagonal+arc search with direct geometric computation.
 */

/**
 * Solves for rectangular anchor positions given a reference point and distances.
 * 
 * Mathematical approach:
 * For a rectangle with corners TL(x1,y2), TR(x2,y2), BL(x1,y1), BR(x2,y1) where x1 < x2, y1 < y2
 * And a reference point P0(x0,y0) with measured distances d_TL, d_TR, d_BL, d_BR:
 * 
 * We have:
 *   d_TL² = (x0-x1)² + (y0-y2)²
 *   d_TR² = (x0-x2)² + (y0-y2)²
 *   d_BL² = (x0-x1)² + (y0-y1)²
 *   d_BR² = (x0-x2)² + (y0-y1)²
 * 
 * From these, we can derive:
 *   d_TL² - d_TR² = (x2² - x1²) + 2*x0*(x1 - x2) = (x2 - x1)(x2 + x1 - 2*x0)
 *   d_BL² - d_BR² = (x2² - x1²) + 2*x0*(x1 - x2) = (x2 - x1)(x2 + x1 - 2*x0)
 *   
 *   d_TL² - d_BL² = (y2² - y1²) + 2*y0*(y1 - y2) = (y2 - y1)(y2 + y1 - 2*y0)
 *   d_TR² - d_BR² = (y2² - y1²) + 2*y0*(y1 - y2) = (y2 - y1)(y2 + y1 - 2*y0)
 * 
 * Let W = x2 - x1 (width) and H = y2 - y1 (height)
 * Let xc = (x1 + x2)/2 (center x) and yc = (y1 + y2)/2 (center y)
 * 
 * Then:
 *   d_TL² - d_TR² = W(2*xc - 2*x0) = 2*W*(xc - x0)
 *   d_TL² - d_BL² = H(2*yc - 2*y0) = 2*H*(yc - y0)
 * 
 * This gives us relationships between width, height, center position and the measured distances.
 * 
 * @param {Object} p0 - Reference point {x, y}
 * @param {Object} distances - Measured distances {tl, tr, bl, br}
 * @returns {Array} Array of potential solutions {tl, tr, bl, br, fitness}
 */
function findRectangularSolutionsFromDistances(p0, distances) {
    const solutions = [];
    const { x: x0, y: y0 } = p0;
    const { tl: d_tl, tr: d_tr, bl: d_bl, br: d_br } = distances;
    
    // Derived constraints from the distance equations
    const d_tl_sq = d_tl * d_tl;
    const d_tr_sq = d_tr * d_tr;
    const d_bl_sq = d_bl * d_bl;
    const d_br_sq = d_br * d_br;
    
    // Key relationships:
    const delta_x_top = d_tl_sq - d_tr_sq;    // = 2*W*(xc - x0)
    const delta_x_bot = d_bl_sq - d_br_sq;    // = 2*W*(xc - x0)
    const delta_y_left = d_tl_sq - d_bl_sq;   // = 2*H*(yc - y0)
    const delta_y_right = d_tr_sq - d_br_sq;  // = 2*H*(yc - y0)
    
    // Sanity check: these should be equal (within tolerance)
    const x_consistency = Math.abs(delta_x_top - delta_x_bot);
    const y_consistency = Math.abs(delta_y_left - delta_y_right);
    
    if (x_consistency > 10 || y_consistency > 10) {
        console.warn('Distance measurements may be inconsistent', {x_consistency, y_consistency});
    }
    
    // Search for solutions by testing different width and height combinations
    // For each (W, H), we can compute the center position (xc, yc)
    
    const minDim = 100;
    const maxDim = 5000;
    const step = 50;
    
    for (let W = minDim; W <= maxDim; W += step) {
        for (let H = minDim; H <= maxDim; H += step) {
            // From d_TL² - d_TR² = 2*W*(xc - x0), solve for xc
            const xc = (delta_x_top / (2 * W)) + x0;
            
            // From d_TL² - d_BL² = 2*H*(yc - y0), solve for yc
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
                // Compute fitness (inverse of total error)
                const fitness = 1 / (totalError + 0.01);
                
                solutions.push({
                    tl, tr, bl, br,
                    width: W,
                    height: H,
                    center: { x: xc, y: yc },
                    error: totalError,
                    fitness: fitness,
                    errors: { tl: error_tl, tr: error_tr, bl: error_bl, br: error_br }
                });
            }
        }
    }
    
    // Sort by fitness (best first)
    solutions.sort((a, b) => b.fitness - a.fitness);
    
    return solutions;
}

/**
 * Refined search around a promising solution.
 * Takes the best coarse solution and refines it with a finer step size.
 * 
 * @param {Object} p0 - Reference point {x, y}
 * @param {Object} distances - Measured distances {tl, tr, bl, br}
 * @param {Object} coarseSolution - A coarse solution to refine
 * @param {number} searchRange - Range to search around the coarse solution (mm)
 * @param {number} step - Step size for refinement (mm)
 * @returns {Object} Refined solution
 */
function refineSolution(p0, distances, coarseSolution, searchRange = 100, step = 1) {
    const { x: x0, y: y0 } = p0;
    const { tl: d_tl, tr: d_tr, bl: d_bl, br: d_br } = distances;
    const { width: W0, height: H0 } = coarseSolution;
    
    let bestSolution = coarseSolution;
    let bestError = coarseSolution.error;
    
    // Derived constraints
    const d_tl_sq = d_tl * d_tl;
    const d_tr_sq = d_tr * d_tr;
    const d_bl_sq = d_bl * d_bl;
    const delta_x_top = d_tl_sq - d_tr_sq;
    const delta_y_left = d_tl_sq - d_bl_sq;
    
    // Refine around the coarse solution
    for (let W = W0 - searchRange; W <= W0 + searchRange; W += step) {
        for (let H = H0 - searchRange; H <= H0 + searchRange; H += step) {
            if (W <= 0 || H <= 0) continue;
            
            // Compute center position
            const xc = (delta_x_top / (2 * W)) + x0;
            const yc = (delta_y_left / (2 * H)) + y0;
            
            // Compute anchor positions
            const x1 = xc - W / 2;
            const x2 = xc + W / 2;
            const y1 = yc - H / 2;
            const y2 = yc + H / 2;
            
            // Compute distances
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
 * Main function to find anchor positions from first measurement.
 * This replaces the diagonal+arc ternary search with direct geometric computation.
 * 
 * @param {Object} firstMeasurement - First measurement {tl, tr, bl, br} distances
 * @param {Object} referencePoint - Reference point {x, y} where measurement was taken
 * @returns {Object} Best anchor configuration {tl, tr, bl, br}
 */
function computeAnchorsFromFirstMeasurement(firstMeasurement, referencePoint) {
    console.log('Computing anchor positions using direct geometric method...');
    console.log('First measurement:', firstMeasurement);
    console.log('Reference point:', referencePoint);
    
    // Phase 1: Coarse search (50mm steps)
    const coarseSolutions = findRectangularSolutionsFromDistances(referencePoint, firstMeasurement);
    
    if (coarseSolutions.length === 0) {
        console.error('No valid solutions found in coarse search');
        return null;
    }
    
    console.log(`Found ${coarseSolutions.length} coarse solutions`);
    console.log('Best coarse solution:', coarseSolutions[0]);
    
    // Phase 2: Refine the best solution (1mm steps)
    const refinedSolution = refineSolution(
        referencePoint, 
        firstMeasurement, 
        coarseSolutions[0],
        100,  // Search within ±100mm of coarse solution
        1     // 1mm step size
    );
    
    console.log('Refined solution:', refinedSolution);
    console.log(`Total error: ${refinedSolution.error.toFixed(3)}mm`);
    
    // Convert to the format expected by the calibration system
    return {
        tl: refinedSolution.tl,
        tr: refinedSolution.tr,
        bl: refinedSolution.bl,
        br: refinedSolution.br,
        fitness: 1 / refinedSolution.error  // Inverse of error as fitness
    };
}

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        findRectangularSolutionsFromDistances,
        refineSolution,
        computeAnchorsFromFirstMeasurement
    };
}
