// Copyright (c) 2024 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#include "MotorUnit.h"
#include "../Report.h"
#include "Maslow.h"
#include "../Machine/MachineConfig.h"
#include "../Config.h"
#include "../Limits.h"

// PID controller tuning
#define P 300
#define I 0
#define D 0

//------------------------------------------------------
//------------------------------------------------------ Core utility functions
//------------------------------------------------------

void MotorUnit::begin(int forwardPin, int backwardPin, int readbackPin, int encoderAddress, int channel1, int channel2) {
    _encoderAddress = encoderAddress;

    // Map encoder address to FluidNC axis index
    // This mapping is specific to the Maslow hardware configuration:
    // - Each motor is identified by its I2C encoder address (0-3)
    // - FluidNC axes A,B,C,D (indices 3,4,5,6) control the four belt motors
    // - Encoder 0 (Bottom Right) -> Axis D (6)
    // - Encoder 1 (Top Right)    -> Axis B (4)
    // - Encoder 2 (Top Left)     -> Axis A (3)
    // - Encoder 3 (Bottom Left)  -> Axis C (5)
    switch (encoderAddress) {
        case 0:
            _axisIndex = 6;
            break;  // BR -> D
        case 1:
            _axisIndex = 4;
            break;  // TR -> B
        case 2:
            _axisIndex = 3;
            break;  // TL -> A
        case 3:
            _axisIndex = 5;
            break;  // BL -> C
        default:
            _axisIndex = -1;
            break;
    }

    String encAddrLabel = Maslow.axis_id_to_label(_encoderAddress);

    Maslow.I2CMux.setPort(_encoderAddress);
    if (!encoder.begin()) {
        log_error("Encoder not found on " << encAddrLabel.c_str());
        Maslow.error        = true;
        Maslow.errorMessage = "Encoder not found on " + encAddrLabel;
    } else {
        log_info("Encoder connected on " << encAddrLabel.c_str());
    }
    zero();

    motor.begin(forwardPin, backwardPin, readbackPin, channel1, channel2);

    positionPID.setPID(P, I, D);
    positionPID.setOutputLimits(-1023, 1023);

    if (!motor_test()) {
        log_error("Motor not found on " << encAddrLabel.c_str());
        Maslow.error        = true;
        Maslow.errorMessage = "Motor not found on " + encAddrLabel;
    } else {
        log_info("Motor detected on " << encAddrLabel.c_str());
    }
}

//Test the motor unit by testing the motor and checking the encoder
bool MotorUnit::test() {
    bool allTestsPassed = true;

    String encAddrLabel = Maslow.axis_id_to_label(_encoderAddress);

    //Check if the motor / motor driver are connected
    if (!motor_test()) {
        log_warn("Motor not found on " << encAddrLabel.c_str());
        Maslow.error        = true;
        Maslow.errorMessage = "Motor not found on " + encAddrLabel;
        allTestsPassed      = false;
    }

    //Check if the encoder is connected
    if (!updateEncoderPosition()) {
        log_warn("Encoder not found on " << encAddrLabel.c_str());
        Maslow.error        = true;
        Maslow.errorMessage = "Encoder not found on " + encAddrLabel;
        allTestsPassed      = false;
    }

    //Check for the presence of the magnet
    if (!encoder.detectMagnet()) {
        log_warn("Magnet not detected on " << encAddrLabel.c_str());
        allTestsPassed = false;
    }

    if (allTestsPassed) {
        log_info("All tests passed on " << encAddrLabel.c_str());
    }

    return !Maslow.error;
}

bool MotorUnit::motor_test() {
    //run motor for 100ms and check if the current gets above zero
    unsigned long time        = millis();
    unsigned long elapsedTime = millis() - time;
    while (elapsedTime < 100) {
        elapsedTime = millis() - time;
        motor.forward(1023);
        if (motor.readCurrent() > 30) {
            motor.stop();
            return true;
        }
    }
    motor.stop();
    return false;
}
// update the motor current buffer and belts speed every >5 ms
void MotorUnit::update() {
    //updating belt speed and motor cutrrent
    //update belt speed every 50ms or so:
    if (millis() - beltSpeedTimer > 50) {
        beltSpeed             = (getPosition() - beltSpeedLastPosition) / ((millis() - beltSpeedTimer) / 1000.0);  // mm/s
        beltSpeedTimer        = millis();
        beltSpeedLastPosition = getPosition();
    }

    if (millis() - motorCurrentTimer > 5) {
        motorCurrentTimer = millis();
        for (int i = 0; i < 9; i++) {
            motorCurrentBuffer[i] = motorCurrentBuffer[i + 1];
        }
        motorCurrentBuffer[9] = motor.readCurrent();
    }
}

