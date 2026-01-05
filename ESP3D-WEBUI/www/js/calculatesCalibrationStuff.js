// When we can change to proper ESM - uncomment this
// import M from "constants";
// import { sendCommand } from "./maslow";

var tlZ = 100
var trZ = 56
var blZ = 34
var brZ = 78
var acceptableCalibrationThreshold = 0.5

//Establish initial guesses for the corners
var initialGuess = {
  tl: { x: 0, y: 2000 },
  tr: { x: 3000, y: 2000 },
  bl: { x: 0, y: 0 },
  br: { x: 3000, y: 0 },
  fitness: 100000000,
}

/**
 * Returns a fresh copy of the default initialGuess values
 * Used to reset calibration to consistent starting point
 */
function getDefaultInitialGuess() {
  return {
    tl: { x: 0, y: 2000 },
    tr: { x: 3000, y: 2000 },
    bl: { x: 0, y: 0 },
    br: { x: 3000, y: 0 },
    fitness: 100000000,
  };
}

let result

/**------------------------------------Intro------------------------------------
 *
 *   If you are reading this code to understand it then I would recommend starting
 *  at the bottom of the page and working your way up. The code is written in a
 * functional style so the function definitions are at the top and the code that
 * actually runs is at the bottom. It was also written quickly and modified a lot
 * so it is not very clean. I apologize for that.
 *
 *------------------------------------------------------------------------------
 */


/**
 * Computes the distance between two points.
 * @param {number} a - The x-coordinate of the first point.
 * @param {number} b - The y-coordinate of the first point.
 * @param {number} c - The x-coordinate of the second point.
 * @param {number} d - The y-coordinate of the second point.
 * @returns {number} - The distance between the two points.
 */
function distanceBetweenPoints(a, b, c, d) {
  var dx = c - a
  var dy = d - b
  return Math.sqrt(dx * dx + dy * dy)
}

/**
 * Computes the end point of a line based on its starting point, angle, and length.
 * @param {number} startX - The x-coordinate of the line's starting point.
 * @param {number} startY - The y-coordinate of the line's starting point.
 * @param {number} angle - The angle of the line in radians.
 * @param {number} length - The length of the line.
 * @returns {Object} - An object containing the x and y coordinates of the line's end point.
 */
function getEndPoint(startX, startY, angle, length) {
  var endX = startX + length * Math.cos(angle)
  var endY = startY + length * Math.sin(angle)
  return { x: endX, y: endY }
}

/**
 * Computes how close all of the line end points are to each other.
 * @param {Object} line1 - The first line to compare.
 * @param {Object} line2 - The second line to compare.
 * @param {Object} line3 - The third line to compare.
 * @param {Object} line4 - The fourth line to compare.
 * @returns {number} - The fitness value, which is the average distance between all line end points.
 */
function computeEndpointFitness(line1, line2, line3, line4) {
  const a = distanceBetweenPoints(line1.xEnd, line1.yEnd, line2.xEnd, line2.yEnd)
  const b = distanceBetweenPoints(line1.xEnd, line1.yEnd, line3.xEnd, line3.yEnd)
  const c = distanceBetweenPoints(line1.xEnd, line1.yEnd, line4.xEnd, line4.yEnd)
  const d = distanceBetweenPoints(line2.xEnd, line2.yEnd, line3.xEnd, line3.yEnd)
  const e = distanceBetweenPoints(line2.xEnd, line2.yEnd, line4.xEnd, line4.yEnd)
  const f = distanceBetweenPoints(line3.xEnd, line3.yEnd, line4.xEnd, line4.yEnd)

  const fitness = (a + b + c + d + e + f) / 6

  return fitness
}

/**
 * Computes the end point of a line based on its starting point, angle, and length.
 * @param {Object} line - The line to compute the end point for.
 * @returns {Object} - The line with the end point added.
 */
function computeLineEndPoint(line) {
  const end = getEndPoint(line.xBegin, line.yBegin, line.theta, line.length)
  line.xEnd = end.x
  line.yEnd = end.y
  return line
}

/**
 * Walks the four lines in the given set, adjusting their endpoints to minimize the distance between them.
 * @param {Object} tlLine - The top-left line in the set.
 * @param {Object} trLine - The top-right line in the set.
 * @param {Object} blLine - The bottom-left line in the set.
 * @param {Object} brLine - The bottom-right line in the set.
 * @param {number} stepSize - The amount to adjust the angle of each line by on each iteration.
 * @returns {Object} - An object containing the final positions of each line.
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

  const result = { tlLine, trLine, blLine, brLine, changeMade };

  sendCalibrationEvent({
    walkedlines: result,
  });

  return result;
}

/**
 * Computes the fitness of a set of lines based on how close their endpoints are to each other.
 * @param {Object} measurement - An object containing the initial theta values and lengths for each line.
 * @param {Object} individual - An object containing the x and y coordinates for each line's starting point.
 * @returns {Object} - An object containing the fitness value and the final positions of each line.
 */
