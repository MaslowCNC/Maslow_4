#include "Calibration.h"
#include "Maslow.h"
#include "../Kinematics/MaslowKinematics.h"
#include "../System.h"
#include "SquareCalculation.h"

// Helper function to get MaslowKinematics instance
static Kinematics::MaslowKinematics* getKinematics() {
    using namespace Kinematics;
    MaslowKinematics* kinematics = getMaslowKinematics();
    if (!kinematics) {
        log_error("MaslowKinematics not available");
    }
    return kinematics;
}

// #define UNKNOWN 0
// #define RETRACTING 1
// #define RETRACTED 2
// #define EXTENDING 3
// #define EXTENDEDOUT 4 //Extended is a reserved word
// #define TAKING_SLACK 5
// #define CALIBRATION_IN_PROGRESS 6
// #define READY_TO_CUT 7
// #define RELEASE_TENSION 8
// #define CALIBRATION_COMPUTING 9

// Constructor
Calibration::Calibration() {
    currentState = UNKNOWN;
    // Initialize calibration loop state variables
    calibrationDirection  = UP;
    measurementInProgress = true;
}

//------------------------------------------------------
//------------------------------------------------------ State Machine Functions
//------------------------------------------------------
int Calibration::getCurrentState() {
    return currentState;
}

//Prints the machine's current state
void Calibration::printCurrentState() {
    log_info("Current state: " << currentState);
}

// Set extended state variables (used when restoring from NVS)
void Calibration::setExtendedState(bool tl, bool tr, bool bl, bool br) {
    extended[_TL] = tl;
    extended[_TR] = tr;
    extended[_BL] = bl;
    extended[_BR] = br;
}