// Check if the current belt position exceeds the soft limit
// This is called before forward motor movements to ensure immediate detection of limit violations.
// The performance overhead is minimal (a few comparisons and a config lookup).
void MotorUnit::checkSoftLimits() {
    // Debug: Log that we're checking (once per motor)
    static bool loggedEntry[4] = { false, false, false, false };
    if (!loggedEntry[_encoderAddress] && _encoderAddress >= 0 && _encoderAddress < 4) {
        log_info("checkSoftLimits() called for encoder " << _encoderAddress << " (axis index " << _axisIndex << ")");
        loggedEntry[_encoderAddress] = true;
    }

    // Skip if axis index is invalid or no config available
    if (_axisIndex < 0 || !config || !config->_axes) {
        return;
    }

    // Ensure the axis array has enough elements for this axis index
    // Use the runtime axis count rather than MAX_N_AXIS since Maslow extends beyond standard axes
    if (_axisIndex >= config->_axes->_numberAxis) {
        return;
    }

    auto axisConfig = config->_axes->_axis[_axisIndex];
    if (!axisConfig) {
        return;
    }

    // Log when soft limits are not enabled to help diagnose configuration issues
    if (!axisConfig->_softLimits) {
        static unsigned long lastWarnTime = 0;
        if (millis() - lastWarnTime > 10000) {  // Warn every 10 seconds
            String encAddrLabel = Maslow.axis_id_to_label(_encoderAddress);
            log_debug("Soft limits not enabled for axis " << encAddrLabel.c_str() << " (axis index " << _axisIndex << ")");
            lastWarnTime = millis();
        }
        return;  // Soft limits not enabled for this axis
    }

    double currentPosition = getPosition();

    // Get the configured soft limits for this axis using FluidNC's limit functions
    // These account for homing direction and machine coordinate system
    float minLimit = limitsMinPosition(_axisIndex);
    float maxLimit = limitsMaxPosition(_axisIndex);

    // For Maslow belts, position is measured from 0 (retracted) outward
    // The actual travel limit is the absolute distance from min to max
    float travelRange = abs(maxLimit - minLimit);

    // Diagnostic logging to help understand what values are being checked
    static unsigned long lastDiagTime = 0;
    if (millis() - lastDiagTime > 5000 && currentPosition > travelRange * 0.8) {  // Log when approaching limit
        String encAddrLabel = Maslow.axis_id_to_label(_encoderAddress);
        log_info("Soft limit check on " << encAddrLabel.c_str() << ": pos=" << currentPosition << "mm, limit=" << travelRange
                                        << "mm (min=" << minLimit << ", max=" << maxLimit << ")");
        lastDiagTime = millis();
    }

    // Belt position measurement:
    // - Position 0 = belt fully retracted (wound up on spool)
    // - Position travelRange = belt fully extended
    // - Positions are absolute distances in mm from the zeroed/retracted position
    // Note: We only check the upper limit here. Negative positions are allowed during
    // retraction/homing operations and will be corrected by the zero() operation.
    if (currentPosition > travelRange) {
        String encAddrLabel = Maslow.axis_id_to_label(_encoderAddress);
        String errorMsg     = "Belt on " + encAddrLabel + " has reached maximum extension (" + String(currentPosition, 1) + "mm > " +
                          String(travelRange, 1) + "mm). Machine shutdown to prevent damage.";

        log_error(errorMsg.c_str());
        Maslow.eStop(errorMsg);
    }
}

// Private wrapper methods that check soft limits before any motor movement
// All motor control should go through these methods to ensure consistent enforcement

void MotorUnit::safeMotorForward(uint16_t speed) {
    checkSoftLimits();
    motor.forward(speed);
}

void MotorUnit::safeMotorBackward(uint16_t speed) {
    // Backward motion (retraction) moves toward position 0, which is always safe
    // We only need to check soft limits for forward motion (extension)
    // Note: Don't call checkSoftLimits() here as backward is always pulling toward safety
    motor.backward(speed);
}