function magneticallyAttractedLinesFitness(measurement, individual) {
  //These set the inital conditions for theta. They don't really matter, they just have to kinda point to the middle of the frame.
  if (typeof measurement.tlTheta === 'undefined') {
    measurement.tlTheta = -0.3;
  }
  if (typeof measurement.trTheta === 'undefined') {
    measurement.trTheta = 3.5;
  }
  if (typeof measurement.blTheta === 'undefined') {
    measurement.blTheta = 0.5;
  }
  if (typeof measurement.brTheta === 'undefined') {
    measurement.brTheta = 2.6;
  }

  //Define the four lines with starting points and lengths
  var tlLine = computeLineEndPoint({
    xBegin: individual.tl.x,
    yBegin: individual.tl.y,
    theta: measurement.tlTheta,
    length: measurement.tl,
  });
  var trLine = computeLineEndPoint({
    xBegin: individual.tr.x,
    yBegin: individual.tr.y,
    theta: measurement.trTheta,
    length: measurement.tr,
  });
  var blLine = computeLineEndPoint({
    xBegin: individual.bl.x,
    yBegin: individual.bl.y,
    theta: measurement.blTheta,
    length: measurement.bl,
  });
  var brLine = computeLineEndPoint({
    xBegin: individual.br.x,
    yBegin: individual.br.y,
    theta: measurement.brTheta,
    length: measurement.br,
  });

  var { tlLine, trLine, blLine, brLine } = walkLines(tlLine, trLine, blLine, brLine, 0.1);
  var { tlLine, trLine, blLine, brLine } = walkLines(tlLine, trLine, blLine, brLine, 0.01);
  var { tlLine, trLine, blLine, brLine } = walkLines(tlLine, trLine, blLine, brLine, 0.001);
  var { tlLine, trLine, blLine, brLine } = walkLines(tlLine, trLine, blLine, brLine, 0.0001);
  var { tlLine, trLine, blLine, brLine } = walkLines(tlLine, trLine, blLine, brLine, 0.00001);
  var { tlLine, trLine, blLine, brLine } = walkLines(tlLine, trLine, blLine, brLine, 0.000001);
  var { tlLine, trLine, blLine, brLine } = walkLines(tlLine, trLine, blLine, brLine, 0.0000001);
  var { tlLine, trLine, blLine, brLine } = walkLines(tlLine, trLine, blLine, brLine, 0.00000001);

  measurement.tlTheta = tlLine.theta;
  measurement.trTheta = trLine.theta;
  measurement.blTheta = blLine.theta;
  measurement.brTheta = brLine.theta;

  //Compute the final fitness
  const finalFitness = computeEndpointFitness(tlLine, trLine, blLine, brLine);

  //Compute the tension in the two upper belts
  const { TL, TR } = calculateTensions(tlLine.xEnd, tlLine.yEnd, individual);
  measurement.TLtension = TL;
  measurement.TRtension = TR;

  const result = { fitness: finalFitness, lines: { tlLine: tlLine, trLine: trLine, blLine: blLine, brLine: brLine } }
  sendCalibrationEvent({
    lines: result,
    individual,
    measurement
  });

  return result;
}

/**
 * Computes the distance of one line's end point from the center of mass of the other three lines.
 * @param {Object} lineToCompare - The line to compute the distance for.
 * @param {Object} line2 - The second line to use in computing the center of mass.
 * @param {Object} line3 - The third line to use in computing the center of mass.
 * @param {Object} line4 - The fourth line to use in computing the center of mass.
 * @returns {Object} - An object containing the x and y distances from the center of mass.
 */
function computeDistanceFromCenterOfMass(lineToCompare, line2, line3, line4) {
  //Compute the center of mass
  const x = (line2.xEnd + line3.xEnd + line4.xEnd) / 3
  const y = (line2.yEnd + line3.yEnd + line4.yEnd) / 3

  return { x: lineToCompare.xEnd - x, y: lineToCompare.yEnd - y }
}

/**
 * Computes the distances from the center of mass for four lines and converts them into the relevant variables that we can tweak.
 * @param {Object} lines - An object containing four lines to compute the distances from the center of mass for.
 * @returns {Object} - An object containing the distances from the center of mass for tlX, tlY, trX, trY, and brX.
 */
function generateTweaks(lines) {
  //We care about the distances for tlX, tlY, trX, trY, brX

  const tlX = computeDistanceFromCenterOfMass(lines.tlLine, lines.trLine, lines.blLine, lines.brLine).x
  const tlY = computeDistanceFromCenterOfMass(lines.tlLine, lines.trLine, lines.blLine, lines.brLine).y
  const trX = computeDistanceFromCenterOfMass(lines.trLine, lines.tlLine, lines.blLine, lines.brLine).x
  const trY = computeDistanceFromCenterOfMass(lines.trLine, lines.tlLine, lines.blLine, lines.brLine).y
  const brX = computeDistanceFromCenterOfMass(lines.brLine, lines.tlLine, lines.trLine, lines.blLine).x

  return { tlX: tlX, tly: tlY, trX: trX, trY: trY, brX: brX }
}

/**
 * Computes all of the tweaks and summarizes them to move the guess furthest from the center of mass of the lines.
 * @param {Array} lines - An array of lines to compute the tweaks for.
 * @param {Object} lastGuess - The last guess made by the algorithm.
 * @returns {Object} - The updated guess with the furthest tweaks applied.
 */
