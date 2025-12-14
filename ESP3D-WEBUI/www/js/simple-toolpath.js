// This file was translated from
//   https://github.com/cncjs/gcode-toolpath/blob/master/src/Toolpath.js
// by Babel (http://babeljs.io/repl), with preset "stage-2"
// The import and export statements were first removed from Toolpath.js

'use strict';

var _typeof = typeof Symbol === "function" && typeof Symbol.iterator === "symbol" ? function (obj) { return typeof obj; } : function (obj) { return obj && typeof Symbol === "function" && obj.constructor === Symbol && obj !== Symbol.prototype ? "symbol" : typeof obj; };

var _extends = Object.assign || function (target) { for (var i = 1; i < arguments.length; i++) { var source = arguments[i]; for (var key in source) { if (Object.prototype.hasOwnProperty.call(source, key)) { target[key] = source[key]; } } } return target; };

var _createClass = function () { function defineProperties(target, props) { for (var i = 0; i < props.length; i++) { var descriptor = props[i]; descriptor.enumerable = descriptor.enumerable || false; descriptor.configurable = true; if ("value" in descriptor) descriptor.writable = true; Object.defineProperty(target, descriptor.key, descriptor); } } return function (Constructor, protoProps, staticProps) { if (protoProps) defineProperties(Constructor.prototype, protoProps); if (staticProps) defineProperties(Constructor, staticProps); return Constructor; }; }();

function _classCallCheck(instance, Constructor) { if (!(instance instanceof Constructor)) { throw new TypeError("Cannot call a class as a function"); } }

// from in to mm
function in2mm(inches) {
    return inches * 25.4;
};

// noop
// var noop = function noop() {};

