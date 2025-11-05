// Copyright (c) 2024 - Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "MaslowKinematics.h"

#include "../Machine/MachineConfig.h"
#include "../Limits.h"
#include "../Machine/Homing.h"
#include "../Protocol.h"
#include "../System.h"
#include <cstring>
#include "../NutsBolts.h"
#include "../MotionControl.h"
#include <cmath>
#include <algorithm>
#include "../Maslow/Maslow.h"

/*
Default configuration for Maslow CNC:

kinematics:
  MaslowKinematics:
    tlX: -27.6
    tlY: 2064.9
    tlZ: 100.0
    trX: 2924.3
    trY: 2066.5
    trZ: 56.0
    blX: 0.0
    blY: 0.0
    blZ: 34.0
    brX: 2953.2
    brY: 0.0
    brZ: 78.0
    beltEndExtension: 30.0
    armLength: 123.4
    tlSoftArmExtension: 0.0
    trSoftArmExtension: 0.0
    blSoftArmExtension: 0.0
    brSoftArmExtension: 0.0
    spoilboardThickness: 0.0
    workThickness: 0.0
    maxSegmentLength: 5.0
    fixedZ: false

This implements the cable-driven kinematics for the Maslow CNC system.
The system has 5 axes:
- A, B, C, D (belt motors: TL, TR, BL, BR mapped to motors 0-3)
- Z (cartesian Z coordinate mapped to motor 4)

The spoilboardThickness and workThickness parameters account for material thickness
placed on the cutting surface. Both values are added to all anchor point Z coordinates
during belt length calculations, effectively raising the reference height for all belts.

The soft arm extension parameters (tlSoftArmExtension, trSoftArmExtension, blSoftArmExtension,
brSoftArmExtension) account for flexible extensions that can move with Z changes. Unlike the
beltEndExtension and armLength which are subtracted in the XY plane projection, soft arm
extensions are added to the total 3D belt length after computing the angled distance. Each
arm can have a different soft arm extension length.

The maxSegmentLength parameter controls belt length synchronization during long moves.
Moves longer than this distance (in mm) will be automatically segmented to ensure
correct belt lengths are computed at intermediate points, preventing belt slack.
*/

namespace Kinematics {

    // Global pointer to the current MaslowKinematics instance
    static MaslowKinematics* g_maslowKinematics = nullptr;

    void MaslowKinematics::init() {
        calculateCenter();
        g_maslowKinematics = this;  // Set global pointer for access
        init_position();
    }

    void MaslowKinematics::init_position() {
        auto n_axis = config->_axes->_numberAxis;
        for (size_t axis = 0; axis < n_axis; axis++) {
            set_motor_steps(axis, 0);  // Set to zeros
        }
    }

    void MaslowKinematics::calculateCenter() {
        // Calculate center point of the frame for coordinate system transformation
        // Find the intersection of the diagonals of the rectangle (proper geometric center)
        float A  = (_trY - _blY) / (_trX - _blX);
        float B  = (_brY - _tlY) / (_brX - _tlX);
        _centerX = (_brY - (B * _brX) + (A * _trX) - _trY) / (A - B);
        _centerY = A * (_centerX - _trX) + _trY;

        //log_info("Maslow center calculated: X=" << _centerX << " Y=" << _centerY);
    }

    bool MaslowKinematics::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        auto n_axis = config->_axes->_numberAxis;

        // Ensure we have the expected number of axes (5: A, B, C, D, Z)
        if (n_axis < 5) {
            log_error("MaslowKinematics requires at least 5 axes");
            return false;
        }

        // Calculate cartesian distance of the move (X,Y,Z only)
        float cartesian_distance = vector_distance(target, position, 3);  // Only X,Y,Z for cartesian

        // Check if this is a Z-only move by examining X,Y changes
        float xy_distance    = sqrt((target[X_AXIS] - position[X_AXIS]) * (target[X_AXIS] - position[X_AXIS]) +
                                 (target[Y_AXIS] - position[Y_AXIS]) * (target[Y_AXIS] - position[Y_AXIS]));
        bool  is_z_only_move = (xy_distance < 0.001f);  // Consider moves < 0.001mm as Z-only