function computeFurthestFromCenterOfMass(lines, lastGuess) {
  let tlX = 0;
  let tlY = 0;
  let trX = 0;
  let trY = 0;
  let brX = 0;

  lines.forEach((line) => {
    const tweaks = generateTweaks(line);

    tlX += tweaks.tlX;
    tlY += tweaks.tly;
    trX += tweaks.trX;
    trY += tweaks.trY;
    brX += tweaks.brX;
  })

  tlX /= lines.length;
  tlY /= lines.length;
  trX /= lines.length;
  trY /= lines.length;
  brX /= lines.length;

  const tlXAbs = Math.abs(tlX);
  const tlyAbs = Math.abs(tlY);
  const trXAbs = Math.abs(trX);
  const tryAbs = Math.abs(trY);
  const brXAbs = Math.abs(brX);
  
  var scalor = -1;
  
  // Find which coordinate has maximum error using explicit comparisons
  // This ensures deterministic behavior even with floating point values
  // We check in a fixed order to break ties consistently
  let maxError = tlXAbs;
  let maxIndex = 0; // 0=tlX, 1=tlY, 2=trX, 3=trY, 4=brX
  
  if (tlyAbs > maxError) {
    maxError = tlyAbs;
    maxIndex = 1;
  }
  if (trXAbs > maxError) {
    maxError = trXAbs;
    maxIndex = 2;
  }
  if (tryAbs > maxError) {
    maxError = tryAbs;
    maxIndex = 3;
  }
  if (brXAbs > maxError) {
    maxError = brXAbs;
    maxIndex = 4;
  }
  
  // Apply adjustment based on which coordinate has maximum error
  switch (maxIndex) {
    case 0:
      //console.log("Move tlX by: " + tlX/divisor);
      lastGuess.tl.x = lastGuess.tl.x + tlX * scalor;
      break;
    case 1:
      //console.log("Move tlY by: " + tlY/divisor);
      lastGuess.tl.y = lastGuess.tl.y + tlY * scalor;
      break;
    case 2:
      //console.log("Move trX by: " + trX/divisor);
      lastGuess.tr.x = lastGuess.tr.x + trX * scalor;
      break;
    case 3:
      //console.log("Move trY by: " + trY/divisor);
      lastGuess.tr.y = lastGuess.tr.y + trY * scalor;
      break;
    case 4:
      //console.log("Move brX by: " + brX/divisor);
      lastGuess.br.x = lastGuess.br.x + brX * scalor;
      break;
    default:
    // Do nothing
  }

  return lastGuess;
}

/**
 * Resets theta values in measurements to ensure consistent initial conditions
 * for the line-fitting algorithm. This prevents theta state from persisting
 * across different calibration attempts.
 * @param {Array} measurements - An array of measurement objects
 */
function resetMeasurementThetas(measurements) {
  measurements.forEach((measurement) => {
    delete measurement.tlTheta;
    delete measurement.trTheta;
    delete measurement.blTheta;
    delete measurement.brTheta;
  });
}

/**
 * Computes the fitness of a guess for a set of measurements by comparing the guess to magnetically attracted lines.
 * @param {Array} measurements - An array of measurements to compare the guess to.
 * @param {Object} lastGuess - The last guess made by the algorithm.
 * @param {boolean} skipThetaUpdates - If true, skips updating the guess based on center of mass (for initial fitness evaluation).
 * @returns {Object} - An object containing the fitness of the guess and the lines used to calculate the fitness.
 */
function computeLinesFitness(measurements, lastGuess, skipThetaUpdates = false) {
  var fitnesses = []
  var allLines = []

  //Check each of the measurements against the guess
  measurements.forEach((measurement) => {
    const { fitness, lines } = magneticallyAttractedLinesFitness(measurement, lastGuess)
    fitnesses.push(fitness)
    allLines.push(lines)
  })

  //Computes the average fitness of all of the measurements
  const fitness = calculateAverage(fitnesses)

  // console.log(fitnesses)

  //Here is where we need to do the calculation of which corner is the worst and which direction to move it
  if (!skipThetaUpdates) {
    lastGuess = computeFurthestFromCenterOfMass(allLines, lastGuess)
  }
  lastGuess.fitness = fitness

  return lastGuess
}

function calculateTensions(x, y, guess) {
  let Xtl = guess.tl.x
  let Ytl = guess.tl.y
  let Xtr = guess.tr.x
  let Ytr = guess.tr.y
  let Xbl = guess.bl.x
  let Ybl = guess.bl.y
  let Xbr = guess.br.x
  let Ybr = guess.br.y

  let mass = 5.0
  const G_CONSTANT = 9.80665
  let alpha = 0.26
  let TL, TR

  let A, C, sinD, cosD, sinE, cosE
  let Fx, Fy

  A = (Xtl - x) / (Ytl - y)
  C = (Xtr - x) / (Ytr - y)
  A = Math.abs(A)
  C = Math.abs(C)
  sinD = x / Math.sqrt(Math.pow(x, 2) + Math.pow(y, 2))
  cosD = y / Math.sqrt(Math.pow(x, 2) + Math.pow(y, 2))
  sinE = Math.abs(Xbr - x) / Math.sqrt(Math.pow(Xbr - x, 2) + Math.pow(y, 2))
  cosE = y / Math.sqrt(Math.pow(Xbr - x, 2) + Math.pow(y, 2))

  Fx = Ybr * sinE - Ybl * sinD
  Fy = Ybr * cosE + Ybl * cosD + mass * G_CONSTANT * Math.cos(alpha)
  // console.log(`Fx = ${Fx.toFixed(1)}, Fy = ${Fy.toFixed(1)}`)

  let TLy = (Fx + C * Fy) / (A + C)
  let TRy = Fy - TLy
  let TRx = C * (Fy - TLy)
  let TLx = A * TLy

  // console.log(`TLy = ${TLy.toFixed(1)}, TRy = ${TRy.toFixed(1)}, TRx = ${TRx.toFixed(1)}, TLx = ${TLx.toFixed(1)}`);

  TL = Math.sqrt(Math.pow(TLx, 2) + Math.pow(TLy, 2))
  TR = Math.sqrt(Math.pow(TRx, 2) + Math.pow(TRy, 2))

  return { TL, TR }
}

