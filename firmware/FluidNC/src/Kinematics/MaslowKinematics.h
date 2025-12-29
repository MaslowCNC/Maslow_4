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

namespace Kinematics {
    class MaslowKinematics : public KinematicSystem {
    public:
        MaslowKinematics() = default;

        MaslowKinematics(const MaslowKinematics&)            = delete;
        MaslowKinematics(MaslowKinematics&&)                 = delete;
        MaslowKinematics& operator=(const MaslowKinematics&) = delete;
        MaslowKinematics& operator=(MaslowKinematics&&)      = delete;

        // Kinematic Interface
        void init() override;
        void init_position() override;
        bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;
        void motors_to_cartesian(float* cartesian, float* motors, int n_axis) override;
        void transform_cartesian_to_motors(float* motors, float* cartesian) override;

        bool canHome(AxisMask axisMask) override;
        void releaseMotors(AxisMask axisMask, MotorMask motors) override;
        bool limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) override;

        // Configuration handlers:
        void validate() override;
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "MaslowKinematics"; }

        ~MaslowKinematics();

        // Public access to generic compute function for calibration system
        float compute(int arm, float x, float y, float z);

        // Generic getter for anchor coordinates
        float getAnchorCoord(int arm, int axis) const;

        float getBeltEndExtension() const { return _beltEndExtension; }
        float getArmLength() const { return _armLength; }
        float getSpoilboardThickness() const { return _spoilboardThickness; }
        float getWorkThickness() const { return _workThickness; }
        float getCenterX() const { return _centerX; }
        float getCenterY() const { return _centerY; }

        // Forward kinematics methods for position synchronization
        bool  computeXYfromBeltLengths(float tlLength, float trLength, float& x, float& y) const;
        float measurementToXYPlane(float measurement, float zHeight) const;

        // Setter methods for calibration system to update frame parameters
        void setFrameSize(float frameSize);
        void setSpoilboardThickness(float thickness);
        void setWorkThickness(float thickness);

    private:
        // Validation and correction helper method
        void validateAndCorrectAnchorCoordinates();

        // Anchor point coordinates (in mm) - stored as [arm][coordinate]
        // arm: 0=TL, 1=TR, 2=BL, 3=BR
        // coordinate: 0=X, 1=Y, 2=Z
        float anchor_location[4][3] = {
            { -27.6f, 2064.9f, 100.0f },   // Top left
            { 2924.3f, 2066.5f, 56.0f },   // Top right
            { 0.0f, 0.0f, 34.0f },         // Bottom left
            { 2953.2f, 0.0f, 78.0f }       // Bottom right
        };

        // Belt and arm parameters (in mm)
        float _beltEndExtension = 30.0f;   // Belt end extension
        float _armLength        = 123.4f;  // Arm length

        // Material thickness offsets (in mm) - accounts for spoil board and work piece thickness
        float _spoilboardThickness = 0.0f;  // Spoil board thickness added to all anchor heights
        float _workThickness       = 0.0f;  // Work piece thickness added to all anchor heights

        // Center offset for coordinate system transformation
        float _centerX = 0.0f;  // Will be calculated from frame dimensions
        float _centerY = 0.0f;  // Will be calculated from frame dimensions

        // Segmentation parameters for belt length synchronization
        float _maxSegmentLength = 5.0f;  // Maximum segment length (mm) before breaking into smaller segments

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