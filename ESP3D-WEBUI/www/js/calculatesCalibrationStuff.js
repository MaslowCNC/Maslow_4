/**
 * Machine anchor geometry shared by the web UI.
 *
 * The calibration solver itself no longer lives here. All of the calibration
 * math (Levenberg-Marquardt anchor recompute, fitness evaluation, measurement
 * projection, retry strategies) now runs in the firmware, in
 * FluidNC/src/Maslow/Calibration.cpp. The web UI only kicks calibration off and
 * waits for the firmware's "Calibration complete" message, so the JS port was
 * dead weight in the bundle and has been removed.
 *
 * A JS copy of that math is still kept, for development and simulation only, in
 * docs/calibration-simulation/calibration-computation.js. It is not shipped to
 * the machine.
 *
 * What remains here is the anchor position store: maslow.js fills it in from the
 * firmware's kinematics config (see cfgDef in maslow.js) and toolpath-displayer.js
 * reads it to draw the frame. This file is concatenated ahead of both, so the
 * global exists before either of them touches it.
 */

var initialGuess = {
  tl: { x: 0, y: 2000 },
  tr: { x: 3000, y: 2000 },
  bl: { x: 0, y: 0 },
  br: { x: 3000, y: 0 },
  fitness: 100000000,
}
