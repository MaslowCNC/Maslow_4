// Copyright (c) 2024 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#pragma once

// Enums for indexing arms and axes to reduce code duplication
enum MaslowArm {
    _TL       = 0,  // Top Left
    _TR       = 1,  // Top Right
    _BL       = 2,  // Bottom Left
    _BR       = 3,  // Bottom Right
    ARM_COUNT = 4
};

enum CartesianAxis {
    Coord_X     = 0,  // X axis
    Coord_Y     = 1,  // Y axis
    Coord_Z     = 2,  // Z axis
    Coord_COUNT = 3
};

// Movement direction for calibration
#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4

// Machine orientation
#define HORIZONTAL 0
#define VERTICAL 1

// Calibration state machine states
#define UNKNOWN 0
#define RETRACTING 1
#define RETRACTED 2
#define EXTENDING 3
#define EXTENDEDOUT 4  // Extended is a reserved word
#define TAKING_SLACK 5
#define CALIBRATION_IN_PROGRESS 6
#define READY_TO_CUT 7
#define RELEASE_TENSION 8
#define CALIBRATION_COMPUTING 9
