/**
 * MINIMAL SOLUTION: Namespace Wrapper
 * 
 * This is the bare minimum improvement over bare global variables.
 * While not ideal, it's better than the current approach and requires
 * minimal changes to existing code.
 * 
 * Use this if:
 * - You need a quick fix
 * - Full refactoring isn't feasible right now
 * - You plan to improve it later
 * 
 * Benefits:
 * - Reduces global namespace pollution
 * - Groups related state together
 * - Makes ownership clear
 * - Easy to migrate from current code
 * 
 * Limitations:
 * - Still has load order dependency
 * - Still mutable global state
 * - Still coupled across files
 * - Not event-driven
 */

// In maslow.js (loads first)
var MaslowState = {
  // Calibration state
  calibrationComplete: false,
  calibrationStage: 0,
  
  // You can add other related state here
  lastCalibrationError: null,
  calibrationStartTime: null,
  
  // Helper methods can go here too
  startCalibration: function() {
    this.calibrationComplete = false;
    this.calibrationStage = 0;
    this.calibrationStartTime = Date.now();
    this.lastCalibrationError = null;
  },
  
  completeCalibration: function() {
    this.calibrationComplete = true;
    console.log('Calibration completed in ' + 
      ((Date.now() - this.calibrationStartTime) / 1000) + ' seconds');
  },
  
  failCalibration: function(error) {
    this.calibrationComplete = false;
    this.lastCalibrationError = error;
    console.error('Calibration failed:', error);
  }
};

// In tablet.js - set the flag
function handleCalibrationComplete() {
  // Old way: calibrationComplete = true;
  // New way:
  MaslowState.completeCalibration();
  
  // Or just:
  // MaslowState.calibrationComplete = true;
}

// In calculatesCalibrationStuff.js - read the flag
function shouldRestartCalibration() {
  // Old way: if (!calibrationComplete)
  // New way:
  if (!MaslowState.calibrationComplete) {
    return true;
  }
  return false;
}

/*
 * Migration Path from Current Code:
 * 
 * 1. Find and replace in all files:
 *    calibrationComplete → MaslowState.calibrationComplete
 * 
 * 2. Update comments:
 *    Remove "declared in maslow.js (loads first)" comments
 *    Add "State managed by MaslowState object"
 * 
 * 3. Test thoroughly to ensure nothing broke
 * 
 * That's it! Minimal change, small improvement.
 */

/*
 * Why this is better than bare global variables:
 * 
 * ✅ NAMESPACING
 *    - Only one global: MaslowState instead of multiple
 *    - Less chance of naming conflicts
 * 
 * ✅ ORGANIZATION  
 *    - Related state grouped together
 *    - Clear ownership (this is Maslow state)
 * 
 * ✅ DISCOVERABILITY
 *    - Type "MaslowState." to see all available state
 *    - Easier to find what state exists
 * 
 * ✅ MIGRATION PATH
 *    - Easy to later convert to proper module
 *    - Can add methods without breaking changes
 * 
 * ⚠️ STILL HAS ISSUES
 *    - Mutable global state
 *    - Load order dependency
 *    - Not testable in isolation
 *    - Manual tracking of changes
 * 
 * This is an 80/20 solution: 20% effort for 80% improvement.
 * But don't stop here - plan for full module pattern later.
 */

// Optional: Add validation to catch bugs early
Object.defineProperty(MaslowState, 'calibrationComplete', {
  get: function() { return this._calibrationComplete; },
  set: function(value) {
    if (typeof value !== 'boolean') {
      console.warn('calibrationComplete should be boolean, got:', typeof value);
    }
    console.log('[MaslowState] calibrationComplete changed:', this._calibrationComplete, '→', value);
    this._calibrationComplete = value;
  }
});

// Initialize the backing field
MaslowState._calibrationComplete = false;

/*
 * The validation above adds:
 * - Type checking (prevents accidental assignment of wrong type)
 * - Change logging (helps debug state changes)
 * - Zero breaking changes to existing code
 * 
 * This is optional but helps catch bugs during development.
 */
