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
#define STATE_COUNT 10  // Total number of states

// Unified state definition structure - combines state info and transitions
// This keeps all state-related data in one place to prevent desynchronization
constexpr int MAX_TRANSITIONS = 5;
struct StateDefinition {
    int         id;              // State ID (matches defines above)
    const char* name;            // Human-readable state name
    const char* backgroundColor; // UI background color for state label (grouped with name for display)
    const char* buttonLabel;     // UI button label (empty string if no button)
    int         allowedTransitions[MAX_TRANSITIONS];  // Allowed next states, -1 terminated
};

// Complete state machine definition
// Each entry defines: id, name, background color, button label, and allowed transitions FROM this state
// Background colors: red=#f8d7da, blue=#cfe2ff, green=#d1e7dd, yellow=#fff3cd
// NOTE: RETRACTING can be entered from any state, so it appears in all transition lists
// NOTE: UNKNOWN has no allowed transitions FROM it - it's an error state that can only be entered, not exited via normal transitions
//       Any state can transition TO UNKNOWN (special handling in requestStateChange bypasses validation)
constexpr StateDefinition stateDefinitions[] = {
    { UNKNOWN, "Unknown", "#f8d7da", "",
        { -1, -1, -1, -1, -1 } },
    { RETRACTING, "Retracting Belts", "#cfe2ff", "Retract All",
        { RETRACTED, RETRACTING, -1, -1, -1 } },
    { RETRACTED, "Belts Retracted", "#d1e7dd", "",
        { EXTENDING, RETRACTING, RELEASE_TENSION, -1, -1 } },
    { EXTENDING, "Extending Belts", "#cfe2ff", "Extend All",
        { EXTENDEDOUT, RETRACTING, -1, -1, -1 } },
    { EXTENDEDOUT, "Belts Extended", "#fff3cd", "",
        { TAKING_SLACK, CALIBRATION_IN_PROGRESS, RELEASE_TENSION, RETRACTING, EXTENDING } },
    { TAKING_SLACK, "Taking Slack", "#cfe2ff", "Apply Tension",
        { EXTENDEDOUT, READY_TO_CUT, RETRACTING, -1, -1 } },
    { CALIBRATION_IN_PROGRESS, "Calibrating", "#cfe2ff", "Find Anchor Locations",
        { CALIBRATION_COMPUTING, READY_TO_CUT, RETRACTING, -1, -1 } },
    { READY_TO_CUT, "Ready To Cut", "#d1e7dd", "",
        { TAKING_SLACK, CALIBRATION_IN_PROGRESS, RELEASE_TENSION, RETRACTING, -1 } },
    { RELEASE_TENSION, "Releasing Tension", "#cfe2ff", "Release Tension",
        { EXTENDEDOUT, RETRACTING, -1, -1, -1 } },
    { CALIBRATION_COMPUTING, "Calibration Computing", "#cfe2ff", "",
        { CALIBRATION_IN_PROGRESS, READY_TO_CUT, RELEASE_TENSION, RETRACTING, -1 } },
};
