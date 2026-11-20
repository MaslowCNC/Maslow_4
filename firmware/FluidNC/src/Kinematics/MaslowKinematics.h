// Copyright (c) 2024 - Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
    MaslowKinematics.h

    This implements Maslow CNC Kinematics for a cable-driven router system.
    
    The Maslow CNC has four anchor points (TL, TR, BL, BR) connected by cables/belts
    to a router sled. The kinematics transforms X,Y,Z coordinates into the four
    belt lengths needed to position the sled.
    
    This replaces the custom coordinate transformation logic previously handled
    in Maslow.cpp, allowing FluidNC to handle acceleration planning and feed rate
    limiting on a per-belt basis.
*/

#include "Kinematics.h"
#include "../Maslow/MaslowEnums.h"

namespace Kinematics {
    class MaslowKinematics : public KinematicSystem {
    public:
        MaslowKinematics(const char* name) : KinematicSystem(name) {}

        MaslowKinematics(const MaslowKinematics&)            = delete;
        MaslowKinematics(MaslowKinematics&&)                 = delete;
        MaslowKinematics& operator=(const MaslowKinematics&) = delete;
        MaslowKinematics& operator=(MaslowKinematics&&)      = delete;

        // Kinematic Interface
        void init() override;
        void init_position() override;
        bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;
        void motors_to_cartesian(float* cartesian, float* motors, axis_t n_axis) override;
        bool transform_cartesian_to_motors(float* motors, float* cartesian) override;

        bool canHome(AxisMask axisMask) override;
        void releaseMotors(AxisMask axisMask, MotorMask motors) override;
        bool limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) override;

        // Configuration handlers:
        void validate() override;
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;

        ~MaslowKinematics();

        // Public access to compute functions for calibration system
        float compute(int arm, float x, float y, float z);
        // Deprecated: Use compute(arm, x, y, z) instead
        float computeTL(float x, float y, float z) { return compute(_TL, x, y, z); }
        float computeTR(float x, float y, float z) { return compute(_TR, x, y, z); }
        float computeBL(float x, float y, float z) { return compute(_BL, x, y, z); }
        float computeBR(float x, float y, float z) { return compute(_BR, x, y, z); }

        // Getters for parameters used by calibration system
        float getAnchorCoord(int arm, int axis) const { return anchor_location[arm][axis]; }
        // Deprecated: Use getAnchorCoord(arm, axis) instead
        float getTlX() const { return getAnchorCoord(_TL, Coord_X); }
        float getTlY() const { return getAnchorCoord(_TL, Coord_Y); }
        float getTlZ() const { return getAnchorCoord(_TL, Coord_Z); }
        float getTrX() const { return getAnchorCoord(_TR, Coord_X); }
        float getTrY() const { return getAnchorCoord(_TR, Coord_Y); }
        float getTrZ() const { return getAnchorCoord(_TR, Coord_Z); }
        float getBlX() const { return getAnchorCoord(_BL, Coord_X); }
        float getBlY() const { return getAnchorCoord(_BL, Coord_Y); }
        float getBlZ() const { return getAnchorCoord(_BL, Coord_Z); }
        float getBrX() const { return getAnchorCoord(_BR, Coord_X); }
        float getBrY() const { return getAnchorCoord(_BR, Coord_Y); }
        float getBrZ() const { return getAnchorCoord(_BR, Coord_Z); }
        float getBeltEndExtension() const { return _beltEndExtension; }
        float getArmLength() const { return _armLength; }
        float getSpoilboardThickness() const { return _spoilboardThickness; }
        float getWorkThickness() const { return _workThickness; }
        float getCenterX() const { return _centerX; }
        float getCenterY() const { return _centerY; }

        // The frame need not be a perfect rectangle, so this is the narrower of its two widths and the shorter of its two heights
        void getFrameSize(float& width, float& height) const;
        // The area jobs are limited to. An axis left at 0 (the default) is sized automatically from the frame
        void getWorkArea(float& width, float& height) const;

        // Belt stretch compensation. A belt under tension F is longer than the length the encoder counts out by
        // F * L / AE. On or off (beltStretchCompensation); the belt's numbers are fixed.
        static constexpr float BELT_AXIAL_STIFFNESS = 178900.0f;  // AE (N), the belt manufacturer's spec
        static constexpr float BELT_PRELOAD         = 156.96f;    // N (16 kgf): Find Anchors' pull, and the cutting preload
        static constexpr float SLED_WEIGHT          = 86.33f;     // N (8.8 kg), vertical frames only