        // For long XY moves, segment the path to maintain belt length synchronization
        // This prevents linear interpolation in motor space from causing belt slack
        // Only segment if we're not already in a segmentation to prevent recursion
        // Apply to both feed moves and rapid moves to ensure consistent belt tension
        if (!_isSegmenting && !is_z_only_move && cartesian_distance > _maxSegmentLength) {
            // Calculate initial segments using base segment length
            uint16_t segments = uint16_t(ceilf(cartesian_distance / _maxSegmentLength));

            // For very long moves, ensure we don't exceed segment limit while maintaining reasonable segmentation
            if (segments > 1000) {
                // Cap at 1000 segments and adjust segment length accordingly
                segments = 1000;
            } else if (cartesian_distance > 100.0f && segments > 100) {
                // For long moves (>100mm), use finer segmentation to minimize belt slack
                // but ensure we don't create an excessive number of segments
                uint16_t desiredSegments = std::min(uint16_t(cartesian_distance / 2.0f), uint16_t(1000));
                segments                 = std::max(segments, desiredSegments);
            }

            if (segments > 1 && segments <= 1000) {  // Increased limit for better belt synchronization
                // Set flag to prevent recursion
                _isSegmenting = true;

                // Similar to arc segmentation in MotionControl.cpp
                // Multiply inverse feed_rate to compensate for the fact that this movement is approximated
                // by a number of discrete segments. The inverse feed_rate should be correct for the sum of
                // all segments.
                if (pl_data->motion.inverseTime) {
                    pl_data->feed_rate *= segments;
                    pl_data->motion.inverseTime = 0;  // Force as feed absolute mode over segments.
                }

                // Calculate increments per segment
                float increment_per_segment[MAX_N_AXIS];
                for (size_t axis = 0; axis < n_axis && axis < MAX_N_AXIS; axis++) {
                    increment_per_segment[axis] = (target[axis] - position[axis]) / segments;
                }

                // Current position for segmentation
                float segment_position[MAX_N_AXIS];
                for (size_t axis = 0; axis < n_axis && axis < MAX_N_AXIS; axis++) {
                    segment_position[axis] = position[axis];
                }

                float original_feedrate = pl_data->feed_rate;  // Save original for proper distribution

                // Submit each segment except the last one
                for (uint16_t i = 1; i < segments; i++) {
                    // Calculate intermediate target position
                    float intermediate_target[MAX_N_AXIS];
                    for (size_t axis = 0; axis < n_axis && axis < MAX_N_AXIS; axis++) {
                        intermediate_target[axis] = position[axis] + (increment_per_segment[axis] * i);
                    }

                    // Create a copy of plan data for this segment
                    plan_line_data_t segment_pl_data = *pl_data;
                    segment_pl_data.feed_rate        = original_feedrate;  // Reset to original before scaling

                    // Submit this segment to the motion controller in cartesian space
                    // This is similar to how arc segmentation works - use mc_linear()
                    // which will call cartesian_to_motors() for proper kinematics transformation
                    if (!mc_linear(intermediate_target, &segment_pl_data, segment_position)) {
                        return false;  // If any segment fails, fail the whole move
                    }

                    // Update segment position for next iteration
                    for (size_t axis = 0; axis < n_axis && axis < MAX_N_AXIS; axis++) {
                        segment_position[axis] = intermediate_target[axis];
                    }
                }

                // Fall through to handle the final segment to the target position
                // Update position to be the last segment position for final segment calculation
                for (size_t axis = 0; axis < n_axis && axis < MAX_N_AXIS; axis++) {
                    position[axis] = segment_position[axis];
                }

                // Reset feed rate for final segment
                pl_data->feed_rate = original_feedrate;

                // Clear the segmentation flag
                _isSegmenting = false;
            }
        }

        // Handle the final segment (or the entire move if no segmentation was needed)
        float motors[n_axis];
        transform_cartesian_to_motors(motors, target);

        // Feedrate scaling removed: feedrate now stays in XY coordinates
        // The machine will move at the set feedrate in XY coordinates rather than scaling to belt space