/**
 * Calculates the average of an array of numbers.
 * @param {number[]} array - The array of numbers to calculate the average of.
 * @returns {number} - The average of the array.
 */
function calculateAverage(array) {
  var total = 0
  var count = 0

  array.forEach(function (item, index) {
    total += Math.abs(item)
    count++
  })

  return total / count
}


/**
 * Projects the measurements to the plane of the machine. This is needed
 * because the belts are not parallel to the surface of the machine.
 * @param {Object} measurement - An object containing the measurements
 * @returns {Object} - An object containing the projected measurements
 */
function projectMeasurement(measurement) {
  const tl = Math.sqrt(Math.pow(measurement.tl, 2) - Math.pow(tlZ, 2))
  const tr = Math.sqrt(Math.pow(measurement.tr, 2) - Math.pow(trZ, 2))
  const bl = Math.sqrt(Math.pow(measurement.bl, 2) - Math.pow(blZ, 2))
  const br = Math.sqrt(Math.pow(measurement.br, 2) - Math.pow(brZ, 2))

  return { tl: tl, tr: tr, bl: bl, br: br }
}

/**
 * Projects an array of measurements to the plane of the machine to account for the fact that the start and end point are not in the same plane.
 * @param {Object[]} measurements - An array of objects containing the measurements of the top left, top right, bottom left, and bottom right corners of a rectangle.
 * @returns {Object[]} - An array of objects containing the projected measurements of the top left, top right, bottom left, and bottom right corners of a rectangle.
 */
function projectMeasurements(measurements) {
  var projectedMeasurements = []

  measurements.forEach((measurement) => {
    projectedMeasurements.push(projectMeasurement(measurement))
  })

  return projectedMeasurements
}

/**
 * Adds a constant to each measurement in an array of measurements.
 * @param {Object[]} measurements - An array of objects containing the measurements of the top left, top right, bottom left, and bottom right corners of a rectangle.
 * @param {number} offset - The constant to add to each measurement.
 * @returns {Object[]} - An array of objects containing the updated measurements of the top left, top right, bottom left, and bottom right corners of a rectangle.
 */
function offsetMeasurements(measurements, offset) {
  const newMeasurements = measurements.map((measurement) => {
    return {
      tl: measurement.tl + offset,
      tr: measurement.tr + offset,
      bl: measurement.bl + offset,
      br: measurement.br + offset,
    }
  })

  return newMeasurements
}

/**
 * Scales each measurement in an array of measurements by a constant.
 * @param {Object[]} measurements - An array of objects containing the measurements of the top left, top right, bottom left, and bottom right corners of a rectangle.
 * @param {number} scale - The constant to multiply each measurement by.
 * @returns {Object[]} - An array of objects containing the updated measurements of the top left, top right, bottom left, and bottom right corners of a rectangle.
 */
function scaleMeasurements(measurements, scale) {
  const newMeasurements = measurements.map((measurement) => {
    return {
      tl: measurement.tl * scale,
      tr: measurement.tr * scale,
      bl: measurement.bl, // * scale,
      br: measurement.br, // * scale
    }
  })

  return newMeasurements
}

function scaleMeasurementsBasedOnTension(measurements, guess) {
  const maxScale = 0.995
  const minScale = 0.994
  const maxTension = 60
  const minTension = 20

  const scaleRange = maxScale - minScale
  const tensionRange = maxTension - minTension

  const newMeasurements = measurements.map((measurement) => {
    const tensionAdjustedTLScale = (1 - (measurement.TLtension - minTension) / tensionRange) * scaleRange + minScale
    const tensionAdjustedTRScale = (1 - (measurement.TRtension - minTension) / tensionRange) * scaleRange + minScale

    return {
      tl: measurement.tl * tensionAdjustedTLScale,
      tr: measurement.tr * tensionAdjustedTRScale,
      bl: measurement.bl, // * scale,
      br: measurement.br, // * scale
    }
  })

  return newMeasurements
}


/**
 * Finds the best rectangular starting configuration by testing different width and height combinations.
 * Uses ternary search to efficiently find optimal dimensions.
 * @param {Array} measurements - An array of measurements to test against.
 * @returns {Object} - The best rectangular guess found.
 */