        // The pull Find Anchors and Apply Tension correct their measurements for now: BELT_PRELOAD, or 0 when off
        float measurementPreload() const { return _beltStretchCompensation ? BELT_PRELOAD : 0.0f; }
        // A belt paid out to paidOut spans paidOut * (1 + F/AE) at that pull; and back
        float spanAtPull(float paidOut) const { return paidOut * (1.0f + measurementPreload() / BELT_AXIAL_STIFFNESS); }
        float paidOutAtPull(float span) const { return span / (1.0f + measurementPreload() / BELT_AXIAL_STIFFNESS); }

        // Cutting follows the anchors (anchorStretchPreload), not the setting, so turning the setting on or off
        // changes nothing until syncAnchorsToBeltStretch() moves the anchors to match.
        bool stretchActive() const { return _anchorStretchPreload > 0.0f; }
        // Moves the anchors to what Find Anchors would have found with the current setting, if they were found (or
        // last moved) with the other one. Each belt's paid-out length with the sled at the frame centre is the span at
        // the old factor; at the new one it spans more or less, so its anchor moves along the belt by the difference.
        // Matches a full refit of the logged data to 0.04 mm. Returns true if the anchors moved.
        bool syncAnchorsToBeltStretch();

        // The tension (N) each belt is commanded to carry with the sled at x, y (centred, scaled coordinates):
        // the force-balanced set closest to BELT_PRELOAD on every belt, plus the sled's weight on a vertical
        // frame. Belts are position controlled, so the lengths this sizes are what set the tension.
        void estimateBeltTensions(float x, float y, float tension[ARM_COUNT]) const;

        // Forward kinematics methods for position synchronization
        bool  computeXYfromBeltLengths(float tlLength, float trLength, float& x, float& y) const;
        float measurementToXYPlane(float measurement, float zHeight) const;

        // Setter methods for calibration system to update frame parameters
        void setFrameSize(float frameSize);
        void setCalibrationAnchors(float tlX, float tlY, float trX, float trY, float brX);
        void setSpoilboardThickness(float thickness);
        void setWorkThickness(float thickness);
        void setBeltStretchCompensation(bool on) { _beltStretchCompensation = on; }

    private:
        // Validation and correction helper method
        void validateAndCorrectAnchorCoordinates();

        // Anchor point coordinates (in mm) - [ARM_COUNT][Coord_COUNT]
        // Access as: anchor_location[arm][coord] where arm is _TL/_TR/_BL/_BR and coord is Coord_X/Coord_Y/Coord_Z
        float anchor_location[ARM_COUNT][Coord_COUNT] = {
            { -27.6f, 2064.9f, 100.0f },  // _TL: Top Left (X, Y, Z)
            { 2924.3f, 2066.5f, 56.0f },  // _TR: Top Right (X, Y, Z)
            { 0.0f, 0.0f, 34.0f },        // _BL: Bottom Left (X, Y, Z)
            { 2953.2f, 0.0f, 78.0f }      // _BR: Bottom Right (X, Y, Z)
        };

        // Belt and arm parameters (in mm)
        float _beltEndExtension = 30.0f;   // Belt end extension
        float _armLength        = 123.4f;  // Arm length

        // Belt stretch compensation (off when a maslow.yaml written before it has neither key)
        bool  _beltStretchCompensation = false;  // the setting
        float _anchorStretchPreload    = 0.0f;   // pull (N) the anchors were corrected for: 0 if found without compensation

        // Material thickness offsets (in mm) - accounts for spoil board and work piece thickness
        float _spoilboardThickness = 0.0f;  // Spoil board thickness added to all anchor heights
        float _workThickness       = 0.0f;  // Work piece thickness added to all anchor heights

        // Center offset for coordinate system transformation
        float _centerX = 0.0f;  // Will be calculated from frame dimensions
        float _centerY = 0.0f;  // Will be calculated from frame dimensions

        // Segmentation parameters for belt length synchronization
        float _maxSegmentLength = 5.0f;  // Maximum segment length (mm) before breaking into smaller segments

        // Belt-to-encoder geometry
        float _beltToothSpacing = 1.9988f;
        float _encoderTeeth     = 22.0f;

        // Flag to prevent recursion during segmentation
        bool _isSegmenting = false;

        // Flag to determine if arms move with Z changes
        bool _fixedZ = false;  // When true, belt lengths ignore current Z position (use only fixed anchor Z values)

        // Initialize center coordinates
        void calculateCenter();
    };

    // Global accessor function to get the current MaslowKinematics instance
    MaslowKinematics* getMaslowKinematics();
}  //  namespace Kinematics