        return mc_move_motors(motors, pl_data);
    }

    void MaslowKinematics::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        /* 
        Forward kinematics for Maslow CNC - convert belt lengths back to X,Y,Z coordinates.
        
        With ABCDZX axis mapping:
        motors[0] = A axis = Top Left belt length
        motors[1] = B axis = Top Right belt length  
        motors[2] = C axis = Bottom Left belt length
        motors[3] = D axis = Bottom Right belt length
        motors[4] = Z axis = Router position
        motors[5] = X axis = (not used)
        */

        // The Z coordinate is straightforward - it's just the Z motor position
        cartesian[Z_AXIS] = motors[4];  // Z from Z axis (index 4 in ABCDZX)

        // For X,Y coordinates, we use the TL and TR belt lengths to solve the forward kinematics
        // We need to convert the raw belt lengths to XY plane distances first
        float tlBeltLength = motors[0];  // Top Left belt length (A axis)
        float trBeltLength = motors[1];  // Top Right belt length (B axis)

        // Calculate complete z-components including spoilboard and work thickness
        // This must match the z-component calculation used in forward kinematics
        float z        = motors[4];
        float tlTotalZ = 0.0f - (z + _tlZ + _spoilboardThickness + _workThickness);
        float trTotalZ = 0.0f - (z + _trZ + _spoilboardThickness + _workThickness);

        // Convert angled belt measurements to XY plane distances using complete z-components
        // Pass the soft arm extension for each arm to properly account for flexible extensions
        float tlXYDistance = measurementToXYPlane(tlBeltLength, fabs(tlTotalZ), _tlSoftArmExtension);
        float trXYDistance = measurementToXYPlane(trBeltLength, fabs(trTotalZ), _trSoftArmExtension);

        // Solve for X,Y position using intersection of circles
        float x, y;
        if (computeXYfromBeltLengths(tlXYDistance, trXYDistance, x, y)) {
            // Apply inverse scale factors to convert from scaled motor space back to cartesian space
            cartesian[X_AXIS] = x / Maslow.scaleX;
            cartesian[Y_AXIS] = y / Maslow.scaleY;
        } else {
            // If we can't solve the kinematics, fall back to (0,0)
            // This can happen if belt lengths are inconsistent
            cartesian[X_AXIS] = 0.0f;
            cartesian[Y_AXIS] = 0.0f;
            // Don't spam the console when belts are at zero length - this is expected behavior
            if (!(tlBeltLength == 0.0f || trBeltLength == 0.0f)) {
                log_error("MaslowKinematics: Failed to compute X,Y from belt lengths, using (0,0)");
            }
        }

        // Copy any additional axes directly (none expected beyond Z for now)
        for (int axis = 3; axis < n_axis && axis < MAX_N_AXIS; axis++) {
            if (axis < 3) {  // Only copy if within valid cartesian range
                cartesian[axis] = motors[axis];
            }
        }
    }

    void MaslowKinematics::transform_cartesian_to_motors(float* motors, float* cartesian) {
        // In this implementation, FluidNC axis order is ABCDZX:
        // motors[0] = A axis = Top Left belt length
        // motors[1] = B axis = Top Right belt length
        // motors[2] = C axis = Bottom Left belt length
        // motors[3] = D axis = Bottom Right belt length
        // motors[4] = Z axis = Router position
        // motors[5] = X axis = (not used, keep as 0)

        // Extract X, Y, Z coordinates from cartesian space and apply scale factors
        float x = cartesian[X_AXIS] * Maslow.scaleX;  // Apply X scale factor
        float y = cartesian[Y_AXIS] * Maslow.scaleY;  // Apply Y scale factor
        float z = cartesian[Z_AXIS];                  // Z_AXIS = 2 (no scaling for Z)

        // Check if belts are ready to cut - if not, don't compute belt movements
        // This allows the Z-axis to move independently when belts are not calibrated
        if (Maslow.calibration.currentState == READY_TO_CUT) {
            // Compute belt lengths for each corner and assign to correct axis
            motors[0] = computeTL(x, y, z);  // Top Left -> A axis
            motors[1] = computeTR(x, y, z);  // Top Right -> B axis
            motors[2] = computeBL(x, y, z);  // Bottom Left -> C axis
            motors[3] = computeBR(x, y, z);  // Bottom Right -> D axis
        } else {
            // When belts are not ready, keep them at their current positions
            // This prevents the motion planner from synchronizing Z-axis with large belt movements
            motors[0] = steps_to_mpos(get_axis_motor_steps(0), 0);  // Keep TL at current position
            motors[1] = steps_to_mpos(get_axis_motor_steps(1), 1);  // Keep TR at current position
            motors[2] = steps_to_mpos(get_axis_motor_steps(2), 2);  // Keep BL at current position
            motors[3] = steps_to_mpos(get_axis_motor_steps(3), 3);  // Keep BR at current position
        }

        motors[4] = z;     // Z position -> Z axis (pass through)
        motors[5] = 0.0f;  // X axis not used

        // Handle any additional axes beyond the 6 we know about
        auto n_axis = config->_axes->_numberAxis;
        for (size_t axis = 6; axis < n_axis; axis++) {
            motors[axis] = cartesian[axis];
        }
    }

    // Belt length calculation functions - moved from Maslow.cpp
    float MaslowKinematics::computeTL(float x, float y, float z) {
        // Move from lower left corner coordinates to centered coordinates
        float orig_x = x, orig_y = y;
        x       = x + _centerX;
        y       = y + _centerY;
        float a = _tlX - x;  // X dist from corner to router center
        float b = _tlY - y;  // Y dist from corner to router center
        // When fixedZ is true, don't use current Z position - only use fixed anchor Z values
        float effectiveZ = _fixedZ ? 0.0f : z;
        float c          = 0.0f - (effectiveZ + _tlZ + _spoilboardThickness +
                          _workThickness);  // Z dist from corner to router center (includes material thickness)

        float XYlength = sqrt(a * a + b * b);  // Get the distance in the XY plane from the corner to the router center
        float XYBeltLength =
            XYlength - (_beltEndExtension + _armLength);           // Subtract the belt end extension and arm length to get the belt length
        float length = sqrt(XYBeltLength * XYBeltLength + c * c);  // Get the angled belt length

        // Add soft arm extension - it flexes with Z so it's added to the total 3D belt length
        // Only add soft arm extension when not using fixedZ (when the arms can move with Z changes)
        if (!_fixedZ) {
            length += _tlSoftArmExtension;
        }

        return length;
    }

    float MaslowKinematics::computeTR(float x, float y, float z) {
        // Move from lower left corner coordinates to centered coordinates
        x       = x + _centerX;
        y       = y + _centerY;
        float a = _trX - x;
        float b = _trY - y;
        // When fixedZ is true, don't use current Z position - only use fixed anchor Z values
        float effectiveZ = _fixedZ ? 0.0f : z;
        float c          = 0.0f - (effectiveZ + _trZ + _spoilboardThickness +
                          _workThickness);  // Z dist from corner to router center (includes material thickness)

        float XYlength = sqrt(a * a + b * b);  // Get the distance in the XY plane from the corner to the router center
        float XYBeltLength =
            XYlength - (_beltEndExtension + _armLength);           // Subtract the belt end extension and arm length to get the belt length
        float length = sqrt(XYBeltLength * XYBeltLength + c * c);  // Get the angled belt length

        // Add soft arm extension - it flexes with Z so it's added to the total 3D belt length
        // Only add soft arm extension when not using fixedZ (when the arms can move with Z changes)
        if (!_fixedZ) {
            length += _trSoftArmExtension;
        }

        return length;
    }

    float MaslowKinematics::computeBL(float x, float y, float z) {
        // Move from lower left corner coordinates to centered coordinates
        x       = x + _centerX;
        y       = y + _centerY;
        float a = _blX - x;  // X dist from corner to router center
        float b = _blY - y;  // Y dist from corner to router center
        // When fixedZ is true, don't use current Z position - only use fixed anchor Z values
        float effectiveZ = _fixedZ ? 0.0f : z;
        float c          = 0.0f - (effectiveZ + _blZ + _spoilboardThickness +
                          _workThickness);  // Z dist from corner to router center (includes material thickness)

        float XYlength = sqrt(a * a + b * b);  // Get the distance in the XY plane from the corner to the router center
        float XYBeltLength =
            XYlength - (_beltEndExtension + _armLength);           // Subtract the belt end extension and arm length to get the belt length
        float length = sqrt(XYBeltLength * XYBeltLength + c * c);  // Get the angled belt length

        // Add soft arm extension - it flexes with Z so it's added to the total 3D belt length
        // Only add soft arm extension when not using fixedZ (when the arms can move with Z changes)
        if (!_fixedZ) {
            length += _blSoftArmExtension;
        }

        return length;
    }

    float MaslowKinematics::computeBR(float x, float y, float z) {
        // Move from lower left corner coordinates to centered coordinates
        x       = x + _centerX;
        y       = y + _centerY;
        float a = _brX - x;
        float b = _brY - y;
        // When fixedZ is true, don't use current Z position - only use fixed anchor Z values
        float effectiveZ = _fixedZ ? 0.0f : z;
        float c          = 0.0f - (effectiveZ + _brZ + _spoilboardThickness +
                          _workThickness);  // Z dist from corner to router center (includes material thickness)

        float XYlength = sqrt(a * a + b * b);  // Get the distance in the XY plane from the corner to the router center
        float XYBeltLength =
            XYlength - (_beltEndExtension + _armLength);           // Subtract the belt end extension and arm length to get the belt length
        float length = sqrt(XYBeltLength * XYBeltLength + c * c);  // Get the angled belt length

        // Add soft arm extension - it flexes with Z so it's added to the total 3D belt length
        // Only add soft arm extension when not using fixedZ (when the arms can move with Z changes)
        if (!_fixedZ) {
            length += _brSoftArmExtension;
        }

        return length;
    }

    bool MaslowKinematics::canHome(AxisMask axisMask) {
        // For Maslow CNC, homing is typically done by retracting all belts
        // until they reach full retraction, then calibrating the system
        return true;
    }

    // Forward kinematics - compute X,Y from belt lengths
    bool MaslowKinematics::computeXYfromBeltLengths(float tlLength, float trLength, float& x, float& y) const {
        // Find the intersection of two circles centered at TL and TR anchor points
        // with radii equal to the belt lengths

        double d = sqrt((_tlX - _trX) * (_tlX - _trX) + (_tlY - _trY) * (_tlY - _trY));
        if (d > tlLength + trLength || d < abs(tlLength - trLength)) {
            // Don't spam the console when belts are at zero length - this is expected behavior
            if (!(tlLength == 0.0f || trLength == 0.0f)) {
                log_info("Unable to determine machine position from belt lengths");
            }
            return false;
        }

        double a    = (tlLength * tlLength - trLength * trLength + d * d) / (2 * d);
        double h    = sqrt(tlLength * tlLength - a * a);
        double x0   = _tlX + a * (_trX - _tlX) / d;
        double y0   = _tlY + a * (_trY - _tlY) / d;
        double rawX = x0 + h * (_trY - _tlY) / d;
        double rawY = y0 - h * (_trX - _tlX) / d;

        // Adjust to the centered coordinates (convert from frame coordinates to centered coordinates)
        x = rawX - _centerX;
        y = rawY - _centerY;

        return true;
    }

    // Convert angled belt measurement to XY plane distance
    float MaslowKinematics::measurementToXYPlane(float measurement, float zHeight, float softArmExtension) const {
        // Remove the soft arm extension from the measurement only if fixedZ is false
        // (soft arm extensions are only added when fixedZ is false in the compute functions)
        float adjustedMeasurement = _fixedZ ? measurement : measurement - softArmExtension;

        // Validate that we have a valid value before taking square root
        float squareValue = adjustedMeasurement * adjustedMeasurement - zHeight * zHeight;
        if (squareValue < 0.0f) {
            // If the calculation would result in a negative value, something is wrong
            // This could happen if soft arm extension is too large or there are measurement errors
            log_error("MaslowKinematics::measurementToXYPlane: Invalid calculation - measurement too small for Z height");
            return 0.0f;  // Return 0 to indicate error
        }

        float lengthInXY = sqrt(squareValue);
        return lengthInXY + _beltEndExtension + _armLength;  // Add belt end extension and arm length
    }

    void MaslowKinematics::releaseMotors(AxisMask axisMask, MotorMask motors) {
        // Release the specified motors
        // This is handled by the base motor system
    }

    bool MaslowKinematics::limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) {
        // For Maslow CNC, limits are based on the frame boundaries and belt lengths
        // This is handled by the motor system and limit switches
        return false;
    }

    void MaslowKinematics::group(Configuration::HandlerBase& handler) {
        handler.item("tlX", _tlX);
        handler.item("tlY", _tlY);
        handler.item("tlZ", _tlZ);
        handler.item("trX", _trX);
        handler.item("trY", _trY);
        handler.item("trZ", _trZ);
        handler.item("blX", _blX);
        handler.item("blY", _blY);
        handler.item("blZ", _blZ);
        handler.item("brX", _brX);
        handler.item("brY", _brY);
        handler.item("brZ", _brZ);
        handler.item("beltEndExtension", _beltEndExtension);
        handler.item("armLength", _armLength);
        handler.item("tlSoftArmExtension", _tlSoftArmExtension);
        handler.item("trSoftArmExtension", _trSoftArmExtension);
        handler.item("blSoftArmExtension", _blSoftArmExtension);
        handler.item("brSoftArmExtension", _brSoftArmExtension);
        handler.item("maxSegmentLength", _maxSegmentLength);
        handler.item("fixedZ", _fixedZ);
    }

    // Setter methods for calibration system to update frame parameters
    void MaslowKinematics::setFrameSize(float frameSize) {
        // Update anchor coordinates for a square frame of size frameSize x frameSize
        // Keep the same Z coordinates but adjust X,Y to form a square
        _blX = 0.0f;
        _blY = 0.0f;
        _brX = frameSize;
        _brY = 0.0f;
        _tlX = 0.0f;
        _tlY = frameSize;
        _trX = frameSize;
        _trY = frameSize;

        // Recalculate center coordinates
        calculateCenter();
    }

    void MaslowKinematics::updateAnchorCoordinates(
        float tlX, float tlY, float tlZ, float trX, float trY, float trZ, float blX, float blY, float blZ, float brX, float brY, float brZ) {
        _tlX = tlX;
        _tlY = tlY;
        _tlZ = tlZ;
        _trX = trX;
        _trY = trY;
        _trZ = trZ;
        _blX = blX;
        _blY = blY;
        _blZ = blZ;
        _brX = brX;
        _brY = brY;
        _brZ = brZ;

        // Recalculate center coordinates
        calculateCenter();

        log_info("Anchor coordinates updated manually");
    }

    void MaslowKinematics::setSpoilboardThickness(float thickness) {
        if (_spoilboardThickness != thickness) {
            _spoilboardThickness = thickness;
            log_info("Spoilboard thickness set to " << thickness << " mm");
        }
    }

    void MaslowKinematics::setWorkThickness(float thickness) {
        if (_workThickness != thickness) {
            _workThickness = thickness;
            log_info("Work thickness set to " << thickness << " mm");
        }
    }

    void MaslowKinematics::validate() {
        validateAndCorrectAnchorCoordinates();
    }

    void MaslowKinematics::validateAndCorrectAnchorCoordinates() {
        const float TOLERANCE            = 0.1f;  // Allow small floating point differences
        bool        coordinatesCorrected = false;

        // Default reasonable values (rounded for clarity that these are placeholder values)
        const float DEFAULT_TLX = -30.0f;
        const float DEFAULT_TLY = 2100.0f;
        const float DEFAULT_TRX = 2950.0f;
        const float DEFAULT_TRY = 2100.0f;
        const float DEFAULT_BLX = 0.0f;
        const float DEFAULT_BLY = 0.0f;
        const float DEFAULT_BRX = 3000.0f;
        const float DEFAULT_BRY = 0.0f;

        // Check that blX, blY, and brY should be zero (or very close to zero)
        if (std::abs(_blX) > TOLERANCE) {
            log_warn("Bottom left X coordinate (blX) should be 0.0, but is " << _blX << ". Correcting to 0.0.");
            _blX                 = 0.0f;
            coordinatesCorrected = true;
        }

        if (std::abs(_blY) > TOLERANCE) {
            log_warn("Bottom left Y coordinate (blY) should be 0.0, but is " << _blY << ". Correcting to 0.0.");
            _blY                 = 0.0f;
            coordinatesCorrected = true;
        }

        if (std::abs(_brY) > TOLERANCE) {
            log_warn("Bottom right Y coordinate (brY) should be 0.0, but is " << _brY << ". Correcting to 0.0.");
            _brY                 = 0.0f;
            coordinatesCorrected = true;
        }

        // Check that tlX < trX (left should be to the left of right)
        if (_tlX >= _trX) {
            log_warn("Top left X coordinate (tlX=" << _tlX << ") should be less than top right X coordinate (trX=" << _trX
                                                   << "). Correcting to reasonable defaults.");
            _tlX                 = DEFAULT_TLX;
            _trX                 = DEFAULT_TRX;
            coordinatesCorrected = true;
        }

        // Check that top points are above bottom points
        if (_tlY <= _blY || _trY <= _brY) {
            char* buffer = getLogBuffer();
            snprintf(buffer,
                     1400,
                     "Top anchor points should be above bottom anchor points. tlY=%g should be > blY=%g, trY=%g should be > brY=%g. "
                     "Correcting to reasonable defaults.",
                     _tlY,
                     _blY,
                     _trY,
                     _brY);
            log_warn(buffer);
            releaseLogBuffer();
            _tlY                 = DEFAULT_TLY;
            _trY                 = DEFAULT_TRY;
            coordinatesCorrected = true;
        }

        // Check side lengths - minimum 500mm, maximum 5000mm
        const float MIN_SIDE_LENGTH = 500.0f;
        const float MAX_SIDE_LENGTH = 5000.0f;

        // Calculate distances for each side of the frame
        float topSideLength    = sqrt((_trX - _tlX) * (_trX - _tlX) + (_trY - _tlY) * (_trY - _tlY));
        float rightSideLength  = sqrt((_brX - _trX) * (_brX - _trX) + (_brY - _trY) * (_brY - _trY));
        float bottomSideLength = sqrt((_brX - _blX) * (_brX - _blX) + (_brY - _blY) * (_brY - _blY));
        float leftSideLength   = sqrt((_tlX - _blX) * (_tlX - _blX) + (_tlY - _blY) * (_tlY - _blY));

        // Check if any side length is outside the valid range
        if (topSideLength < MIN_SIDE_LENGTH || topSideLength > MAX_SIDE_LENGTH || rightSideLength < MIN_SIDE_LENGTH ||
            rightSideLength > MAX_SIDE_LENGTH || bottomSideLength < MIN_SIDE_LENGTH || bottomSideLength > MAX_SIDE_LENGTH ||
            leftSideLength < MIN_SIDE_LENGTH || leftSideLength > MAX_SIDE_LENGTH) {
            // Frame dimensions are out of bounds - this is a critical error that cannot be auto-corrected
            // Operating with incorrect anchor points could damage the machine
            char* buffer = getLogBuffer();
            snprintf(buffer,
                     1400,
                     "Frame side lengths are outside valid range (500-5000mm). "
                     "Top=%gmm, Right=%gmm, Bottom=%gmm, Left=%gmm. Calibration cannot proceed with these dimensions.",
                     topSideLength,
                     rightSideLength,
                     bottomSideLength,
                     leftSideLength);
            log_error(buffer);
            releaseLogBuffer();
            String errorMsg = "Frame dimensions out of bounds. Top=" + String(topSideLength, 1) + "mm, Right=" + String(rightSideLength, 1) +
                              "mm, Bottom=" + String(bottomSideLength, 1) + "mm, Left=" + String(leftSideLength, 1) + "mm";
            Maslow.eStop(errorMsg);
        }

        // Sanity check for reasonable coordinate values (not negative for most coordinates, not excessively large)
        const float MAX_REASONABLE_COORD = 10000.0f;  // 10 meters should be more than enough for any Maslow frame

        if (_tlY < 0 || _trY < 0 || _tlY > MAX_REASONABLE_COORD || _trY > MAX_REASONABLE_COORD || _blX < 0 || _brX < 0 ||
            _brX > MAX_REASONABLE_COORD) {
            log_warn("Anchor coordinates contain unrealistic values. Resetting to reasonable defaults.");
            _tlX                 = DEFAULT_TLX;
            _tlY                 = DEFAULT_TLY;
            _trX                 = DEFAULT_TRX;
            _trY                 = DEFAULT_TRY;
            _blX                 = DEFAULT_BLX;
            _blY                 = DEFAULT_BLY;
            _brX                 = DEFAULT_BRX;
            _brY                 = DEFAULT_BRY;
            coordinatesCorrected = true;
        }

        if (coordinatesCorrected) {
            char* buffer = getLogBuffer();
            snprintf(buffer,
                     1400,
                     "Anchor coordinates corrected. New values: tlX=%g tlY=%g trX=%g trY=%g blX=%g blY=%g brX=%g brY=%g",
                     _tlX,
                     _tlY,
                     _trX,
                     _trY,
                     _blX,
                     _blY,
                     _brX,
                     _brY);
            log_info(buffer);
            releaseLogBuffer();
            // Recalculate center coordinates after correction
            calculateCenter();
        }
    }

    // Destructor - clear global pointer
    MaslowKinematics::~MaslowKinematics() {
        if (g_maslowKinematics == this) {
            g_maslowKinematics = nullptr;
        }
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<MaslowKinematics> registration("MaslowKinematics");
    }

    // Global accessor function to get the current MaslowKinematics instance
    MaslowKinematics* getMaslowKinematics() {
        return g_maslowKinematics;
    }
}