void MotorUnit::safeMotorRunAtPWM(long signed_speed) {
    // Only check soft limits for forward motion (positive speed)
    // Backward motion (negative speed) moves toward position 0, which is always safe
    if (signed_speed > 0) {
        checkSoftLimits();
    }
    motor.runAtPWM(signed_speed);
}

void MotorUnit::safeMotorFullOut() {
    checkSoftLimits();
    motor.fullOut();
}

void MotorUnit::safeMotorFullIn() {
    // Full-in motion (retraction) moves toward position 0, which is always safe
    // Note: Don't call checkSoftLimits() here as backward is always pulling toward safety
    motor.fullIn();
}

// Reads the encoder value and updates it's position
bool MotorUnit::updateEncoderPosition() {
    if (!Maslow.I2CMux.setPort(_encoderAddress))
        return false;

    String encAddrLabel = Maslow.axis_id_to_label(_encoderAddress);

    if (encoder.isConnected()) {                                               //this func has 50ms timeout (or worse?, hard to tell)
        mostRecentCumulativeEncoderReading = encoder.getCumulativePosition();  //This updates and returns the encoder value

        // Check for I2C communication errors using new AS5600 error handling
        int error = encoder.lastError();
        if (error != 0) {  // AS5600_OK = 0
            if (millis() - encoderReadFailurePrintTime > 5000) {
                encoderReadFailurePrintTime = millis();
                log_warn("Encoder I2C error " << error << " on " << encAddrLabel.c_str());
            }
            return false;
        }
        return true;
    } else if (millis() - encoderReadFailurePrintTime > 5000) {
        encoderReadFailurePrintTime = millis();
        log_warn("Encoder read failure on " << encAddrLabel.c_str());
        //Maslow.panic();
    }
    return false;
}

/*!
 *  @brief  Gets the current error in the axis position
 */
double MotorUnit::getPositionError() {
    return getPosition() - setpoint;
}

// Recomputes the PID and drives the output
double MotorUnit::recomputePID() {
    _commandPWM = positionPID.getOutput(getPosition(), setpoint);

    safeMotorRunAtPWM(_commandPWM);

    return _commandPWM;
}

//------------------------------------------------------
//------------------------------------------------------ Homing/calibration functions
//------------------------------------------------------

// Sets the motor to comply with how it is being pulled, non-blocking.
bool MotorUnit::comply() {
    //Call it every 25 ms
    if (millis() - lastCallToComply < 25) {
        return true;
    }

    //If we've moved any, then drive the motor outwards to extend the belt
    float positionNow = getPosition();
    float distMoved   = positionNow - lastPosition;

    //If the belt is moving out, let's keep it moving out
    if (distMoved > .1) {  //EXPERIMENTAL

        safeMotorForward(amtToMove);

        if (amtToMove < 100)
            amtToMove = 100;
        amtToMove = amtToMove * 1.4;

        amtToMove = min(amtToMove, 1023.0);
    }

    //Finally if the belt is not moving we want to spool things down
    else {
        amtToMove = amtToMove / 1.25;
        safeMotorForward(amtToMove);
    }

    _commandPWM  = amtToMove;  //update actual motor power, so the get motor power isn't lying to us
    lastPosition = positionNow;

    lastCallToComply = millis();
    return true;
}

// Pulls_tight and zeros axis; returns true when done
bool MotorUnit::retract() {
    if (!pull_tight(Maslow.calibration.retractCurrentThreshold))
        return false;

    String encAddrLabel = Maslow.axis_id_to_label(_encoderAddress);
    log_info(encAddrLabel.c_str() << " pulled tight with offset " << getPosition());
    zero();

    return true;
}

