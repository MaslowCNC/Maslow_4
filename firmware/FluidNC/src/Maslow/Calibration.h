#pragma once
#include <Arduino.h>
#include "MaslowEnums.h"

class Calibration {
public:
    // Constructor
    Calibration();

    // Public method
    void home();
    void updateCenterXY();

    bool all_axis_homed();
    bool allAxisExtended();
    void safety_control();
    void update_frame_xyz();

    //Used to override and drive the motors directly
    void TLI();
    void TRI();
    void BLI();
    void BRI();
    void TLO();
    void TRO();
    void BLO();
    void BRO();
    void handleMotorOverides();
    bool checkOverides();
    void clearMotorOverrides();

    bool generate_calibration_grid();
    //void   printCalibrationGrid();
    bool  move_with_slack(double fromX, double fromY, double toX, double toY);
    int   get_direction(double x, double y, double targetX, double targetY);
    bool  take_measurement_avg_with_check(int waypoint, int dir);
    bool  take_measurement(float result[4], int dir, int run, int current, int waypoint);
    float measurementToXYPlane(float measurement, float zHeight);
    float measurementFromXYPlane(float xyPlaneDistance, float zHeight);
    bool  takeSlackFunc();
    bool  adjustFrameSizeToMatchFirstMeasurement();
    bool  computeXYfromLengths(double TL, double TR, float& x, float& y);
    void  calibration_loop();
    void  print_calibration_data();
    void  calibrationDataRecieved();
    void  checkCalibrationData();

    void allocateCalibrationMemory();
    void deallocateCalibrationMemory();
    void resetCalibrationState();

    void comply();
    bool detectOrientation();

    void hold(unsigned long time);

    void setSafety(bool state);

    //State machine functions
    int  getCurrentState();
    void printCurrentState();
    bool requestStateChange(int newState);
    const int* getAllowedTransitions(int fromState, int& count);
    const char* getStateName(int state);
    const char* getStateButtonLabel(int state);

    // Set extended state variables (used when restoring from NVS)
    void setExtendedState(bool tl, bool tr, bool bl, bool br);

    //Public Variables
    //hold
    unsigned long holdTimer = millis();
    bool          holding   = false;
    unsigned long holdTime  = 0;

    //Public calibration state variables. These need to be public since they are accessed externally.
    //They probably shouldn't be.

    //Variables used by retraction
    int  retractCurrentThreshold = 1300;
    bool axisHomed[ARM_COUNT]    = { false, false, false, false };

    //Variables used by extension
    float extendDist = 1700;

    //Variables used by calibration
    bool  orientation;
    float acceptableCalibrationThreshold = 0.5;
    int   calibrationGridSize            = 9;
    float calibration_grid_width_mm_X    = 0;      // mm grid width (0 = auto-calculate as 50% of frame width)
    float calibration_grid_height_mm_Y   = 0;      // mm grid height (0 = auto-calculate as 20% of frame height)
    float calibrationMaxSpacingMm        = 260.0;  // Maximum allowed spacing between calibration points when auto-selecting grid size
    bool  calibrationInProgress;                   //Used to turn off regular movements during calibration

    //State machine variables
    int currentState = UNKNOWN;

private:
    //Variables used for retracting state
    bool axis_homed[4]         = { false, false, false, false };
    bool retracting[ARM_COUNT] = { false, false, false, false };

    // Store the previous state before entering RELEASE_TENSION
    int previousState = UNKNOWN;

    //Variables used by extending
    bool extended[ARM_COUNT] = { false, false, false, false };

    //Variables used by take slack
    bool takeSlack = false;

