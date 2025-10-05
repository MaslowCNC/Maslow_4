// Copyright (c) 2024 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#include "Maslow.h"
#include "../Report.h"
#include "../WebUI/WifiConfig.h"
#include "../Protocol.h"
#include "../System.h"
#include "../FileStream.h"
#include "../Kinematics/MaslowKinematics.h"

// Maslow specific defines
#define VERSION_NUMBER "1.12"

#define TLEncoderLine 2
#define TREncoderLine 1
#define BLEncoderLine 3
#define BREncoderLine 0

#define motorPWMFreq 2000
#define motorPWMRes 10

#define tlIn1Pin 45
#define tlIn1Channel 0
#define tlIn2Pin 21
#define tlIn2Channel 1
#define tlADCPin 18

#define trIn1Pin 42
#define trIn1Channel 2
#define trIn2Pin 41
#define trIn2Channel 3
#define trADCPin 6

#define blIn1Pin 37
#define blIn1Channel 4
#define blIn2Pin 36
#define blIn2Channel 5
#define blADCPin 8

#define brIn1Pin 9
#define brIn1Channel 6
#define brIn2Pin 3
#define brIn2Channel 7
#define brADCPin 7

#define coolingFanPin 47

#define SERVOFAULT 40

#define ETHERNETLEDPIN 39
#define WIFILED 35
#define REDLED 14

int ENCODER_READ_FREQUENCY_HZ = 1000;  //max frequency for polling the encoders

//------------------------------------------------------
//------------------------------------------------------ Main Function Loops
//------------------------------------------------------

// Initialization function
void Maslow_::begin(void (*sys_rt)()) {
    log_info("Maslow_::begin() ENTRY - first line of function");
    Wire.begin(5, 4, 200000);
    log_info("Wire.begin() completed");
    I2CMux.begin(TCAADDR, Wire);
    log_info("I2CMux.begin() completed");

    axisTL.begin(tlIn1Pin, tlIn2Pin, tlADCPin, TLEncoderLine, tlIn1Channel, tlIn2Channel);
    log_info("axisTL.begin() completed");
    axisTR.begin(trIn1Pin, trIn2Pin, trADCPin, TREncoderLine, trIn1Channel, trIn2Channel);
    log_info("axisTR.begin() completed");
    axisBL.begin(blIn1Pin, blIn2Pin, blADCPin, BLEncoderLine, blIn1Channel, blIn2Channel);
    log_info("axisBL.begin() completed");
    axisBR.begin(brIn1Pin, brIn2Pin, brADCPin, BREncoderLine, brIn1Channel, brIn2Channel);
    log_info("axisBR.begin() completed");

    calibration.axisBLHomed = false;
    calibration.axisBRHomed = false;
    calibration.axisTRHomed = false;
    calibration.axisTLHomed = false;

    //Recompute the center XY
    calibration.updateCenterXY();

    pinMode(coolingFanPin, OUTPUT);
    pinMode(ETHERNETLEDPIN, OUTPUT);
    pinMode(WIFILED, OUTPUT);
    pinMode(REDLED, OUTPUT);

    digitalWrite(ETHERNETLEDPIN, LOW);

    pinMode(SERVOFAULT, INPUT);

    lastCallToUpdate = millis();

    log_info("Maslow.begin() called - about to check if positions should be loaded");

    // Only load positions from NVS on the first call to begin() (initial power-on)
    // Subsequent calls (e.g., after soft reset) should not reload stale NVS data
    static bool positionsLoaded = false;
    log_info("positionsLoaded flag is: " << (positionsLoaded ? "true" : "false"));
    if (!positionsLoaded) {
        log_info("Calling loadZPos() and loadBeltPositions()");
        loadZPos();           //Loads the z-axis position from EEPROM
        loadBeltPositions();  //Loads the belt positions from EEPROM
        positionsLoaded = true;
    } else {
        log_info("Skipping NVS load - positions already loaded");
    }

    stopMotors();

    Wire.setTimeOut(10);

    if (error) {
        log_error(M + " failed to initialize - fix errors and restart");
    } else {
        log_info("Starting " + M + " Version " << VERSION_NUMBER);
    }
}