async function findBestRectangularStart(measurements) {
  const messagesBox = document.getElementById('messages');
  messagesBox.textContent += "Searching for best rectangular starting configuration...\n";
  messagesBox.textContent += "Phase 1: Finding optimal radius along diagonal using ternary search...\n";
  messagesBox.scrollTop = messagesBox.scrollHeight;

  // Deep copy measurements to avoid mutation during search
  const measurementsCopy = JSON.parse(JSON.stringify(measurements));

  // PHASE 1: Use ternary search to find optimal diagonal size efficiently
  let diagonalBestFitness = Infinity;
  let diagonalBestSize = 0;
  let phase1TestCount = 0;

  // Helper function to evaluate fitness at a given diagonal size
  async function evaluateDiagonalSize(size) {
    const testMeasurements = JSON.parse(JSON.stringify(measurementsCopy));
    const guess = {
      tl: { x: 0, y: size },
      tr: { x: size, y: size },
      bl: { x: 0, y: 0 },
      br: { x: size, y: 0 },
      fitness: 0
    };

    const result = computeLinesFitness(testMeasurements, guess, true);
    phase1TestCount++;

    return { fitness: result.fitness, size: size };
  }

  // Ternary search for optimal diagonal size (100 to 5000mm range)
  let leftSize = 100;
  let rightSize = 5000;
  const precision = 2; // Stop when range is less than 2mm

  while (rightSize - leftSize > precision) {
    const leftThird = leftSize + (rightSize - leftSize) / 3;
    const rightThird = rightSize - (rightSize - leftSize) / 3;

    const leftResult = await evaluateDiagonalSize(Math.round(leftThird));
    const rightResult = await evaluateDiagonalSize(Math.round(rightThird));

    if (leftResult.fitness < diagonalBestFitness) {
      diagonalBestFitness = leftResult.fitness;
      diagonalBestSize = leftResult.size;
    }
    if (rightResult.fitness < diagonalBestFitness) {
      diagonalBestFitness = rightResult.fitness;
      diagonalBestSize = rightResult.size;
    }

    // Allow UI to update
    await new Promise(resolve => setTimeout(resolve, 0));

    if (leftResult.fitness < rightResult.fitness) {
      rightSize = rightThird;
    } else {
      leftSize = leftThird;
    }
  }

  // Evaluate the final center point
  const finalDiagonalSize = Math.round((leftSize + rightSize) / 2);
  const finalDiagonalResult = await evaluateDiagonalSize(finalDiagonalSize);
  if (finalDiagonalResult.fitness < diagonalBestFitness) {
    diagonalBestFitness = finalDiagonalResult.fitness;
    diagonalBestSize = finalDiagonalResult.size;
  }

  messagesBox.textContent += `Phase 1 complete: Optimal radius found at ${diagonalBestSize}mm using ${phase1TestCount} evaluations\n`;
  messagesBox.scrollTop = messagesBox.scrollHeight;

  // Calculate optimal radius (distance from origin to diagonal point)
  const optimalRadius = Math.sqrt(diagonalBestSize * diagonalBestSize + diagonalBestSize * diagonalBestSize);

  messagesBox.textContent += `Phase 2: Finding best aspect ratio on arc at ${optimalRadius.toFixed(0)}mm radius...\n`;
  messagesBox.scrollTop = messagesBox.scrollHeight;

  // PHASE 2: Use ternary search to efficiently find the maximum on the arc
  let bestGuess = null;
  let bestFitness = Infinity;
  let testedCount = 0;

  // Sample angles from 0° to 90° (first quadrant only)
  // We limit to angles where aspect ratio <= 3:1
  const minAngle = Math.atan(1 / 3); // ~18.43° in radians (aspect ratio 3:1, wide)
  const maxAngle = Math.PI / 2 - minAngle; // ~71.57° in radians (aspect ratio 1:3, tall)

  // Convert arc spacing (1mm) to angle precision for ternary search termination
  const arcSpacing = 1;
  const anglePrecision = (2 * arcSpacing) / optimalRadius;

  // Helper function to evaluate fitness at a given angle
  async function evaluateFitnessAtAngle(angleRad) {
    const width = optimalRadius * Math.cos(angleRad);
    const height = optimalRadius * Math.sin(angleRad);

    // Skip invalid dimensions
    if (width <= 0 || height <= 0) return { fitness: Infinity, result: null };

    const testMeasurements = JSON.parse(JSON.stringify(measurementsCopy));
    const guess = {
      tl: { x: 0, y: height },
      tr: { x: width, y: height },
      bl: { x: 0, y: 0 },
      br: { x: width, y: 0 },
      fitness: 0
    };

    const result = computeLinesFitness(testMeasurements, guess, true);
    testedCount++;

    return { fitness: result.fitness, result: result, angle: angleRad };
  }

  // Ternary search to find the angle with maximum fitness (minimum rawFitness)
  let left = minAngle;
  let right = maxAngle;

  while (right - left > anglePrecision) {
    // Divide the range into three parts
    const leftThird = left + (right - left) / 3;
    const rightThird = right - (right - left) / 3;

    // Evaluate fitness at the two interior points
    const leftResult = await evaluateFitnessAtAngle(leftThird);
    const rightResult = await evaluateFitnessAtAngle(rightThird);

    // Update best guess if we found a better fitness
    if (leftResult.fitness < bestFitness) {
      bestFitness = leftResult.fitness;
      bestGuess = JSON.parse(JSON.stringify(leftResult.result));
    }
    if (rightResult.fitness < bestFitness) {
      bestFitness = rightResult.fitness;
      bestGuess = JSON.parse(JSON.stringify(rightResult.result));
    }

    // Allow UI to update
    await new Promise(resolve => setTimeout(resolve, 0));

    // Narrow the search range based on which point has better fitness
    if (leftResult.fitness < rightResult.fitness) {
      right = rightThird;
    } else {
      left = leftThird;
    }
  }

  // Evaluate the final center point for completeness
  const finalAngle = (left + right) / 2;
  const finalResult = await evaluateFitnessAtAngle(finalAngle);
  if (finalResult.fitness < bestFitness) {
    bestFitness = finalResult.fitness;
    bestGuess = JSON.parse(JSON.stringify(finalResult.result));
  }

  messagesBox.textContent += `Search complete! Tested ${testedCount} points using ternary search.\n`;
  messagesBox.textContent += `Best rectangular start: Width: ${bestGuess.tr.x.toFixed(1)}mm, Height: ${bestGuess.tr.y.toFixed(1)}mm, Fitness: ${(1 / bestGuess.fitness).toFixed(4)}\n`;
  messagesBox.textContent += "Starting optimization...\n";
  messagesBox.scrollTop = messagesBox.scrollHeight;

  return bestGuess;
}