    //Variables used by calibration
    float** calibration_data      = nullptr;
    int     pointCount            = 0;     //number of actual points in the grid,  < GRID_SIZE_MAX
    int     waypoint              = 0;     //The current waypoint in the calibration process
    int     calibrationDirection  = 0;     //Direction for calibration measurements (replaces static variable)
    bool    measurementInProgress = true;  //Whether currently taking measurement or moving (replaces static variable)
    int     frame_dimention_MIN   = 400;   //Is this used? This should be enforced by the user settings. TODO.
    int     frame_dimention_MAX   = 15000;
    float (*calibrationGrid)[2]   = nullptr;
    int    recomputePoints[10];          // Stores the index of the points where we want to trigger a recompute
    int    recomputeCountIndex    = 0;   // Stores the index of the recompute point we are currently on
    int    recomputeCount         = 0;   // Stores the number of recompute points
    double calibrationDataWaiting = -1;  //-1 if data is not waiting, other wise the milis since the data was last sent

    //Used to keep track of how often the PID controller is updated
    unsigned long lastCallToPID    = millis();
    unsigned long lastMiss         = millis();
    unsigned long lastCallToUpdate = millis();
    unsigned long extendCallTimer  = millis();
    unsigned long complyCallTimer  = millis();

    //Variables used for orientation detection
    unsigned long orientationDetectTimer   = 0;
    double        tlStartPosition          = 0;
    double        trStartPosition          = 0;
    bool          orientationDetectionDone = false;

    //Used to overide and drive motors directly...dangerous
    bool          TLIOveride   = false;
    bool          TRIOveride   = false;
    bool          BLIOveride   = false;
    bool          BRIOveride   = false;
    bool          TLOOveride   = false;
    bool          TROOveride   = false;
    bool          BLOOveride   = false;
    bool          BROOveride   = false;
    unsigned long overideTimer = millis();

    bool safetyOn         = true;
    bool HeartBeatEnabled = true;

    //A structure to hold the state information
    struct StateInfo {
        int         state;
        const char* name;
        const char* buttonLabel;  // Label for UI button, empty string if no button
    };
    StateInfo stateNames[11] = { 
        { UNKNOWN, "Unknown", "" },
        { RETRACTING, "Retracting Belts", "Retract All" },
        { RETRACTED, "Belts Retracted", "" },
        { EXTENDING, "Extending Belts", "Extend All" },
        { EXTENDEDOUT, "Belts Extended", "" },
        { TAKING_SLACK, "Taking Slack", "Apply Tension" },
        { CALIBRATION_IN_PROGRESS, "Calibrating", "Find Anchor Locations" },
        { READY_TO_CUT, "Ready To Cut", "" },
        { RELEASE_TENSION, "Releasing Tension", "Release Tension" },
        { CALIBRATION_COMPUTING, "Calibration Computing", "" } };

    // State transition map - defines which states can transition to which other states
    // Each entry is a list of allowed destination states for a given source state
    // terminated by -1
    static constexpr int MAX_TRANSITIONS = 5;
    struct StateTransitions {
        int fromState;
        int allowedStates[MAX_TRANSITIONS];
    };

    // Define allowed state transitions for each state
    // Based on the comments and logic in requestStateChange()
    static constexpr StateTransitions stateTransitionMap[] = {
        { UNKNOWN, { RETRACTING, -1, -1, -1, -1 } },
        { RETRACTING, { RETRACTED, -1, -1, -1, -1 } },
        { RETRACTED, { EXTENDING, RETRACTING, -1, -1, -1 } },
        { EXTENDING, { EXTENDEDOUT, -1, -1, -1, -1 } },
        { EXTENDEDOUT, { TAKING_SLACK, CALIBRATION_IN_PROGRESS, RELEASE_TENSION, RETRACTING, EXTENDING } },
        { TAKING_SLACK, { EXTENDEDOUT, READY_TO_CUT, -1, -1, -1 } },
        { CALIBRATION_IN_PROGRESS, { CALIBRATION_COMPUTING, -1, -1, -1, -1 } },
        { READY_TO_CUT, { TAKING_SLACK, CALIBRATION_IN_PROGRESS, RELEASE_TENSION, RETRACTING, -1 } },
        { RELEASE_TENSION, { EXTENDEDOUT, -1, -1, -1, -1 } },
        { CALIBRATION_COMPUTING, { CALIBRATION_IN_PROGRESS, READY_TO_CUT, RELEASE_TENSION, -1, -1 } },
    };
};