// Maslow main loop, everything is processed here
void Maslow_::update() {
    static State prevState = sys.state();

    //If we are in an error state, blink the LED and stop the motors
    if (error) {
        static unsigned long timer = millis();
        static bool          st    = true;  //This is used to blink the LED
        if (millis() - timer > 300) {
            stopMotors();
            st = !st;
            digitalWrite(REDLED, st);
            timer = millis();
            if (errorMessage != "") {
                log_error(errorMessage.c_str());
            }
            errorMessage = "";
        }
        return;
    }

    //Blinks the Ethernet LEDs randomly...these have been removed from the board, this pin should be freed up
    if (random(10000) == 0) {
        digitalWrite(ETHERNETLEDPIN, LOW);
    }
    if (random(10000) == 0) {
        digitalWrite(ETHERNETLEDPIN, HIGH);
    }

    //Save the z-axis position if the prevous state was jog or cycle and the current state is idle
    if ((prevState == State::Jog || prevState == State::Cycle) && sys.state() == State::Idle) {
        saveZPos();
        // Only save belt positions when in READY_TO_CUT or RETRACTED state (belts are tight and valid)
        int currentMaslowState = calibration.getCurrentState();
        if (currentMaslowState == READY_TO_CUT || currentMaslowState == RETRACTED) {
            saveBeltPositions();
        }
    }

    // Track state changes and mark belt positions as stale when leaving READY_TO_CUT or RETRACTED
    static int prevMaslowState    = calibration.getCurrentState();
    int        currentMaslowState = calibration.getCurrentState();
    if (prevMaslowState != currentMaslowState) {
        // If we're leaving READY_TO_CUT or RETRACTED, mark positions as stale
        if ((prevMaslowState == READY_TO_CUT || prevMaslowState == RETRACTED) &&
            (currentMaslowState != READY_TO_CUT && currentMaslowState != RETRACTED)) {
            markBeltPositionsStale();
        }
        prevMaslowState = currentMaslowState;
    }

    blinkIPAddress();

    //Make sure we're running maslow config file
    if (!Maslow.using_default_config) {
        lastCallToUpdate = millis();

        Maslow.updateEncoderPositions();  //We always update encoder positions in any state,

        axisTL.update();  //update motor currents and belt speeds like this for now
        axisTR.update();
        axisBL.update();
        axisBR.update();

        if (safetyOn)
            safety_control();

        //quick solution for delay without blocking
        if (calibration.holding && millis() - calibration.holdTimer > calibration.holdTime) {
            calibration.holding = false;
        } else if (calibration.holding)
            return;

        //temp test function...This is used for debugging when the test command is sent
        if (test) {
            test = false;
        }

        //------------------------ Maslow State Machine

        //-------Jog or G-code execution.
        if (sys.state() == State::Jog || sys.state() == State::Cycle) {
            // With MaslowKinematics, read belt motor positions directly from the axis system
            // Axis mapping: A=TL, B=TR, C=BL, D=BR, Z=Router
            float tlBeltLength = steps_to_mpos(get_axis_motor_steps(0), 0);  // TL from A axis (axis 0)
            float trBeltLength = steps_to_mpos(get_axis_motor_steps(1), 1);  // TR from B axis (axis 1)
            float blBeltLength = steps_to_mpos(get_axis_motor_steps(2), 2);  // BL from C axis (axis 2)
            float brBeltLength = steps_to_mpos(get_axis_motor_steps(3), 3);  // BR from D axis (axis 3)
            float zPosition    = steps_to_mpos(get_axis_motor_steps(4), 4);  // Z from Z axis (axis 4)

            // Set individual belt targets using the computed positions
            axisTL.setTarget(tlBeltLength);
            axisTR.setTarget(trBeltLength);
            axisBL.setTarget(blBeltLength);
            axisBR.setTarget(brBeltLength);

            // Update internal target tracking for getTargetX/Y/Z functions
            // For now, we'll use the Z position directly and estimate X,Y from frame center
            // A full implementation would require solving inverse kinematics from belt lengths
            targetZ = zPosition;
            // Simple approximation - this could be improved with proper inverse kinematics
            // For now, we'll assume we're near the center if we don't have better information
            if (targetX == 0 && targetY == 0) {
                using namespace Kinematics;
                MaslowKinematics* kinematics = getMaslowKinematics();
                if (kinematics) {
                    targetX = (kinematics->getTrX() + kinematics->getBlX()) / 2.0f - kinematics->getCenterX();  // Approximate center X
                    targetY = (kinematics->getTrY() + kinematics->getBlY()) / 2.0f - kinematics->getCenterY();  // Approximate center Y
                }
            }

            //We used to call Maslow.setTargets() here, but now we use the axis system directly

            //This disables the belt motors until the user has completed calibration or apply tension and they have succeeded
            if (calibration.currentState == READY_TO_CUT) {
                Maslow.recomputePID();
            }
        }
        //--------Homing routines
        else if (sys.state() == State::Homing) {
            calibration.home();
        } else {  //This is confusing to understand. This is an else if so this is only run if we are not in jog, cycle, or homing
            Maslow.stopMotors();
        }

        //If we are in any state other than idle or alarm turn the cooling fan on
        if (sys.state() != State::Idle && sys.state() != State::Alarm) {
            digitalWrite(coolingFanPin, HIGH);  //keep the cooling fan on
        }
        //If we are doing calibration turn the cooling fan on
        else if (calibration.calibrationInProgress || extendingALL || retractingTL || retractingTR || retractingBL || retractingBR) {
            digitalWrite(coolingFanPin, HIGH);  //keep the cooling fan on
        } else {
            digitalWrite(coolingFanPin, LOW);  //Turn the cooling fan off
        }

        //Check to see if we need to resend the calibration data
        calibration.checkCalibrationData();

        //------------------------ End of Maslow State Machine

        //if the update function is not being called enough, stop everything to prevent damage
        if (millis() - lastCallToUpdate > 100) {
            Maslow.panic();
            int elapsedTime = millis() - lastCallToUpdate;
            log_error("Emergency stop. Update function not being called enough." << elapsedTime << "ms since last call");
        }

        print_motor_currents();
    }

    prevState = sys.state();  //Store for next time
}

//------------------------------------------------------
//------------------------------------------------------ Position Control Functions
//------------------------------------------------------

