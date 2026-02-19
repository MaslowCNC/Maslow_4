// When we can change to proper ESM - uncomment this
// import M from "constants";
// import { sendCommand } from "./maslow";

// NOTE: Core calibration computation functions are now in calibration-computation.js
// This file contains only UI-specific logic and helper functions

var tlZ = 100
var trZ = 56
var blZ = 34
var brZ = 78
var acceptableCalibrationThreshold = 0.5

// Maximum number of low-fitness retry attempts before giving up
const MAX_LOW_FITNESS_RETRIES = 10;

// Aspect ratios to test as retry points (width:height)
const RETRY_ASPECT_RATIOS = [2.0, 1.5, 1.0, 1.0/1.5, 1.0/2.0];

/**
 * Calculate a rectangular frame configuration from aspect ratio on an arc
 * @param {number} aspectRatio - Width:height ratio (e.g., 2.0 for 2:1)
 * @param {number} radius - Optimal radius from phase 2
 * @returns {Object} Frame configuration with tl, tr, bl, br coordinates
 */
function calculateAspectRatioFrame(aspectRatio, radius) {
  const angle = Math.atan(1.0 / aspectRatio);
  const width = radius * Math.cos(angle);
  const height = radius * Math.sin(angle);

  return {
    tl: { x: 0, y: height },
    tr: { x: width, y: height },
    bl: { x: 0, y: 0 },
    br: { x: width, y: 0 }
  };
}

/**
 * Apply retry strategy to update guess coordinates
 * @param {Object} targetGuess - The guess object to update
 * @param {Object} params - Strategy parameters
 * @param {number|null} params.optimalRadiusForRetry - Optimal radius from phase 1&2, or null if skipped
 * @param {number} params.lowFitnessRetryCount - Current retry count (1-based)
 * @param {Object} params.bestGuess - Best guess so far (for random perturbations)
 * @param {Object} params.startingGuess - Original starting guess (for fallback)
 * @returns {Object} Info about the strategy used: { strategy: 'aspectRatio'|'random'|'fallback', description: string }
 */
function applyRetryStrategy(targetGuess, params) {
  const { optimalRadiusForRetry, lowFitnessRetryCount, bestGuess, startingGuess } = params;

  if (optimalRadiusForRetry !== null && lowFitnessRetryCount <= RETRY_ASPECT_RATIOS.length) {
    // Use aspect ratio points on the arc from phase 2
    const aspectRatio = RETRY_ASPECT_RATIOS[lowFitnessRetryCount - 1];
    const frame = calculateAspectRatioFrame(aspectRatio, optimalRadiusForRetry);

    targetGuess.tl.x = frame.tl.x;
    targetGuess.tl.y = frame.tl.y;
    targetGuess.tr.x = frame.tr.x;
    targetGuess.tr.y = frame.tr.y;
    targetGuess.bl.x = frame.bl.x;
    targetGuess.bl.y = frame.bl.y;
    targetGuess.br.x = frame.br.x;
    targetGuess.br.y = frame.br.y;

    return {
      strategy: 'aspectRatio',
      description: `with aspect ratio ${aspectRatio.toFixed(2)}:1 (Width: ${frame.tr.x.toFixed(1)}mm, Height: ${frame.tr.y.toFixed(1)}mm)`
    };
  } else if (optimalRadiusForRetry === null) {
    // Random perturbations when phase 1&2 were skipped
    targetGuess.tl.x = bestGuess.tl.x + Math.random() * 100 - 50;
    targetGuess.tl.y = bestGuess.tl.y + Math.random() * 100 - 50;
    targetGuess.tr.x = bestGuess.tr.x + Math.random() * 100 - 50;
    targetGuess.tr.y = bestGuess.tr.y + Math.random() * 100 - 50;
    targetGuess.br.x = bestGuess.br.x + Math.random() * 100 - 50;

    return {
      strategy: 'random',
      description: 'with random perturbations (±50mm)'
    };
  } else {
    // Fallback to original guess if exceeded retry attempts with aspect ratios
    targetGuess.tl.x = startingGuess.tl.x;
    targetGuess.tl.y = startingGuess.tl.y;
    targetGuess.tr.x = startingGuess.tr.x;
    targetGuess.tr.y = startingGuess.tr.y;
    targetGuess.bl.x = startingGuess.bl.x;
    targetGuess.bl.y = startingGuess.bl.y;
    targetGuess.br.x = startingGuess.br.x;
    targetGuess.br.y = startingGuess.br.y;

    return {
      strategy: 'fallback',
      description: 'with original starting guess'
    };
  }
}

