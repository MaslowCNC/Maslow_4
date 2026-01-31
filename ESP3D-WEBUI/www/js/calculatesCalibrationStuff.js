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
 * NEW: Uses direct anchor point computation from first measurement
 * instead of diagonal+arc ternary search
 */
async function findBestRectangularStart(measurements) {
  const messagesBox = document.getElementById('messages');
  messagesBox.textContent += "Computing anchor positions using direct geometric method...\n";
  messagesBox.scrollTop = messagesBox.scrollHeight;

  // Use the first measurement to directly compute anchor positions
  const firstMeasurement = measurements[0];
  
  // Assume the reference point is roughly at the center of the workspace
  // We need to estimate this from the initial guess
  const estimatedCenterX = (initialGuess.tl.x + initialGuess.tr.x) / 2;
  const estimatedCenterY = (initialGuess.tl.y + initialGuess.bl.y) / 2;
  const referencePoint = { x: estimatedCenterX, y: estimatedCenterY };
  
  messagesBox.textContent += `Reference point estimate: (${estimatedCenterX.toFixed(1)}, ${estimatedCenterY.toFixed(1)})\n`;
  messagesBox.textContent += `First measurement distances: TL=${firstMeasurement.tl.toFixed(1)}, TR=${firstMeasurement.tr.toFixed(1)}, BL=${firstMeasurement.bl.toFixed(1)}, BR=${firstMeasurement.br.toFixed(1)}\n`;
  messagesBox.scrollTop = messagesBox.scrollHeight;
  
  messagesBox.textContent += "Phase 1: Coarse search (50mm steps)...\n";
  messagesBox.scrollTop = messagesBox.scrollHeight;
  
  // Phase 1: Coarse search
  const coarseSolutions = findRectangularSolutionsFromDistances(referencePoint, firstMeasurement);
  
  if (coarseSolutions.length === 0) {
    messagesBox.textContent += "❌ No valid solutions found! Falling back to initial guess.\n";
    messagesBox.scrollTop = messagesBox.scrollHeight;
    return initialGuess;
  }
  
  messagesBox.textContent += `Found ${coarseSolutions.length} potential configurations\n`;
  messagesBox.textContent += `Best coarse solution: Width=${coarseSolutions[0].width.toFixed(1)}mm, Height=${coarseSolutions[0].height.toFixed(1)}mm, Error=${coarseSolutions[0].error.toFixed(3)}mm\n`;
  messagesBox.scrollTop = messagesBox.scrollHeight;
  
  // Yield to UI
  await new Promise(resolve => setTimeout(resolve, 0));
  
  messagesBox.textContent += "Phase 2: Refining solution (1mm steps)...\n";
  messagesBox.scrollTop = messagesBox.scrollHeight;
  
  // Phase 2: Refine the best solution
  const refinedSolution = refineSolution(
    referencePoint, 
    firstMeasurement, 
    coarseSolutions[0],
    100,  // Search within ±100mm
    1     // 1mm step size
  );
  
  messagesBox.textContent += `Refined solution: Width=${refinedSolution.width.toFixed(1)}mm, Height=${refinedSolution.height.toFixed(1)}mm\n`;
  messagesBox.textContent += `Final error: ${refinedSolution.error.toFixed(3)}mm\n`;
  messagesBox.textContent += `TL: (${refinedSolution.tl.x.toFixed(1)}, ${refinedSolution.tl.y.toFixed(1)})\n`;
  messagesBox.textContent += `TR: (${refinedSolution.tr.x.toFixed(1)}, ${refinedSolution.tr.y.toFixed(1)})\n`;
  messagesBox.textContent += `BL: (${refinedSolution.bl.x.toFixed(1)}, ${refinedSolution.bl.y.toFixed(1)})\n`;
  messagesBox.textContent += `BR: (${refinedSolution.br.x.toFixed(1)}, ${refinedSolution.br.y.toFixed(1)})\n`;
  messagesBox.textContent += "Starting optimization...\n";
  messagesBox.scrollTop = messagesBox.scrollHeight;
  
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
  messagesBox.scrollTop = messagesBox.scrollHeight;

  // Determine if we should do rectangular optimization
  const frameWidth = initialGuess.tr.x - initialGuess.tl.x;
  const frameHeight = initialGuess.tl.y - initialGuess.bl.y;
  const aspectRatio = frameWidth / frameHeight;

  messagesBox.textContent += `Frame dimensions: ${frameWidth.toFixed(1)}mm x ${frameHeight.toFixed(1)}mm (aspect ratio: ${aspectRatio.toFixed(2)}:1)\n`;
  messagesBox.scrollTop = messagesBox.scrollHeight;

  let startingGuess = JSON.parse(JSON.stringify(initialGuess));

  // Run rectangular optimization if needed
  if (initialFitness < 0.1 || (aspectRatio > 0.95 && aspectRatio < 1.05)) {
    if (initialFitness < 0.1) {
      messagesBox.textContent += "Initial fitness < 0.1 (poor guess), running rectangular optimization to find better starting point.\n";
    } else {
      messagesBox.textContent += "Frame is nearly square (aspect ratio " + aspectRatio.toFixed(2) + ":1), running rectangular optimization to find better dimensions.\n";
    }
    messagesBox.scrollTop = messagesBox.scrollHeight;

    startingGuess = await findBestRectangularStart(projectedMeasurements);
  } else {
    messagesBox.textContent += "Initial fitness >= 0.1 and frame is not square, skipping rectangular optimization and using initial guess directly.\n";
    messagesBox.scrollTop = messagesBox.scrollHeight;
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
        messagesBox.scrollTop = messagesBox.scrollHeight;
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
          messagesBox.scrollTop = messagesBox.scrollHeight;

          initialGuess = JSON.parse(JSON.stringify(bestGuessAcrossAllRetries));
          initialGuess.fitness = 100000000;

          messagesBox.textContent += '\n\n❌ Calibration stopped due to low fitness after maximum retries.';
          messagesBox.textContent += '\nOptions:';
          messagesBox.textContent += '\n  1. Click "Calibrate" to restart with updated frame size estimate';
          messagesBox.textContent += '\n  2. Manually check belt tension and frame measurements';
          messagesBox.textContent += '\n  3. Verify measurements are accurate';
          messagesBox.scrollTop = messagesBox.scrollHeight;

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

      messagesBox.textContent += '\nCalibration values:';
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
      messagesBox.scrollTop = messagesBox.scrollHeight;

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
        messagesBox.scrollTop = messagesBox.scrollHeight;

        initialGuess = bestGuess;
        initialGuess.fitness = 100000000;

        scheduleCallback(() => { onCalibrationButtonsClick('$CAL', 'Calibrate'); }, 2000);
      } else {
        sendCalibrationEvent({
          good: false,
          final: true,
          guess: bestGuess
        }, true);

        messagesBox.textContent += '\n Restarting';

        // Add random perturbation and retry
        initialGuess.tl.x = bestGuess.tl.x + Math.random() * 100 - 50;
        initialGuess.tl.y = bestGuess.tl.y + Math.random() * 100 - 50;
        initialGuess.tr.x = bestGuess.tr.x + Math.random() * 100 - 50;
        initialGuess.tr.y = bestGuess.tr.y + Math.random() * 100 - 50;
        initialGuess.br.x = bestGuess.br.x + Math.random() * 100 - 50;

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