//updating encoder positions for all 4 arms, cycling through them each call, at ENCODER_READ_FREQUENCY_HZ frequency
bool Maslow_::updateEncoderPositions() {
    bool                 success               = true;
    static unsigned long lastCallToEncoderRead = millis();

    static int           encoderFailCounter[4] = { 0, 0, 0, 0 };
    static unsigned long encoderFailTimer      = millis();

    if (!readingFromSD && (millis() - lastCallToEncoderRead > 1000 / (ENCODER_READ_FREQUENCY_HZ))) {
        static int encoderToRead = 0;
        switch (encoderToRead) {
            case 0:
                if (!axisTL.updateEncoderPosition()) {
                    encoderFailCounter[TLEncoderLine]++;
                }
                break;
            case 1:
                if (!axisTR.updateEncoderPosition()) {
                    encoderFailCounter[TREncoderLine]++;
                }
                break;
            case 2:
                if (!axisBL.updateEncoderPosition()) {
                    encoderFailCounter[BLEncoderLine]++;
                }
                break;
            case 3:
                if (!axisBR.updateEncoderPosition()) {
                    encoderFailCounter[BREncoderLine]++;
                }
                break;
        }
        encoderToRead++;
        if (encoderToRead > 3) {
            encoderToRead         = 0;
            lastCallToEncoderRead = millis();
        }
    }

    // if more than 1% of readings fail, warn user, if more than 10% fail, stop the machine and raise alarm
    if (millis() - encoderFailTimer > 1000) {
        for (int i = 0; i < 4; i++) {
            //turn i into proper label
            String label = axis_id_to_label(i);
            if (encoderFailCounter[i] > 0.1 * ENCODER_READ_FREQUENCY_HZ) {
                // log error statement with appropriate label
                log_error("Failure on " << label.c_str() << " encoder, failed to read " << encoderFailCounter[i]
                                        << " times in the last second");
                Maslow.panic();
            } else if (encoderFailCounter[i] > 0) {  //0.01*ENCODER_READ_FREQUENCY_HZ){
                log_warn("Bad connection on " << label.c_str() << " encoder, failed to read " << encoderFailCounter[i]
                                              << " times in the last second");
            }
            encoderFailCounter[i] = 0;
            encoderFailTimer      = millis();
        }
    }

    return success;
}

// This computes the target lengths of the belts based on the target x and y coordinates and sends that information to each arm.
void Maslow_::setTargets(float xTarget, float yTarget, float zTarget, bool tl, bool tr, bool bl, bool br) {
    //Store the target x and y coordinates for the getTargetN() functions
    targetX = xTarget;
    targetY = yTarget;
    targetZ = zTarget;

    using namespace Kinematics;
    MaslowKinematics* kinematics = getMaslowKinematics();
    if (!kinematics) {
        log_error("MaslowKinematics not available");
        return;
    }

    if (tl) {
        axisTL.setTarget(kinematics->computeTL(xTarget, yTarget, zTarget));
    }
    if (tr) {
        axisTR.setTarget(kinematics->computeTR(xTarget, yTarget, zTarget));
    }
    if (bl) {
        axisBL.setTarget(kinematics->computeBL(xTarget, yTarget, zTarget));
    }
    if (br) {
        axisBR.setTarget(kinematics->computeBR(xTarget, yTarget, zTarget));
    }
}

// Get's the most recently set target position in X
double Maslow_::getTargetX() {
    return targetX;
}

// Get's the most recently set target position in Y
double Maslow_::getTargetY() {
    return targetY;
}

//Get's the most recently set target position in Z
double Maslow_::getTargetZ() {
    return targetZ;
}

//updates motor powers for all axis, based on targets set by setTargets()
void Maslow_::recomputePID() {
    axisBL.recomputePID();
    axisBR.recomputePID();
    axisTR.recomputePID();
    axisTL.recomputePID();

    digitalWrite(coolingFanPin, HIGH);  //keep the cooling fan on

    if (digitalRead(SERVOFAULT) ==
        1) {  //The servo drives have a fault pin that goes high when there is a fault (ie one over heats). We should probably call panic here. Also this should probably be read in the main loop
        log_info("Servo fault!");
    }
}

//------------
// Z-Axis Functions
//------------

//This function saves the current z-axis position to the non-volitle storage
void Maslow_::saveZPos() {
    nvs_handle_t nvsHandle;
    esp_err_t    ret = nvs_open("maslow", NVS_READWRITE, &nvsHandle);
    if (ret != ESP_OK) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " opening NVS handle!\n");
        return;
    }

    // Read the current value
    int32_t currentZPos;
    ret = nvs_get_i32(nvsHandle, "zPos", &currentZPos);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " reading from NVS!\n");
        return;
    }

    // Write - Convert the float to an int32_t and write only if it has changed
    union FloatInt32 {
        float   f;
        int32_t i;
    };
    FloatInt32 fi;
    fi.f = targetZ;
    if (ret == ESP_ERR_NVS_NOT_FOUND || currentZPos != fi.i) {  // Only write if the value has changed
        ret = nvs_set_i32(nvsHandle, "zPos", fi.i);
        if (ret != ESP_OK) {
            log_info("Error " + std::string(esp_err_to_name(ret)) + " writing to NVS!\n");
        } else {
            //log_info("Written value = " + std::to_string(targetZ));

            // Commit written value to non-volatile storage
            ret = nvs_commit(nvsHandle);
            if (ret != ESP_OK) {
                log_info("Error " + std::string(esp_err_to_name(ret)) + " committing changes to NVS!\n");
            }
        }
    }
}

//This function loads the z-axis position from the non-volitle storage
void Maslow_::loadZPos() {
    nvs_handle_t nvsHandle;
    esp_err_t    ret = nvs_open("maslow", NVS_READWRITE, &nvsHandle);
    if (ret != ESP_OK) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " opening NVS handle!\n");
        return;
    }

    // Read
    int32_t value2;
    ret = nvs_get_i32(nvsHandle, "zPos", &value2);
    if (ret != ESP_OK) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " reading from NVS!");
    } else {
        union FloatInt32 {
            float   f;
            int32_t i;
        };
        FloatInt32 fi;
        fi.i    = value2;
        targetZ = fi.f;

        // Use Z_AXIS constant (2) for cartesian coordinate, not motor index (4)
        float* mpos  = get_mpos();
        mpos[Z_AXIS] = targetZ;
        set_motor_steps_from_mpos(mpos);

        log_info("Current z-axis position loaded as: " << targetZ);

        gc_sync_position();  //This updates the Gcode engine with the new position from the stepping engine that we set with set_motor_steps
        plan_sync_position();
    }
}