// Pulls the belt until we hit a current treshold; returns true when done
bool MotorUnit::pull_tight(int currentThreshold) {
    //call at most every 5ms
    if (millis() - lastCallToRetract < 4) {
        return false;
    }
    lastCallToRetract = millis();

    //Gradually increase the pulling speed
    if (random(0, 2) == 1) {
        retract_speed = min(retract_speed + 1, 1023);
    }

    safeMotorBackward(retract_speed);
    _commandPWM = -retract_speed;  //This is only used for the getPWM function. There's got to be a tidier way to do this.

    //When taught
    int currentMeasurement = motor.readCurrent();

    retract_baseline = alpha * float(currentMeasurement) + (1 - alpha) * retract_baseline;

    if (currentMeasurement - retract_baseline > incrementalThreshold) {
        incrementalThresholdHits = incrementalThresholdHits + 1;
    } else {
        incrementalThresholdHits = 0;
    }

    if (retract_speed > 15) {  //20 is not the actual speed, it is the amount of time so we don't trigger immediately
        if (currentMeasurement > currentThreshold || incrementalThresholdHits > 2) {
            //stop motor, reset variables
            stop();
            retract_speed    = 0;
            retract_baseline = 700;
            return true;
        } else {
            return false;
        }
    }
    return false;
}

// extends the belt to the target length until it hits the target length, returns true when target length is reached
bool MotorUnit::extend(double targetLength) {
    //unsigned long timeLastMoved = millis();

    if (getPosition() < targetLength) {
        comply();  //Comply does the actual moving
        return false;
    }
    //If reached target position, Stop and return
    setTarget(getPosition());
    stop();

    //log_info("Belt positon after extend: ");
    //log_info(getPosition());
    return true;
}

//------------------------------------------------------
//------------------------------------------------------ Utility functions
//------------------------------------------------------

// Sets the target location in mm
void MotorUnit::setTarget(double newTarget) {
    setpoint = newTarget;
}

// Gets the target location in mm
double MotorUnit::getTarget() {
    return setpoint;
}

// Returns the current position of the axis in mm
double MotorUnit::getPosition() {
    double positionNow = (mostRecentCumulativeEncoderReading / 4096.0) * _mmPerRevolution * -1;
    return positionNow;
}

// Returns the current motor power draw
double MotorUnit::getCurrent() {
    return motor.readCurrent();
}

// Stops the motor
void MotorUnit::stop() {
    motor.stop();
    _commandPWM = 0;
}

// Returns the PWM values set to the motor
double MotorUnit::getMotorPower() {
    return _commandPWM;
}

// Returns current belts speed, remove? (TODO)
double MotorUnit::getBeltSpeed() {
    return beltSpeed;
}

// Returns average motor current over last 10 reads
double MotorUnit::getMotorCurrent() {
    //return average motor current of the last 10 readings:
    double sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += motorCurrentBuffer[i];
    }
    return sum / 10.0;
}

// Checking if we are at the target position within certain precision:
bool MotorUnit::onTarget(double precision) {
    if (abs(getTarget() - getPosition()) < precision)
        return true;
    else
        return false;
}

//Runs the motor to extend at full speed
void MotorUnit::decompressBelt() {
    int decompressSpeed = 800;
    safeMotorForward(decompressSpeed);
    _commandPWM = decompressSpeed;
}

//Runs the motor at full speed out
void MotorUnit::fullOut() {
    safeMotorFullOut();
    _commandPWM = 1023;
}
//Runs the motor at full speed in
void MotorUnit::fullIn() {
    safeMotorFullIn();
    _commandPWM = 1023;
}

// Reset all the axis variables
void MotorUnit::reset() {
    retract_speed            = 0;
    retract_baseline         = 700;
    incrementalThresholdHits = 0;
    amtToMove                = 0;
    lastPosition             = getPosition();
    beltSpeedTimer           = millis();
}

//sets the encoder position to 0
void MotorUnit::zero() {
    Maslow.I2CMux.setPort(_encoderAddress);
    encoder.resetCumulativePosition();
}

//sets the encoder position to a specific value (for restoring from NVS)
void MotorUnit::setPosition(double position) {
    // Convert position in mm to encoder counts
    // Formula: encoderCounts = -(position_mm * 4096.0) / _mmPerRevolution
    int32_t encoderCounts = (int32_t)(-(position * 4096.0) / _mmPerRevolution);

    Maslow.I2CMux.setPort(_encoderAddress);
    encoder.resetCumulativePosition(encoderCounts);

    // Update our cached value
    mostRecentCumulativeEncoderReading = encoderCounts;
}

//Gets the current raw encoder angle (0-4095)
uint16_t MotorUnit::getRawEncoderAngle() {
    Maslow.I2CMux.setPort(_encoderAddress);
    return encoder.readAngle();
}
