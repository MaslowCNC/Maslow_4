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

// projectMeasurement and projectMeasurements are now in calibration-helpers.js

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
 * Wrapper around shared findBestRectangularStart that adds UI logging
 */
async function findBestRectangularStartWithUI(measurements) {
  const messagesBox = document.getElementById('messages');
  
  // Create logging function that writes to messages box
  const logFn = (message) => {
    messagesBox.textContent += message + "\n";
    messagesBox.scrollTop = messagesBox.scrollHeight;
  };
  
  // Call shared function with UI logging
  return await findBestRectangularStart(measurements, initialGuess, logFn);
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

    startingGuess = await findBestRectangularStartWithUI(projectedMeasurements);
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
          messagesBox.textContent += '\n[DEBUG] Sending $CALRESET command to reset calibration state';
          console.log('[DEBUG] Maximum retries reached - count:', lowFitnessRetryCount, 'fitness:', currentFitness);
          console.log('[DEBUG] Sending calibration event (good: false, maxRetriesReached: true)');
          console.log('[DEBUG] Sending $CALRESET command');
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
        console.log('[DEBUG] Calibration successful - fitness:', currentFitness);
        messagesBox.textContent += '\n[DEBUG] Sending calibration parameters to firmware...';
        
        sendCommand(`$/kinematics/MaslowKinematics/tlX=${tlxStr}`);
        sendCommand(`$/kinematics/MaslowKinematics/tlY=${tlyStr}`);
        sendCommand(`$/kinematics/MaslowKinematics/trX=${trxStr}`);
        sendCommand(`$/kinematics/MaslowKinematics/trY=${tryStr}`);
        sendCommand(`$/kinematics/MaslowKinematics/blX=${blxStr}`);
        sendCommand(`$/kinematics/MaslowKinematics/blY=${blyStr}`);
        sendCommand(`$/kinematics/MaslowKinematics/brX=${brxStr}`);
        sendCommand(`$/kinematics/MaslowKinematics/brY=${bryStr}`);

        console.log('[DEBUG] Sending calibration event (good: true, final: true)');
        sendCalibrationEvent({
          good: true,
          final: true,
          bestGuess: bestGuess
        }, true);
        
        console.log('[DEBUG] Refreshing settings and saving Maslow YAML');
        refreshSettings(current_setting_filter);
        saveMaslowYaml();

        messagesBox.textContent += '\nA command to save these values has been successfully sent for you. Please check for any error messages.';
        messagesBox.textContent += '\n[DEBUG] Calibration complete. Machine should now exit Home state.';
        console.log('[DEBUG] Calibration complete - fitness above threshold, not scheduling additional $CAL command');
        messagesBox.scrollTop = messagesBox.scrollHeight;

        initialGuess = bestGuess;
        initialGuess.fitness = 100000000;
      } else {
        console.log('[DEBUG] Fitness below threshold - fitness:', currentFitness, 'threshold:', acceptableCalibrationThreshold);
        console.log('[DEBUG] Sending calibration event (good: false, final: true)');
        sendCalibrationEvent({
          good: false,
          final: true,
          guess: bestGuess
        }, true);

        messagesBox.textContent += '\n[DEBUG] Restarting calibration with random perturbation...';
        console.log('[DEBUG] Adding random perturbation (±50mm) to initial guess');
        messagesBox.scrollTop = messagesBox.scrollHeight;

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