/** Sets the 'bottom' Z position, this is a 'stop' beyond which travel cannot continue */
void Maslow_::setZStop() {
    log_info("Setting z-stop position");

    targetZ = 0;

    // Use Z_AXIS constant (2) for cartesian coordinate, not motor index (4)
    float* mpos  = get_mpos();
    mpos[Z_AXIS] = targetZ;
    set_motor_steps_from_mpos(mpos);

    gc_sync_position();  //This updates the Gcode engine with the new position from the stepping engine that we set with set_motor_steps
    plan_sync_position();
}

//This function saves the current belt positions to non-volatile storage
void Maslow_::saveBeltPositions() {
    // Only save if in a valid state (belts are tight)
    int currentState = calibration.getCurrentState();
    if (currentState != READY_TO_CUT && currentState != RETRACTED) {
        log_debug("Belt positions NOT saved - invalid state (not READY_TO_CUT or RETRACTED), currentState=" << currentState);
        return;
    }

    nvs_handle_t nvsHandle;
    esp_err_t    ret = nvs_open("maslow", NVS_READWRITE, &nvsHandle);
    if (ret != ESP_OK) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " opening NVS handle for belt positions!\n");
        return;
    }

    // Union for float to int32_t conversion
    union FloatInt32 {
        float   f;
        int32_t i;
    };

    // Get current belt positions
    float tlPos = axisTL.getPosition();
    float trPos = axisTR.getPosition();
    float blPos = axisBL.getPosition();
    float brPos = axisBR.getPosition();

    // Save TL belt position
    int32_t currentTLPos;
    ret = nvs_get_i32(nvsHandle, "tlPos", &currentTLPos);
    FloatInt32 tlFi;
    tlFi.f = tlPos;
    if (ret == ESP_ERR_NVS_NOT_FOUND || currentTLPos != tlFi.i) {
        ret = nvs_set_i32(nvsHandle, "tlPos", tlFi.i);
        if (ret != ESP_OK) {
            log_info("Error " + std::string(esp_err_to_name(ret)) + " writing TL belt position to NVS!\n");
        }
    }

    // Save TR belt position
    int32_t currentTRPos;
    ret = nvs_get_i32(nvsHandle, "trPos", &currentTRPos);
    FloatInt32 trFi;
    trFi.f = trPos;
    if (ret == ESP_ERR_NVS_NOT_FOUND || currentTRPos != trFi.i) {
        ret = nvs_set_i32(nvsHandle, "trPos", trFi.i);
        if (ret != ESP_OK) {
            log_info("Error " + std::string(esp_err_to_name(ret)) + " writing TR belt position to NVS!\n");
        }
    }

    // Save BL belt position
    int32_t currentBLPos;
    ret = nvs_get_i32(nvsHandle, "blPos", &currentBLPos);
    FloatInt32 blFi;
    blFi.f = blPos;
    if (ret == ESP_ERR_NVS_NOT_FOUND || currentBLPos != blFi.i) {
        ret = nvs_set_i32(nvsHandle, "blPos", blFi.i);
        if (ret != ESP_OK) {
            log_info("Error " + std::string(esp_err_to_name(ret)) + " writing BL belt position to NVS!\n");
        }
    }

    // Save BR belt position
    int32_t currentBRPos;
    ret = nvs_get_i32(nvsHandle, "brPos", &currentBRPos);
    FloatInt32 brFi;
    brFi.f = brPos;
    if (ret == ESP_ERR_NVS_NOT_FOUND || currentBRPos != brFi.i) {
        ret = nvs_set_i32(nvsHandle, "brPos", brFi.i);
        if (ret != ESP_OK) {
            log_info("Error " + std::string(esp_err_to_name(ret)) + " writing BR belt position to NVS!\n");
        }
    }

    // Mark the data as valid
    ret = nvs_set_i32(nvsHandle, "beltValid", 1);
    if (ret != ESP_OK) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " writing belt validity marker to NVS!\n");
    }

    // Commit all changes to non-volatile storage
    ret = nvs_commit(nvsHandle);
    if (ret != ESP_OK) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " committing belt position changes to NVS!\n");
    }

    nvs_close(nvsHandle);

    // Log the save operation
    log_debug("Belt positions saved to NVS: TL=" << tlPos << " TR=" << trPos << " BL=" << blPos << " BR=" << brPos
                                                 << " state=" << currentState);
}