//Establish initial guesses for the corners
var initialGuess = {
  tl: { x: 0, y: 2000 },
  tr: { x: 3000, y: 2000 },
  bl: { x: 0, y: 0 },
  br: { x: 3000, y: 0 },
  fitness: 100000000,
}

let result

/**------------------------------------Intro------------------------------------
 *
 * Core calibration computation functions have been moved to calibration-computation.js
 * This file now contains only UI-specific logic for the ESP3D web interface.
 *
 *------------------------------------------------------------------------------
 */

/**
 * Computes tensions at given coordinates
 * @param {number} x - X coordinate
 * @param {number} y - Y coordinate  
 * @param {Object} guess - Frame anchor positions
 * @returns {Object} - Tensions for each belt
 */
function calculateTensions(x, y, guess) {
  const tlDistance = distanceBetweenPoints(guess.tl.x, guess.tl.y, x, y)
  const trDistance = distanceBetweenPoints(guess.tr.x, guess.tr.y, x, y)
  const blDistance = distanceBetweenPoints(guess.bl.x, guess.bl.y, x, y)
  const brDistance = distanceBetweenPoints(guess.br.x, guess.br.y, x, y)

  const tlTension = (tlDistance / 10) * 1.6 * 9.81
  const trTension = (trDistance / 10) * 1.6 * 9.81
  const blTension = (blDistance / 10) * 1.6 * 9.81
  const brTension = (brDistance / 10) * 1.6 * 9.81

  return {
    TL: tlTension,
    TR: trTension,
    BL: blTension,
    BR: brTension,
  }
}

/**
 * Calculates the average of an array
 * @param {number[]} array - Array of numbers
 * @returns {number} - Average value
 */
function calculateAverage(array) {
  let total = 0
  let count = 0
  array.forEach(function (item, index) {
    total += Math.abs(item);
    count++;
  });

  return total / count;
}

/**
 * Projects the measurements to the plane of the machine
 * @param {Object} measurement - Measurement object
 * @returns {Object} - Projected measurements
 */
function projectMeasurement(measurement) {
  const tl = Math.sqrt(Math.pow(measurement.tl, 2) - Math.pow(tlZ, 2))
  const tr = Math.sqrt(Math.pow(measurement.tr, 2) - Math.pow(trZ, 2))
  const bl = Math.sqrt(Math.pow(measurement.bl, 2) - Math.pow(blZ, 2))
  const br = Math.sqrt(Math.pow(measurement.br, 2) - Math.pow(brZ, 2))

  return { tl: tl, tr: tr, bl: bl, br: br }
}

/**
 * Projects array of measurements
 * @param {Object[]} measurements - Array of measurements
 * @returns {Object[]} - Array of projected measurements
 */
function projectMeasurements(measurements) {
  var projectedMeasurements = []

  measurements.forEach((measurement) => {
    projectedMeasurements.push(projectMeasurement(measurement))
  })

  return projectedMeasurements
}

/**
 * Adds offset to measurements
 * @param {Object[]} measurements - Array of measurements
 * @param {number} offset - Offset to add
 * @returns {Object[]} - Offset measurements
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
 * Scales measurements by a constant
 * @param {Object[]} measurements - Array of measurements
 * @param {number} scale - Scale factor
 * @returns {Object[]} - Scaled measurements
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

/**
 * Scales measurements based on tension
 * @param {Object[]} measurements - Array of measurements
 * @param {Object} guess - Frame anchor positions
 * @returns {Object[]} - Tension-scaled measurements
 */
