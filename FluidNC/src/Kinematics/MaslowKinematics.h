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
#include "../Maslow/Maslow.h"  // For MaslowArm and CartesianAxis enums

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
        void afterParse() override {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "MaslowKinematics"; }

        ~MaslowKinematics();

        // Generic compute function for any arm
        float compute(int arm, float x, float y, float z);

        // Generic getter for anchor coordinates
        float getAnchorCoord(int arm, int axis) const { return anchor_location[arm][axis]; }
        
        // Legacy getters for backward compatibility
        float getTlX() const { return anchor_location[_TL][Coord_X]; }
        float getTlY() const { return anchor_location[_TL][Coord_Y]; }
        float getTlZ() const { return anchor_location[_TL][Coord_Z]; }
        float getTrX() const { return anchor_location[_TR][Coord_X]; }
        float getTrY() const { return anchor_location[_TR][Coord_Y]; }
        float getTrZ() const { return anchor_location[_TR][Coord_Z]; }
        float getBlX() const { return anchor_location[_BL][Coord_X]; }
        float getBlY() const { return anchor_location[_BL][Coord_Y]; }
        float getBlZ() const { return anchor_location[_BL][Coord_Z]; }
        float getBrX() const { return anchor_location[_BR][Coord_X]; }
        float getBrY() const { return anchor_location[_BR][Coord_Y]; }
        float getBrZ() const { return anchor_location[_BR][Coord_Z]; }
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
        void updateAnchorCoordinates(
            float tlX, float tlY, float tlZ, float trX, float trY, float trZ, float blX, float blY, float blZ, float brX, float brY, float brZ);
        void setSpoilboardThickness(float thickness);
        void setWorkThickness(float thickness);

    private:
        // Validation and correction helper method
        void validateAndCorrectAnchorCoordinates();

        // Anchor point coordinates (in mm) - 2D array indexed by [arm][axis]
        // First index: arm (_TL, _TR, _BL, _BR)
        // Second index: axis (_X, _Y, _Z)
        float anchor_location[ARM_COUNT][Coord_COUNT] = {
            { -27.6f, 2064.9f, 100.0f },   // _TL: X, Y, Z
            { 2924.3f, 2066.5f, 56.0f },   // _TR: X, Y, Z
            { 0.0f, 0.0f, 34.0f },         // _BL: X, Y, Z
            { 2953.2f, 0.0f, 78.0f }       // _BR: X, Y, Z
        };
        
        // Legacy individual accessors (maintained for backward compatibility)
        float& _tlX = anchor_location[_TL][Coord_X];
        float& _tlY = anchor_location[_TL][Coord_Y];
        float& _tlZ = anchor_location[_TL][Coord_Z];
        float& _trX = anchor_location[_TR][Coord_X];
        float& _trY = anchor_location[_TR][Coord_Y];
        float& _trZ = anchor_location[_TR][Coord_Z];
        float& _blX = anchor_location[_BL][Coord_X];
        float& _blY = anchor_location[_BL][Coord_Y];
        float& _blZ = anchor_location[_BL][Coord_Z];
        float& _brX = anchor_location[_BR][Coord_X];
        float& _brY = anchor_location[_BR][Coord_Y];
        float& _brZ = anchor_location[_BR][Coord_Z];
        
        // Deprecated separate arrays (kept in sync via references)
        float* _anchorX = anchor_location[0];  // Points to X coordinates
        float* _anchorY = &anchor_location[0][1];  // Points to Y coordinates  
        float* _anchorZ = &anchor_location[0][2];  // Points to Z coordinates

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