//This function loads the belt positions from non-volatile storage
void Maslow_::loadBeltPositions() {
    log_info("loadBeltPositions() called");

    nvs_handle_t nvsHandle;
    esp_err_t    ret = nvs_open("maslow", NVS_READWRITE, &nvsHandle);
    if (ret != ESP_OK) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " opening NVS handle for belt positions!\n");
        return;
    }

    // Check if the data is valid
    int32_t validityMarker;
    ret = nvs_get_i32(nvsHandle, "beltValid", &validityMarker);
    log_info("Validity check: ret=" << ret << " validityMarker=" << validityMarker);
    if (ret != ESP_OK || validityMarker != 1) {
        log_debug("Belt positions NOT loaded from NVS - data is stale/invalid or not found");
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            log_info("No saved belt positions found in NVS - machine will remain in UNKNOWN state until belts are calibrated/extended");
        } else if (validityMarker != 1) {
            log_info("Saved belt positions in NVS are marked as stale/invalid (value="
                     << validityMarker << ") - machine will remain in UNKNOWN state until belts are calibrated/extended");
        }
        nvs_close(nvsHandle);
        return;
    }

    // Union for int32_t to float conversion
    union FloatInt32 {
        float   f;
        int32_t i;
    };

    // Load TL belt position
    int32_t tlValue;
    ret         = nvs_get_i32(nvsHandle, "tlPos", &tlValue);
    float tlPos = 0;
    if (ret == ESP_OK) {
        FloatInt32 tlFi;
        tlFi.i = tlValue;
        tlPos  = tlFi.f;
        // Set motor steps directly for TL belt (A axis = motor 0)
        set_motor_steps(0, mpos_to_steps(tlFi.f, 0));
        axisTL.setTarget(tlFi.f);
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " reading TL belt position from NVS!");
    }

    // Load TR belt position
    int32_t trValue;
    ret         = nvs_get_i32(nvsHandle, "trPos", &trValue);
    float trPos = 0;
    if (ret == ESP_OK) {
        FloatInt32 trFi;
        trFi.i = trValue;
        trPos  = trFi.f;
        // Set motor steps directly for TR belt (B axis = motor 1)
        set_motor_steps(1, mpos_to_steps(trFi.f, 1));
        axisTR.setTarget(trFi.f);
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " reading TR belt position from NVS!");
    }

    // Load BL belt position
    int32_t blValue;
    ret         = nvs_get_i32(nvsHandle, "blPos", &blValue);
    float blPos = 0;
    if (ret == ESP_OK) {
        FloatInt32 blFi;
        blFi.i = blValue;
        blPos  = blFi.f;
        // Set motor steps directly for BL belt (C axis = motor 2)
        set_motor_steps(2, mpos_to_steps(blFi.f, 2));
        axisBL.setTarget(blFi.f);
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " reading BL belt position from NVS!");
    }

    // Load BR belt position
    int32_t brValue;
    ret         = nvs_get_i32(nvsHandle, "brPos", &brValue);
    float brPos = 0;
    if (ret == ESP_OK) {
        FloatInt32 brFi;
        brFi.i = brValue;
        brPos  = brFi.f;
        // Set motor steps directly for BR belt (D axis = motor 3)
        set_motor_steps(3, mpos_to_steps(brFi.f, 3));
        axisBR.setTarget(brFi.f);
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " reading BR belt position from NVS!");
    }

    nvs_close(nvsHandle);

    // Sync position with G-code parser and planner
    gc_sync_position();
    plan_sync_position();

    // Determine the state based on belt lengths
    // If all belt lengths are zero, go to RETRACTED state, otherwise READY_TO_CUT
    bool allZero  = (tlPos == 0 && trPos == 0 && blPos == 0 && brPos == 0);
    int  newState = allZero ? RETRACTED : READY_TO_CUT;

    // Set the state directly (bypass requestStateChange validation)
    // This is appropriate when restoring a known-good saved state at boot time
    // requestStateChange() would reject READY_TO_CUT from UNKNOWN state
    calibration.currentState = newState;
    log_info("Belt position load: Set currentState directly to " << (newState == READY_TO_CUT ? "READY_TO_CUT" : "RETRACTED"));
    
    // Disable alarm if present
    if (sys.state() == State::Alarm) {
        sys.set_state(State::Idle);
    }

    log_debug("Belt positions loaded from NVS: TL=" << tlPos << " TR=" << trPos << " BL=" << blPos << " BR=" << brPos
                                                    << " newState=" << newState);
}

//This function marks the belt positions in NVS as stale/invalid
void Maslow_::markBeltPositionsStale() {
    nvs_handle_t nvsHandle;
    esp_err_t    ret = nvs_open("maslow", NVS_READWRITE, &nvsHandle);
    if (ret != ESP_OK) {
        log_info("Error " + std::string(esp_err_to_name(ret)) + " opening NVS handle for belt positions!\n");
        return;
    }

    // Read the current validity marker value
    int32_t currentValid;
    ret = nvs_get_i32(nvsHandle, "beltValid", &currentValid);

    // Only write if the value needs to change (not already 0)
    // This preserves NVS write cycles since NVS has a finite write limit
    if (ret == ESP_ERR_NVS_NOT_FOUND || currentValid != 0) {
        // Mark the data as invalid by setting validity marker to 0
        ret = nvs_set_i32(nvsHandle, "beltValid", 0);
        if (ret != ESP_OK) {
            log_info("Error " + std::string(esp_err_to_name(ret)) + " writing belt validity marker to NVS!\n");
        } else {
            // Commit the change
            ret = nvs_commit(nvsHandle);
            if (ret != ESP_OK) {
                log_info("Error " + std::string(esp_err_to_name(ret)) + " committing belt validity change to NVS!\n");
            }
        }
    }

    nvs_close(nvsHandle);

    int currentState = calibration.getCurrentState();
    log_debug("Belt positions marked as stale/invalid in NVS, state=" << currentState);
}

//------------------------------------------------------
//------------------------------------------------------ Utility Functions
//------------------------------------------------------

// int to string name conversion for axis labels
String Maslow_::axis_id_to_label(int axis_id) {
    String label;
    switch (axis_id) {
        case TLEncoderLine:
            label = "Top Left";
            break;
        case TREncoderLine:
            label = "Top Right";
            break;
        case BREncoderLine:
            label = "Bottom Right";
            break;
        case BLEncoderLine:
            label = "Bottom Left";
            break;
    }
    return label;
}

//Runs the self test feature
void Maslow_::test_() {
    log_info("Firmware Version: " << VERSION_NUMBER);

    log_info("I2C Timeout: ");
    log_info(Wire.getTimeOut());

    axisTL.test();
    axisTR.test();
    axisBL.test();
    axisBR.test();
}