function scaleMeasurementsBasedOnTension(measurements, guess) {
  const maxScale = 0.995
  const minScale = 0.994
  const maxTension = 60
  const minTension = 20

  const scaleRange = maxScale - minScale
  const tensionRange = maxTension - minTension

  const newMeasurements = measurements.map((measurement) => {
    const tensionAdjustedTLScale =
      (1 - (measurement.TLtension - minTension) / tensionRange) * scaleRange +
      minScale
    const tensionAdjustedTRScale =
      (1 - (measurement.TRtension - minTension) / tensionRange) * scaleRange +
      minScale

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
 * Searches for best rectangular starting configuration
 * This is a UI-specific helper function for the optimization
 */
async function findBestRectangularStart(measurements) {
  const messagesBox = document.getElementById('messages');
  messagesBox.textContent += "Searching for best rectangular starting configuration...\n";
  messagesBox.textContent += "Phase 1: Finding optimal radius along diagonal using ternary search...\n";
  if (shouldAutoScroll()) messagesBox.scrollTop = messagesBox.scrollHeight;

  // Deep copy measurements
  const measurementsCopy = JSON.parse(JSON.stringify(measurements));

  // PHASE 1: Ternary search for optimal diagonal size
  let diagonalBestFitness = Infinity;
  let diagonalBestSize = 0;
  let diagonalResults = [];
  let phase1TestCount = 0;

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
    const fitnessValue = 1 / result.fitness;
    diagonalResults.push({ size, fitness: fitnessValue, rawFitness: result.fitness });
    phase1TestCount++;

    console.log(`[Diagonal Test #${phase1TestCount}] Size: ${size}mm, Raw Fitness: ${result.fitness.toFixed(6)}, Display Fitness (1/raw): ${fitnessValue.toFixed(4)}`);

    return { fitness: result.fitness, size: size };
  }

  // Ternary search for optimal diagonal size (100 to 5000mm range)
  let leftSize = 100;
  let rightSize = 5000;
  const epsilon = 50; // Stop when range is less than 50mm

  while ((rightSize - leftSize) > epsilon) {
    const mid1 = leftSize + (rightSize - leftSize) / 3;
    const mid2 = rightSize - (rightSize - leftSize) / 3;

    const result1 = await evaluateDiagonalSize(mid1);
    const result2 = await evaluateDiagonalSize(mid2);

    if (result1.fitness < result2.fitness) {
      rightSize = mid2;
      if (result1.fitness < diagonalBestFitness) {
        diagonalBestFitness = result1.fitness;
        diagonalBestSize = mid1;
      }
    } else {
      leftSize = mid1;
      if (result2.fitness < diagonalBestFitness) {
        diagonalBestFitness = result2.fitness;
        diagonalBestSize = mid2;
      }
    }

    await new Promise(resolve => setTimeout(resolve, 0));
  }

  const optimalRadius = diagonalBestSize;
  messagesBox.textContent += `Phase 1 complete: Optimal radius found at ${diagonalBestSize}mm using ${phase1TestCount} evaluations\n`;
  if (shouldAutoScroll()) messagesBox.scrollTop = messagesBox.scrollHeight;

  // PHASE 2: Search for best aspect ratio on arc at optimal radius
  messagesBox.textContent += `Phase 2: Finding best aspect ratio on arc at ${optimalRadius.toFixed(0)}mm radius...\n`;
  if (shouldAutoScroll()) messagesBox.scrollTop = messagesBox.scrollHeight;

  let bestGuess = null;
  let bestFitnessOverall = Infinity;
  let phase2TestCount = 0;
  let testedPoints = [];

  async function evaluateFitnessAtAngle(angleRad) {
    const testMeasurements = JSON.parse(JSON.stringify(measurementsCopy));
    const width = optimalRadius * Math.cos(angleRad);
    const height = optimalRadius * Math.sin(angleRad);

    const guess = {
      tl: { x: 0, y: height },
      tr: { x: width, y: height },
      bl: { x: 0, y: 0 },
      br: { x: width, y: 0 },
      fitness: 0
    };

    const result = computeLinesFitness(testMeasurements, guess, true);
    const fitnessValue = 1 / result.fitness;
    phase2TestCount++;

    testedPoints.push({
      width: width,
      height: height,
      angle: angleRad,
      fitness: result.fitness,
      displayFitness: fitnessValue
    });

    console.log(`[Phase 2 Test #${phase2TestCount}] Angle: ${(angleRad * 180 / Math.PI).toFixed(1)}°, Width: ${width.toFixed(1)}mm, Height: ${height.toFixed(1)}mm, Fitness: ${fitnessValue.toFixed(4)}`);

    return { fitness: result.fitness, width: width, height: height, angle: angleRad };
  }

  // Ternary search on angle (from 0 to π/2)
  let leftAngle = 0.1; // Avoid exactly 0 or π/2
  let rightAngle = Math.PI / 2 - 0.1;
  const angleEpsilon = 0.01; // ~0.57 degrees

  while ((rightAngle - leftAngle) > angleEpsilon) {
    const mid1 = leftAngle + (rightAngle - leftAngle) / 3;
    const mid2 = rightAngle - (rightAngle - leftAngle) / 3;

    const result1 = await evaluateFitnessAtAngle(mid1);
    const result2 = await evaluateFitnessAtAngle(mid2);

    if (result1.fitness < result2.fitness) {
      rightAngle = mid2;
      if (result1.fitness < bestFitnessOverall) {
        bestFitnessOverall = result1.fitness;
        bestGuess = {
          tl: { x: 0, y: result1.height },
          tr: { x: result1.width, y: result1.height },
          bl: { x: 0, y: 0 },
          br: { x: result1.width, y: 0 },
          fitness: result1.fitness
        };
      }
    } else {
      leftAngle = mid1;
      if (result2.fitness < bestFitnessOverall) {
        bestFitnessOverall = result2.fitness;
        bestGuess = {
          tl: { x: 0, y: result2.height },
          tr: { x: result2.width, y: result2.height },
          bl: { x: 0, y: 0 },
          br: { x: result2.width, y: 0 },
          fitness: result2.fitness
        };
      }
    }

    await new Promise(resolve => setTimeout(resolve, 0));
  }

  const testedCount = phase1TestCount + phase2TestCount;
  messagesBox.textContent += `Search complete! Tested ${testedCount} points using ternary search.\n`;
  messagesBox.textContent += `Best rectangular start: Width: ${bestGuess.tr.x.toFixed(1)}mm, Height: ${bestGuess.tr.y.toFixed(1)}mm, Fitness: ${(1 / bestGuess.fitness).toFixed(4)}\n`;
  messagesBox.textContent += "Starting optimization...\n";
  if (shouldAutoScroll()) messagesBox.scrollTop = messagesBox.scrollHeight;

  // Store the optimal radius for retry point calculations
  bestGuess.optimalRadius = optimalRadius;

  return bestGuess;
}

/**
 * Main calibration optimization function
 * Uses the shared CalibrationComputer from calibration-computation.js
 */
async function findMaxFitness(measurements) {
  const messagesBox = document.getElementById('messages');

  // Project measurements
  let projectedMeasurements = projectMeasurements(measurements);

  // Calculate initial fitness
  const initialFitness = 1 / computeLinesFitness(projectedMeasurements, initialGuess).fitness;
  messagesBox.textContent += `Initial guess fitness: ${initialFitness.toFixed(7)}\n`;
  if (shouldAutoScroll()) messagesBox.scrollTop = messagesBox.scrollHeight;

  // Determine if we should do rectangular optimization
  const frameWidth = initialGuess.tr.x - initialGuess.tl.x;
  const frameHeight = initialGuess.tl.y - initialGuess.bl.y;
  const aspectRatio = frameWidth / frameHeight;

  messagesBox.textContent += `Frame dimensions: ${frameWidth.toFixed(1)}mm x ${frameHeight.toFixed(1)}mm (aspect ratio: ${aspectRatio.toFixed(2)}:1)\n`;
  if (shouldAutoScroll()) messagesBox.scrollTop = messagesBox.scrollHeight;

  let startingGuess = JSON.parse(JSON.stringify(initialGuess));

  // Run rectangular optimization if needed
  if (initialFitness < 0.1 || (aspectRatio > 0.95 && aspectRatio < 1.05)) {
    if (initialFitness < 0.1) {
      messagesBox.textContent += "Initial fitness < 0.1 (poor guess), running rectangular optimization to find better starting point.\n";
    } else {
      messagesBox.textContent += "Frame is nearly square (aspect ratio " + aspectRatio.toFixed(2) + ":1), running rectangular optimization to find better dimensions.\n";
    }
    if (shouldAutoScroll()) messagesBox.scrollTop = messagesBox.scrollHeight;

    startingGuess = await findBestRectangularStart(projectedMeasurements);
  } else {
    messagesBox.textContent += "Initial fitness >= 0.1 and frame is not square, skipping rectangular optimization and using initial guess directly.\n";
    if (shouldAutoScroll()) messagesBox.scrollTop = messagesBox.scrollHeight;
  }

  // Main optimization loop using the shared computation library
  let currentGuess = JSON.parse(JSON.stringify(startingGuess));
  let stagnantCounter = 0;
  let totalCounter = 0;
  let bestGuess = JSON.parse(JSON.stringify(startingGuess));

  // Track retry attempts
  let lowFitnessRetryCount = 0;
  let bestGuessAcrossAllRetries = JSON.parse(JSON.stringify(startingGuess));
  let bestFitnessAcrossAllRetries = 1 / bestGuess.fitness;

  // Store optimal radius from rectangular optimization for retry point calculations
  let optimalRadiusForRetry = startingGuess.optimalRadius || null;

  function iterate() {
    if (stagnantCounter < 1000 && totalCounter < 200000) {

      currentGuess = computeLinesFitness(projectedMeasurements, currentGuess);

      if (1 / currentGuess.fitness > 1 / bestGuess.fitness) {
        bestGuess = JSON.parse(JSON.stringify(currentGuess));
        stagnantCounter = 0;
      } else {
        stagnantCounter++;
      }

      totalCounter++;
      
      sendCalibrationEvent({
        final: false,
        guess: currentGuess,
        bestGuess: bestGuess,
        totalCounter
      });

      // Every 100 iterations print out the fitness
      if (totalCounter % 100 === 0) {
        messagesBox.textContent += `Fitness: ${(1 / bestGuess.fitness).toFixed(7)} in ${totalCounter}\n`;
        if (shouldAutoScroll()) messagesBox.scrollTop = messagesBox.scrollHeight;
      }

      // Schedule the next iteration
      scheduleTask(iterate);

    } else { // Calibration complete
      const currentFitness = 1 / bestGuess.fitness;
      
      if (currentFitness > bestFitnessAcrossAllRetries) {
        bestFitnessAcrossAllRetries = currentFitness;
        bestGuessAcrossAllRetries = JSON.parse(JSON.stringify(bestGuess));
      }

      if (currentFitness < acceptableCalibrationThreshold) {
        messagesBox.textContent += `\nCalculated Fitness Too Low (${currentFitness.toFixed(7)} < ${acceptableCalibrationThreshold}).`;

        if (lowFitnessRetryCount >= MAX_LOW_FITNESS_RETRIES) {
          messagesBox.textContent += `\n\n⚠️ Maximum retry attempts (${MAX_LOW_FITNESS_RETRIES}) reached.`;
          messagesBox.textContent += `\nBest fitness achieved: ${bestFitnessAcrossAllRetries.toFixed(7)}`;
          messagesBox.textContent += '\nUpdating initial frame size with best estimate from all attempts.';
          if (shouldAutoScroll()) messagesBox.scrollTop = messagesBox.scrollHeight;

          initialGuess = JSON.parse(JSON.stringify(bestGuessAcrossAllRetries));
          initialGuess.fitness = 100000000;

          messagesBox.textContent += '\n\n❌ Find Anchors stopped due to low fitness after maximum retries.';
          messagesBox.textContent += '\nOptions:';
          messagesBox.textContent += '\n  1. Click "Find Anchors" to restart.';
          messagesBox.textContent += '\n  2. Check to make sure that all four belts are fully tight with each measurement.';
          messagesBox.textContent += '\n  3. Check to see if the frame could be flexing when the measurements are taken which will lead to inacruate measurements.';
          if (shouldAutoScroll()) messagesBox.scrollTop = messagesBox.scrollHeight;

          sendCalibrationEvent({
            good: false,
            final: true,
            maxRetriesReached: true,
            retryCount: lowFitnessRetryCount,
            bestGuess: bestGuessAcrossAllRetries,
            bestFitness: bestFitnessAcrossAllRetries
          }, true);

          sendCommand('$CALRESET');
          return;
        }

        lowFitnessRetryCount++;
        messagesBox.textContent += ` Retry ${lowFitnessRetryCount}/${MAX_LOW_FITNESS_RETRIES}...`;
      }

      messagesBox.textContent += '\nFind Anchor Values:';
      messagesBox.textContent += `\nFitness: ${currentFitness.toFixed(7)}`;

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
      if (shouldAutoScroll()) messagesBox.scrollTop = messagesBox.scrollHeight;

      if (currentFitness > acceptableCalibrationThreshold) {
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
        if (shouldAutoScroll()) messagesBox.scrollTop = messagesBox.scrollHeight;

        initialGuess = bestGuess;
        initialGuess.fitness = 100000000;

        scheduleCallback(() => { onCalibrationButtonsClick('$CAL', 'Find Anchors'); }, 2000);
      } else {
        sendCalibrationEvent({
          good: false,
          final: true,
          guess: bestGuess
        }, true);

        messagesBox.textContent += '\n Restarting';

        const retryInfo = applyRetryStrategy(initialGuess, {
          optimalRadiusForRetry,
          lowFitnessRetryCount,
          bestGuess,
          startingGuess
        });

        messagesBox.textContent += ` ${retryInfo.description}`;

        stagnantCounter = 0;
        totalCounter = 0;

        bestGuess = JSON.parse(JSON.stringify(initialGuess));
        currentGuess = JSON.parse(JSON.stringify(initialGuess));

        scheduleTask(iterate);
      }
    }
  }

  // Start the iteration
  iterate();
}

/**
 * Sends calibration events to allow external monitoring
 * @param {Object} dataToSend - Event data
 * @param {boolean} log - Whether to log to console
 */
function sendCalibrationEvent(dataToSend, log = false) {
  try {
    if (log) {
      console.log(JSON.stringify(dataToSend, null, 2));
    }
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
