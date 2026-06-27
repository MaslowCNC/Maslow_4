// Copyright (c) 2024 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#pragma once
#include <Arduino.h>
#include "MaslowEnums.h"
#include "MotorUnit.h"
#include "Calibration.h"
#include "../System.h"  // sys.*
#include "../Planner.h"
#include <nvs.h>
#include "FreeRTOS.h"
#include "semphr.h"

#define TCAADDR 0x70

#define coolingFanPin 47

#define CALIBRATION_GRID_SIZE_MAX (10 * 10) + 2

#define MASLOW_TELEM_FILE "M4_telemetry.bin"

// Common Default strings - especially used by config
const std::string M = "Maslow";
// Non-volatile storage name
//const char * nvs_t = "maslow";

struct TelemetryFileHeader {
    unsigned int structureSize;  // 4 bytes
    char         version[10];    // 10
    // if you add to the header take bytes from this
    char _unused[64];  // 64 bytes
};

struct TelemetryData {
    unsigned long timestamp;
    // motors - indexed by MaslowArm enum
    double Current[ARM_COUNT];
    double Power[ARM_COUNT];
    double Speed[ARM_COUNT];
    double Pos[ARM_COUNT];
    int    State[ARM_COUNT];
    bool   extended[ARM_COUNT];

    bool extendingALL;
    bool complyALL;
    bool takeSlack;

    bool   safetyOn;
    double targetX;
    double targetY;
    double targetZ;
    double x;
    double y;
    double z;

    bool          test;
    int           pointCount;
    int           waypoint;
    int           calibrationGridSize;
    unsigned long holdTimer;
    bool          holding;
    unsigned long holdTime;
    float         centerX;
    float         centerY;
    unsigned long lastCallToPID;
    unsigned long lastMiss;
    unsigned long lastCallToUpdate;
    unsigned long extendCallTimer;
    unsigned long complyCallTimer;
    unsigned int  panicCount;
};

class Maslow_ {
private:
    Maslow_() = default;  // Make constructor private

public:
    static Maslow_& getInstance();  // Accessor for singleton instance

    Maslow_(const Maslow_&)            = delete;  // no copying
    Maslow_& operator=(const Maslow_&) = delete;

public:
    //main utility functions
    void   begin(void (*sys_rt)());
    void   update();
    void   blinkIPAddress();
    void   print_motor_currents();
    bool   updateEncoderPositions();
    void   setTargets(float xTarget, float yTarget, float zTarget, bool tl = true, bool tr = true, bool bl = true, bool br = true);
    void   setEncoderGeometry(float beltToothSpacing, float encoderTeeth);
    double getTargetX();
    double getTargetY();
    double getTargetZ();
    void   recomputePID();

    //Save and load z-axis position, set z-stop
    void saveZPos();
    void loadZPos();
    void logLoadZPosDebug();
    /** Sets the 'bottom' Z position, this is a 'stop' beyond which travel cannot continue */
    void setZStop();

    //Save and load belt positions
    void saveBeltPositions();
    void loadBeltPositions();
    void markBeltPositionsStale();

    void stopMotors();
    /** Raises Z axis to Z home + 2mm to prevent workpiece damage after stop */
    void raiseZ();

    void   stop();
    void   eStop(String message = "Emergency stop triggered.");
    void   panic();
    void   resetUpdateWatchdog();

    // Set true during file/firmware uploads to suppress the update-loop watchdog.
    // Flash write operations stall both CPU cores; without this flag the 100 ms
    // watchdog fires spuriously and latches the red LED until a power cycle.
    volatile bool uploadInProgress = false;
    String axis_id_to_label(int axis_id);
    void   safety_control();
    bool   axis_homed[4] = { false, false, false, false };

    bool extended[ARM_COUNT] = { false, false, false, false };

    bool takeSlack = false;

    bool safetyOn = true;

    Calibration calibration;

    void getInfo();
    bool telemetry_enabled = false;
    // TODO: probably need to use this for all fields in telemetry, but we'll try without first
    // SemaphoreHandle_t telemetry_mutex = xSemaphoreCreateMutex();
    TelemetryData get_telemetry_data();
    // turns on or off telemetry gathering
    void set_telemetry(bool enabled);
    void dump_telemetry(const char* filename);
    // writes whatever is in teh telemetry buffer to SD card
    void write_telemetry_buffer(uint8_t* buffer, size_t length);

    //These are the current targets set by the setTargets function used for moving the machine during normal operations
    double targetX = 0;
    double targetY = 0;
    double targetZ = 0;

    MotorUnit axis[ARM_COUNT];

    bool readingFromSD = false;  //Used to turn off reading from the encoders when reading from the - i dont think we need this anymore TODO
    bool using_default_config = false;
    QWIICMUX I2CMux;

    bool   error = false;
    String errorMessage;

    void test_();
    void reset_all_axis();
    //keep track of where Maslow actually is
    double x;
    double y;
    float  scaleX = 1.0;
    float  scaleY = 1.0;

    // Work area constraints
    float workAreaX             = 2440.0;
    float workAreaY             = 1220.0;
    float workAreaCenterOffsetX = 0.0;
    float workAreaCenterOffsetY = 0.0;

    // Park position (machine coordinates, Z moved first)
    float parkZ = 2.0;
    float parkX = 0.0;
    float parkY = 0.0;

    bool test         = false;
    bool debugEnabled = false;

    // Direct belt mode: when true, G1 A/B/C/Y moves bypass Cartesian XY kinematics
    // and command the four belt lengths directly.
    // WARNING: This mode bypasses normal Maslow Cartesian kinematics.
    // Enabled by M700, disabled by M701. Not persisted across reboot.
    bool directBeltMode = false;

    // Manual mode PID flag: when calibration.currentState == MANUAL,
    // recomputePID() is called only when this is true.
    // Toggled by the $MMPID command. Automatically cleared when leaving MANUAL state.
    bool manualPIDEnabled = false;

    // PID controller gains - configurable via FluidNC settings (Maslow_PID_P/I/D_gain).
    // Applied to all four belt motors. Default: P=300, I=0, D=0.
    float pidP = 300.0f;
    float pidI = 0.0f;
    float pidD = 0.0f;

    // PID deadband thresholds - configurable via FluidNC settings.
    // Output is held at zero while |error| <= pidDeadbandEnter; normal PID resumes
    // once |error| > pidDeadbandExit. Both zero (default) disables the feature.
    float pidDeadbandEnter = 0.0f;
    float pidDeadbandExit  = 0.0f;

    // Propagates current pidP/pidI/pidD values to all four motor unit PID controllers.
    // Called automatically when settings are saved (MachineConfig::afterParse).
    void updatePIDGains();

    // Propagates current pidDeadbandEnter/Exit values to all four motor unit PID controllers.
    // Called automatically when settings are saved (MachineConfig::afterParse).
    void updatePIDDeadband();

private:
    //Used to keep track of how often the PID controller is updated
    unsigned long lastCallToPID    = millis();
    unsigned long lastMiss         = millis();
    unsigned long lastCallToUpdate = millis();
    unsigned long extendCallTimer  = millis();
    unsigned long complyCallTimer  = millis();
    unsigned int  panicCount       = 0;
    bool          watchdogFired    = false;

    //Stores a reference to the global system runtime function to be called when blocking operations are needed
    void (*_sys_rt)() = nullptr;

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

    bool HeartBeatEnabled = true;
    void log_telem_hdr_csv();
    void log_telem_pt_csv(TelemetryData data);
    float currentZHome() const;
};

extern Maslow_& Maslow;

// actual task loop for gathering telemetry data (runs on utility core)
void telemetry_loop(void* unused);