//Blinks out the IP address of the machine on the blue LED
void Maslow_::blinkIPAddress() {
    static size_t        currentChar   = 0;
    static int           currentBlink  = 0;
    static unsigned long lastBlinkTime = 0;
    static bool          inPause       = false;

    int shortMS = 400;
    int longMS  = 500;
    int pauseMS = 2000;

    std::string IP_String = WebUI::wifi_config.getIP();

    if (currentChar >= IP_String.length()) {
        currentChar   = 0;
        currentBlink  = 0;
        lastBlinkTime = 0;
        inPause       = false;
        digitalWrite(WIFILED, LOW);
        return;
    }

    char c = IP_String[currentChar];
    if (isdigit(c)) {
        int blinkCount = c - '0';
        if (currentBlink < blinkCount * 2) {
            if (millis() - lastBlinkTime >= shortMS) {
                //log_info("Blinking Digit: " << c);
                digitalWrite(WIFILED, currentBlink % 2 == 0 ? HIGH : LOW);
                currentBlink++;
                lastBlinkTime = millis();
            }
        } else if (!inPause) {
            inPause       = true;
            lastBlinkTime = millis();
        } else if (millis() - lastBlinkTime >= pauseMS) {
            inPause = false;
            currentChar++;
            currentBlink = 0;
        }
    } else if (c == '.') {
        if (millis() - lastBlinkTime >= longMS) {
            digitalWrite(WIFILED, LOW);
            currentChar++;
            currentBlink = 0;
        } else {
            digitalWrite(WIFILED, HIGH);
        }
    }
}

//Print the motor currents. Used for monitioring the power consumption of each motor.
void Maslow_::print_motor_currents() {
    if (!debugEnabled) {
        return;
    }

    static unsigned long lastExecutionTime = 0;
    unsigned long        currentTime       = millis();

    if (currentTime - lastExecutionTime >= 1000) {
        log_info("TLC: " << axisTL.getMotorCurrent() << " TRC: " << axisTR.getMotorCurrent() << " BLC: " << axisBL.getMotorCurrent()
                         << " BRC: " << axisBR.getMotorCurrent());
        lastExecutionTime = currentTime;
    }
}

//------------------------------------------------------
//------------------------------------------------------ Stops    TODO: Do we need all of these? Can some be combigned or reduced?
//------------------------------------------------------

// Resets variables on all 4 axis
void Maslow_::reset_all_axis() {
    axisTL.reset();
    axisTR.reset();
    axisBL.reset();
    axisBR.reset();
}

// Stop all motors and reset all state variables
void Maslow_::stop() {
    stopMotors();
    retractingTL                      = false;
    retractingTR                      = false;
    retractingBL                      = false;
    retractingBR                      = false;
    extendingALL                      = false;
    complyALL                         = false;
    calibration.calibrationInProgress = false;
    test                              = false;
    takeSlack                         = false;

    axisTL.reset();
    axisTR.reset();
    axisBL.reset();
    axisBR.reset();

    // if we are stopping, stop any running job too
    allChannels.stopJob();
}

// Stop all the motors
void Maslow_::stopMotors() {
    axisBL.stop();
    axisBR.stop();
    axisTR.stop();
    axisTL.stop();
}

static void stopEverything() {
    sys.set_state(State::Alarm);
    protocol_disable_steppers();
}

// Panic function, stops all motors and sets state to alarm
void Maslow_::panic() {
    stop();
    stopEverything();
}

//Emergecy Stop
void Maslow_::eStop(String message) {
    log_error("Emergency stop! Stopping all motors");
    log_warn("The machine will not respond until turned off and back on again");
    stop();
    error        = true;
    errorMessage = message;
    stopEverything();
}

//This is the function that should prevent machine from damaging itself
void Maslow_::safety_control() {
    //We need to keep track of average belt speeds and motor currents for every axis
    static bool          tick[4]                 = { false, false, false, false };
    static unsigned long spamTimer               = millis();
    static int           tresholdHitsBeforePanic = 150;
    static int           panicCounter[4]         = { 0 };

    static int   positionErrorCounter[4]  = { 0 };
    static float previousPositionError[4] = { 0, 0, 0, 0 };

    MotorUnit* axis[4] = { &axisTL, &axisTR, &axisBL, &axisBR };
    for (int i = 0; i < 4; i++) {
        //If the current exceeds some absolute value, we need to call panic() and stop the machine
        if (axis[i]->getMotorCurrent() > 4000 && !tick[i]) {
            panicCounter[i]++;
            if (panicCounter[i] > tresholdHitsBeforePanic) {
                if (sys.state() == State::Jog || sys.state() == State::Cycle) {
                    log_warn("Motor current on " << axis_id_to_label(i).c_str() << " axis exceeded threshold of " << 4000);
                    //Maslow.panic();
                }
                tick[i] = true;
            }
        } else {
            panicCounter[i] = 0;
        }

        //If the motor torque is high, but the belt is not moving
        //  if motor is moving IN, this means the axis is STALL, we should warn the user and lower torque to the motor
        //  if the motor is moving OUT, that means the axis has SLACK, so we should warn the user and stop the motor, until the belt starts moving again
        // don't spam log, no more than once every 5 seconds

        static int axisSlackCounter[4] = { 0, 0, 0, 0 };

        axisSlackCounter[i] = 0;  //TEMP
        if (axis[i]->getMotorPower() > 450 && abs(axis[i]->getBeltSpeed()) < 0.1 && !tick[i]) {
            axisSlackCounter[i]++;
            if (axisSlackCounter[i] > 3000) {
                // log_info("SLACK:" << axis_id_to_label(i).c_str() << " motor power is " << int(axis[i]->getMotorPower())
                //                   << ", but the belt speed is" << axis[i]->getBeltSpeed());
                // log_info(axisSlackCounter[i]);
                // log_info("Pull on " << axis_id_to_label(i).c_str() << " and restart!");
                tick[i]             = true;
                axisSlackCounter[i] = 0;
                Maslow.panic();
            }
        } else
            axisSlackCounter[i] = 0;

        //If the motor has a position error greater than 1mm and we are running a file or jogging
        if ((abs(axis[i]->getPositionError()) > 1) && (sys.state() == State::Jog || sys.state() == State::Cycle) && !tick[i]) {
            // log_error("Position error on " << axis_id_to_label(i).c_str() << " axis exceeded 1mm, error is " << axis[i]->getPositionError()
            //                                << "mm");
            tick[i] = true;
        }

        //If the motor has a position error greater than 15mm and we are running a file or jogging
        previousPositionError[i] = axis[i]->getPositionError();
        if ((abs(axis[i]->getPositionError()) > 15) && (sys.state() == State::Cycle)) {
            positionErrorCounter[i]++;
            log_warn("Position error on " << axis_id_to_label(i).c_str() << " axis exceeded 15mm while running. Error is "
                                          << axis[i]->getPositionError() << "mm" << " Counter: " << positionErrorCounter[i]);
            log_warn("Previous error was " << previousPositionError[i] << "mm");

            if (positionErrorCounter[i] > 5) {
                Maslow.eStop("Position error > 15mm while running. E-Stop triggered.");
            }
        } else {
            positionErrorCounter[i] = 0;
        }
    }

    if (millis() - spamTimer > 5000) {
        for (int i = 0; i < 4; i++) {
            tick[i] = false;
        }
        spamTimer = millis();
    }
}