async function findMaxFitness(measurements) {
  const messagesBox = document.getElementById('messages');
  
  // Reset theta values to ensure consistent initial conditions
  // This prevents theta state from persisting across calibration attempts
  resetMeasurementThetas(measurements);

  // Declare startingGuess that will be set based on calibration stage
  let startingGuess;

  // Reset initialGuess to default at the start of a new calibration run
  // The first stage sends exactly 6 measurements (initial grid estimate)
  // Subsequent stages send more measurements (full grid)
  const INITIAL_CALIBRATION_MEASUREMENT_COUNT = 6;
  if (measurements.length === INITIAL_CALIBRATION_MEASUREMENT_COUNT) {
    messagesBox.textContent += 'New calibration run detected (6-point initial stage). Resetting initial guess to default.\n';
    // Always use rectangular optimization for first stage to ensure robust starting point
    // This searches for best fit rather than using a fixed default
    initialGuess = getDefaultInitialGuess();
    startingGuess = await findBestRectangularStart(measurements);
    messagesBox.textContent += 'Using rectangular optimization result as starting point for consistency.\n';
  } else {
    messagesBox.textContent += `Continuing calibration with ${measurements.length} measurements. Using previous stage result as starting point.\n`;
    
    // Evaluate the fitness of the initial guess from previous stage
    const initialGuessCopy = JSON.parse(JSON.stringify(initialGuess));
    const evaluatedGuess = computeLinesFitness(measurements, initialGuessCopy, true);
    const initialFitness = 1 / evaluatedGuess.fitness;

    messagesBox.textContent += `Initial guess fitness: ${initialFitness.toFixed(7)}\n`;
    messagesBox.scrollTop = messagesBox.scrollHeight;

    // Calculate frame dimensions from initial guess
    const frameWidth = initialGuess.tr.x - initialGuess.tl.x;
    const frameHeight = initialGuess.tl.y - initialGuess.bl.y;
    const aspectRatio = frameWidth / frameHeight;
    
    // Check if frame is square or near-square (aspect ratio between 0.9 and 1.11)
    // This covers ratios like 10:9 to 10:11, which are "close to square"
    const isNearlySquare = aspectRatio >= 0.9 && aspectRatio <= 1.11;
    
    messagesBox.textContent += `Frame dimensions: ${frameWidth.toFixed(1)}mm x ${frameHeight.toFixed(1)}mm (aspect ratio: ${aspectRatio.toFixed(2)}:1)\n`;
    messagesBox.scrollTop = messagesBox.scrollHeight;

    // Use rectangular optimization if:
    // 1. Initial fitness is poor (< 0.1), OR
    // 2. Frame is square or nearly square (likely a default guess)
    if (initialFitness < 0.1 || isNearlySquare) {
      if (initialFitness < 0.1) {
        messagesBox.textContent += "Initial fitness < 0.1 (poor guess), running rectangular optimization to find better starting point.\n";
      }
      if (isNearlySquare) {
        messagesBox.textContent += "Frame is nearly square (aspect ratio " + aspectRatio.toFixed(2) + ":1), running rectangular optimization to find better dimensions.\n";
      }
      messagesBox.scrollTop = messagesBox.scrollHeight;
      // Find the best rectangular starting configuration
      startingGuess = await findBestRectangularStart(measurements);
    } else {
      messagesBox.textContent += "Initial fitness >= 0.1 and frame is not square, skipping rectangular optimization and using initial guess directly.\n";
      messagesBox.scrollTop = messagesBox.scrollHeight;
      startingGuess = initialGuess;
    }
  }

  sendCalibrationEvent({
    initialGuess
  }, true);

  //Project the measurements into the XY plane...this is now done on the firmware side
  //measurements = projectMeasurements(measurements);

  let currentGuess = JSON.parse(JSON.stringify(startingGuess));
  let stagnantCounter = 0;
  let totalCounter = 0;
  let bestGuess = JSON.parse(JSON.stringify(startingGuess));
  let retryAttempt = 0;  // Track retry attempts to prevent infinite loops

  function iterate() {
    if (stagnantCounter < 1000 && totalCounter < 200000) {

      currentGuess = computeLinesFitness(measurements, currentGuess);

      if (1 / currentGuess.fitness > 1 / bestGuess.fitness) {
        bestGuess = JSON.parse(JSON.stringify(currentGuess));
        stagnantCounter = 0;
      } else {
        stagnantCounter++;
      }

      totalCounter++;
      // console.log("Total Counter: " + totalCounter);
      sendCalibrationEvent({
        final: false,
        guess: currentGuess,
        bestGuess: bestGuess,
        totalCounter
      });

      //Every 100 iterations print out the fitness
      if (totalCounter % 100 === 0) {
        messagesBox.textContent += `Fitness: ${(1 / bestGuess.fitness).toFixed(7)} in ${totalCounter}\n`;
        messagesBox.scrollTop = messagesBox.scrollHeight;
      }

      // Schedule the next iteration
      setTimeout(iterate, 0);

    } else { //We have completed the calibration (success or timeout)
      if (1 / bestGuess.fitness < acceptableCalibrationThreshold) {
        messagesBox.textContent += '\nCalculated Fitness Too Low. The process will automatically try again.!';
      }

      messagesBox.textContent += '\nCalibration values:';
      messagesBox.textContent += `\nFitness: ${(1 / bestGuess.fitness).toFixed(7)}`;

      const tlxStr = bestGuess.tl.x.toFixed(1), tlyStr = bestGuess.tl.y.toFixed(1);
      const trxStr = bestGuess.tr.x.toFixed(1), tryStr = bestGuess.tr.y.toFixed(1);
      const blxStr = bestGuess.bl.x.toFixed(1), blyStr = bestGuess.bl.y.toFixed(1);
      const brxStr = bestGuess.br.x.toFixed(1), bryStr = bestGuess.br.y.toFixed(1);

      messagesBox.textContent += `\n${M}_tlX: ${tlxStr}`;
      messagesBox.textContent += `\n${M}_tlY: ${tlyStr}`;
      messagesBox.textContent += `\n${M}_trX: ${trxStr}`;
      messagesBox.textContent += `\n${M}_trY: ${tryStr}`;
      messagesBox.textContent += `\n${M}_blX: ${blxStr}`;
      messagesBox.textContent += `\n${M}_blY: ${blyStr}`;
      messagesBox.textContent += `\n${M}_brX: ${brxStr}`;
      messagesBox.textContent += `\n${M}_brY: ${bryStr}`;
      messagesBox.scrollTop
      messagesBox.scrollTop = messagesBox.scrollHeight;

      if (1 / bestGuess.fitness > acceptableCalibrationThreshold) {
        sendCommand(`$/kinematics/MaslowKinematics/tlX=${tlxStr}`);
        sendCommand(`$/kinematics/MaslowKinematics/tlY=${tlyStr}`);
        sendCommand(`$/kinematics/MaslowKinematics/trX=${trxStr}`);
        sendCommand(`$/kinematics/MaslowKinematics/trY=${tryStr}`);
        sendCommand(`$/kinematics/MaslowKinematics/blX=${blxStr}`);
        sendCommand(`$/kinematics/MaslowKinematics/blY=${blyStr}`);
        sendCommand(`$/kinematics/MaslowKinematics/brX=${brxStr}`);
        sendCommand(`$/kinematics/MaslowKinematics/brY=${bryStr}`);

        sendCalibrationEvent({
          good: true,
          final: true,
          bestGuess: bestGuess
        }, true);
        refreshSettings(current_setting_filter);
        saveMaslowYaml();

        messagesBox.textContent += '\nA command to save these values has been successfully sent for you. Please check for any error messages.';
        messagesBox.scrollTop = messagesBox.scrollHeight;

        initialGuess = bestGuess;
        initialGuess.fitness = 100000000;

        // This restarts calibration process for the next stage
        setTimeout(() => { onCalibrationButtonsClick('$CAL', 'Calibrate'); }, 2000);
      } else {

        sendCalibrationEvent({
          good: false,
          final: true,
          guess: bestGuess
        }, true);

        // Limit retry attempts to prevent infinite loops
        const MAX_RETRY_ATTEMPTS = 5;
        if (retryAttempt >= MAX_RETRY_ATTEMPTS) {
          messagesBox.textContent += '\n\nMaximum retry attempts reached. Using best result found.';
          messagesBox.textContent += `\nFitness: ${(1 / bestGuess.fitness).toFixed(7)}`;
          messagesBox.textContent += '\n\nCalibration values:';
          
          const tlxStr = bestGuess.tl.x.toFixed(1), tlyStr = bestGuess.tl.y.toFixed(1);
          const trxStr = bestGuess.tr.x.toFixed(1), tryStr = bestGuess.tr.y.toFixed(1);
          const blxStr = bestGuess.bl.x.toFixed(1), blyStr = bestGuess.bl.y.toFixed(1);
          const brxStr = bestGuess.br.x.toFixed(1), bryStr = bestGuess.br.y.toFixed(1);

          messagesBox.textContent += `\n${M}_tlX: ${tlxStr}`;
          messagesBox.textContent += `\n${M}_tlY: ${tlyStr}`;
          messagesBox.textContent += `\n${M}_trX: ${trxStr}`;
          messagesBox.textContent += `\n${M}_trY: ${tryStr}`;
          messagesBox.textContent += `\n${M}_blX: ${blxStr}`;
          messagesBox.textContent += `\n${M}_blY: ${blyStr}`;
          messagesBox.textContent += `\n${M}_brX: ${brxStr}`;
          messagesBox.textContent += `\n${M}_brY: ${bryStr}`;
          messagesBox.scrollTop = messagesBox.scrollHeight;
          
          // Save the best result even if fitness is below threshold
          sendCommand(`$/kinematics/MaslowKinematics/tlX=${tlxStr}`);
          sendCommand(`$/kinematics/MaslowKinematics/tlY=${tlyStr}`);
          sendCommand(`$/kinematics/MaslowKinematics/trX=${trxStr}`);
          sendCommand(`$/kinematics/MaslowKinematics/trY=${tryStr}`);
          sendCommand(`$/kinematics/MaslowKinematics/blX=${blxStr}`);
          sendCommand(`$/kinematics/MaslowKinematics/blY=${blyStr}`);
          sendCommand(`$/kinematics/MaslowKinematics/brX=${brxStr}`);
          sendCommand(`$/kinematics/MaslowKinematics/brY=${bryStr}`);
          
          refreshSettings(current_setting_filter);
          saveMaslowYaml();
          
          initialGuess = bestGuess;
          initialGuess.fitness = 100000000;
          
          // Continue to next calibration stage
          setTimeout(() => { onCalibrationButtonsClick('$CAL', 'Calibrate'); }, 2000);
          return;
        }

        retryAttempt++;
        messagesBox.textContent += `\n Restarting (attempt ${retryAttempt} of ${MAX_RETRY_ATTEMPTS})`;

        // Reset theta values before retry to ensure consistent initial conditions
        resetMeasurementThetas(measurements);

        // Use deterministic perturbations based on retry attempt
        // This creates a systematic search pattern instead of random exploration
        // Note: bl (bottom-left) is fixed at origin (0,0) and br.y stays at 0 to keep bottom edge horizontal
        const perturbationPatterns = [
          // Pattern 1: Expand frame uniformly
          { tlX: 30, tlY: 30, trX: -30, trY: 30, brX: -30 },
          // Pattern 2: Contract frame uniformly  
          { tlX: -30, tlY: -30, trX: 30, trY: -30, brX: 30 },
          // Pattern 3: Shift frame up
          { tlX: 0, tlY: 40, trX: 0, trY: 40, brX: 0 },
          // Pattern 4: Shift frame down
          { tlX: 0, tlY: -40, trX: 0, trY: -40, brX: 0 },
          // Pattern 5: Adjust aspect ratio (widen)
          { tlX: -20, tlY: 0, trX: -20, trY: 0, brX: -20 }
        ];
        
        const pattern = perturbationPatterns[retryAttempt - 1];
        initialGuess.tl.x = bestGuess.tl.x + pattern.tlX;
        initialGuess.tl.y = bestGuess.tl.y + pattern.tlY;
        initialGuess.tr.x = bestGuess.tr.x + pattern.trX;
        initialGuess.tr.y = bestGuess.tr.y + pattern.trY;
        initialGuess.br.x = bestGuess.br.x + pattern.brX;
        // Note: bl.x, bl.y remain at origin (0,0) and br.y stays at 0

        //Reset the counters
        stagnantCounter = 0;
        totalCounter = 0;

        //Try again with different starting conditions
        bestGuess = JSON.parse(JSON.stringify(initialGuess));
        currentGuess = JSON.parse(JSON.stringify(initialGuess));

        //Restart the iteration
        setTimeout(iterate, 0);
      }
    }
  }

  // Start the iteration
  iterate();
}


/**
 * This function will allow us to hook data into events that we can just copy this file into another project
 * to have the calibration run in other contexts and still gather events from the calculations to plot things, gather data, etc.
 */
function sendCalibrationEvent(dataToSend, log = false) {
  try {
    if (log) {
      console.log(JSON.stringify(dataToSend, null, 2));
    } //else if (dataToSend.totalCounter) {
    //   console.log("total counter:", dataToSend.totalCounter);
    // }
    document.body.dispatchEvent(new CustomEvent(CALIBRATION_EVENT_NAME, {
      bubbles: true,
      cancelable: true,
      detail: dataToSend
    }));
  } catch (err) {
    console.error('Unexpected:', err);
  }
}
const CALIBRATION_EVENT_NAME = 'calibration-data';
//This is where the program really begins. The above is all function definitions
//The way that the progam works is that we basically guess where the four corners are and then
//check to see how good that guess was. To see how good a guess was we "draw" circles from the four corner points
//with radiuses of the measured distances. If the guess was good then all four circles will intersect at a single point.
//The closer the circles are to intersecting at a single point the better the guess is.

//Once we've figured out how good our guess was we try a different guess. We keep the good guesses and throw away the bad guesses
//using a genetic algorithm
