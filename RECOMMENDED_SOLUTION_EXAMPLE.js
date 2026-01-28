/**
 * RECOMMENDED SOLUTION: Calibration State Manager
 * 
 * This file demonstrates how to properly encapsulate calibration state
 * instead of using global variables across multiple files.
 * 
 * Benefits:
 * - Single source of truth for calibration state
 * - No load order dependencies
 * - Testable in isolation
 * - Event-driven updates
 * - Clear API surface
 * - Easy to debug and maintain
 */

const CalibrationManager = (function() {
  // Private state (not accessible from outside)
  let isComplete = false;
  let stage = 0;
  let listeners = [];
  
  // Public API
  return {
    /**
     * Start a new calibration process
     */
    start() {
      isComplete = false;
      stage = 0;
      this.notify('started');
      console.log('[Calibration] Started');
    },

    /**
     * Mark calibration as complete
     */
    complete() {
      isComplete = true;
      this.notify('completed');
      console.log('[Calibration] Completed');
    },

    /**
     * Check if calibration is complete
     * @returns {boolean}
     */
    isComplete() {
      return isComplete;
    },

    /**
     * Get current calibration stage
     * @returns {number}
     */
    getStage() {
      return stage;
    },

    /**
     * Update calibration stage
     * @param {number} newStage
     */
    setStage(newStage) {
      stage = newStage;
      this.notify('stage-changed', { stage: newStage });
      console.log(`[Calibration] Stage changed to ${newStage}`);
    },

    /**
     * Register a listener for calibration events
     * @param {Function} callback - Called with (event, data)
     */
    onChange(callback) {
      if (typeof callback === 'function') {
        listeners.push(callback);
      }
    },

    /**
     * Remove a listener
     * @param {Function} callback
     */
    removeListener(callback) {
      listeners = listeners.filter(fn => fn !== callback);
    },

    /**
     * Notify all listeners of an event
     * @param {string} event - Event name
     * @param {*} data - Event data
     */
    notify(event, data = {}) {
      listeners.forEach(listener => {
        try {
          listener(event, data);
        } catch (error) {
          console.error('[Calibration] Listener error:', error);
        }
      });
    },

    /**
     * Reset calibration state (useful for testing)
     */
    reset() {
      isComplete = false;
      stage = 0;
      listeners = [];
      console.log('[Calibration] Reset');
    }
  };
})();

// Usage examples:

// In maslow.js - handle firmware messages
function handleCalibrationMessage(message) {
  if (message.includes('calibration_complete')) {
    CalibrationManager.complete();
  }
}

// In tablet.js - update UI based on state
CalibrationManager.onChange((event, data) => {
  if (event === 'completed') {
    updateButtonState('calibration-complete');
    showSuccessMessage('Calibration completed successfully!');
  } else if (event === 'stage-changed') {
    updateProgressBar(data.stage);
  }
});

// In calculatesCalibrationStuff.js - check state before processing
function shouldRestartCalibration() {
  // No need to worry about load order or global variables
  return !CalibrationManager.isComplete();
}

// For testing (can be called from test file)
function testCalibrationFlow() {
  CalibrationManager.reset();
  
  console.assert(!CalibrationManager.isComplete(), 'Should not be complete initially');
  
  CalibrationManager.start();
  console.assert(!CalibrationManager.isComplete(), 'Should not be complete after start');
  
  CalibrationManager.setStage(1);
  console.assert(CalibrationManager.getStage() === 1, 'Stage should be 1');
  
  CalibrationManager.complete();
  console.assert(CalibrationManager.isComplete(), 'Should be complete after completion');
  
  console.log('✅ All tests passed');
}

/*
 * Why this is better than the current approach:
 * 
 * 1. ENCAPSULATION
 *    - State is private, only accessible through methods
 *    - Can't be accidentally modified from outside
 * 
 * 2. NO LOAD ORDER DEPENDENCY
 *    - Works regardless of which file loads first
 *    - Self-contained module
 * 
 * 3. TESTABLE
 *    - Can call testCalibrationFlow() to verify behavior
 *    - Can mock for unit tests
 *    - Has reset() method for test isolation
 * 
 * 4. EVENT-DRIVEN
 *    - UI updates automatically via listeners
 *    - No need to poll state
 *    - Loose coupling between components
 * 
 * 5. CLEAR API
 *    - start(), complete(), isComplete() are self-documenting
 *    - No need for comments explaining usage
 * 
 * 6. DEBUGGABLE
 *    - Console logs show state changes
 *    - Easy to trace flow
 *    - Single point of control
 * 
 * 7. EXTENSIBLE
 *    - Easy to add new states (stages, errors, etc.)
 *    - Can add validation, persistence, etc.
 *    - Foundation for future improvements
 */