//Request a state change to a new state. Returns true on success and false on failure (although return value is never used atm)
bool Calibration::requestStateChange(int newState) {
    log_info("Requesting state change from " << stateNames[currentState].name << " to " << stateNames[newState].name);

    bool success = false;

    switch (newState) {
        case UNKNOWN:  //We can enter unknown from any stable state (the machine is not currently performing an action)
            currentState = UNKNOWN;
            success      = true;
            break;
        case RETRACTING:  //We can enter retracting from any state
            currentState = RETRACTING;

            retracting[_TL] = true;
            retracting[_TR] = true;
            retracting[_BL] = true;
            retracting[_BR] = true;
            for (int arm = _TL; arm < ARM_COUNT; arm++) {
                Maslow.axis[arm].reset();
            }

            success = true;
            break;
        case RETRACTED:  //We can enter retracted from retracting only
            if (currentState == RETRACTING) {
                currentState = RETRACTED;
                sys.set_state(State::Idle);
                // Explicitly save belt positions now that belts are retracted and tight
                Maslow.saveBeltPositions();
                success = true;
                break;
            } else {
                break;
            }
        case EXTENDING:  //We can enter extending from retracted or extended out
            if (currentState == RETRACTED || currentState == EXTENDEDOUT) {
                currentState = EXTENDING;
                Maslow.stop();
                sys.set_state(State::Homing);

                extended[_TL] = false;
                extended[_TR] = false;
                extended[_BL] = false;
                extended[_BR] = false;

                updateCenterXY();  //Why is this needed here?
                success = true;
                break;
            } else {
                log_info("Cannot extend the belts until they have been retracted");
                break;
            }
        case EXTENDEDOUT:  //We can enter extended from extending or in the event of a failure from taking slack or release tension
            if (currentState == EXTENDING || currentState == TAKING_SLACK || currentState == RELEASE_TENSION) {
                currentState = EXTENDEDOUT;
                sys.set_state(State::Idle);
                // Save belt positions now that belts are extended and at a known position
                Maslow.saveBeltPositions();
                success = true;
                break;
            } else {
                break;
            }
        case TAKING_SLACK:  //We can enter taking slack from extended or ready to cut
            if (currentState == EXTENDEDOUT || currentState == READY_TO_CUT) {
                currentState = TAKING_SLACK;

                //Reset the axis targets at the beginning of taking slack
                Maslow.axis[_TL].setTarget(Maslow.axis[_TL].getPosition());
                Maslow.axis[_TR].setTarget(Maslow.axis[_TR].getPosition());

                retracting[_TL] = false;  //Should be replaced by state now
                retracting[_TR] = false;
                retracting[_BL] = false;
                retracting[_BR] = false;

                for (int arm = _TL; arm < ARM_COUNT; arm++) {
                    Maslow.axis[arm].reset();
                }

                Maslow.x  = 0;
                Maslow.y  = 0;
                takeSlack = true;

                //Alocate the memory to store the measurements in. This is used here because take slack will use the same memory as the calibration
                allocateCalibrationMemory();
                success = true;
                break;
            } else {
                log_info("Cannot take slack until the belts have been extended");
                break;
            }
        case CALIBRATION_IN_PROGRESS:  //We can enter calibration in progress from EXTENDEDOUT, READY_TO_CUT, or CALIBRATION_COMPUTING
            if (currentState == EXTENDEDOUT || currentState == READY_TO_CUT || currentState == CALIBRATION_COMPUTING) {
                currentState = CALIBRATION_IN_PROGRESS;

                //Reset the axis targets at the beginning of calibration
                Maslow.axis[_TL].setTarget(Maslow.axis[_TL].getPosition());
                Maslow.axis[_TR].setTarget(Maslow.axis[_TR].getPosition());

                sys.set_state(State::Homing);

                //If we are at the first point we need to generate the grid before we can start
                if (waypoint == 0) {
                    // Initialize calibration loop state for fresh start
                    calibrationDirection     = UP;
                    measurementInProgress    = true;
                    orientationDetectionDone = false;  // Reset orientation detection flag for new calibration
                    orientationDetectTimer   = 0;      // Reset timer so detection starts fresh

                    if (!generate_calibration_grid()) {  //Fail out if the grid cannot be generated
                        return false;
                    }
                }
                //We have just finished the first six points and have updated anchor locations so it's time to generate the grid again
                //if the user has selected for it to be generated automatically
                if (waypoint == 6 && calibration_grid_width_mm_X == 0 && calibration_grid_height_mm_Y == 0) {
                    if (!generate_calibration_grid()) {  //Fail out if the grid cannot be generated
                        return false;
                    }
                }
                Maslow.stop();

                //Save the z-axis 'stop' position
                Maslow.targetZ = 0;
                Maslow.setZStop();

                //Recalculate the center position because the machine dimensions may have been updated
                updateCenterXY();

                //At this point it's likely that we have just sent the machine new cordinates for the anchor points so we need to figure out our new XY
                //cordinates by looking at the current lengths of the top two belts.
                //If we can't load the position, that's OK, we can still go ahead with the calibration and the first point will make a guess for it
                float x          = 0;
                float y          = 0;
                auto  kinematics = getKinematics();

                // Get current Z position to accurately convert measurements to XY plane
                float* mpos     = get_mpos();
                float  currentZ = mpos[2];

                // Compute total vertical distances for TL and TR
                float tlTotalZ = (currentZ + kinematics->getTlZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
                float trTotalZ = (currentZ + kinematics->getTrZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());

                if (kinematics && computeXYfromLengths(measurementToXYPlane(Maslow.axis[_TL].getPosition(), tlTotalZ),
                                                       measurementToXYPlane(Maslow.axis[_TR].getPosition(), trTotalZ),
                                                       x,
                                                       y)) {
                    //We reset the last waypoint to where it actually is so that we can move from the updated position to the next waypoint
                    if (waypoint > 0) {
                        calibrationGrid[waypoint - 1][0] = x;
                        calibrationGrid[waypoint - 1][1] = y;
                    }

                    log_info("Machine Position found as X: " << x << " Y: " << y);

                    //Set the internal machine position using actual belt positions to avoid synchronization issues
                    // Get current belt positions from hardware and set motor steps directly
                    float beltLength[ARM_COUNT];
                    for (int arm = _TL; arm < ARM_COUNT; arm++) {
                        beltLength[arm] = Maslow.axis[arm].getPosition();  // Actual belt position from hardware
                    }

                    log_info("Setting motor positions from hardware readings:");
                    log_info("TL: " << beltLength[_TL] << " TR: " << beltLength[_TR] << " BL: " << beltLength[_BL]
                                    << " BR: " << beltLength[_BR]);

                    // Set motor positions directly from hardware readings
                    // Axis mapping: A=TL(0), B=TR(1), C=BL(2), D=BR(3), Z=router(4)
                    set_motor_steps(0, mpos_to_steps(beltLength[_TL], 0));  // A axis = TL belt
                    set_motor_steps(1, mpos_to_steps(beltLength[_TR], 1));  // B axis = TR belt
                    set_motor_steps(2, mpos_to_steps(beltLength[_BL], 2));  // C axis = BL belt
                    set_motor_steps(3, mpos_to_steps(beltLength[_BR], 3));  // D axis = BR belt
                    set_motor_steps(4, mpos_to_steps(0.0, 4));              // Z axis = 0 (surface level) during calibration

                    gc_sync_position();  //This updates the Gcode engine with the new position from the stepping engine that we set with set_motor_steps
                    plan_sync_position();
                }

                sys.set_state(State::Homing);

                calibrationInProgress = true;  //Should be replaced by state machine
                success               = true;
                break;
            } else {
                log_info("Cannot start calibration until the belts have been extended");
                break;
            }
        case CALIBRATION_COMPUTING:  //We can enter calibration computing from calibration in progress
            if (currentState == CALIBRATION_IN_PROGRESS) {
                currentState          = CALIBRATION_COMPUTING;
                calibrationInProgress = false;
                success               = true;
                break;
            } else {
                log_info("Cannot enter calibration computing from state " << stateNames[currentState].name);
                break;
            }
        case READY_TO_CUT:  //We can enter ready to cut from calibration in progress, calibration computing or taking slack
            if (currentState == CALIBRATION_IN_PROGRESS || currentState == CALIBRATION_COMPUTING || currentState == TAKING_SLACK) {
                currentState = READY_TO_CUT;
                sys.set_state(State::Idle);
                // Explicitly save belt positions now that calibration/take-slack is complete and belts are tight
                Maslow.saveBeltPositions();
                success = true;
                break;
            } else {
                break;
            }
        case RELEASE_TENSION:  //We can enter release tension from any stable state (the machine is not currently performing an action)
            if (currentState == READY_TO_CUT || currentState == UNKNOWN || currentState == EXTENDEDOUT ||
                currentState == CALIBRATION_COMPUTING) {
                previousState   = currentState;  // Store the previous state
                currentState    = RELEASE_TENSION;
                complyCallTimer = millis();
                retracting[_TL] = false;
                retracting[_TR] = false;
                retracting[_BL] = false;
                retracting[_BR] = false;
                for (int arm = _TL; arm < ARM_COUNT; arm++) {
                    Maslow.axis[arm].reset();  //This just resets the thresholds for pull tight
                }
                success = true;
                break;
            } else {
                log_info("Cannot release tension from state " << stateNames[currentState].name);
                break;
            }
        default:
            return false;
    }

    if (success) {
        log_info("Succeeded");
    }

    printCurrentState();

    return success;
}

// -Maslow homing loop. This is used whenver any of the homing funcitons are active (belts extending or retracting)
void Calibration::home() {
    switch (currentState) {
        case RETRACTING:
            //run all the retract functions untill we hit the current limit
            if (retracting[_TL]) {
                if (Maslow.axis[_TL].retract()) {
                    retracting[_TL] = false;
                    axis_homed[0]   = true;
                    extended[_TL]   = false;
                }
            }
            if (retracting[_TR]) {
                if (Maslow.axis[_TR].retract()) {
                    retracting[_TR] = false;
                    axis_homed[1]   = true;
                    extended[_TR]   = false;
                }
            }
            if (retracting[_BL]) {
                if (Maslow.axis[_BL].retract()) {
                    retracting[_BL] = false;
                    axis_homed[2]   = true;
                    extended[_BL]   = false;
                }
            }
            if (retracting[_BR]) {
                if (Maslow.axis[_BR].retract()) {
                    retracting[_BR] = false;
                    axis_homed[3]   = true;
                    extended[_BR]   = false;
                }
            }

            //Once the limits are hit switch to the next state
            if (!retracting[_TL] && !retracting[_BL] && !retracting[_BR] && !retracting[_TR]) {
                requestStateChange(RETRACTED);
            }

            break;
        case EXTENDING:
            //decompress belts for the first half second
            if (millis() - extendCallTimer < 700) {
                if (millis() - extendCallTimer > 0)
                    Maslow.axis[_BR].decompressBelt();
                if (millis() - extendCallTimer > 150)
                    Maslow.axis[_BL].decompressBelt();
                if (millis() - extendCallTimer > 250)
                    Maslow.axis[_TR].decompressBelt();
                if (millis() - extendCallTimer > 350)
                    Maslow.axis[_TL].decompressBelt();
            }
            //then make all the belts comply until they are extended fully, or user terminates it
            else {
                if (!extended[_TL])
                    extended[_TL] = Maslow.axis[_TL].extend(extendDist);
                if (!extended[_TR])
                    extended[_TR] = Maslow.axis[_TR].extend(extendDist);
                if (!extended[_BL])
                    extended[_BL] = Maslow.axis[_BL].extend(extendDist);
                if (!extended[_BR])
                    extended[_BR] = Maslow.axis[_BR].extend(extendDist);
                if (extended[_TL] && extended[_TR] && extended[_BL] && extended[_BR]) {
                    log_info("All belts extended to " << extendDist << "mm");
                    requestStateChange(EXTENDEDOUT);
                }
            }
            break;
        case TAKING_SLACK:
            if (takeSlackFunc()) {  //Returns true. Requests correct state transition within function
                takeSlack = false;
                deallocateCalibrationMemory();
            }
            break;
        case RELEASE_TENSION:
            //decompress belts for the first half second
            if (millis() - complyCallTimer < 40) {
                Maslow.axis[_BR].decompressBelt();
                Maslow.axis[_BL].decompressBelt();
                Maslow.axis[_TR].decompressBelt();
                Maslow.axis[_TL].decompressBelt();
            } else if (millis() - complyCallTimer < 800) {
                for (int arm = _TL; arm < ARM_COUNT; arm++) {
                    Maslow.axis[arm].comply();
                }
            } else {
                for (int arm = _TL; arm < ARM_COUNT; arm++) {
                    Maslow.axis[arm].stop();
                }
                sys.set_state(State::Idle);

                // If the machine was in READY_TO_CUT, EXTENDEDOUT, or CALIBRATION_COMPUTING state before releasing tension,
                // return to EXTENDEDOUT state, otherwise go to UNKNOWN
                if (previousState == READY_TO_CUT || previousState == EXTENDEDOUT || previousState == CALIBRATION_COMPUTING) {
                    requestStateChange(EXTENDEDOUT);
                } else {
                    requestStateChange(UNKNOWN);
                }
            }
            break;
        case CALIBRATION_IN_PROGRESS:
            calibration_loop();
            break;
    }

    handleMotorOverides();

    //if we are done with all the homing moves, switch system state back to Idle?
    if (currentState != RETRACTING && currentState != EXTENDING && currentState != RELEASE_TENSION && !calibrationInProgress &&
        !takeSlack && !checkOverides()) {
        sys.set_state(State::Idle);
    }
}

//------------------------------------------------------
//------------------------------------------------------ Homing and calibration functions
//------------------------------------------------------

// --Maslow calibration loop
void Calibration::calibration_loop() {
    if (waypoint >
        pointCount) {  //Point count is the total number of points to measure so if waypoint > pointcount then the overall measurement process is complete
        //Reset all of the calibration variables to the defaults so that calibration can be run again
        resetCalibrationState();
        requestStateChange(READY_TO_CUT);
        log_info("Calibration complete");
        return;
    }

    // Run orientation detection at the start of calibration (waypoint == 0)
    // Continue calling detectOrientation() until it returns true (all phases complete)
    if (waypoint == 0 && !detectOrientation()) {
        return;  // Exit early while detection is in progress
    }

    //Taking measurment once we've reached the point
    if (measurementInProgress) {
        if (take_measurement_avg_with_check(waypoint, calibrationDirection)) {  //Takes a measurement and returns true if it's done
            measurementInProgress = false;

            waypoint++;  //Increment the waypoint counter

            // Only send batch at the first recompute point (after first 6 measurements)
            // After that, send individual measurements only
            if (waypoint > recomputePoints[recomputeCountIndex] && recomputeCountIndex == 0) {
                // First recompute point - send batch of first 6 measurements
                requestStateChange(CALIBRATION_COMPUTING);
                print_calibration_data();
                calibrationDataWaiting = millis();
                sys.set_state(State::Idle);
                recomputeCountIndex++;
            } else {
                // For all points after the initial 6, send individual measurements immediately
                if (waypoint > 6) {
                    print_single_measurement(waypoint - 1);  // waypoint was already incremented
                    calibrationDataWaiting = millis();
                }
                // Skip subsequent recompute points - we're not batching anymore
                if (waypoint > recomputePoints[recomputeCountIndex] && recomputeCountIndex > 0) {
                    recomputeCountIndex++;
                }
                hold(250);
            }
        }
    }

    //Move to the next point in the grid
    else {
        if (move_with_slack(calibrationGrid[waypoint - 1][0],
                            calibrationGrid[waypoint - 1][1],
                            calibrationGrid[waypoint][0],
                            calibrationGrid[waypoint][1])) {
            measurementInProgress = true;
            calibrationDirection  = get_direction(
                calibrationGrid[waypoint - 1][0],
                calibrationGrid[waypoint - 1][1],
                calibrationGrid[waypoint][0],
                calibrationGrid[waypoint][1]);  //This is used to set the order that the belts are pulled tight in the following measurement
            Maslow.x = calibrationGrid[waypoint][0];  //Are these ever used anywhere?
            Maslow.y = calibrationGrid[waypoint][1];
            hold(250);
        }
    }
}

/*
* This function is used to take up the slack in the belts and confirm that the calibration values are resonable
* It is run when the "Apply Tension" button is pressed in the UI
* It does this by retracting the two lower belts and taking a measurement. The machine's position is then calculated 
* from the lenghts of the two upper belts. The lengths of the two lower belts are then compared to their expected calculated lengths
* If the difference is beyond a threshold we know that the stored anchor point locations do not match the real dimensons and and error is thrown
* Returns true when it is finished regardless of result. Otherwise returns false. 
*/
bool Calibration::takeSlackFunc() {
    static int takeSlackState = 0;  //0 -> Starting, 1-> Moving to (0,0), 2-> Taking a measurement. Where should this be defined correctly?
    static unsigned long holdTimer = millis();
    static float         startingX = 0;
    static float         startingY = 0;

    //Take a measurement
    if (takeSlackState == 0) {
        if (take_measurement_avg_with_check(
                0, UP)) {  //We really shouldn't be using the first position to store the data, it should have it's own array

            float x = 0;
            float y = 0;
            if (!computeXYfromLengths(calibration_data[0][0], calibration_data[0][1], x, y)) {
                log_error("Failed to compute XY from lengths");
                return true;
            }

            auto kinematics = getKinematics();
            if (!kinematics)
                return true;

            // Get current Z position to accurately compute expected belt lengths
            float* mpos     = get_mpos();
            float  currentZ = mpos[2];  // Z position from motor position array

            float extension = kinematics->getBeltEndExtension() + kinematics->getArmLength();

            // Compute total vertical distances from each anchor to router (including current Z position)
            float tlTotalZ = (currentZ + kinematics->getTlZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
            float trTotalZ = (currentZ + kinematics->getTrZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
            float blTotalZ = (currentZ + kinematics->getBlZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
            float brTotalZ = (currentZ + kinematics->getBrZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());

            //This should use it's own array, this is not calibration data
            float diff[ARM_COUNT];
            diff[_TL] = calibration_data[0][0] - measurementToXYPlane(kinematics->computeTL(x, y, currentZ), tlTotalZ);
            diff[_TR] = calibration_data[0][1] - measurementToXYPlane(kinematics->computeTR(x, y, currentZ), trTotalZ);
            diff[_BL] = calibration_data[0][2] - measurementToXYPlane(kinematics->computeBL(x, y, currentZ), blTotalZ);
            diff[_BR] = calibration_data[0][3] - measurementToXYPlane(kinematics->computeBR(x, y, currentZ), brTotalZ);
            log_info("Center point deviation: TL: " << diff[_TL] << " TR: " << diff[_TR] << " BL: " << diff[_BL] << " BR: " << diff[_BR]);
            double threshold = 12;
            if (abs(diff[_TL]) > threshold || abs(diff[_TR]) > threshold || abs(diff[_BL]) > threshold || abs(diff[_BR]) > threshold) {
                log_error("Center point deviation over "
                          << threshold << "mm, your coordinate system is not accurate, maybe try running calibration again?");
                //Should we enter an alarm state here to prevent things from going wrong?

                //Reset
                takeSlackState = 0;
                requestStateChange(EXTENDEDOUT);
                return true;
            } else {
                log_info("Center point deviation within " << threshold << "mm, your coordinate system is accurate");
                takeSlackState = 0;
                holdTimer      = millis();

                // Instead of setting cartesian position and letting kinematics recalculate motor positions,
                // we need to set the motor positions directly from the measured belt lengths to avoid
                // position synchronization issues between hardware and FluidNC's motion planning system

                // Get current motor position array
                float* mpos     = get_mpos();
                float  currentZ = mpos[2];

                // Compute total vertical distances from each anchor to router (including current Z position)
                float tlTotalZ = (currentZ + kinematics->getTlZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
                float trTotalZ = (currentZ + kinematics->getTrZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
                float blTotalZ = (currentZ + kinematics->getBlZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
                float brTotalZ = (currentZ + kinematics->getBrZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());

                // Convert measured XY plane distances to actual belt lengths for motor positions
                // calibration_data[0] contains measured XY plane distances: [TL, TR, BL, BR]
                float beltLength[ARM_COUNT];
                beltLength[_TL] = measurementFromXYPlane(calibration_data[0][0], tlTotalZ);
                beltLength[_TR] = measurementFromXYPlane(calibration_data[0][1], trTotalZ);
                beltLength[_BL] = measurementFromXYPlane(calibration_data[0][2], blTotalZ);
                beltLength[_BR] = measurementFromXYPlane(calibration_data[0][3], brTotalZ);

                // Set motor positions directly from measured belt lengths
                // Axis mapping: A=TL(0), B=TR(1), C=BL(2), D=BR(3), Z=router(4)
                set_motor_steps(0, mpos_to_steps(beltLength[_TL], 0));  // A axis = TL belt
                set_motor_steps(1, mpos_to_steps(beltLength[_TR], 1));  // B axis = TR belt
                set_motor_steps(2, mpos_to_steps(beltLength[_BL], 2));  // C axis = BL belt
                set_motor_steps(3, mpos_to_steps(beltLength[_BR], 3));  // D axis = BR belt
                // Z axis is left unchanged during Apply Tension process

                // Verify that the position was set correctly by reading back from motors
                float* verify_mpos = get_mpos();

                gc_sync_position();  //This updates the Gcode engine with the new position from the stepping engine that we set with set_motor_steps
                plan_sync_position();

                sys.set_state(State::Idle);
                requestStateChange(READY_TO_CUT);
            }
        }
    }

    //Position hold for 2 seconds
    if (takeSlackState == 1) {
        if (millis() - holdTimer > 2000) {
            takeSlackState = 0;
            return true;
        }
    }

    return false;
}

/*
*Computes the current xy cordinates of the sled based on the lengths of the upper two belts
*/
bool Calibration::computeXYfromLengths(double TL, double TR, float& x, float& y) {
    auto kinematics = getKinematics();
    if (!kinematics)
        return false;

    double tlLength = TL;  //measurementToXYPlane(TL, tlZ);
    double trLength = TR;  //measurementToXYPlane(TR, trZ);

    //Find the intersection of the two circles centered at tlX, tlY and trX, trY with radii tlLength and trLength
    double tlX = kinematics->getTlX();
    double tlY = kinematics->getTlY();
    double trX = kinematics->getTrX();
    double trY = kinematics->getTrY();

    double d = sqrt((tlX - trX) * (tlX - trX) + (tlY - trY) * (tlY - trY));
    if (d > tlLength + trLength || d < abs(tlLength - trLength)) {
        log_info("Unable to determine machine position");
        return false;
    }

    double a    = (tlLength * tlLength - trLength * trLength + d * d) / (2 * d);
    double h    = sqrt(tlLength * tlLength - a * a);
    double x0   = tlX + a * (trX - tlX) / d;
    double y0   = tlY + a * (trY - tlY) / d;
    double rawX = x0 + h * (trY - tlY) / d;
    double rawY = y0 - h * (trX - tlX) / d;

    // Adjust to the centered coordinates
    x = rawX - kinematics->getCenterX();
    y = rawY - kinematics->getCenterY();

    return true;
}

/**
 * Takes one measurement and returns true when it's done. The result is stored in the passed array.
 * Each measurement is the raw belt length processed into XY plane coordinates.
 * 
 * The function handles two orientations: VERTICAL and HORIZONTAL.
 * 
 * In VERTICAL orientation:
 * - Pulls two bottom belts tight one after another based on the x-coordinate.
 * - Takes a measurement once both belts are tight and stores it in the calibration data array.
 * 
 * In HORIZONTAL orientation:
 * - For the first waypoint (waypoint == 0), pulls all 4 belts tight to ensure proper initial tension
 * - For subsequent waypoints, pulls belts tight based on the direction of the last move.
 * - Takes a measurement once both belts are tight and stores it in the calibration data array.
 * 
 * @param result The array to store the measurement result.
 * @param dir The direction of the last move (UP, DOWN, LEFT, RIGHT). This is used to decide which belts to tighten first
 * @param run The measurement run number at current waypoint (0-3, with first 2 discarded).
 * @param current The current threshold for pulling belts tight.
 * @param waypoint The waypoint number being measured (0 = first waypoint).
 * @return True when the measurement is done, false otherwise.
 */
bool Calibration::take_measurement(float result[4], int dir, int run, int current, int waypoint) {
    //Shouldn't this be handled with the same code as below but with the direction set to UP?
    if (orientation == VERTICAL) {
        //first we pull two bottom belts tight one after another, if x<0 we pull left belt first, if x>0 we pull right belt first
        static bool BL_tight = false;
        static bool BR_tight = false;
        Maslow.axis[_TL].recomputePID();
        Maslow.axis[_TR].recomputePID();

        //On the left side of the sheet we want to pull the left belt tight first
        if (Maslow.x < 0) {
            if (!BL_tight) {
                if (Maslow.axis[_BL].pull_tight(current)) {
                    BL_tight = true;
                    //log_info("Pulled BL tight");
                }
                return false;
            }
            if (!BR_tight) {
                if (Maslow.axis[_BR].pull_tight(current)) {
                    BR_tight = true;
                    //log_info("Pulled BR tight");
                }
                return false;
            }
        }

        //On the right side of the sheet we want to pull the right belt tight first
        else {
            if (!BR_tight) {
                if (Maslow.axis[_BR].pull_tight(current)) {
                    BR_tight = true;
                    //log_info("Pulled BR tight");
                }
                return false;
            }
            if (!BL_tight) {
                if (Maslow.axis[_BL].pull_tight(current)) {
                    BL_tight = true;
                    //log_info("Pulled BL tight");
                }
                return false;
            }
        }

        //once both belts are pulled, take a measurement
        if (BR_tight && BL_tight) {
            auto kinematics = getKinematics();
            if (!kinematics)
                return false;

            // Get current Z position to accurately convert measurements to XY plane
            float* mpos     = get_mpos();
            float  currentZ = mpos[2];

            // Compute total vertical distances from each anchor to router (including current Z position)
            float tlTotalZ = (currentZ + kinematics->getTlZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
            float trTotalZ = (currentZ + kinematics->getTrZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
            float blTotalZ = (currentZ + kinematics->getBlZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
            float brTotalZ = (currentZ + kinematics->getBrZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());

            //take measurement and record it to the calibration data array.
            result[0] = measurementToXYPlane(Maslow.axis[_TL].getPosition(), tlTotalZ);
            result[1] = measurementToXYPlane(Maslow.axis[_TR].getPosition(), trTotalZ);
            result[2] = measurementToXYPlane(Maslow.axis[_BL].getPosition(), blTotalZ);
            result[3] = measurementToXYPlane(Maslow.axis[_BR].getPosition(), brTotalZ);
            BR_tight  = false;
            BL_tight  = false;
            return true;
        }
        return false;
    }
    // in HoRIZONTAL orientation we pull on the belts depending on the direction of the last move. This is important because the other two belts are likely slack
    else if (orientation == HORIZONTAL) {
        // For the first waypoint (waypoint == 0), use a two-phase approach to ensure proper tension
        if (waypoint == 0) {
            static bool tight[ARM_COUNT]         = { false, false, false, false };
            static bool initial_tension_complete = false;

            // Phase 1: Pull all four belts tight simultaneously to eliminate slack
            if (!initial_tension_complete) {
                if (Maslow.axis[_TL].pull_tight(current)) {
                    tight[_TL] = true;
                }
                if (Maslow.axis[_TR].pull_tight(current)) {
                    tight[_TR] = true;
                }
                if (Maslow.axis[_BL].pull_tight(current)) {
                    tight[_BL] = true;
                }
                if (Maslow.axis[_BR].pull_tight(current)) {
                    tight[_BR] = true;
                }

                // Once all belts are tight, move to phase 2
                if (tight[_TL] && tight[_TR] && tight[_BL] && tight[_BR]) {
                    initial_tension_complete = true;
                    // Set TL and TR targets to their current positions to prevent unwanted movement
                    Maslow.axis[_TL].setTarget(Maslow.axis[_TL].getPosition());
                    Maslow.axis[_TR].setTarget(Maslow.axis[_TR].getPosition());
                    // Reset belt tight flags for the actual measurement phase
                    tight[_BL] = false;
                    tight[_BR] = false;
                }
                return false;
            }

            // Phase 2: Hold TL and TR in position, then pull BL and BR based on x-coordinate
            // This ensures TL and TR remain as stable reference points for position calculation
            Maslow.axis[_TL].recomputePID();
            Maslow.axis[_TR].recomputePID();

            // Pull bottom belts based on x-coordinate (same logic as vertical mode)
            if (Maslow.x < 0) {
                // On the left side, pull BL first, then BR
                if (!tight[_BL]) {
                    if (Maslow.axis[_BL].pull_tight(current)) {
                        tight[_BL] = true;
                    }
                    return false;
                }
                if (!tight[_BR]) {
                    if (Maslow.axis[_BR].pull_tight(current)) {
                        tight[_BR] = true;
                    }
                    return false;
                }
            } else {
                // On the right side, pull BR first, then BL
                if (!tight[_BR]) {
                    if (Maslow.axis[_BR].pull_tight(current)) {
                        tight[_BR] = true;
                    }
                    return false;
                }
                if (!tight[_BL]) {
                    if (Maslow.axis[_BL].pull_tight(current)) {
                        tight[_BL] = true;
                    }
                    return false;
                }
            }

            // Once both bottom belts are tight, take the measurement
            if (tight[_BL] && tight[_BR]) {
                auto kinematics = getKinematics();
                if (!kinematics)
                    return false;

                // Get current Z position to accurately convert measurements to XY plane
                float* mpos     = get_mpos();
                float  currentZ = mpos[2];

                // Compute total vertical distances from each anchor to router (including current Z position)
                float tlTotalZ = (currentZ + kinematics->getTlZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
                float trTotalZ = (currentZ + kinematics->getTrZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
                float blTotalZ = (currentZ + kinematics->getBlZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
                float brTotalZ = (currentZ + kinematics->getBrZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());

                //take measurement and record it to the calibration data array.
                result[0] = measurementToXYPlane(Maslow.axis[_TL].getPosition(), tlTotalZ);
                result[1] = measurementToXYPlane(Maslow.axis[_TR].getPosition(), trTotalZ);
                result[2] = measurementToXYPlane(Maslow.axis[_BL].getPosition(), blTotalZ);
                result[3] = measurementToXYPlane(Maslow.axis[_BR].getPosition(), brTotalZ);
                // Reset all flags for next measurement
                tight[_TL]               = false;
                tight[_TR]               = false;
                tight[_BL]               = false;
                tight[_BR]               = false;
                initial_tension_complete = false;
                return true;
            }
            return false;
        }
        // For subsequent waypoints, use directional logic to pull only relevant belts
        else {
            static MotorUnit* pullAxis1;
            static MotorUnit* pullAxis2;
            static MotorUnit* holdAxis1;
            static MotorUnit* holdAxis2;
            static bool       pull1_tight = false;
            static bool       pull2_tight = false;
            switch (dir) {
                case UP:
                    holdAxis1 = &Maslow.axis[_TL];
                    holdAxis2 = &Maslow.axis[_TR];
                    if (Maslow.x < 0) {
                        pullAxis1 = &Maslow.axis[_BL];
                        pullAxis2 = &Maslow.axis[_BR];
                    } else {
                        pullAxis1 = &Maslow.axis[_BR];
                        pullAxis2 = &Maslow.axis[_BL];
                    }
                    break;
                case DOWN:
                    holdAxis1 = &Maslow.axis[_BL];
                    holdAxis2 = &Maslow.axis[_BR];
                    if (Maslow.x < 0) {
                        pullAxis1 = &Maslow.axis[_TL];
                        pullAxis2 = &Maslow.axis[_TR];
                    } else {
                        pullAxis1 = &Maslow.axis[_TR];
                        pullAxis2 = &Maslow.axis[_TL];
                    }
                    break;
                case LEFT:
                    holdAxis1 = &Maslow.axis[_TL];
                    holdAxis2 = &Maslow.axis[_BL];
                    if (Maslow.y < 0) {
                        pullAxis1 = &Maslow.axis[_BR];
                        pullAxis2 = &Maslow.axis[_TR];
                    } else {
                        pullAxis1 = &Maslow.axis[_TR];
                        pullAxis2 = &Maslow.axis[_BR];
                    }
                    break;
                case RIGHT:
                    holdAxis1 = &Maslow.axis[_TR];
                    holdAxis2 = &Maslow.axis[_BR];
                    if (Maslow.y < 0) {
                        pullAxis1 = &Maslow.axis[_BL];
                        pullAxis2 = &Maslow.axis[_TL];
                    } else {
                        pullAxis1 = &Maslow.axis[_TL];
                        pullAxis2 = &Maslow.axis[_BL];
                    }
                    break;
            }
            holdAxis1->recomputePID();
            holdAxis2->recomputePID();

            if (pullAxis1->pull_tight(current)) {
                pull1_tight = true;
            }
            if (pullAxis2->pull_tight(current)) {
                pull2_tight = true;
            }

            if (pull1_tight && pull2_tight) {
                auto kinematics = getKinematics();
                if (!kinematics)
                    return false;

                // Get current Z position to accurately convert measurements to XY plane
                float* mpos     = get_mpos();
                float  currentZ = mpos[2];

                // Compute total vertical distances from each anchor to router (including current Z position)
                float tlTotalZ = (currentZ + kinematics->getTlZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
                float trTotalZ = (currentZ + kinematics->getTrZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
                float blTotalZ = (currentZ + kinematics->getBlZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());
                float brTotalZ = (currentZ + kinematics->getBrZ() + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness());

                //take measurement and record it to the calibration data array.
                result[0]   = measurementToXYPlane(Maslow.axis[_TL].getPosition(), tlTotalZ);
                result[1]   = measurementToXYPlane(Maslow.axis[_TR].getPosition(), trTotalZ);
                result[2]   = measurementToXYPlane(Maslow.axis[_BL].getPosition(), blTotalZ);
                result[3]   = measurementToXYPlane(Maslow.axis[_BR].getPosition(), brTotalZ);
                pull1_tight = false;
                pull2_tight = false;
                return true;
            }
        }
    }

    return false;
}

static float** measurements = nullptr;

void allocateMeasurements() {
    measurements = new float*[4];
    for (int i = 0; i < 4; ++i) {
        measurements[i] = new float[4];
    }
}

void freeMeasurements() {
    for (int i = 0; i < 4; ++i) {
        delete[] measurements[i];
    }
    delete[] measurements;
    measurements = nullptr;
}

// Takes a series of measurements, calculates average and records calibration data;  Returns true when it's done and the result has been stored
// There is way too much being done in this function. It needs to be split apart and cleaned up
bool Calibration::take_measurement_avg_with_check(int waypoint, int dir) {
    //take 5 measurements in a row, (ignoring the first one), if they are all within 1mm of each other, take the average and record it to the calibration data array
    static int   run         = 0;
    static float avg         = 0;
    static float sum         = 0;
    static bool  measureFlex = false;

    if (measurements == nullptr) {
        allocateMeasurements();  //This is structured [[tl],[tr],[bl],[br]],[[tl],[tr],[bl],[br]],[[tl],[tr],[bl],[br]],[[tl],[tr],[bl],[br]]
    }

    int howHardToPull = retractCurrentThreshold;
    if (measureFlex) {
        howHardToPull = retractCurrentThreshold + 500;
    }

    if (take_measurement(measurements[max(run - 2, 0)], dir, run, howHardToPull, waypoint)) {  //Throw away measurements are stored in [0]
        if (run < 2) {
            run++;
            return false;  //discard the first two measurements
        }

        run++;

        static int criticalCounter = 0;
        if (run > 5) {
            run = 0;

            //check if all measurements are within 1mm of each other
            float maxDeviation[4] = { 0 };
            float maxDeviationAbs = 0;
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 3; j++) {
                    //find max deviation between measurements
                    maxDeviation[i] = max(maxDeviation[i], abs(measurements[j][i] - measurements[j + 1][i]));
                }
            }

            for (int i = 0; i < 4; i++) {
                maxDeviationAbs = max(maxDeviationAbs, maxDeviation[i]);
            }
            if (maxDeviationAbs > 2.5) {
                log_error("Measurement error, measurements are not within 2.5 mm of each other, trying again");
                log_info("Max deviation: " << maxDeviationAbs);

                //print all the measurements in readable form:
                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 4; j++) {
                        //use axis id to label:
                        log_info(Maslow.axis_id_to_label(i).c_str() << " " << measurements[j][i]);
                    }
                }
                //reset the run counter to run the measurements again
                if (criticalCounter++ > 8) {  //This updates the counter and checks
                    log_error("Critical error, measurements are not within 1.5mm of each other 8 times in a row, stopping calibration");
                    resetCalibrationState();
                    criticalCounter = 0;
                    freeMeasurements();
                    requestStateChange(EXTENDEDOUT);
                    return false;
                }
                freeMeasurements();
                return false;
            }

            //If we are measurring the flex we don't want to save the result and instead we want to compare it to the last result
            // COMMENTED OUT: Frame flex measurement calculation disabled
            /*
            if (measureFlex) {
                float newLenTLBR = measurements[0][0] + measurements[0][3];
                float newLenTRBL = measurements[0][1] + measurements[0][2];

                float origLenTLBR = calibration_data[0][0] + calibration_data[0][3];
                float origLenTRBL = calibration_data[0][1] + calibration_data[0][2];

                float diffTLBR = abs(newLenTLBR - origLenTLBR);
                float diffTRBL = abs(newLenTRBL - origLenTRBL);

                log_info("Flex measurement: TLBR: " << diffTLBR << " TRBL: " << diffTRBL);

                measureFlex = false;

                freeMeasurements();  //We have completed this measurement, but we don't want to store anything this time
                return true;
            }
            */

            //If the measurements seem valid, take the average and record it to the calibration data array. This is the only place we should be writing to the calibration_data array
            for (int i = 0; i < 4; i++) {  //For each axis
                sum                           = measurements[0][i] + measurements[1][i] + measurements[2][i] + measurements[3][i];
                avg                           = sum / 4;
                calibration_data[waypoint][i] = avg;  //This is the only time we should be writing to the calibration data array
                sum                           = 0;
                criticalCounter               = 0;
            }
            log_info("Measured waypoint " << waypoint);
            log_debug("Waypoint " << waypoint << " coordinates: X=" << calibrationGrid[waypoint][0] << " Y=" << calibrationGrid[waypoint][1]);

            //A check to see if the results on the first point are within the expected range
            //This logic should only run during calibration, not during Apply Tension
            if (waypoint == 0 && currentState == CALIBRATION_IN_PROGRESS) {
                //Recompute the machine position with the belt lenths and compare the results to that
                float x = 0;
                float y = 0;
                computeXYfromLengths(measurements[0][0], measurements[0][1], x, y);

                //If the frame size is way off, we will compute a rough (assumed to be a square) frame size from the first measurmeent
                auto kinematics = getKinematics();
                if (!kinematics)
                    return false;

                double threshold = 100;
                float  diff[ARM_COUNT];
                diff[_TL] = measurements[0][0] - measurementToXYPlane(kinematics->computeTL(x, y, 0), kinematics->getTlZ());
                diff[_TR] = measurements[0][1] - measurementToXYPlane(kinematics->computeTR(x, y, 0), kinematics->getTrZ());
                diff[_BL] = measurements[0][2] - measurementToXYPlane(kinematics->computeBL(x, y, 0), kinematics->getBlZ());
                diff[_BR] = measurements[0][3] - measurementToXYPlane(kinematics->computeBR(x, y, 0), kinematics->getBrZ());
                log_info("Center point off by: TL: " << diff[_TL] << " TR: " << diff[_TR] << " BL: " << diff[_BL] << " BR: " << diff[_BR]);

                if (abs(diff[_TL]) > threshold || abs(diff[_TR]) > threshold || abs(diff[_BL]) > threshold || abs(diff[_BR]) > threshold) {
                    log_error("Center point off by over " << threshold << "mm");

                    if (!adjustFrameSizeToMatchFirstMeasurement()) {
                        Maslow.eStop("Unable to find a valid frame size to match the first measurement");
                        resetCalibrationState();
                        criticalCounter = 0;
                        freeMeasurements();
                        requestStateChange(EXTENDEDOUT);
                        return false;
                    }
                }

                //Compute the current XY position from the top two belt measurements...needs to be redone because we've adjusted the frame size by here
                if (!computeXYfromLengths(calibration_data[0][0], calibration_data[0][1], x, y)) {
                    Maslow.eStop("Unable to find machine position from measurements");
                    resetCalibrationState();
                    criticalCounter = 0;
                    freeMeasurements();
                    requestStateChange(EXTENDEDOUT);
                    return false;
                }

                log_info("Machine Position computed as X: " << x << " Y: " << y);

                //Recompute the first four waypoint locations based on the current position
                calibrationGrid[0][0] =
                    x;  //This first point is never really used because we've already measured here, but it shouldn't be left undefined
                calibrationGrid[0][1] = y;
                calibrationGrid[1][0] = x + 150;
                calibrationGrid[1][1] = y;
                calibrationGrid[2][0] = x + 150;
                calibrationGrid[2][1] = y + 150;
                calibrationGrid[3][0] = x;
                calibrationGrid[3][1] = y + 150;
                calibrationGrid[4][0] = x - 150;
                calibrationGrid[4][1] = y + 150;
                calibrationGrid[5][0] = x - 150;
                calibrationGrid[5][1] = y;
            }

            //This is the exit to indicate that the measurement was successful
            freeMeasurements();

            //Special case where we have a good measurement but we need to take another at this point to measure the flex of the frame
            //Frame flex should only be measured during calibration process, not during "Apply Tension"
            // COMMENTED OUT: Frame flex measurement disabled
            /*
            if (waypoint == 0 && currentState == CALIBRATION_IN_PROGRESS) {
                measureFlex = true;
                log_info("Measuring Frame Flex");
                return false;
            }
            */

            return true;
        }
    }
    //We don't free memory alocated here because we will cycle through again and need it
    return false;
}

// Move pulling just two belts depending in the direction of the movement
bool Calibration::move_with_slack(double fromX, double fromY, double toX, double toY) {
    //This is where we want to introduce some slack so the system
    static unsigned long moveBeginTimer = millis();
    static bool          decompress     = true;
    float                stepSize       = 0.06;

    static int direction = UP;

    static float xStepSize = 1;
    static float yStepSize = 1;

    static bool tlExtending = false;
    static bool trExtending = false;
    static bool blExtending = false;
    static bool brExtending = false;

    bool withSlack = true;
    if (waypoint > recomputePoints[0]) {  //If we have completed the first round of calibraiton
        withSlack = false;
    }

    //This runs once at the beginning of the move
    if (decompress) {
        moveBeginTimer = millis();
        decompress     = false;
        direction      = get_direction(fromX, fromY, toX, toY);

        //Compute the X and Y step Size
        if (abs(toX - fromX) > abs(toY - fromY)) {
            xStepSize = (toX - fromX) > 0 ? stepSize : -stepSize;
            yStepSize = ((toY - fromY) > 0 ? stepSize : -stepSize) * abs(toY - fromY) / abs(toX - fromX);
        } else {
            yStepSize = (toY - fromY) > 0 ? stepSize : -stepSize;
            xStepSize = ((toX - fromX) > 0 ? stepSize : -stepSize) * abs(toX - fromX) / abs(toY - fromY);
        }

        //Compute which belts will be getting longer. If the current length is less than the final length the belt needs to get longer
        auto kinematics = getKinematics();
        if (!kinematics)
            return false;

        if (kinematics->computeTL(fromX, fromY, 0) < kinematics->computeTL(toX, toY, 0)) {
            tlExtending = true;
        } else {
            tlExtending = false;
        }
        if (kinematics->computeTR(fromX, fromY, 0) < kinematics->computeTR(toX, toY, 0)) {
            trExtending = true;
        } else {
            trExtending = false;
        }
        if (kinematics->computeBL(fromX, fromY, 0) < kinematics->computeBL(toX, toY, 0)) {
            blExtending = true;
        } else {
            blExtending = false;
        }
        if (kinematics->computeBR(fromX, fromY, 0) < kinematics->computeBR(toX, toY, 0)) {
            brExtending = true;
        } else {
            brExtending = false;
        }

        //Set the target to the starting position
        Maslow.setTargets(fromX, fromY, 0);
    }

    //Decompress belts for 500ms...this happens by returning right away before running any of the rest of the code
    if (millis() - moveBeginTimer < 750 && withSlack) {
        if (orientation == VERTICAL) {
            Maslow.axis[_TL].recomputePID();
            Maslow.axis[_TR].recomputePID();
            Maslow.axis[_BL].decompressBelt();
            Maslow.axis[_BR].decompressBelt();
        } else {
            switch (direction) {
                case UP:
                    Maslow.axis[_BL].decompressBelt();
                    Maslow.axis[_BR].decompressBelt();
                    break;
                case DOWN:
                    Maslow.axis[_TL].decompressBelt();
                    Maslow.axis[_TR].decompressBelt();
                    break;
                case LEFT:
                    Maslow.axis[_TR].decompressBelt();
                    Maslow.axis[_BR].decompressBelt();
                    break;
                case RIGHT:
                    Maslow.axis[_TL].decompressBelt();
                    Maslow.axis[_BL].decompressBelt();
                    break;
            }
        }

        return false;
    }

    //Stop for 50ms
    //we need to stop motors after decompression was finished once
    else if (millis() - moveBeginTimer < 800) {
        Maslow.stopMotors();
        return false;
    }

    //Set the targets
    Maslow.setTargets(Maslow.getTargetX() + xStepSize, Maslow.getTargetY() + yStepSize, 0);

    //Check to see if we have reached our target position
    if (abs(Maslow.getTargetX() - toX) < 5 && abs(Maslow.getTargetY() - toY) < 5) {
        // First, set ALL belt targets to their current position to prevent unwinding
        for (int arm = _TL; arm < ARM_COUNT; arm++) {
            Maslow.axis[arm].setTarget(Maslow.axis[arm].getPosition());
        }

        // Stabilize all belts at their new target positions to prevent unwinding
        for (int arm = _TL; arm < ARM_COUNT; arm++) {
            Maslow.axis[arm].recomputePID();
        }

        // Small delay to allow stabilization
        static unsigned long stabilizeTimer = 0;
        if (stabilizeTimer == 0) {
            stabilizeTimer = millis();
            return false;  // Continue stabilizing
        }
        if (millis() - stabilizeTimer < 50) {  // 50ms stabilization period
            return false;                      // Continue stabilizing
        }
        stabilizeTimer = 0;  // Reset for next waypoint

        // Now stop and reset only the belts that should be slack for the upcoming measurement
        if (orientation == VERTICAL) {
            // In vertical mode, maintain top belt tension, allow bottom belts to slack
            Maslow.axis[_BL].stop();
            Maslow.axis[_BR].stop();
            // Reset only the stopped axes
            Maslow.axis[_BL].reset();
            Maslow.axis[_BR].reset();
        } else {
            // In horizontal mode, stop belts based on measurement direction
            int measurementDirection = get_direction(fromX, fromY, toX, toY);
            switch (measurementDirection) {
                case UP:
                    // TL and TR will be hold belts, stop BL and BR
                    Maslow.axis[_BL].stop();
                    Maslow.axis[_BR].stop();
                    // Reset only the stopped axes
                    Maslow.axis[_BL].reset();
                    Maslow.axis[_BR].reset();
                    break;
                case DOWN:
                    // BL and BR will be hold belts, stop TL and TR
                    Maslow.axis[_TL].stop();
                    Maslow.axis[_TR].stop();
                    // Reset only the stopped axes
                    Maslow.axis[_TL].reset();
                    Maslow.axis[_TR].reset();
                    break;
                case LEFT:
                    // TL and BL will be hold belts, stop TR and BR
                    Maslow.axis[_TR].stop();
                    Maslow.axis[_BR].stop();
                    // Reset only the stopped axes
                    Maslow.axis[_TR].reset();
                    Maslow.axis[_BR].reset();
                    break;
                case RIGHT:
                    // TR and BR will be hold belts, stop TL and BL
                    Maslow.axis[_TL].stop();
                    Maslow.axis[_BL].stop();
                    // Reset only the stopped axes
                    Maslow.axis[_TL].reset();
                    Maslow.axis[_BL].reset();
                    break;
            }
        }

        decompress = true;  //Reset for the next pass
        return true;
    }

    //In vertical orientation we want to move with the top two belts and always have the lower ones be slack
    if (orientation == VERTICAL) {
        Maslow.axis[_TL].recomputePID();
        Maslow.axis[_TR].recomputePID();
        if (withSlack) {
            Maslow.axis[_BL].comply();
            Maslow.axis[_BR].comply();
        } else {
            Maslow.axis[_BL].recomputePID();
            Maslow.axis[_BR].recomputePID();
        }
    } else {
        //For each belt we check to see if it should be slack
        if (withSlack && tlExtending) {
            Maslow.axis[_TL].comply();
        } else {
            Maslow.axis[_TL].recomputePID();
        }

        if (withSlack && trExtending) {
            Maslow.axis[_TR].comply();
        } else {
            Maslow.axis[_TR].recomputePID();
        }

        if (withSlack && blExtending) {
            Maslow.axis[_BL].comply();
        } else {
            Maslow.axis[_BL].recomputePID();
        }

        if (withSlack && brExtending) {
            Maslow.axis[_BR].comply();
        } else {
            Maslow.axis[_BR].recomputePID();
        }
    }

    return false;  //We have not yet reached our target position
}

//The number of points high and wide  must be an odd number
bool Calibration::generate_calibration_grid() {
    //Allocate memory for the calibration grid
    allocateCalibrationMemory();

    float xSpacing = calibration_grid_width_mm_X / (calibrationGridSize - 1);
    float ySpacing = calibration_grid_height_mm_Y / (calibrationGridSize - 1);

    //If either dimension is set to zero we automatically compute it as half the frame size
    if (calibration_grid_height_mm_Y == 0 || calibration_grid_width_mm_X == 0) {
        float frameWidth  = getKinematics()->getTrX() - getKinematics()->getTlX();
        float frameHeight = getKinematics()->getTlY() - getKinematics()->getBlY();

        // Calculate initial grid size based on frame dimensions
        float gridWidth  = frameWidth * 0.5;
        float gridHeight = frameHeight * 0.2;

        // Constrain grid to be at most 200mm less than work area to ensure it stays within bounds
        float maxGridWidth  = Maslow.workAreaX - 200.0;
        float maxGridHeight = Maslow.workAreaY - 200.0;

        if (gridWidth > maxGridWidth) {
            gridWidth = maxGridWidth;
            log_info("Grid width constrained to work area: " << gridWidth << " mm (work area X: " << Maslow.workAreaX << " mm)");
        }
        if (gridHeight > maxGridHeight) {
            gridHeight = maxGridHeight;
            log_info("Grid height constrained to work area: " << gridHeight << " mm (work area Y: " << Maslow.workAreaY << " mm)");
        }

        // Automatically select the grid spacing (3x3, 5x5, 7x7, or 9x9) such that
        // the smallest grid is used which will not leave more than 260mm between each point
        const int availableGridSizes[] = { 3, 5, 7, 9 };
        const int numGridSizes         = sizeof(availableGridSizes) / sizeof(availableGridSizes[0]);
        int       selectedGridSize     = availableGridSizes[numGridSizes - 1];  // Default to largest grid size

        // Try each grid size from smallest to largest
        for (int i = 0; i < numGridSizes; i++) {
            int   trySize       = availableGridSizes[i];
            float tryXSpacing   = gridWidth / (trySize - 1);
            float tryYSpacing   = gridHeight / (trySize - 1);
            float maxTrySpacing = max(tryXSpacing, tryYSpacing);

            if (maxTrySpacing <= calibrationMaxSpacingMm) {
                selectedGridSize = trySize;
                break;  // Found the smallest grid that satisfies the constraint
            }
        }

        calibrationGridSize = selectedGridSize;

        xSpacing = gridWidth / (calibrationGridSize - 1);
        ySpacing = gridHeight / (calibrationGridSize - 1);
    }

    int numberOfCycles = 0;

    switch (calibrationGridSize) {
        case 3:
            numberOfCycles = 1;  // 3x3 grid
            break;
        case 5:
            numberOfCycles = 2;  // 5x5 grid
            break;
        case 7:
            numberOfCycles = 3;  // 7x7 grid
            break;
        case 9:
            numberOfCycles = 4;  // 9x9 grid
            break;
        default:
            log_error("Invalid " + M + "_calibration_grid_size: " << calibrationGridSize);
            return false;  // return false or handle error appropriately
    }

    pointCount         = 6;  //The first four points are computed dynamically
    recomputePoints[0] = 5;

    //The point in the center
    calibrationGrid[pointCount][0] = 0;
    calibrationGrid[pointCount][1] = 0;

    pointCount++;

    int maxX = 1;
    int maxY = 1;

    int currentX = 0;
    int currentY = -1;

    recomputeCount = 1;

    while (maxX <= numberOfCycles) {  //4 produces a 9x9 grid
        while (currentX > -1 * maxX) {
            calibrationGrid[pointCount][0] = currentX * xSpacing;
            calibrationGrid[pointCount][1] = currentY * ySpacing;
            pointCount++;
            currentX--;
        }
        while (currentY < maxY) {
            calibrationGrid[pointCount][0] = currentX * xSpacing;
            calibrationGrid[pointCount][1] = currentY * ySpacing;
            pointCount++;
            currentY++;
        }
        while (currentX < maxX) {
            calibrationGrid[pointCount][0] = currentX * xSpacing;
            calibrationGrid[pointCount][1] = currentY * ySpacing;
            pointCount++;
            currentX++;
        }
        while (currentY > -1 * maxY) {
            calibrationGrid[pointCount][0] = currentX * xSpacing;
            calibrationGrid[pointCount][1] = currentY * ySpacing;
            pointCount++;
            currentY--;
        }

        //Add the last point to the recompute list
        calibrationGrid[pointCount][0] = currentX * xSpacing;
        calibrationGrid[pointCount][1] = currentY * ySpacing;
        pointCount++;

        recomputePoints[recomputeCount] = pointCount - 1;  //Minus one because we increment after each point is generated
        recomputeCount++;

        maxX = maxX + 1;
        maxY = maxY + 1;

        currentY = currentY + -1;
    }

    //Move back to the center
    calibrationGrid[pointCount][0] = 0;
    calibrationGrid[pointCount][1] = (currentY + 1) * ySpacing;  //The last loop added an nunecessary -1 to the y position
    pointCount++;

    calibrationGrid[pointCount][0] = 0;
    calibrationGrid[pointCount][1] = 0;

    recomputePoints[recomputeCount] = pointCount;

    return true;
}

/*
* This function takes a single measurement and adjusts the frame dimensions to find a valid frame size that matches the measurement
* Uses the algorithm described in https://math.stackexchange.com/questions/5013127/find-square-size-from-inscribed-triangles?noredirect=1#comment10752043_5013127 for calculating square size from distances to vertices
*/
bool Calibration::adjustFrameSizeToMatchFirstMeasurement() {
    //Get the last measurements
    double tlLen = measurements[0][0];  // distance to A (top-left)
    double trLen = measurements[0][1];  // distance to B (top-right)
    double blLen = measurements[0][2];  // distance to C (bottom-left)
    double brLen = measurements[0][3];  // distance to D (bottom-right)

    // Use the Stack Exchange algorithm to compute the square side length
    // The algorithm works for any interior point E inside the square
    double L = SquareCalculation::calculateSquareSideLength(tlLen, trLen, blLen, brLen);

    if (L < 500.0 || L > 5000.0) {  // Sanity check on square size
        log_error("Unable to adjust frame size. Calculated size " << L << "mm is outside reasonable range");
        return false;
    }

    //Adjust the frame size to match the computed size
    auto kinematics = getKinematics();
    if (!kinematics) {
        log_error("adjustFrameSizeToMatchFirstMeasurement: MaslowKinematics not available");
        return false;
    }

    // Update the frame size in the MaslowKinematics system
    kinematics->setFrameSize(L);

    log_info("Frame size successfully adjusted to: " << L << " x " << L);
    return true;
}

// ------------------------------------------------------
// ------------------------------------------------------ Communication Functions
// ------------------------------------------------------

//Checks to see if the calibration data needs to be sent again
void Calibration::checkCalibrationData() {
    if (calibrationDataWaiting > 0) {
        if (millis() - calibrationDataWaiting > 30007) {
            log_error("Calibration data not acknowledged by computer, resending");
            print_calibration_data();
            calibrationDataWaiting = millis();
        }
    }
}

// function for outputting calibration data in the log line by line like this: {bl:2376.69,   br:923.40,   tr:1733.87,   tl:2801.87},
void Calibration::print_calibration_data() {
    auto kinematics = getKinematics();
    if (!kinematics)
        return;

    //These are used to set the browser side initial guess for the frame size
    log_data("$/" << M << "_tlX=" << kinematics->getTlX());
    log_data("$/" << M << "_tlY=" << kinematics->getTlY());
    log_data("$/" << M << "_trX=" << kinematics->getTrX());
    log_data("$/" << M << "_trY=" << kinematics->getTrY());
    log_data("$/" << M << "_brX=" << kinematics->getBrX());

    String data = "CLBM:[";
    for (int i = 0; i < waypoint; i++) {
        data += "{bl:" + String(calibration_data[i][2]) + ",   br:" + String(calibration_data[i][3]) +
                ",   tr:" + String(calibration_data[i][1]) + ",   tl:" + String(calibration_data[i][0]) + "},";
    }
    data += "]";
    HeartBeatEnabled = false;
    log_data(data.c_str());
    HeartBeatEnabled = true;
}

// Send a single measurement to the browser for incremental processing
void Calibration::print_single_measurement(int waypointIndex) {
    if (waypointIndex < 0 || waypointIndex >= CALIBRATION_GRID_SIZE_MAX || calibration_data == nullptr) {
        log_error("Invalid waypoint index for print_single_measurement: " << waypointIndex);
        return;
    }

    // Send as a single-element array for consistency with browser expectations
    String data = "CLBM:[{bl:" + String(calibration_data[waypointIndex][2]) + ",   br:" + String(calibration_data[waypointIndex][3]) +
                  ",   tr:" + String(calibration_data[waypointIndex][1]) + ",   tl:" + String(calibration_data[waypointIndex][0]) + "}]";
    HeartBeatEnabled = false;
    log_data(data.c_str());
    HeartBeatEnabled = true;
}

//Runs when the calibration data has been acknowledged as received by the computer and the calibration process is progressing
void Calibration::calibrationDataRecieved() {
    // log_info("Calibration data acknowledged received by computer");
    calibrationDataWaiting = -1;
}

//non-blocking delay, just pauses everything for specified time
void Calibration::hold(unsigned long time) {
    holdTime  = time;
    holding   = true;
    holdTimer = millis();
}

//Print calibration grid
// void Calibration::printCalibrationGrid() {
//     for (int i = 0; i <= pointCount; i++) {
//         log_info("Point " << i << ": " << calibrationGrid[i][0] << ", " << calibrationGrid[i][1]);
//     }
//     log_info("Max value for pointCount: " << pointCount);

//     for(int i = 0; i < recomputeCount; i++){
//         log_info("Recompute point: " << recomputePoints[i]);
//     }

//     log_info("Times to recompute: " << recomputeCount);

// }

//------------------------------------------------------
//------------------------------------------------------ Utility Functions
//------------------------------------------------------

//This function is used for release tension...is this function still needed?
void Calibration::comply() {
    complyCallTimer = millis();
    retracting[_TL] = false;
    retracting[_TR] = false;
    retracting[_BL] = false;
    retracting[_BR] = false;
    for (int arm = _TL; arm < ARM_COUNT; arm++) {
        Maslow.axis[arm].reset();  //This just resets the thresholds for pull tight
    }
}

// Direction from maslow current coordinates to the target coordinates
int Calibration::get_direction(double x, double y, double targetX, double targetY) {
    int direction = UP;

    if (targetX - x > 1) {
        direction = RIGHT;
    } else if (targetX - x < -1) {
        direction = LEFT;
    } else if (targetY - y > 1) {
        direction = UP;
    } else if (targetY - y < -1) {
        direction = DOWN;
    }

    return direction;
}

// Function to allocate memory for calibration arrays
void Calibration::allocateCalibrationMemory() {
    if (calibrationGrid == nullptr) {  //Check to prevent realocating
        calibrationGrid = new float[CALIBRATION_GRID_SIZE_MAX][2];
    }
    if (calibration_data == nullptr) {
        calibration_data = new float*[CALIBRATION_GRID_SIZE_MAX];
        for (int i = 0; i < CALIBRATION_GRID_SIZE_MAX; ++i) {
            calibration_data[i] = new float[4];
        }
    }
}

// Function to deallocate memory for calibration arrays
void Calibration::deallocateCalibrationMemory() {
    delete[] calibrationGrid;
    calibrationGrid = nullptr;
    for (int i = 0; i < CALIBRATION_GRID_SIZE_MAX; ++i) {
        delete[] calibration_data[i];
    }
    delete[] calibration_data;
    calibration_data = nullptr;
}

// Function to reset all calibration state variables to initial values
void Calibration::resetCalibrationState() {
    // Reset calibration progress variables
    waypoint               = 0;
    pointCount             = 0;
    recomputeCountIndex    = 0;
    calibrationInProgress  = false;
    calibrationDataWaiting = -1;

    // Reset calibration loop state variables
    calibrationDirection  = UP;    // Default direction
    measurementInProgress = true;  // Start by taking a measurement

    // Reset orientation detection variables so it runs on next calibration
    orientationDetectionDone = false;
    orientationDetectTimer   = 0;

    // Deallocate memory if allocated
    deallocateCalibrationMemory();

    log_info("Calibration state reset");
}

//Takes a raw measurement, projects it into the XY plane, then adds the belt end extension and arm length to get the actual distance.
float Calibration::measurementToXYPlane(float measurement, float zHeight) {
    auto kinematics = getKinematics();
    if (!kinematics)
        return 0.0f;

    // Include spoilboard and work thickness in the total Z height to match normal operation
    // This ensures calibration measurements are consistent with actual cutting operations
    float totalZHeight = zHeight + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness();

    // Validate that measurement is sufficient to reach the XY plane at this Z height
    // If totalZHeight >= measurement, the belt would be too short to reach the anchor point
    if (totalZHeight * totalZHeight >= measurement * measurement) {
        log_error("Invalid geometry: belt length " << measurement << " too short for Z height " << totalZHeight);
        return 0.0f;
    }

    float lengthInXY = sqrt(measurement * measurement - totalZHeight * totalZHeight);
    return lengthInXY + kinematics->getBeltEndExtension() +
           kinematics->getArmLength();  //Add the belt end extension and arm length to get the actual distance
}

//Takes an XY plane distance, subtracts the belt end extension and arm length, then calculates the angled belt measurement.
float Calibration::measurementFromXYPlane(float xyPlaneDistance, float zHeight) {
    auto kinematics = getKinematics();
    if (!kinematics)
        return 0.0f;

    // Include spoilboard and work thickness in the total Z height to match normal operation
    // This ensures calibration measurements are consistent with actual cutting operations
    float totalZHeight = zHeight + kinematics->getSpoilboardThickness() + kinematics->getWorkThickness();
    float lengthInXY =
        xyPlaneDistance - kinematics->getBeltEndExtension() - kinematics->getArmLength();  //Subtract the belt end extension and arm length

    // Validate that the XY plane distance is sufficient after subtracting mechanical offsets
    // Negative lengthInXY indicates an invalid configuration (router position too close to anchor)
    if (lengthInXY < 0.0f) {
        log_error("Invalid geometry: XY distance " << xyPlaneDistance << " too short for belt end extension + arm length");
        return 0.0f;
    }

    return sqrt(lengthInXY * lengthInXY + totalZHeight * totalZHeight);  //Calculate the angled belt length
}

/* Calculates and updates the center (X, Y) position based on the coordinates of the four corners
* (top-left, top-right, bottom-left, bottom-right) of a rectangular area. The center is determined
* by finding the intersection of the diagonals of the rectangle.
*/
void Calibration::updateCenterXY() {
    // The MaslowKinematics system handles center calculation automatically
    // We no longer maintain separate center coordinates in the Maslow class
    auto kinematics = getKinematics();
    if (kinematics) {
        // The center is already calculated in MaslowKinematics and accessible via getters
    }
}

// True if all axis were zeroed
bool Calibration::all_axis_homed() {
    return axis_homed[0] && axis_homed[1] && axis_homed[2] && axis_homed[3];
}

// True if all axis were extended
bool Calibration::allAxisExtended() {
    return extended[_TL] && extended[_TR] && extended[_BL] && extended[_BR];
}

bool Calibration::checkOverides() {
    if (TLIOveride || TRIOveride || BLIOveride || BRIOveride || TLOOveride || TROOveride || BLOOveride || BROOveride) {
        return true;
    }
    return false;
}

void Calibration::clearMotorOverrides() {
    TLIOveride = false;
    TRIOveride = false;
    BLIOveride = false;
    BRIOveride = false;
    TLOOveride = false;
    TROOveride = false;
    BLOOveride = false;
    BROOveride = false;
}

void Calibration::setSafety(bool state) {
    safetyOn = state;
}

//------------------------------------------------------
//------------------------------------------------------ Motor Overides
//------------------------------------------------------

//These are used to force one motor to rotate
void Calibration::TLI() {
    TLIOveride   = true;
    overideTimer = millis();
}
void Calibration::TRI() {
    TRIOveride   = true;
    overideTimer = millis();
}
void Calibration::BLI() {
    BLIOveride   = true;
    overideTimer = millis();
}
void Calibration::BRI() {
    BRIOveride   = true;
    overideTimer = millis();
}
void Calibration::TLO() {
    TLOOveride   = true;
    overideTimer = millis();
}
void Calibration::TRO() {
    TROOveride   = true;
    overideTimer = millis();
}
void Calibration::BLO() {
    BLOOveride   = true;
    overideTimer = millis();
}
void Calibration::BRO() {
    BROOveride   = true;
    overideTimer = millis();
}

/*
* This function is used to manuall force the motors to move for a fraction of a second to clear jams
*/
void Calibration::handleMotorOverides() {
    if (TLIOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axis[_TL].fullIn();
        } else {
            TLIOveride = false;
            Maslow.axis[_TL].stop();
        }
    }
    if (BRIOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axis[_BR].fullIn();
        } else {
            BRIOveride = false;
            Maslow.axis[_BR].stop();
        }
    }
    if (TRIOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axis[_TR].fullIn();
        } else {
            TRIOveride = false;
            Maslow.axis[_TR].stop();
        }
    }
    if (BLIOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axis[_BL].fullIn();
        } else {
            BLIOveride = false;
            Maslow.axis[_BL].stop();
        }
    }
    if (TLOOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axis[_TL].fullOut();
        } else {
            TLOOveride = false;
            Maslow.axis[_TL].stop();
        }
    }
    if (BROOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axis[_BR].fullOut();
        } else {
            BROOveride = false;
            Maslow.axis[_BR].stop();
        }
    }
    if (TROOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axis[_TR].fullOut();
        } else {
            TROOveride = false;
            Maslow.axis[_TR].stop();
        }
    }
    if (BLOOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axis[_BL].fullOut();
        } else {
            BLOOveride = false;
            Maslow.axis[_BL].stop();
        }
    }
}

/*
 * Detects the orientation (horizontal vs vertical) of the machine
 * by measuring TL and TR belt extension under gravity.
 * Returns true when detection is complete.
 */
bool Calibration::detectOrientation() {
    const unsigned long STARTUP_DELAY_MS                = 50;    // Delay before starting test to ensure stable starting position
    const unsigned long ORIENTATION_DETECT_DURATION_MS  = 1500;  // Duration in ms to run orientation detection test (1.5 seconds)
    const float         ORIENTATION_DETECT_THRESHOLD_MM = 35.0;  // Minimum extension in mm to detect vertical orientation
    const int           ORIENTATION_DETECT_SPEED        = 716;   // PWM speed for motors (70% of max 1023)
    const unsigned long MOTOR_SETTLING_PAUSE_MS         = 500;   // Pause duration after retraction to allow motors to settle

    // Track whether the settling pause has completed
    static bool settlingCompleted = false;

    // If settling has already completed, return true immediately on subsequent calls
    // This prevents re-running detection on subsequent calibration_loop iterations
    if (settlingCompleted) {
        return true;
    }

    // Initialize timer on first call
    if (orientationDetectTimer == 0) {
        orientationDetectTimer = millis();
        settlingCompleted      = false;  // Reset the flag when starting new detection
    }

    unsigned long elapsedTime = millis() - orientationDetectTimer;

    // Phase 1: Record starting positions (once at beginning)
    if (!orientationDetectionDone && elapsedTime < STARTUP_DELAY_MS) {
        // Only record and log on first cycle (elapsedTime will be very small)
        if (elapsedTime < 10) {  // First few milliseconds only
            tlStartPosition = Maslow.axis[_TL].getPosition();
            trStartPosition = Maslow.axis[_TR].getPosition();
        }
        return false;
    }

    // Phase 2: Actively drive TL and TR motors outward at 70% speed for 1.5 seconds
    // BL and BR motors are kept stopped (not powered)
    // In vertical orientation, gravity assists and belts extend significantly
    // In horizontal orientation, belts extend minimally despite motor drive
    if (!orientationDetectionDone && elapsedTime >= STARTUP_DELAY_MS && elapsedTime < (STARTUP_DELAY_MS + ORIENTATION_DETECT_DURATION_MS)) {
        // Drive TL and TR motors at 70% speed in extend direction
        Maslow.axis[_TL].driveOut(ORIENTATION_DETECT_SPEED);
        Maslow.axis[_TR].driveOut(ORIENTATION_DETECT_SPEED);
        // Ensure BL and BR are stopped
        Maslow.axis[_BL].stop();
        Maslow.axis[_BR].stop();
        return false;
    }

    // Phase 3: Measure extension and return to starting positions
    if (!orientationDetectionDone && elapsedTime >= (STARTUP_DELAY_MS + ORIENTATION_DETECT_DURATION_MS)) {
        double tlCurrentPosition = Maslow.axis[_TL].getPosition();
        double trCurrentPosition = Maslow.axis[_TR].getPosition();

        double tlExtension  = tlCurrentPosition - tlStartPosition;
        double trExtension  = trCurrentPosition - trStartPosition;
        double avgExtension = (tlExtension + trExtension) / 2.0;

        // Determine orientation based on extension amount
        bool detectedOrientation = HORIZONTAL;
        if (avgExtension > ORIENTATION_DETECT_THRESHOLD_MM) {
            detectedOrientation = VERTICAL;
            log_info("Detected VERTICAL orientation (extension > " << ORIENTATION_DETECT_THRESHOLD_MM << " mm)");
        } else {
            detectedOrientation = HORIZONTAL;
            log_info("Detected HORIZONTAL orientation (extension <= " << ORIENTATION_DETECT_THRESHOLD_MM << " mm)");
        }

        // Update the calibration object's orientation
        // This will be reflected in the Maslow_vertical configuration parameter
        orientation = detectedOrientation;
        log_info("Orientation set to: " << (orientation == VERTICAL ? "VERTICAL" : "HORIZONTAL")
                                        << " (Maslow_vertical=" << (orientation ? "true" : "false") << ")");

        // Set targets to return to starting positions
        Maslow.axis[_TL].setTarget(tlStartPosition);
        Maslow.axis[_TR].setTarget(trStartPosition);

        orientationDetectionDone = true;
        return false;
    }

    // Phase 4: Return to starting positions
    if (orientationDetectionDone) {
        // Use PID to return to starting positions
        Maslow.axis[_TL].recomputePID();
        Maslow.axis[_TR].recomputePID();

        double tlCurrentPosition = Maslow.axis[_TL].getPosition();
        double trCurrentPosition = Maslow.axis[_TR].getPosition();

        // Check if we've returned to starting positions (within 5mm tolerance)
        if (fabs(tlCurrentPosition - tlStartPosition) < 5.0 && fabs(trCurrentPosition - trStartPosition) < 5.0) {
            // Phase 5: Power down all motors and pause to allow settling
            // This prevents current spikes when immediately trying to pull belts tight in horizontal orientation
            static unsigned long settlingStartTime = 0;

            // Initialize settling timer on first entry to this phase
            if (settlingStartTime == 0) {
                settlingStartTime = millis();
            }

            // Power down all four motors during the settling pause
            for (int arm = _TL; arm < ARM_COUNT; arm++) {
                Maslow.axis[arm].stop();
            }

            // Check if settling pause is complete
            if (millis() - settlingStartTime >= MOTOR_SETTLING_PAUSE_MS) {
                settlingStartTime = 0;     // Reset for next calibration run
                settlingCompleted = true;  // Mark settling as complete to prevent re-running detection
                return true;
            }

            return false;  // Continue settling pause
        }
    }

    return false;
}