//---------------
// Telemetry
//---------------

// Prints out state
void Maslow_::getInfo() {
    log_data("MINFO: { \"homed\": " << (calibration.all_axis_homed() ? "true" : "false") << ","
                                    << "\"calibrationInProgress\": " << (calibration.calibrationInProgress ? "true" : "false") << ","
                                    << "\"tl\": " << axisTL.getPosition() << "," << "\"tr\": " << axisTR.getPosition() << ","
                                    << "\"br\": " << axisBR.getPosition() << "," << "\"bl\": " << axisBL.getPosition() << ","
                                    << "\"etl\": " << axisTL.getPositionError() << "," << "\"etr\": " << axisTR.getPositionError() << ","
                                    << "\"ebr\": " << axisBR.getPositionError() << "," << "\"ebl\": " << axisBL.getPositionError() << ","
                                    << "\"extended\": " << (calibration.allAxisExtended() ? "true" : "false") << "}");
}

void Maslow_::set_telemetry(bool enabled) {
    if (enabled) {
        // Start off the file with the length of each struct in it.
        FileStream* file = new FileStream(MASLOW_TELEM_FILE, "w", "sd");
        // write header
        TelemetryFileHeader header;
        header.structureSize = sizeof(TelemetryData);
        strcpy(header.version, VERSION_NUMBER);
        file->write(reinterpret_cast<uint8_t*>(&header), sizeof(TelemetryFileHeader));
        file->flush();
        delete file;
    } else {
        // TODO: not sure why this fails to find the file
        // std::string filePath = MASLOW_TELEM_FILE;
        // std::string newFilePath = filePath + "." + std::to_string(millis());
        // std::error_code ec;
        // log_info("renaming file: " + filePath + " to " + newFilePath);
        // stdfs::rename(filePath, newFilePath, ec);
        // if (ec) {
        //     log_error(std::string("Error renaming file: ") + ec.message());
        // }
    }
    telemetry_enabled = enabled;
    log_info("Telemetry: " << (enabled ? "enabled" : "disabled"));
}

void Maslow_::log_telem_hdr_csv() {
    log_data("millis," << "tlCurrent," << "trCurrent," << "blCurrent," << "brCurrent," << "tlPower," << "trPower," << "blPower,"
                       << "brPower," << "tlSpeed," << "trSpeed," << "blSpeed," << "brSpeed," << "tlPos," << "trPos," << "blPos," << "brPos,"
                       << "extendedTL," << "extendedTR," << "extendedBL," << "extendedBR," << "extendingALL," << "complyALL,"
                       << "takeSlack," << "safetyOn," << "targetX," << "targetY," << "targetZ," << "x," << "y," << "test," << "pointCount,"
                       << "waypoint," << "calibrationGridSize," << "holdTimer," << "holding," << "holdTime," << "centerX," << "centerY,"
                       << "lastCallToPID," << "lastMiss," << "lastCallToUpdate," << "extendCallTimer," << "complyCallTimer");
}

void Maslow_::log_telem_pt_csv(TelemetryData data) { log_data(
    std::to_string(data.timestamp) + "," + std::to_string(data.tlCurrent) + "," + std::to_string(data.trCurrent) + "," +
    std::to_string(data.blCurrent) + "," + std::to_string(data.brCurrent) + "," + std::to_string(data.tlPower) + "," +
    std::to_string(data.trPower) + "," + std::to_string(data.blPower) + "," + std::to_string(data.brPower) + "," +
    std::to_string(data.tlSpeed) + "," + std::to_string(data.trSpeed) + "," + std::to_string(data.blSpeed) + "," +
    std::to_string(data.brSpeed) + "," + std::to_string(data.tlPos) + "," + std::to_string(data.trPos) + "," + std::to_string(data.blPos) +
    "," + std::to_string(data.brPos) + "," + std::to_string(data.extendedTL) + "," + std::to_string(data.extendedTR) + "," +
    std::to_string(data.extendedBL) + "," + std::to_string(data.extendedBR) + "," + std::to_string(data.extendingALL) + "," +
    std::to_string(data.complyALL) + "," + std::to_string(data.takeSlack) + "," + std::to_string(data.safetyOn) + "," +
    std::to_string(data.targetX) + "," + std::to_string(data.targetY) + "," + std::to_string(data.targetZ) + "," + std::to_string(data.x) +
    "," + std::to_string(data.y) + "," + std::to_string(data.test) + "," + std::to_string(data.pointCount) + "," +
    std::to_string(data.waypoint) + "," + std::to_string(data.calibrationGridSize) + "," + std::to_string(data.holdTimer) + "," +
    std::to_string(data.holding) + "," + std::to_string(data.holdTime) + "," + std::to_string(data.centerX) + "," +
    std::to_string(data.centerY) + "," + std::to_string(data.lastCallToPID) + "," + std::to_string(data.lastMiss) + "," +
    std::to_string(data.lastCallToUpdate) + "," + std::to_string(data.extendCallTimer) + "," + std::to_string(data.complyCallTimer)) }