var Toolpath = function () {

    // @param {object} [options]
    // @param {object} [options.position]
    // @param {object} [options.modal]
    // @param {function} [options.addLine]
    // @param {function} [options.addArcCurve]
    function Toolpath(options) {
        var _this = this;

        _classCallCheck(this, Toolpath);

        this.g92offset = {
            x: 0,
            y: 0,
            z: 0
        };
        this.machineCoordinateMode = false;
        function offsetG92(pos) {
            // Skip G92 offset when in machine coordinate mode (G53)
            if (_this.machineCoordinateMode) {
                return {
                    x: pos.x,
                    y: pos.y,
                    z: pos.z,
                };
            }
            return {
                x: pos.x + _this.g92offset.x,
                y: pos.y + _this.g92offset.y,
                z: pos.z + _this.g92offset.z,
            };
        }
        function offsetAddLine(start, end) {
            _this.fn.addLine(_this.modal, offsetG92(start), offsetG92(end));
        }
        function offsetAddArcCurve(start, end, center, extraRotations) {
            _this.fn.addArcCurve(_this.modal, offsetG92(start), offsetG92(end), offsetG92(center), extraRotations);
        }
        this.position = {
            x: 0,
            y: 0,
            z: 0
        };
        this.modal = {
            // Motion Mode
            // G0, G1, G2, G3, G38.2, G38.3, G38.4, G38.5, G80
            motion: 'G0',

            // Coordinate System Select
            // G54, G55, G56, G57, G58, G59
            wcs: 'G54',

            // Plane Select
            // G17: XY-plane, G18: ZX-plane, G19: YZ-plane
            plane: 'G17',

            // Units Mode
            // G20: Inches, G21: Millimeters
            units: 'G21',

            // Distance Mode
            // G90: Absolute, G91: Relative
            distance: 'G90',

            // Arc IJK distance mode
            arc: 'G91.1',

            // Feed Rate Mode
            // G93: Inverse time mode, G94: Units per minute mode, G95: Units per rev mode
            feedrate: 'G94',

            // Cutter Radius Compensation
            cutter: 'G40',

            // Tool Length Offset
            // G43.1, G49
            tlo: 'G49',

            // Program Mode
            // M0, M1, M2, M30
            program: 'M0',

            // Spingle State
            // M3, M4, M5
            spindle: 'M5',

            // Coolant State
            // M7, M8, M9
            coolant: 'M9', // 'M7', 'M8', 'M7,M8', or 'M9'

            // Tool Select
            tool: 0
        };
        this.handlers = {
            // G0: Rapid Linear Move
            'G0': function G0(params) {
                if (_this.modal.motion !== 'G0') {
                    _this.setModal({ motion: 'G0' });
                }

                var v1 = {
                    x: _this.position.x,
                    y: _this.position.y,
                    z: _this.position.z
                };
                var v2 = {
                    x: _this.translateX(params.X, _this.isRelativeDistance()),
                    y: _this.translateY(params.Y, _this.isRelativeDistance()),
                    z: _this.translateZ(params.Z, _this.isRelativeDistance())
                };
                var targetPosition = { x: v2.x, y: v2.y, z: v2.z };

                offsetAddLine(v1, v2);

                // Update position
                _this.setPosition(targetPosition.x, targetPosition.y, targetPosition.z);
            },
            // G1: Linear Move
            // Usage
            //   G1 Xnnn Ynnn Znnn Ennn Fnnn Snnn
            // Parameters
            //   Xnnn The position to move to on the X axis
            //   Ynnn The position to move to on the Y axis
            //   Znnn The position to move to on the Z axis
            //   Fnnn The feedrate per minute of the move between the starting point and ending point (if supplied)
            //   Snnn Flag to check if an endstop was hit (S1 to check, S0 to ignore, S2 see note, default is S0)
            // Examples
            //   G1 X12 (move to 12mm on the X axis)
            //   G1 F1500 (Set the feedrate to 1500mm/minute)
            //   G1 X90.6 Y13.8 E22.4 (Move to 90.6mm on the X axis and 13.8mm on the Y axis while extruding 22.4mm of material)
            //
            'G1': function G1(params) {
                if (_this.modal.motion !== 'G1') {
                    _this.setModal({ motion: 'G1' });
                }

                var v1 = {
                    x: _this.position.x,
                    y: _this.position.y,
                    z: _this.position.z
                };
                var v2 = {
                    x: _this.translateX(params.X, _this.isRelativeDistance()),
                    y: _this.translateY(params.Y, _this.isRelativeDistance()),
                    z: _this.translateZ(params.Z, _this.isRelativeDistance())
                };
                var targetPosition = { x: v2.x, y: v2.y, z: v2.z };

                offsetAddLine(v1, v2);

                // Update position
                _this.setPosition(targetPosition.x, targetPosition.y, targetPosition.z);
            },
            // G2 & G3: Controlled Arc Move
            // Usage
            //   G2 Xnnn Ynnn Innn Jnnn Ennn Fnnn (Clockwise Arc)
            //   G3 Xnnn Ynnn Innn Jnnn Ennn Fnnn (Counter-Clockwise Arc)
            // Parameters
            //   Xnnn The position to move to on the X axis
            //   Ynnn The position to move to on the Y axis
            //   Innn The point in X space from the current X position to maintain a constant distance from
            //   Jnnn The point in Y space from the current Y position to maintain a constant distance from
            //   Fnnn The feedrate per minute of the move between the starting point and ending point (if supplied)
            // Examples
            //   G2 X90.6 Y13.8 I5 J10 E22.4 (Move in a Clockwise arc from the current point to point (X=90.6,Y=13.8),
            //   with a center point at (X=current_X+5, Y=current_Y+10), extruding 22.4mm of material between starting and stopping)
            //   G3 X90.6 Y13.8 I5 J10 E22.4 (Move in a Counter-Clockwise arc from the current point to point (X=90.6,Y=13.8),
            //   with a center point at (X=current_X+5, Y=current_Y+10), extruding 22.4mm of material between starting and stopping)
            // Referring
            //   http://linuxcnc.org/docs/2.5/html/gcode/gcode.html#sec:G2-G3-Arc
            //   https://github.com/grbl/grbl/issues/236
            'G2': function G2(params) {
                if (_this.modal.motion !== 'G2') {
                    _this.setModal({ motion: 'G2' });
                }

                let v1 = _this.position;
                let v2 = {
                    x: _this.translateX(params.X, _this.isRelativeDistance()),
                    y: _this.translateY(params.Y, _this.isRelativeDistance()),
                    z: _this.translateZ(params.Z, _this.isRelativeDistance())
                };
                let v0 = { // fixed point
                    x: _this.translateI(params.I),
                    y: _this.translateJ(params.J),
                    z: _this.translateK(params.K)
                };
                let isClockwise = true;
                let targetPosition = { x: v2.x, y: v2.y, z: v2.z };

                if (_this.isXYPlane()) {
                    let _ref1 = [v1.x, v1.y, v1.z]; // XY-plane
                    v1.x = _ref1[0];
                    v1.y = _ref1[1];
                    v1.z = _ref1[2];
                    let _ref2 = [v2.x, v2.y, v2.z];
                    v2.x = _ref2[0];
                    v2.y = _ref2[1];
                    v2.z = _ref2[2];
                    let _ref0 = [v0.x, v0.y, v0.z];
                    v0.x = _ref0[0];
                    v0.y = _ref0[1];
                    v0.z = _ref0[2];
                } else if (_this.isZXPlane()) {
                    let _ref1 = [v1.z, v1.x, v1.y]; // ZX-plane
                    v1.x = _ref1[0];
                    v1.y = _ref1[1];
                    v1.z = _ref1[2];
                    let _ref2 = [v2.z, v2.x, v2.y];
                    v2.x = _ref2[0];
                    v2.y = _ref2[1];
                    v2.z = _ref2[2];
                    let _ref0 = [v0.z, v0.x, v0.y];
                    v0.x = _ref0[0];
                    v0.y = _ref0[1];
                    v0.z = _ref0[2];
                } else if (_this.isYZPlane()) {
                    let _ref1 = [v1.y, v1.z, v1.x]; // YZ-plane
                    v1.x = _ref1[0];
                    v1.y = _ref1[1];
                    v1.z = _ref1[2];
                    let _ref2 = [v2.y, v2.z, v2.x];
                    v2.x = _ref2[0];
                    v2.y = _ref2[1];
                    v2.z = _ref2[2];
                    let _ref0 = [v0.y, v0.z, v0.x];
                    v0.x = _ref0[0];
                    v0.y = _ref0[1];
                    v0.z = _ref0[2];
                } else {
                    console.error('The plane mode is invalid', _this.modal.plane);
                    return;
                }

                if (params.R) {
                    let radius = _this.translateR(Number(params.R) || 0);
                    let x = v2.x - v1.x;
                    let y = v2.y - v1.y;
                    let distance = Math.hypot(x, y);
                    let height = Math.sqrt(4 * radius * radius - x * x - y * y) / 2;

                    if (isClockwise) {
                        height = -height;
                    }
                    if (radius < 0) {
                        height = -height;
                    }

                    let offsetX = x / 2 - y / distance * height;
                    let offsetY = y / 2 + x / distance * height;

                    v0.x = v1.x + offsetX;
                    v0.y = v1.y + offsetY;
                }

                offsetAddArcCurve(v1, v2, v0, params.P ? params.P : 0);

                // Update position
                _this.setPosition(targetPosition.x, targetPosition.y, targetPosition.z);
            },
            'G3': function G3(params) {
                if (_this.modal.motion !== 'G3') {
                    _this.setModal({ motion: 'G3' });
                }

                let v1 = _this.position;
                let v2 = {
                    x: _this.translateX(params.X, _this.isRelativeDistance()),
                    y: _this.translateY(params.Y, _this.isRelativeDistance()),
                    z: _this.translateZ(params.Z, _this.isRelativeDistance())
                };
                let v0 = { // fixed point
                    x: _this.translateI(params.I),
                    y: _this.translateJ(params.J),
                    z: _this.translateK(params.K)
                };
                let isClockwise = false;
                let targetPosition = { x: v2.x, y: v2.y, z: v2.z };

                if (_this.isXYPlane()) {
                    let _ref1 = [v1.x, v1.y, v1.z]; // XY-plane

                    v1.x = _ref1[0];
                    v1.y = _ref1[1];
                    v1.z = _ref1[2];
                    let _ref2 = [v2.x, v2.y, v2.z];
                    v2.x = _ref2[0];
                    v2.y = _ref2[1];
                    v2.z = _ref2[2];
                    let _ref0 = [v0.x, v0.y, v0.z];
                    v0.x = _ref0[0];
                    v0.y = _ref0[1];
                    v0.z = _ref0[2];
                } else if (_this.isZXPlane()) {
                    let _ref1 = [v1.z, v1.x, v1.y]; // ZX-plane
                    v1.x = _ref1[0];
                    v1.y = _ref1[1];
                    v1.z = _ref1[2];
                    let _ref2 = [v2.z, v2.x, v2.y];
                    v2.x = _ref2[0];
                    v2.y = _ref2[1];
                    v2.z = _ref2[2];
                    let _ref0 = [v0.z, v0.x, v0.y];
                    v0.x = _ref0[0];
                    v0.y = _ref0[1];
                    v0.z = _ref0[2];
                } else if (_this.isYZPlane()) {
                    let _ref1 = [v1.y, v1.z, v1.x]; // YZ-plane
                    v1.x = _ref1[0];
                    v1.y = _ref1[1];
                    v1.z = _ref1[2];
                    let _ref2 = [v2.y, v2.z, v2.x];
                    v2.x = _ref2[0];
                    v2.y = _ref2[1];
                    v2.z = _ref2[2];
                    let _ref0 = [v0.y, v0.z, v0.x];
                    v0.x = _ref0[0];
                    v0.y = _ref0[1];
                    v0.z = _ref0[2];
                } else {
                    console.error('The plane mode is invalid', _this.modal.plane);
                    return;
                }

                if (params.R) {
                    let radius = _this.translateR(Number(params.R) || 0);
                    let x = v2.x - v1.x;
                    let y = v2.y - v1.y;
                    let distance = Math.hypot(x, y);
                    let height = Math.sqrt(4 * radius * radius - x * x - y * y) / 2;

                    if (isClockwise) {
                        height = -height;
                    }
                    if (radius < 0) {
                        height = -height;
                    }

                    let offsetX = x / 2 - y / distance * height;
                    let offsetY = y / 2 + x / distance * height;

                    v0.x = v1.x + offsetX;
                    v0.y = v1.y + offsetY;
                }

                offsetAddArcCurve(v1, v2, v0, params.P ? params.P : 0);

                // Update position
                _this.setPosition(targetPosition.x, targetPosition.y, targetPosition.z);
            },
            // G4: Dwell
            // Parameters
            //   Pnnn Time to wait, in milliseconds
            // Example
            //   G4 P200
            'G4': function G4(params) {},
            // G10: Coordinate System Data Tool and Work Offset Tables
            'G10': function G10(params) {},
            // G17..19: Plane Selection
            // G17: XY (default)
            'G17': function G17(params) {
                if (_this.modal.plane !== 'G17') {
                    _this.setModal({ plane: 'G17' });
                }
            },
            // G18: XZ
            'G18': function G18(params) {
                if (_this.modal.plane !== 'G18') {
                    _this.setModal({ plane: 'G18' });
                }
            },
            // G19: YZ
            'G19': function G19(params) {
                if (_this.modal.plane !== 'G19') {
                    _this.setModal({ plane: 'G19' });
                }
            },
            // G20: Use inches for length units
            'G20': function G20(params) {
                if (_this.modal.units !== 'G20') {
                    _this.setModal({ units: 'G20' });
                }
            },
            // G21: Use millimeters for length units
            'G21': function G21(params) {
                if (_this.modal.units !== 'G21') {
                    _this.setModal({ units: 'G21' });
                }
            },
            // G38.x: Straight Probe
            // G38.2: Probe toward workpiece, stop on contact, signal error if failure
            'G38.2': function G382(params) {
                if (_this.modal.motion !== 'G38.2') {
                    _this.setModal({ motion: 'G38.2' });
                }
            },
            // G38.3: Probe toward workpiece, stop on contact
            'G38.3': function G383(params) {
                if (_this.modal.motion !== 'G38.3') {
                    _this.setModal({ motion: 'G38.3' });
                }
            },
            // G38.4: Probe away from workpiece, stop on loss of contact, signal error if failure
            'G38.4': function G384(params) {
                if (_this.modal.motion !== 'G38.4') {
                    _this.setModal({ motion: 'G38.4' });
                }
            },
            // G38.5: Probe away from workpiece, stop on loss of contact
            'G38.5': function G385(params) {
                if (_this.modal.motion !== 'G38.5') {
                    _this.setModal({ motion: 'G38.5' });
                }
            },
            // G43.1: Tool Length Offset
            'G43.1': function G431(params) {
                if (_this.modal.tlo !== 'G43.1') {
                    _this.setModal({ tlo: 'G43.1' });
                }
            },
            // G49: No Tool Length Offset
            'G49': function G49() {
                if (_this.modal.tlo !== 'G49') {
                    _this.setModal({ tlo: 'G49' });
                }
            },
            // G53: Move in Machine Coordinates (non-modal)
            // This is a one-shot command that causes the next move to be
            // interpreted in machine coordinates, ignoring work coordinate
            // system offsets (G54-G59) and G92 offsets
            'G53': function G53(params) {
                _this.machineCoordinateMode = true;
            },
            // G54..59: Coordinate System Select
            'G54': function G54() {
                if (_this.modal.wcs !== 'G54') {
                    _this.setModal({ wcs: 'G54' });
                }
            },
            'G55': function G55() {
                if (_this.modal.wcs !== 'G55') {
                    _this.setModal({ wcs: 'G55' });
                }
            },
            'G56': function G56() {
                if (_this.modal.wcs !== 'G56') {
                    _this.setModal({ wcs: 'G56' });
                }
            },
            'G57': function G57() {
                if (_this.modal.wcs !== 'G57') {
                    _this.setModal({ wcs: 'G57' });
                }
            },
            'G58': function G58() {
                if (_this.modal.wcs !== 'G58') {
                    _this.setModal({ wcs: 'G58' });
                }
            },
            'G59': function G59() {
                if (_this.modal.wcs !== 'G59') {
                    _this.setModal({ wcs: 'G59' });
                }
            },
            // G80: Cancel Canned Cycle
            'G80': function G80() {
                if (_this.modal.motion !== 'G80') {
                    _this.setModal({ motion: 'G80' });
                }
            },
            // G90: Set to Absolute Positioning
            // Example
            //   G90
            // All coordinates from now on are absolute relative to the origin of the machine.
            'G90': function G90() {
                if (_this.modal.distance !== 'G90') {
                    _this.setModal({ distance: 'G90' });
                }
            },
            // G91: Set to Relative Positioning
            // Example
            //   G91
            // All coordinates from now on are relative to the last position.
            'G91': function G91() {
                if (_this.modal.distance !== 'G91') {
                    _this.setModal({ distance: 'G91' });
                }
            },
            // G92: Set Position
            // Parameters
            //   This command can be used without any additional parameters.
            //   Xnnn new X axis position
            //   Ynnn new Y axis position
            //   Znnn new Z axis position
            // Example
            //   G92 X10
            // Allows programming of absolute zero point, by reseting the current position to the params specified.
            // This would set the machine's X coordinate to 10. No physical motion will occur.
            // A G92 without coordinates will reset all axes to zero.
            'G92': function G92(params) {
                // A G92 without coordinates will reset all axes to zero.
                if (params.X === undefined && params.Y === undefined && params.Z === undefined) {
                    _this.position.x += _this.g92offset.x;
                    _this.g92offset.x = 0;
                    _this.position.y += _this.g92offset.y;
                    _this.g92offset.y = 0;
                    _this.position.z += _this.g92offset.z;
                    _this.g92offset.z = 0;
                } else {
		    // The calls to translateX/Y/Z() below are necessary for inch/mm conversion
		    // params.X/Y/Z must be interpreted as absolute positions, hence the "false"
                    if (params.X != undefined) {
			var xmm = _this.translateX(params.X, false);
                        _this.g92offset.x += _this.position.x - xmm;
                        _this.position.x = xmm;
                    }
                    if (params.Y != undefined) {
			var ymm = _this.translateY(params.Y, false);
                        _this.g92offset.y += _this.position.y - ymm;
                        _this.position.y = ymm;
                    }
                    if (params.Z != undefined) {
			var zmm = _this.translateX(params.Z, false);
                        _this.g92offset.z += _this.position.z - zmm;
                        _this.position.z = zmm;
                    }
                }
            },
            // G92.1: Cancel G92 offsets
            // Parameters
            //   none
            'G92.1': function G921(params) {
                    _this.position.x += _this.g92offset.x;
                    _this.g92offset.x = 0;
                    _this.position.y += _this.g92offset.y;
                    _this.g92offset.y = 0;
                    _this.position.z += _this.g92offset.z;
                    _this.g92offset.z = 0;
            },
            // G93: Inverse Time Mode
            // In inverse time feed rate mode, an F word means the move should be completed in
            // [one divided by the F number] minutes.
            // For example, if the F number is 2.0, the move should be completed in half a minute.
            'G93': function G93() {
                if (_this.modal.feedmode !== 'G93') {
                    _this.setModal({ feedmode: 'G93' });
                }
            },
            // G94: Units per Minute Mode
            // In units per minute feed rate mode, an F word on the line is interpreted to mean the
            // controlled point should move at a certain number of inches per minute,
            // millimeters per minute or degrees per minute, depending upon what length units
            // are being used and which axis or axes are moving.
            'G94': function G94() {
                if (_this.modal.feedmode !== 'G94') {
                    _this.setModal({ feedmode: 'G94' });
                }
            },
            // G94: Units per Revolution Mode
            // In units per rev feed rate mode, an F word on the line is interpreted to mean the
            // controlled point should move at a certain number of inches per spindle revolution,
            // millimeters per spindle revolution or degrees per spindle revolution, depending upon
            // what length units are being used and which axis or axes are moving.
            'G95': function G95() {
                if (_this.modal.feedmode !== 'G95') {
                    _this.setModal({ feedmode: 'G95' });
                }
            },
            // M0: Program Pause
            'M0': function M0() {
                if (_this.modal.program !== 'M0') {
                    _this.setModal({ program: 'M0' });
                }
            },
            // M1: Program Pause
            'M1': function M1() {
                if (_this.modal.program !== 'M1') {
                    _this.setModal({ program: 'M1' });
                }
            },
            // M2: Program End
            'M2': function M2() {
                if (_this.modal.program !== 'M2') {
                    _this.setModal({ program: 'M2' });
                }
            },
            // M30: Program End
            'M30': function M30() {
                if (_this.modal.program !== 'M30') {
                    _this.setModal({ program: 'M30' });
                }
            },
            // Spindle Control
            // M3: Start the spindle turning clockwise at the currently programmed speed
            'M3': function M3(params) {
                if (_this.modal.spindle !== 'M3') {
                    _this.setModal({ spindle: 'M3' });
                }
            },
            // M4: Start the spindle turning counterclockwise at the currently programmed speed
            'M4': function M4(params) {
                if (_this.modal.spindle !== 'M4') {
                    _this.setModal({ spindle: 'M4' });
                }
            },
            // M5: Stop the spindle from turning
            'M5': function M5() {
                if (_this.modal.spindle !== 'M5') {
                    _this.setModal({ spindle: 'M5' });
                }
            },
            // M6: Tool Change
            'M6': function M6(params) {
                if (params && params.T !== undefined) {
                    _this.setModal({ tool: params.T });
                }
            },
            // Coolant Control
            // M7: Turn mist coolant on
            'M7': function M7() {
                var coolants = _this.modal.coolant.split(',');
                if (coolants.indexOf('M7') >= 0) {
                    return;
                }

                _this.setModal({
                    coolant: coolants.indexOf('M8') >= 0 ? 'M7,M8' : 'M7'
                });
            },
            // M8: Turn flood coolant on
            'M8': function M8() {
                var coolants = _this.modal.coolant.split(',');
                if (coolants.indexOf('M8') >= 0) {
                    return;
                }

                _this.setModal({
                    coolant: coolants.indexOf('M7') >= 0 ? 'M7,M8' : 'M8'
                });
            },
            // M9: Turn all coolant off
            'M9': function M9() {
                if (_this.modal.coolant !== 'M9') {
                    _this.setModal({ coolant: 'M9' });
                }
            },
            'T': function T(tool) {
                if (tool !== undefined) {
                    _this.setModal({ tool: tool });
                }
            }
        };

        var _options = _extends({}, options),
            position = _options.position,
            modal = _options.modal,
            _options$addLine = _options.addLine,
            addLine = _options$addLine === undefined ? noop : _options$addLine,
            _options$addArcCurve = _options.addArcCurve,
            addArcCurve = _options$addArcCurve === undefined ? noop : _options$addArcCurve;

        // Position


        if (position) {
            var _position = _extends({}, position),
                x = _position.x,
                y = _position.y,
                z = _position.z;

            this.setPosition(x, y, z);
        }
        this.g92offset.x = this.g92offset.y = this.g92offset.z = 0;

        // Modal
        var nextModal = {};
        Object.keys(_extends({}, modal)).forEach(function (key) {
            if (!Object.prototype.hasOwnProperty.call(_this.modal, key)) {
                return;
            }
            nextModal[key] = modal[key];
        });
        this.setModal(nextModal);

        this.fn = { addLine: addLine, addArcCurve: addArcCurve };

        var toolpath = new Interpreter({ handlers: this.handlers });
        toolpath.getPosition = function () {
            return _extends({}, _this.position);
        };
        toolpath.getModal = function () {
            return _extends({}, _this.modal);
        };
        toolpath.setPosition = function () {
            return _this.setPosition.apply(_this, arguments);
        };
        toolpath.setModal = function (modal) {
            return _this.setModal(modal);
        };

        // Override loadFromLinesSync to reset machineCoordinateMode at the start of each line
        // This ensures G53 only affects axis words on the same line, matching firmware behavior
        toolpath.loadFromLinesSync = function(lines) {
            for (var i = 0; i < lines.length; ++i) {
                var line = lines[i].trim();
                if (line.length !== 0) {
                    // Reset machine coordinate mode at the start of each line
                    // G53 is non-modal and only affects the line it appears on
                    _this.machineCoordinateMode = false;
                    // Parse and interpret this line (note: parseLine and interpret must be globally available)
                    interpret(this, parseLine(line, {}));
                }
            }
        };

        return toolpath;
    }

    _createClass(Toolpath, [{
        key: 'setModal',
        value: function setModal(modal) {
            this.modal = _extends({}, this.modal, modal);
            return this.modal;
        }
    }, {
        key: 'isMetricUnits',
        value: function isMetricUnits() {
            // mm
            return this.modal.units === 'G21';
        }
    }, {
        key: 'isImperialUnits',
        value: function isImperialUnits() {
            // inches
            return this.modal.units === 'G20';
        }
    }, {
        key: 'isAbsoluteDistance',
        value: function isAbsoluteDistance() {
            return this.modal.distance === 'G90';
        }
    }, {
        key: 'isRelativeDistance',
        value: function isRelativeDistance() {
            return this.modal.distance === 'G91';
        }
    }, {
        key: 'isXYPlane',
        value: function isXYPlane() {
            return this.modal.plane === 'G17';
        }
    }, {
        key: 'isZXPlane',
        value: function isZXPlane() {
            return this.modal.plane === 'G18';
        }
    }, {
        key: 'isYZPlane',
        value: function isYZPlane() {
            return this.modal.plane === 'G19';
        }
    }, {
        key: 'setPosition',
        value: function setPosition() {
            for (var _len = arguments.length, pos = Array(_len), _key = 0; _key < _len; _key++) {
                pos[_key] = arguments[_key];
            }

            if (_typeof(pos[0]) === 'object') {
                var _pos$ = _extends({}, pos[0]),
                    x = _pos$.x,
                    y = _pos$.y,
                    z = _pos$.z;

                this.position.x = typeof x === 'number' ? x : this.position.x;
                this.position.y = typeof y === 'number' ? y : this.position.y;
                this.position.z = typeof z === 'number' ? z : this.position.z;
            } else {
                var _x = pos[0],
                    _y = pos[1],
                    _z = pos[2];

                this.position.x = typeof _x === 'number' ? _x : this.position.x;
                this.position.y = typeof _y === 'number' ? _y : this.position.y;
                this.position.z = typeof _z === 'number' ? _z : this.position.z;
            }
        }
    }, {
        key: 'translatePosition',
        value: function translatePosition(position, newPosition, relative) {
            if (newPosition == undefined) {
                return position;
            }
            newPosition = this.isImperialUnits() ? in2mm(newPosition) : newPosition;
            newPosition = Number(newPosition);
            if (Number.isNaN(newPosition)) {
                return position;
            }
            return (!!relative) ? position + newPosition : newPosition;
        }
    }, {
        key: 'translateX',
        value: function translateX(x, relative) {
            return this.translatePosition(this.position.x, x, relative);
        }
    }, {
        key: 'translateY',
        value: function translateY(y, relative) {
            return this.translatePosition(this.position.y, y, relative);
        }
    }, {
        key: 'translateZ',
        value: function translateZ(z, relative) {
            return this.translatePosition(this.position.z, z, relative);
        }
    }, {
        key: 'translateI',
        value: function translateI(i) {
            return this.translateX(i, true);
        }
    }, {
        key: 'translateJ',
        value: function translateJ(j) {
            return this.translateY(j, true);
        }
    }, {
        key: 'translateK',
        value: function translateK(k) {
            return this.translateZ(k, true);
        }
    }, {
        key: 'translateR',
        value: function translateR(r) {
            r = Number(r);
            if (Number.isNaN(r)) {
                return 0;
            }
            return this.isImperialUnits() ? in2mm(r) : r;
        }
    }]);

    return Toolpath;
}();