TelemetryData Maslow_::get_telemetry_data() {
    TelemetryData data;

    // TODO: probably, we ought to use mutexes here?, but it is not implemented yet and
    // it may not matter much. the reads are generally of types that don't
    //if (xSemaphoreTake(telemetry_mutex, portMAX_DELAY)) {
    // Access shared variables here
    data.timestamp = millis();
    data.tlCurrent = axisTL.getMotorCurrent();
    data.trCurrent = axisTR.getMotorCurrent();
    data.blCurrent = axisBL.getMotorCurrent();
    data.brCurrent = axisBR.getMotorCurrent();

    data.tlPower = axisTL.getMotorPower();
    data.trPower = axisTR.getMotorPower();
    data.blPower = axisBL.getMotorPower();
    data.brPower = axisBR.getMotorPower();

    data.tlSpeed = axisTL.getBeltSpeed();
    data.trSpeed = axisTR.getBeltSpeed();
    data.blSpeed = axisBL.getBeltSpeed();
    data.brSpeed = axisBR.getBeltSpeed();

    data.tlPos = axisTL.getPosition();
    data.trPos = axisTR.getPosition();
    data.blPos = axisBL.getPosition();
    data.brPos = axisBR.getPosition();

    data.extendedTL   = extendedTL;
    data.extendedTR   = extendedTR;
    data.extendedBL   = extendedBL;
    data.extendedBR   = extendedBR;
    data.extendingALL = extendingALL;
    data.complyALL    = complyALL;
    data.takeSlack    = takeSlack;
    data.safetyOn     = safetyOn;
    data.targetX      = targetX;
    data.targetY      = targetY;
    data.targetZ      = targetZ;
    data.x            = x;
    data.y            = y;
    data.test         = test;
    // data.pointCount          = calibration.pointCount;
    // data.waypoint            = calibration.waypoint;
    data.calibrationGridSize = calibration.calibrationGridSize;
    data.holdTimer           = calibration.holdTimer;
    data.holding             = calibration.holding;
    data.holdTime            = calibration.holdTime;
    using namespace Kinematics;
    MaslowKinematics* kinematics = getMaslowKinematics();
    data.centerX                 = kinematics ? kinematics->getCenterX() : 0.0f;
    data.centerY                 = kinematics ? kinematics->getCenterY() : 0.0f;
    data.lastCallToPID           = lastCallToPID;
    data.lastMiss                = lastMiss;
    data.lastCallToUpdate        = lastCallToUpdate;
    data.extendCallTimer         = extendCallTimer;
    data.complyCallTimer         = complyCallTimer;
    // xSemaphoreGive(telemetry_mutex);
    //}
    return data;
}
void Maslow_::write_telemetry_buffer(uint8_t* buffer, size_t length) {
    // Open the file in append mode
    FileStream* file = new FileStream(MASLOW_TELEM_FILE, "a", "sd");

    // Write the buffer to the file
    file->write(buffer, length);
    file->flush();

    // Close the file
    delete file;
}

void Maslow_::dump_telemetry(const char* file) {
    log_info("Dumping telemetry...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    // open the file
    FileStream* f = new FileStream(MASLOW_TELEM_FILE, "r", "sd");
    if (f) {
        // read the size of each struct from the file
        TelemetryFileHeader header;
        f->read(reinterpret_cast<char*>(&header), sizeof(TelemetryFileHeader));
        log_info("Struct size " << header.structureSize);
        log_info("Dump version " << header.version);
        log_telem_hdr_csv();
        // TODO: check version and adapt?
        TelemetryData* data   = new TelemetryData();
        char*          buffer = new char[header.structureSize];
        while (f->available()) {
            f->read(&buffer[0], header.structureSize);
            // populate data from buffer
            memcpy(data, buffer, header.structureSize);
            // print the data
            log_telem_pt_csv(*data);
            // log_telem_pt(*data);
        }
        // delete the buffer
        delete[] buffer;
        // delete the data
        delete data;
    } else {
        log_info("File not found")
    }
    delete f;
}

// Called on utility core as a task to gather telemetry and write it to an SD log
void telemetry_loop(void* unused) {
    const int bufferSize = 5000;
    uint8_t   buffer[bufferSize];
    int       bufferIndex = 0;

    while (true) {
        if (Maslow.telemetry_enabled) {
            TelemetryData data = Maslow.get_telemetry_data();

            // Copy the telemetry data into the buffer
            memcpy(buffer + bufferIndex, reinterpret_cast<uint8_t*>(&data), sizeof(TelemetryData));
            // increment the index
            bufferIndex += sizeof(TelemetryData);

            // Check if the buffer is about to overflow
            if (bufferIndex >= bufferSize) {
                log_debug("!" << bufferIndex);
                Maslow.write_telemetry_buffer(buffer, bufferIndex);
                bufferIndex = 0;
            }
        } else {
            if (bufferIndex > 0) {
                Maslow.write_telemetry_buffer(buffer, bufferIndex);
                bufferIndex = 0;
            }
        }
        // Start with 2Hz-ish
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

Maslow_& Maslow_::getInstance() {
    static Maslow_ instance;
    return instance;
}

Maslow_& Maslow = Maslow.getInstance();