#include "Calibration.h"
#include "Maslow.h"
#include "../Kinematics/MaslowKinematics.h"
#include "../System.h"
#include "SquareCalculation.h"
#include <sstream>
#include <iomanip>

// Helper macros for accessing calibrationGrid as 1D array
// calibrationGrid is allocated as float[numPoints * 2]
// Each point has X at [i*2] and Y at [i*2+1]
#define GRID_X(i) (i * 2)
#define GRID_Y(i) (i * 2 + 1)

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
    extendedTL = tl;
    extendedTR = tr;
    extendedBL = bl;
    extendedBR = br;
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

            retractingTL = true;
            retractingTR = true;
            retractingBL = true;
            retractingBR = true;
            complyALL    = false;
            extendingALL = false;
            Maslow.axisTL.reset();
            Maslow.axisTR.reset();
            Maslow.axisBL.reset();
            Maslow.axisBR.reset();

            success = true;
            break;
        case RETRACTED:  //We can enter retracted from retracting only
            if (currentState == RETRACTING) {
                currentState = RETRACTED;
                sys.set_state(State::Idle);
                success = true;
                break;
            } else {
                break;
            }
        case EXTENDING:  //We can enter extending from retracted or extended out
            if (currentState == RETRACTED || currentState == EXTENDEDOUT) {
                currentState = EXTENDING;
                Maslow.stop();
                extendingALL = true;  //This should be replaced by state variables
                sys.set_state(State::Homing);

                extendedTL = false;
                extendedTR = false;
                extendedBL = false;
                extendedBR = false;

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
                success = true;
                break;
            } else {
                break;
            }
        case TAKING_SLACK:  //We can enter taking slack from extended only
            if (currentState == EXTENDEDOUT) {
                currentState = TAKING_SLACK;

                //Reset the axis targets at the beginning of taking slack
                Maslow.axisTL.setTarget(Maslow.axisTL.getPosition());
                Maslow.axisTR.setTarget(Maslow.axisTR.getPosition());

                retractingTL = false;  //Should be replaced by state now
                retractingTR = false;
                retractingBL = false;
                retractingBR = false;
                extendingALL = false;
                complyALL    = false;

                Maslow.axisTL.reset();
                Maslow.axisTR.reset();
                Maslow.axisBL.reset();
                Maslow.axisBR.reset();

                Maslow.x  = 0;
                Maslow.y  = 0;
                takeSlack = true;

                //Alocate the memory to store the measurements in. This is used here because take slack will use the same memory as the calibration
                // Only need 1 measurement point for take slack
                allocateCalibrationMemory(1);
                success = true;
                break;
            } else {
                log_info("Cannot take slack until the belts have been extended");
                break;
            }
        case CALIBRATION_IN_PROGRESS:  //We can enter calibration in progress from EXTENDEDOUT, READY_TO_CUT, or CALIBRATION_COMPUTING
            if (currentState == EXTENDEDOUT || currentState == READY_TO_CUT || currentState == CALIBRATION_COMPUTING) {
                currentState = CALIBRATION_IN_PROGRESS;

                //Update encoder positions before reading them to ensure fresh values
                Maslow.axisTL.updateEncoderPosition();
                Maslow.axisTR.updateEncoderPosition();
                Maslow.axisBL.updateEncoderPosition();
                Maslow.axisBR.updateEncoderPosition();

                //Reset the axis targets at the beginning of calibration
                float tlPos = Maslow.axisTL.getPosition();
                float trPos = Maslow.axisTR.getPosition();
                float blPos = Maslow.axisBL.getPosition();
                float brPos = Maslow.axisBR.getPosition();
                
                log_info("Motor positions before setTarget: TL=" << tlPos << " TR=" << trPos << " BL=" << blPos << " BR=" << brPos);
                
                Maslow.axisTL.setTarget(tlPos);
                Maslow.axisTR.setTarget(trPos);
                Maslow.axisBL.setTarget(blPos);
                Maslow.axisBR.setTarget(brPos);

                sys.set_state(State::Homing);

                // Log the calibration orientation mode for debugging
                log_info("Calibration starting in " << (orientation == VERTICAL ? "VERTICAL" : "HORIZONTAL") << " orientation mode");

                //If we are at the first point, initialize calibration state
                if (waypoint == 0) {
                    // Initialize calibration loop state for fresh start
                    calibrationDirection  = UP;
                    measurementInProgress = true;
                    // Allocate memory for at least the first 6 waypoints (0-5)
                    // The full grid will be generated and allocated after waypoint 5 calibration calculation
                    allocateCalibrationMemory(6);
                    // Set pointCount to 6 for the initial waypoints (will be updated after grid generation)
                    pointCount = 6;
                    // Set the first recompute point to waypoint 5
                    recomputePoints[0]  = 5;
                    recomputeCountIndex = 0;
                    
                    // Log the expected calibration parameters before starting
                    log_info("=== Starting Calibration ===");
                    log_info("Orientation: " << (orientation == VERTICAL ? "VERTICAL" : "HORIZONTAL"));
                    log_info("Grid size: " << calibrationGridSize << "x" << calibrationGridSize);
                    log_info("Grid dimensions: " << calibration_grid_width_mm_X << "x" << calibration_grid_height_mm_Y << "mm");
                    log_info("First 6 waypoints will be measured to determine machine position");
                    log_info("Full calibration grid will be generated after waypoint 5");
                    
                    // Calculate expected number of waypoints for user information
                    // Use calibrationGridSize (or calibrationGridSizeX/Y if those are used)
                    int expectedGridSizeX = (calibrationGridSizeX > 0) ? calibrationGridSizeX : calibrationGridSize;
                    int expectedGridSizeY = (calibrationGridSizeY > 0) ? calibrationGridSizeY : calibrationGridSize;
                    int expectedTotalWaypoints = 6 + (expectedGridSizeX * expectedGridSizeY) + 2;
                    log_info("Expected total waypoints: " << expectedTotalWaypoints 
                            << " (6 initial + " << (expectedGridSizeX * expectedGridSizeY) << " grid + 2 return)");
                }

                // If at waypoint 6 and grid hasn't been generated yet (pointCount still 6), generate the calibration grid
                // This ensures the grid uses the updated anchor positions from the first calibration calculation
                // Only generate once - not on subsequent CALIBRATION_COMPUTING cycles
                if (waypoint == 6 && pointCount == 6) {
                    log_info("Generating calibration grid after waypoint 5 calculation with updated anchor positions");

                    // Save the calibration data from the first 6 waypoints before deallocation
                    float savedCalibrationData[6][4];
                    for (int i = 0; i < 6; i++) {
                        for (int j = 0; j < 4; j++) {
                            savedCalibrationData[i][j] = calibration_data[i][j];
                        }
                    }

                    // Deallocate the initial 6-point allocation
                    deallocateCalibrationMemory();

                    // Generate the full grid which will allocate the correct amount
                    if (!generate_calibration_grid()) {
                        log_error("Failed to generate calibration grid after waypoint 5 calculation");
                        return false;
                    }

                    // Restore the calibration data from the first 6 waypoints
                    for (int i = 0; i < 6; i++) {
                        for (int j = 0; j < 4; j++) {
                            calibration_data[i][j] = savedCalibrationData[i][j];
                        }
                    }

                    // Recalculate the first 6 waypoint coordinates based on the current machine position
                    // using the updated anchor positions from the calibration calculations
                    float x = 0;
                    float y = 0;
                    if (computeXYfromLengths(calibration_data[0][0], calibration_data[0][1], x, y)) {
                        log_info("Recalculating first 6 waypoint coordinates with updated anchors. Machine position: X=" << x << " Y=" << y);
                        calibrationGrid[GRID_X(0)] = x;
                        calibrationGrid[GRID_Y(0)] = y;
                        calibrationGrid[GRID_X(1)] = x + 150;
                        calibrationGrid[GRID_Y(1)] = y;
                        calibrationGrid[GRID_X(2)] = x + 150;
                        calibrationGrid[GRID_Y(2)] = y + 150;
                        calibrationGrid[GRID_X(3)] = x;
                        calibrationGrid[GRID_Y(3)] = y + 150;
                        calibrationGrid[GRID_X(4)] = x - 150;
                        calibrationGrid[GRID_Y(4)] = y + 150;
                        calibrationGrid[GRID_X(5)] = x - 150;
                        calibrationGrid[GRID_Y(5)] = y;
                    } else {
                        log_error("Failed to compute machine position for waypoint coordinate recalculation");
                        return false;
                    }

                    // Log waypoint locations after grid generation and restoration of first 6 waypoints (up to 30 items per log message)
                    log_info("=== Calibration Grid Generated ===");
                    log_info("Total waypoints: " << pointCount + 1 << " (waypoints 0-" << pointCount << ")");
                    log_info("Grid points: " << (pointCount + 1 - 6 - 2) << " (after 6 initial, before 2 return-to-center)");
                    log_info("Waypoint coordinates:");
                    for (int i = 0; i <= pointCount; i += 30) {
                        // Build log message more carefully to avoid heap issues
                        std::stringstream logMsg;
                        logMsg << "  [" << i << "-" << min(i + 29, pointCount) << "]: ";
                        for (int j = i; j <= min(i + 29, pointCount); j++) {
                            logMsg << "(" << j << ":" << std::fixed << std::setprecision(1) 
                                   << calibrationGrid[GRID_X(j)] << "," << calibrationGrid[GRID_Y(j)] << ")";
                            if (j < min(i + 29, pointCount)) {
                                logMsg << " ";
                            }
                        }
                        log_info(logMsg.str().c_str());
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
                if (kinematics && computeXYfromLengths(measurementToXYPlane(Maslow.axisTL.getPosition(), kinematics->getTlZ()),
                                                       measurementToXYPlane(Maslow.axisTR.getPosition(), kinematics->getTrZ()),
                                                       x,
                                                       y)) {
                    //We reset the last waypoint to where it actually is so that we can move from the updated position to the next waypoint
                    if (waypoint > 0) {
                        calibrationGrid[GRID_X(waypoint - 1)] = x;
                        calibrationGrid[GRID_Y(waypoint - 1)] = y;
                    }

                    log_info("Machine Position found as X: " << x << " Y: " << y);

                    //Set the internal machine position using actual belt positions to avoid synchronization issues
                    // Get current belt positions from hardware and set motor steps directly
                    float tlBeltLength = Maslow.axisTL.getPosition();  // Actual belt position from hardware
                    float trBeltLength = Maslow.axisTR.getPosition();
                    float blBeltLength = Maslow.axisBL.getPosition();
                    float brBeltLength = Maslow.axisBR.getPosition();

                    log_info("Setting motor positions from hardware readings:");
                    log_info("TL: " << tlBeltLength << " TR: " << trBeltLength << " BL: " << blBeltLength << " BR: " << brBeltLength);

                    // Set motor positions directly from hardware readings
                    // Axis mapping: A=TL(0), B=TR(1), C=BL(2), D=BR(3), Z=router(4)
                    set_motor_steps(0, mpos_to_steps(tlBeltLength, 0));  // A axis = TL belt
                    set_motor_steps(1, mpos_to_steps(trBeltLength, 1));  // B axis = TR belt
                    set_motor_steps(2, mpos_to_steps(blBeltLength, 2));  // C axis = BL belt
                    set_motor_steps(3, mpos_to_steps(brBeltLength, 3));  // D axis = BR belt
                    set_motor_steps(4, mpos_to_steps(0.0, 4));           // Z axis = 0 (surface level) during calibration

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
                retractingTL    = false;
                retractingTR    = false;
                retractingBL    = false;
                retractingBR    = false;
                extendingALL    = false;
                complyALL       = true;
                Maslow.axisTL.reset();  //This just resets the thresholds for pull tight
                Maslow.axisTR.reset();
                Maslow.axisBL.reset();
                Maslow.axisBR.reset();
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
            if (retractingTL) {
                if (Maslow.axisTL.retract()) {
                    retractingTL  = false;
                    axis_homed[0] = true;
                    extendedTL    = false;
                }
            }
            if (retractingTR) {
                if (Maslow.axisTR.retract()) {
                    retractingTR  = false;
                    axis_homed[1] = true;
                    extendedTR    = false;
                }
            }
            if (retractingBL) {
                if (Maslow.axisBL.retract()) {
                    retractingBL  = false;
                    axis_homed[2] = true;
                    extendedBL    = false;
                }
            }
            if (retractingBR) {
                if (Maslow.axisBR.retract()) {
                    retractingBR  = false;
                    axis_homed[3] = true;
                    extendedBR    = false;
                }
            }

            //Once the limits are hit switch to the next state
            if (!retractingTL && !retractingBL && !retractingBR && !retractingTR) {
                requestStateChange(RETRACTED);
            }

            break;
        case EXTENDING:
            //decompress belts for the first half second
            if (millis() - extendCallTimer < 700) {
                if (millis() - extendCallTimer > 0)
                    Maslow.axisBR.decompressBelt();
                if (millis() - extendCallTimer > 150)
                    Maslow.axisBL.decompressBelt();
                if (millis() - extendCallTimer > 250)
                    Maslow.axisTR.decompressBelt();
                if (millis() - extendCallTimer > 350)
                    Maslow.axisTL.decompressBelt();
            }
            //then make all the belts comply until they are extended fully, or user terminates it
            else {
                if (!extendedTL)
                    extendedTL = Maslow.axisTL.extend(extendDist);
                if (!extendedTR)
                    extendedTR = Maslow.axisTR.extend(extendDist);
                if (!extendedBL)
                    extendedBL = Maslow.axisBL.extend(extendDist);
                if (!extendedBR)
                    extendedBR = Maslow.axisBR.extend(extendDist);
                if (extendedTL && extendedTR && extendedBL && extendedBR) {
                    extendingALL = false;
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
                Maslow.axisBR.decompressBelt();
                Maslow.axisBL.decompressBelt();
                Maslow.axisTR.decompressBelt();
                Maslow.axisTL.decompressBelt();
            } else if (millis() - complyCallTimer < 800) {
                Maslow.axisTL.comply();
                Maslow.axisTR.comply();
                Maslow.axisBL.comply();
                Maslow.axisBR.comply();
            } else {
                Maslow.axisTL.stop();
                Maslow.axisTR.stop();
                Maslow.axisBL.stop();
                Maslow.axisBR.stop();
                complyALL = false;
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
    if (!retractingTL && !retractingBL && !retractingBR && !retractingTR && !extendingALL && !complyALL && !calibrationInProgress &&
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
        //Log all belt measurements before resetting calibration state (up to 5 items per log message)
        log_info("Logging belt measurements for " << waypoint << " waypoints");
        for (int i = 0; i < waypoint; i += 5) {
            // Build log message more carefully to avoid heap issues
            std::stringstream logMsg;
            logMsg << "Belt lengths [" << i << "-" << min(i + 4, waypoint - 1) << "]: ";
            for (int j = i; j < min(i + 5, waypoint); j++) {
                logMsg << "(" << j << ":TL=" << std::fixed << std::setprecision(2) << calibration_data[j][0]
                       << ",TR=" << calibration_data[j][1] << ",BL=" << calibration_data[j][2]
                       << ",BR=" << calibration_data[j][3] << ")";
                if (j < min(i + 4, waypoint - 1)) {
                    logMsg << " ";
                }
            }
            log_info(logMsg.str().c_str());
        }

        //Reset all of the calibration variables to the defaults so that calibration can be run again
        resetCalibrationState();
        requestStateChange(READY_TO_CUT);
        log_info("Calibration complete");
        return;
    }
    //Taking measurment once we've reached the point
    if (measurementInProgress) {
        if (take_measurement_avg_with_check(waypoint, calibrationDirection)) {  //Takes a measurement and returns true if it's done
            measurementInProgress = false;

            waypoint++;  //Increment the waypoint counter

            if (waypoint > recomputePoints[recomputeCountIndex]) {  //If we have reached the end of this stage of the calibration process
                requestStateChange(CALIBRATION_COMPUTING);
                print_calibration_data();
                calibrationDataWaiting = millis();
                sys.set_state(State::Idle);
                recomputeCountIndex++;
            } else {
                hold(250);
            }
        }
    }

    //Move to the next point in the grid
    else {
        if (move_with_slack(calibrationGrid[GRID_X(waypoint - 1)],
                            calibrationGrid[GRID_Y(waypoint - 1)],
                            calibrationGrid[GRID_X(waypoint)],
                            calibrationGrid[GRID_Y(waypoint)])) {
            measurementInProgress = true;
            calibrationDirection  = get_direction(
                calibrationGrid[GRID_X(waypoint - 1)],
                calibrationGrid[GRID_Y(waypoint - 1)],
                calibrationGrid[GRID_X(waypoint)],
                calibrationGrid[GRID_Y(waypoint)]);  //This is used to set the order that the belts are pulled tight in the following measurement
            Maslow.x = calibrationGrid[GRID_X(waypoint)];  //Are these ever used anywhere?
            Maslow.y = calibrationGrid[GRID_Y(waypoint)];
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

            float extension = kinematics->getBeltEndExtension() + kinematics->getArmLength();

            //This should use it's own array, this is not calibration data
            float diffTL = calibration_data[0][0] - measurementToXYPlane(kinematics->computeTL(x, y, 0), kinematics->getTlZ());
            float diffTR = calibration_data[0][1] - measurementToXYPlane(kinematics->computeTR(x, y, 0), kinematics->getTrZ());
            float diffBL = calibration_data[0][2] - measurementToXYPlane(kinematics->computeBL(x, y, 0), kinematics->getBlZ());
            float diffBR = calibration_data[0][3] - measurementToXYPlane(kinematics->computeBR(x, y, 0), kinematics->getBrZ());
            log_info("Center point deviation: TL: " << diffTL << " TR: " << diffTR << " BL: " << diffBL << " BR: " << diffBR);
            double threshold = 12;
            if (abs(diffTL) > threshold || abs(diffTR) > threshold || abs(diffBL) > threshold || abs(diffBR) > threshold) {
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

                log_info("Current machine position loaded as X: " << x << " Y: " << y);

                // Instead of setting cartesian position and letting kinematics recalculate motor positions,
                // we need to set the motor positions directly from the measured belt lengths to avoid
                // position synchronization issues between hardware and FluidNC's motion planning system

                // Get current motor position array
                float* mpos = get_mpos();
                log_info("Before update - mpos: X=" << mpos[0] << " Y=" << mpos[1] << " Z=" << mpos[2]);

                // Convert measured XY plane distances to actual belt lengths for motor positions
                // calibration_data[0] contains measured XY plane distances: [TL, TR, BL, BR]
                float tlBeltLength = measurementFromXYPlane(calibration_data[0][0], kinematics->getTlZ());
                float trBeltLength = measurementFromXYPlane(calibration_data[0][1], kinematics->getTrZ());
                float blBeltLength = measurementFromXYPlane(calibration_data[0][2], kinematics->getBlZ());
                float brBeltLength = measurementFromXYPlane(calibration_data[0][3], kinematics->getBrZ());

                log_info("Setting motor positions directly from measurements:");
                log_info("TL belt: " << tlBeltLength << " TR belt: " << trBeltLength);
                log_info("BL belt: " << blBeltLength << " BR belt: " << brBeltLength);

                // Set motor positions directly from measured belt lengths
                // Axis mapping: A=TL(0), B=TR(1), C=BL(2), D=BR(3), Z=router(4)
                set_motor_steps(0, mpos_to_steps(tlBeltLength, 0));  // A axis = TL belt
                set_motor_steps(1, mpos_to_steps(trBeltLength, 1));  // B axis = TR belt
                set_motor_steps(2, mpos_to_steps(blBeltLength, 2));  // C axis = BL belt
                set_motor_steps(3, mpos_to_steps(brBeltLength, 3));  // D axis = BR belt
                // Z axis is left unchanged during Apply Tension process

                // Verify that the position was set correctly by reading back from motors
                float* verify_mpos = get_mpos();
                log_info("After update - mpos: X=" << verify_mpos[0] << " Y=" << verify_mpos[1] << " Z=" << verify_mpos[2]);

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
        Maslow.axisTL.recomputePID();
        Maslow.axisTR.recomputePID();

        //On the left side of the sheet we want to pull the left belt tight first
        if (Maslow.x < 0) {
            if (!BL_tight) {
                if (Maslow.axisBL.pull_tight(current)) {
                    BL_tight = true;
                    //log_info("Pulled BL tight");
                }
                return false;
            }
            if (!BR_tight) {
                if (Maslow.axisBR.pull_tight(current)) {
                    BR_tight = true;
                    //log_info("Pulled BR tight");
                }
                return false;
            }
        }

        //On the right side of the sheet we want to pull the right belt tight first
        else {
            if (!BR_tight) {
                if (Maslow.axisBR.pull_tight(current)) {
                    BR_tight = true;
                    //log_info("Pulled BR tight");
                }
                return false;
            }
            if (!BL_tight) {
                if (Maslow.axisBL.pull_tight(current)) {
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
            //take measurement and record it to the calibration data array.
            result[0] = measurementToXYPlane(Maslow.axisTL.getPosition(), kinematics->getTlZ());
            result[1] = measurementToXYPlane(Maslow.axisTR.getPosition(), kinematics->getTrZ());
            result[2] = measurementToXYPlane(Maslow.axisBL.getPosition(), kinematics->getBlZ());
            result[3] = measurementToXYPlane(Maslow.axisBR.getPosition(), kinematics->getBrZ());
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
            static bool tl_tight                 = false;
            static bool tr_tight                 = false;
            static bool bl_tight                 = false;
            static bool br_tight                 = false;
            static bool initial_tension_complete = false;

            // Phase 1: Pull all four belts tight simultaneously to eliminate slack
            if (!initial_tension_complete) {
                if (Maslow.axisTL.pull_tight(current)) {
                    tl_tight = true;
                }
                if (Maslow.axisTR.pull_tight(current)) {
                    tr_tight = true;
                }
                if (Maslow.axisBL.pull_tight(current)) {
                    bl_tight = true;
                }
                if (Maslow.axisBR.pull_tight(current)) {
                    br_tight = true;
                }

                // Once all belts are tight, move to phase 2
                if (tl_tight && tr_tight && bl_tight && br_tight) {
                    initial_tension_complete = true;
                    // Set TL and TR targets to their current positions to prevent unwanted movement
                    Maslow.axisTL.setTarget(Maslow.axisTL.getPosition());
                    Maslow.axisTR.setTarget(Maslow.axisTR.getPosition());
                    // Reset belt tight flags for the actual measurement phase
                    bl_tight = false;
                    br_tight = false;
                }
                return false;
            }

            // Phase 2: Hold TL and TR in position, then pull BL and BR based on x-coordinate
            // This ensures TL and TR remain as stable reference points for position calculation
            Maslow.axisTL.recomputePID();
            Maslow.axisTR.recomputePID();

            // Pull bottom belts based on x-coordinate (same logic as vertical mode)
            if (Maslow.x < 0) {
                // On the left side, pull BL first, then BR
                if (!bl_tight) {
                    if (Maslow.axisBL.pull_tight(current)) {
                        bl_tight = true;
                    }
                    return false;
                }
                if (!br_tight) {
                    if (Maslow.axisBR.pull_tight(current)) {
                        br_tight = true;
                    }
                    return false;
                }
            } else {
                // On the right side, pull BR first, then BL
                if (!br_tight) {
                    if (Maslow.axisBR.pull_tight(current)) {
                        br_tight = true;
                    }
                    return false;
                }
                if (!bl_tight) {
                    if (Maslow.axisBL.pull_tight(current)) {
                        bl_tight = true;
                    }
                    return false;
                }
            }

            // Once both bottom belts are tight, take the measurement
            if (bl_tight && br_tight) {
                auto kinematics = getKinematics();
                if (!kinematics)
                    return false;
                //take measurement and record it to the calibration data array.
                result[0] = measurementToXYPlane(Maslow.axisTL.getPosition(), kinematics->getTlZ());
                result[1] = measurementToXYPlane(Maslow.axisTR.getPosition(), kinematics->getTrZ());
                result[2] = measurementToXYPlane(Maslow.axisBL.getPosition(), kinematics->getBlZ());
                result[3] = measurementToXYPlane(Maslow.axisBR.getPosition(), kinematics->getBrZ());
                // Reset all flags for next measurement
                tl_tight                 = false;
                tr_tight                 = false;
                bl_tight                 = false;
                br_tight                 = false;
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
                    holdAxis1 = &Maslow.axisTL;
                    holdAxis2 = &Maslow.axisTR;
                    if (Maslow.x < 0) {
                        pullAxis1 = &Maslow.axisBL;
                        pullAxis2 = &Maslow.axisBR;
                    } else {
                        pullAxis1 = &Maslow.axisBR;
                        pullAxis2 = &Maslow.axisBL;
                    }
                    break;
                case DOWN:
                    holdAxis1 = &Maslow.axisBL;
                    holdAxis2 = &Maslow.axisBR;
                    if (Maslow.x < 0) {
                        pullAxis1 = &Maslow.axisTL;
                        pullAxis2 = &Maslow.axisTR;
                    } else {
                        pullAxis1 = &Maslow.axisTR;
                        pullAxis2 = &Maslow.axisTL;
                    }
                    break;
                case LEFT:
                    holdAxis1 = &Maslow.axisTL;
                    holdAxis2 = &Maslow.axisBL;
                    if (Maslow.y < 0) {
                        pullAxis1 = &Maslow.axisBR;
                        pullAxis2 = &Maslow.axisTR;
                    } else {
                        pullAxis1 = &Maslow.axisTR;
                        pullAxis2 = &Maslow.axisBR;
                    }
                    break;
                case RIGHT:
                    holdAxis1 = &Maslow.axisTR;
                    holdAxis2 = &Maslow.axisBR;
                    if (Maslow.y < 0) {
                        pullAxis1 = &Maslow.axisBL;
                        pullAxis2 = &Maslow.axisTL;
                    } else {
                        pullAxis1 = &Maslow.axisTL;
                        pullAxis2 = &Maslow.axisBL;
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
                //take measurement and record it to the calibration data array.
                result[0]   = measurementToXYPlane(Maslow.axisTL.getPosition(), kinematics->getTlZ());
                result[1]   = measurementToXYPlane(Maslow.axisTR.getPosition(), kinematics->getTrZ());
                result[2]   = measurementToXYPlane(Maslow.axisBL.getPosition(), kinematics->getBlZ());
                result[3]   = measurementToXYPlane(Maslow.axisBR.getPosition(), kinematics->getBrZ());
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

    int howHardToPull = calibrationCurrentThreshold;
    if (measureFlex) {
        howHardToPull = calibrationCurrentThreshold + 500;
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
            
            // Log individual measurements before averaging for debugging
            std::stringstream logMsg;
            logMsg << "Waypoint " << waypoint << " measurements: ";
            for (int j = 0; j < 4; j++) {
                logMsg << "[" << j << ":TL=" << std::fixed << std::setprecision(2) << measurements[j][0]
                       << ",TR=" << measurements[j][1] << ",BL=" << measurements[j][2] << ",BR=" << measurements[j][3] << "]";
                if (j < 3) logMsg << " ";
            }
            log_debug(logMsg.str().c_str());
            
            for (int i = 0; i < 4; i++) {  //For each axis
                sum                           = measurements[0][i] + measurements[1][i] + measurements[2][i] + measurements[3][i];
                avg                           = sum / 4;
                calibration_data[waypoint][i] = avg;  //This is the only time we should be writing to the calibration data array
                sum                           = 0;
                criticalCounter               = 0;
            }
            log_info("Measured waypoint " << waypoint << " - Averaged belt lengths: TL=" << calibration_data[waypoint][0] << " TR=" << calibration_data[waypoint][1] << " BL=" << calibration_data[waypoint][2] << " BR=" << calibration_data[waypoint][3]);

            //A check to see if the results on the first point are within the expected range
            //This logic should only run during calibration, not during Apply Tension
            if (waypoint == 0 && currentState == CALIBRATION_IN_PROGRESS) {
                //Recompute the machine position with the belt lenths and compare the results to that
                float x = 0;
                float y = 0;
                computeXYfromLengths(calibration_data[0][0], calibration_data[0][1], x, y);

                //If the frame size is way off, we will compute a rough (assumed to be a square) frame size from the first measurmeent
                auto kinematics = getKinematics();
                if (!kinematics)
                    return false;

                double threshold = 100;
                float  diffTL    = calibration_data[0][0] - measurementToXYPlane(kinematics->computeTL(x, y, 0), kinematics->getTlZ());
                float  diffTR    = calibration_data[0][1] - measurementToXYPlane(kinematics->computeTR(x, y, 0), kinematics->getTrZ());
                float  diffBL    = calibration_data[0][2] - measurementToXYPlane(kinematics->computeBL(x, y, 0), kinematics->getBlZ());
                float  diffBR    = calibration_data[0][3] - measurementToXYPlane(kinematics->computeBR(x, y, 0), kinematics->getBrZ());
                log_info("Center point off by: TL: " << diffTL << " TR: " << diffTR << " BL: " << diffBL << " BR: " << diffBR);

                if (abs(diffTL) > threshold || abs(diffTR) > threshold || abs(diffBL) > threshold || abs(diffBR) > threshold) {
                    log_error("Center point off by over " << threshold << "mm");

                    if (!adjustFrameSizeToMatchFirstMeasurement()) {
                        Maslow.eStop("Unable to find a valid frame size to match the first measurement");
                        resetCalibrationState();
                        criticalCounter = 0;
                        freeMeasurements();
                        requestStateChange(EXTENDEDOUT);
                        return false;
                    }

                    // Frame size adjusted - grid will be generated after waypoint 5 calibration calculation
                    log_info("Frame size adjusted based on first measurement");
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
                calibrationGrid[GRID_X(0)] =
                    x;  //This first point is never really used because we've already measured here, but it shouldn't be left undefined
                calibrationGrid[GRID_Y(0)] = y;
                calibrationGrid[GRID_X(1)] = x + 150;
                calibrationGrid[GRID_Y(1)] = y;
                calibrationGrid[GRID_X(2)] = x + 150;
                calibrationGrid[GRID_Y(2)] = y + 150;
                calibrationGrid[GRID_X(3)] = x;
                calibrationGrid[GRID_Y(3)] = y + 150;
                calibrationGrid[GRID_X(4)] = x - 150;
                calibrationGrid[GRID_Y(4)] = y + 150;
                calibrationGrid[GRID_X(5)] = x - 150;
                calibrationGrid[GRID_Y(5)] = y;
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
            Maslow.axisTL.recomputePID();
            Maslow.axisTR.recomputePID();
            Maslow.axisBL.decompressBelt();
            Maslow.axisBR.decompressBelt();
        } else {
            switch (direction) {
                case UP:
                    Maslow.axisBL.decompressBelt();
                    Maslow.axisBR.decompressBelt();
                    break;
                case DOWN:
                    Maslow.axisTL.decompressBelt();
                    Maslow.axisTR.decompressBelt();
                    break;
                case LEFT:
                    Maslow.axisTR.decompressBelt();
                    Maslow.axisBR.decompressBelt();
                    break;
                case RIGHT:
                    Maslow.axisTL.decompressBelt();
                    Maslow.axisBL.decompressBelt();
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
        Maslow.axisTL.setTarget(Maslow.axisTL.getPosition());
        Maslow.axisTR.setTarget(Maslow.axisTR.getPosition());
        Maslow.axisBL.setTarget(Maslow.axisBL.getPosition());
        Maslow.axisBR.setTarget(Maslow.axisBR.getPosition());

        // Stabilize all belts at their new target positions to prevent unwinding
        Maslow.axisTL.recomputePID();
        Maslow.axisTR.recomputePID();
        Maslow.axisBL.recomputePID();
        Maslow.axisBR.recomputePID();

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
            Maslow.axisBL.stop();
            Maslow.axisBR.stop();
            // Reset only the stopped axes
            Maslow.axisBL.reset();
            Maslow.axisBR.reset();
        } else {
            // In horizontal mode, stop belts based on measurement direction
            int measurementDirection = get_direction(fromX, fromY, toX, toY);
            switch (measurementDirection) {
                case UP:
                    // TL and TR will be hold belts, stop BL and BR
                    Maslow.axisBL.stop();
                    Maslow.axisBR.stop();
                    // Reset only the stopped axes
                    Maslow.axisBL.reset();
                    Maslow.axisBR.reset();
                    break;
                case DOWN:
                    // BL and BR will be hold belts, stop TL and TR
                    Maslow.axisTL.stop();
                    Maslow.axisTR.stop();
                    // Reset only the stopped axes
                    Maslow.axisTL.reset();
                    Maslow.axisTR.reset();
                    break;
                case LEFT:
                    // TL and BL will be hold belts, stop TR and BR
                    Maslow.axisTR.stop();
                    Maslow.axisBR.stop();
                    // Reset only the stopped axes
                    Maslow.axisTR.reset();
                    Maslow.axisBR.reset();
                    break;
                case RIGHT:
                    // TR and BR will be hold belts, stop TL and BL
                    Maslow.axisTL.stop();
                    Maslow.axisBL.stop();
                    // Reset only the stopped axes
                    Maslow.axisTL.reset();
                    Maslow.axisBL.reset();
                    break;
            }
        }

        decompress = true;  //Reset for the next pass
        return true;
    }

    //In vertical orientation we want to move with the top two belts and always have the lower ones be slack
    if (orientation == VERTICAL) {
        Maslow.axisTL.recomputePID();
        Maslow.axisTR.recomputePID();
        if (withSlack) {
            Maslow.axisBL.comply();
            Maslow.axisBR.comply();
        } else {
            Maslow.axisBL.recomputePID();
            Maslow.axisBR.recomputePID();
        }
    } else {
        //For each belt we check to see if it should be slack
        if (withSlack && tlExtending) {
            Maslow.axisTL.comply();
        } else {
            Maslow.axisTL.recomputePID();
        }

        if (withSlack && trExtending) {
            Maslow.axisTR.comply();
        } else {
            Maslow.axisTR.recomputePID();
        }

        if (withSlack && blExtending) {
            Maslow.axisBL.comply();
        } else {
            Maslow.axisBL.recomputePID();
        }

        if (withSlack && brExtending) {
            Maslow.axisBR.comply();
        } else {
            Maslow.axisBR.recomputePID();
        }
    }

    return false;  //We have not yet reached our target position
}

//The number of points high and wide  must be an odd number
bool Calibration::generate_calibration_grid() {
    // Calculate grid dimensions based on frame geometry to stay "in the green"
    auto kinematics = getKinematics();
    if (!kinematics) {
        log_error("generate_calibration_grid: MaslowKinematics not available");
        return false;
    }

    // Check if any of the four parameters is 0 - if so, auto-calculate all four from geometry
    // Changed from AND logic to OR logic: if ANY parameter is 0, calculate ALL from geometry
    // Copy to local variables first to ensure proper alignment and avoid potential access issues
    float widthParam  = calibration_grid_width_mm_X;
    float heightParam = calibration_grid_height_mm_Y;
    int   sizeXParam  = calibrationGridSizeX;
    int   sizeYParam  = calibrationGridSizeY;

    bool autoCalculateFromGeometry = (widthParam == 0.0f || heightParam == 0.0f || sizeXParam == 0 || sizeYParam == 0);

    float gridWidth, gridHeight;
    float frameWidth, frameHeight;  // Declare here so they're available later

    if (autoCalculateFromGeometry) {
        log_info("Auto-calculating calibration grid parameters from frame geometry (at least one parameter set to 0)");

        // Step 1: Calculate grid dimensions from geometry
        // Get anchor coordinates
        float tlX = kinematics->getTlX();
        float tlY = kinematics->getTlY();
        float trX = kinematics->getTrX();
        float trY = kinematics->getTrY();
        float blX = kinematics->getBlX();
        float blY = kinematics->getBlY();
        float brX = kinematics->getBrX();
        float brY = kinematics->getBrY();

        // Get center point (already calculated in kinematics)
        float centerX = kinematics->getCenterX();
        float centerY = kinematics->getCenterY();

        // Calculate direction vector from center to top-left anchor
        float dirX      = tlX - centerX;
        float dirY      = tlY - centerY;
        float dirLength = sqrt(dirX * dirX + dirY * dirY);

        // Normalize direction vector
        dirX /= dirLength;
        dirY /= dirLength;

        // We need to find point A on the line from center to TL where angle BL-A-TR is 130 degrees
        // Using binary search to find the point
        const float targetAngleDeg = 130.0f;
        const float targetAngleRad = targetAngleDeg * M_PI / 180.0f;

        float minDist     = 0.0f;
        float maxDist     = dirLength * 2.0f;  // Search up to 2x the distance to TL
        float optimalDist = maxDist / 2.0f;

        // Binary search for the point where angle is closest to 130 degrees
        for (int iter = 0; iter < 30; iter++) {
            float testDist = (minDist + maxDist) / 2.0f;
            float testX    = centerX + dirX * testDist;
            float testY    = centerY + dirY * testDist;

            // Calculate vectors from test point to BL and TR
            float toBLX = blX - testX;
            float toBLY = blY - testY;
            float toTRX = trX - testX;
            float toTRY = trY - testY;

            // Calculate angle between vectors using dot product
            float lenBL = sqrt(toBLX * toBLX + toBLY * toBLY);
            float lenTR = sqrt(toTRX * toTRX + toTRY * toTRY);

            if (lenBL > 0 && lenTR > 0) {
                float dotProduct = (toBLX * toTRX + toBLY * toTRY) / (lenBL * lenTR);
                dotProduct       = constrain(dotProduct, -1.0f, 1.0f);  // Clamp to avoid numerical issues
                float angle      = acos(dotProduct);

                // Adjust search range
                if (angle < targetAngleRad) {
                    maxDist = testDist;
                } else {
                    minDist = testDist;
                }

                optimalDist = testDist;
            }
        }

        // Point A is at optimalDist from center along the line to TL
        float pointAX = centerX + dirX * optimalDist;
        float pointAY = centerY + dirY * optimalDist;

        // Point B is at 80% of the distance from center to point A
        float pointBX = centerX + dirX * optimalDist * 0.8f;
        float pointBY = centerY + dirY * optimalDist * 0.8f;

        // Calculate grid dimensions as 2x the distance from B to center
        gridWidth  = 2.0f * fabs(pointBX - centerX);
        gridHeight = 2.0f * fabs(pointBY - centerY);

        // Apply safety constraints
        // 1. Maximum calibration area limits (7' wide x 3' high)
        const float maxCalibrationWidth  = 7.0f * 12.0f * 25.4f;  // 7 feet = 84 inches = 2133.6mm
        const float maxCalibrationHeight = 3.0f * 12.0f * 25.4f;  // 3 feet = 36 inches = 914.4mm

        if (gridWidth > maxCalibrationWidth) {
            gridWidth = maxCalibrationWidth;
            log_info("Grid width limited to maximum: " << maxCalibrationWidth << "mm (7 feet)");
        }
        if (gridHeight > maxCalibrationHeight) {
            gridHeight = maxCalibrationHeight;
            log_info("Grid height limited to maximum: " << maxCalibrationHeight << "mm (3 feet)");
        }

        // 2. Grid must be at least 16" (406.4mm) smaller than frame in each dimension
        //    to prevent sled (16" diameter) from hitting frame edges
        const float sledDiameter = 406.4f;           // 16 inches in mm
        frameWidth               = fabs(trX - tlX);  // Approximate frame width
        frameHeight              = fabs(tlY - blY);  // Approximate frame height

        float maxGridWidth  = frameWidth - sledDiameter;
        float maxGridHeight = frameHeight - sledDiameter;

        if (gridWidth > maxGridWidth) {
            gridWidth = maxGridWidth;
        }
        if (gridHeight > maxGridHeight) {
            gridHeight = maxGridHeight;
        }

        // 3. Ensure minimum grid dimensions to accommodate at least 3 points in each direction
        //    For 3 points, we need at least 2 intervals, so minimum spacing should be reasonable
        //    Minimum grid dimension = 50mm to ensure 3 points can be placed with 25mm spacing
        const float minGridDimension = 50.0f;  // mm
        if (gridWidth < minGridDimension) {
            gridWidth = minGridDimension;
        }
        if (gridHeight < minGridDimension) {
            gridHeight = minGridDimension;
        }

        // 4. Sanity check: Verify angle constraint at top of calibration area
        //    Check that angle from TL anchor to point at (centerX, centerY + gridHeight/2) to TR anchor is < 140 degrees
        //    If the angle is too large, reduce the grid height until it passes
        const float maxTopAngleDeg           = 140.0f;
        const float maxTopAngleRad           = maxTopAngleDeg * M_PI / 180.0f;
        float       topAngleDeg              = 0.0f;
        int         sanityCheckIterations    = 0;
        const int   maxSanityCheckIterations = 20;

        while (sanityCheckIterations < maxSanityCheckIterations) {
            float yDistFromCenter = gridHeight / 2.0f;
            float checkPointX     = centerX;
            float checkPointY     = centerY + yDistFromCenter;

            // Calculate vectors from check point to TL and TR anchors
            float toTLX = tlX - checkPointX;
            float toTLY = tlY - checkPointY;
            float toTRX = trX - checkPointX;
            float toTRY = trY - checkPointY;

            // Calculate angle between vectors
            float lenTL = sqrt(toTLX * toTLX + toTLY * toTLY);
            float lenTR = sqrt(toTRX * toTRX + toTRY * toTRY);

            if (lenTL > 0 && lenTR > 0) {
                float dotProduct = (toTLX * toTRX + toTLY * toTRY) / (lenTL * lenTR);
                dotProduct       = constrain(dotProduct, -1.0f, 1.0f);
                float topAngle   = acos(dotProduct);
                topAngleDeg      = topAngle * 180.0f / M_PI;

                if (topAngleDeg < maxTopAngleDeg) {
                    // Sanity check passed
                    break;
                }

                // Sanity check failed - reduce grid height by 5%
                float oldGridHeight = gridHeight;
                gridHeight *= 0.95f;

                // Check if we hit minimum
                if (gridHeight < minGridDimension) {
                    gridHeight = minGridDimension;
                    log_warn("Grid height reduced to minimum (" << minGridDimension << "mm) but sanity check still fails");
                    break;
                }

                if (sanityCheckIterations == 0) {
                    log_info("Calibration area sanity check failed: angle at top = " << topAngleDeg << " degrees (>= " << maxTopAngleDeg
                                                                                     << "), reducing grid height...");
                }
            } else {
                break;
            }

            sanityCheckIterations++;
        }

        log_info("Calibration area sanity check: angle at top = " << topAngleDeg << " degrees (should be < " << maxTopAngleDeg << ")");

        if (topAngleDeg >= maxTopAngleDeg) {
            log_warn("Warning: Calibration area sanity check failed after " << sanityCheckIterations << " reduction attempts");
        } else if (sanityCheckIterations > 0) {
            log_info("Grid height reduced in " << sanityCheckIterations << " iterations to pass sanity check");
        }
    } else {
        // User has provided all four parameters - use them directly
        log_info("Using user-provided calibration grid parameters (all four parameters set to non-zero values)");
        gridWidth  = calibration_grid_width_mm_X;
        gridHeight = calibration_grid_height_mm_Y;

        // Calculate frame dimensions for logging purposes
        float tlX   = kinematics->getTlX();
        float tlY   = kinematics->getTlY();
        float brX   = kinematics->getBrX();
        float blY   = kinematics->getBlY();
        frameWidth  = fabs(brX - tlX);
        frameHeight = fabs(tlY - blY);
    }

    // Validate that we have valid grid dimensions before proceeding
    if (gridWidth <= 0 || gridHeight <= 0) {
        log_error("Invalid grid dimensions: gridWidth=" << gridWidth << "mm, gridHeight=" << gridHeight << "mm");
        return false;
    }

    // Store calculated or user-provided dimensions in internal variables
    // This preserves the original YAML config values (e.g., 0.0 for auto-calculate)
    _calculated_grid_width_mm  = gridWidth;
    _calculated_grid_height_mm = gridHeight;

    // Step 2: Now handle grid sizes
    // Use separate X and Y grid sizes, fall back to single gridSize for backward compatibility
    // Initialize to safe defaults first
    int gridSizeX = 9;
    int gridSizeY = 9;

    if (autoCalculateFromGeometry) {
        // Auto-calculate grid sizes from dimensions and spacing
        if (_calculated_grid_width_mm > 0 && calibrationGridSpacing > 0) {
            gridSizeX = (int)(_calculated_grid_width_mm / calibrationGridSpacing) + 1;
            // Ensure at least 3 points
            if (gridSizeX < 3) {
                gridSizeX = 3;
            }
            // Cap at 99 points
            if (gridSizeX > 99) {
                gridSizeX = 99;
            }
            log_info("Auto-calculated gridSizeX: " << gridSizeX << " points");
        } else {
            log_warn("Failed to auto-calculate gridSizeX, using default: " << gridSizeX);
        }

        if (_calculated_grid_height_mm > 0 && calibrationGridSpacing > 0) {
            gridSizeY = (int)(_calculated_grid_height_mm / calibrationGridSpacing) + 1;
            // Ensure at least 3 points
            if (gridSizeY < 3) {
                gridSizeY = 3;
            }
            // Cap at 99 points
            if (gridSizeY > 99) {
                gridSizeY = 99;
            }
            log_info("Auto-calculated gridSizeY: " << gridSizeY << " points");
        } else {
            log_warn("Failed to auto-calculate gridSizeY, using default: " << gridSizeY);
        }
    } else {
        // Use user-provided grid sizes
        gridSizeX = (calibrationGridSizeX > 0) ? calibrationGridSizeX : calibrationGridSize;
        gridSizeY = (calibrationGridSizeY > 0) ? calibrationGridSizeY : calibrationGridSize;
    }

    // Final validation: ensure we have valid grid sizes
    if (gridSizeX < 3 || gridSizeY < 3) {
        log_error("Invalid grid size configuration: gridSizeX=" << gridSizeX << ", gridSizeY=" << gridSizeY << " (minimum 3 required)");
        return false;
    }

    // Step 3: Calculate the number of points needed and allocate memory accordingly
    int estimatedPoints = calculateGridPointCount(gridSizeX, gridSizeY);
    allocateCalibrationMemory(estimatedPoints);
    log_info("Allocated memory for " << estimatedPoints << " calibration points");

    // Warn if calculated values are outside typical range (100-3000mm)
    const float minTypicalDimension = 100.0f;   // mm
    const float maxTypicalDimension = 3000.0f;  // mm
    if (gridWidth < minTypicalDimension || gridWidth > maxTypicalDimension) {
        log_warn("Calculated grid width (" << gridWidth << "mm) is outside typical range (100-3000mm)");
    }
    if (gridHeight < minTypicalDimension || gridHeight > maxTypicalDimension) {
        log_warn("Calculated grid height (" << gridHeight << "mm) is outside typical range (100-3000mm)");
    }

    // gridSizeX and gridSizeY already calculated earlier
    int totalPoints = gridSizeX * gridSizeY;

    log_info("=== Calibration Grid ===");
    log_info("Dimensions: " << gridWidth << "x" << gridHeight << "mm");
    log_info("Grid: " << gridSizeX << "x" << gridSizeY << " (" << totalPoints << " pts)");
    log_info("Frame: " << frameWidth << "x" << frameHeight << "mm");

    float xSpacing = _calculated_grid_width_mm / (gridSizeX - 1);
    float ySpacing = _calculated_grid_height_mm / (gridSizeY - 1);

    // Calculate number of cycles dynamically based on grid size
    // For odd grids (e.g., 5x5): coords are integers -2,-1,0,1,2, need to reach index 2, so numberOfCycles = 2
    // For even grids (e.g., 6x6): coords are -2.5,-1.5,-0.5,0.5,1.5,2.5, need to reach 2.5, so numberOfCycles = 2
    // Formula: numberOfCycles = (gridSize - 1) / 2 for both odd and even (but we use offset for even)
    int numberOfCyclesX = (gridSizeX - 1) / 2;
    int numberOfCyclesY = (gridSizeY - 1) / 2;
    int numberOfCycles  = max(numberOfCyclesX, numberOfCyclesY);

    // For even-sized grids, we need to use half-integer coordinates (0.5, 1.5, 2.5, etc.)
    // to properly center the grid around (0,0)
    float xOffset = (gridSizeX % 2 == 0) ? 0.5f : 0.0f;
    float yOffset = (gridSizeY % 2 == 0) ? 0.5f : 0.0f;

    pointCount         = 6;  //The first six points are computed dynamically
    recomputePoints[0] = 5;

    //The point in the center
    calibrationGrid[GRID_X(pointCount)] = 0;
    calibrationGrid[GRID_Y(pointCount)] = 0;

    pointCount++;

    float maxX = 1.0f;
    float maxY = 1.0f;

    float currentX = 0.0f + xOffset;
    float currentY = -1.0f - yOffset;

    recomputeCount = 1;

    while (maxX <= numberOfCyclesX + xOffset || maxY <= numberOfCyclesY + yOffset) {
        // Move left (decreasing X) - only if we haven't reached the X boundary
        if (maxX <= numberOfCyclesX + xOffset) {
            while (currentX > (-1 * maxX - xOffset)) {
                calibrationGrid[GRID_X(pointCount)] = currentX * xSpacing;
                calibrationGrid[GRID_Y(pointCount)] = currentY * ySpacing;
                pointCount++;
                currentX -= 1.0f;
            }
        }

        // Move up (increasing Y) - only if we haven't reached the Y boundary
        if (maxY <= numberOfCyclesY + yOffset) {
            while (currentY < (maxY + yOffset)) {
                calibrationGrid[GRID_X(pointCount)] = currentX * xSpacing;
                calibrationGrid[GRID_Y(pointCount)] = currentY * ySpacing;
                pointCount++;
                currentY += 1.0f;
            }
        }

        // Move right (increasing X) - only if we haven't reached the X boundary
        if (maxX <= numberOfCyclesX + xOffset) {
            while (currentX < (maxX + xOffset)) {
                calibrationGrid[GRID_X(pointCount)] = currentX * xSpacing;
                calibrationGrid[GRID_Y(pointCount)] = currentY * ySpacing;
                pointCount++;
                currentX += 1.0f;
            }
        }

        // Move down (decreasing Y) - only if we haven't reached the Y boundary
        if (maxY <= numberOfCyclesY + yOffset) {
            while (currentY > (-1 * maxY - yOffset)) {
                calibrationGrid[GRID_X(pointCount)] = currentX * xSpacing;
                calibrationGrid[GRID_Y(pointCount)] = currentY * ySpacing;
                pointCount++;
                currentY -= 1.0f;
            }
        }

        //Add the last point to the recompute list
        calibrationGrid[GRID_X(pointCount)] = currentX * xSpacing;
        calibrationGrid[GRID_Y(pointCount)] = currentY * ySpacing;
        pointCount++;

        recomputePoints[recomputeCount] = pointCount - 1;  //Minus one because we increment after each point is generated
        recomputeCount++;

        maxX += 1.0f;
        maxY += 1.0f;

        currentY -= 1.0f;
    }

    //Move back to the center
    calibrationGrid[GRID_X(pointCount)] = 0;
    calibrationGrid[GRID_Y(pointCount)] = (currentY + 1) * ySpacing;  //The last loop added an nunecessary -1 to the y position
    pointCount++;

    calibrationGrid[GRID_X(pointCount)] = 0;
    calibrationGrid[GRID_Y(pointCount)] = 0;

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
//         log_info("Point " << i << ": " << calibrationGrid[GRID_X(i)] << ", " << calibrationGrid[GRID_Y(i)]);
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
    retractingTL    = false;
    retractingTR    = false;
    retractingBL    = false;
    retractingBR    = false;
    extendingALL    = false;
    complyALL       = true;
    Maslow.axisTL.reset();  //This just resets the thresholds for pull tight
    Maslow.axisTR.reset();
    Maslow.axisBL.reset();
    Maslow.axisBR.reset();
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

// Function to calculate the number of points that will be in the calibration grid
// This is used to allocate the right amount of memory before generating the grid
int Calibration::calculateGridPointCount(int gridSizeX, int gridSizeY) {
    // The grid starts with 6 fixed points (waypoints 0-5) plus the center point at index 6
    int count = 7;  // Points 0-6

    // Calculate number of cycles dynamically based on grid size
    int numberOfCyclesX = (gridSizeX - 1) / 2;
    int numberOfCyclesY = (gridSizeY - 1) / 2;

    // For even-sized grids, we need to use half-integer coordinates
    float xOffset = (gridSizeX % 2 == 0) ? 0.5f : 0.0f;
    float yOffset = (gridSizeY % 2 == 0) ? 0.5f : 0.0f;

    // Simulate the actual spiral pattern to count points
    // This must match the logic in generate_calibration_grid exactly
    float maxX = 1.0f;
    float maxY = 1.0f;
    
    float currentX = 0.0f + xOffset;
    float currentY = -1.0f - yOffset;
    
    while (maxX <= numberOfCyclesX + xOffset || maxY <= numberOfCyclesY + yOffset) {
        // Move left (decreasing X) - only if we haven't reached the X boundary
        if (maxX <= numberOfCyclesX + xOffset) {
            while (currentX > (-1 * maxX - xOffset)) {
                count++;
                currentX -= 1.0f;
            }
        }
        
        // Move up (increasing Y) - only if we haven't reached the Y boundary
        if (maxY <= numberOfCyclesY + yOffset) {
            while (currentY < (maxY + yOffset)) {
                count++;
                currentY += 1.0f;
            }
        }
        
        // Move right (increasing X) - only if we haven't reached the X boundary
        if (maxX <= numberOfCyclesX + xOffset) {
            while (currentX < (maxX + xOffset)) {
                count++;
                currentX += 1.0f;
            }
        }
        
        // Move down (decreasing Y) - only if we haven't reached the Y boundary
        if (maxY <= numberOfCyclesY + yOffset) {
            while (currentY > (-1 * maxY - yOffset)) {
                count++;
                currentY -= 1.0f;
            }
        }
        
        // Add the corner point at the end of each cycle
        count++;
        
        maxX += 1.0f;
        maxY += 1.0f;
        
        currentY -= 1.0f;
    }

    // Add the final two return-to-center points
    count += 2;

    return count;
}

// Function to allocate memory for calibration arrays based on actual grid size
void Calibration::allocateCalibrationMemory(int numPoints) {
    if (calibrationGrid == nullptr) {  //Check to prevent realocating
        // Allocate as 1D array: (numPoints * 2) + 2 extra floats for final point
        // The +2 accounts for the final center point written after the last increment
        // Access: calibrationGrid[i*2] = X, calibrationGrid[i*2+1] = Y
        calibrationGrid = new float[numPoints * 2 + 2];
    }
    if (calibration_data == nullptr) {
        calibration_data = new float*[numPoints];
        for (int i = 0; i < numPoints; ++i) {
            calibration_data[i] = new float[4];
        }
    }
    // Store the allocated size for later deallocation
    allocatedPoints = numPoints;
    pointCount      = 0;  // Will be populated during grid generation
}

// Function to deallocate memory for calibration arrays
void Calibration::deallocateCalibrationMemory() {
    if (calibrationGrid != nullptr) {
        delete[] calibrationGrid;
        calibrationGrid = nullptr;
    }
    if (calibration_data != nullptr) {
        for (int i = 0; i < allocatedPoints; ++i) {
            delete[] calibration_data[i];
        }
        delete[] calibration_data;
        calibration_data = nullptr;
    }
    allocatedPoints = 0;
}

// Function to reset all calibration state variables to initial values
void Calibration::resetCalibrationState() {
    // Reset calibration progress variables
    waypoint               = 0;
    pointCount             = 0;
    recomputeCountIndex    = 0;
    recomputeCount         = 0;  // Reset recompute count to ensure consistent grid generation
    calibrationInProgress  = false;
    calibrationDataWaiting = -1;

    // Reset calibration loop state variables
    calibrationDirection  = UP;    // Default direction
    measurementInProgress = true;  // Start by taking a measurement

    // Deallocate memory if allocated
    deallocateCalibrationMemory();

    log_info("Calibration state reset");
}

//Takes a raw measurement, projects it into the XY plane, then adds the belt end extension and arm length to get the actual distance.
float Calibration::measurementToXYPlane(float measurement, float zHeight) {
    auto kinematics = getKinematics();
    if (!kinematics)
        return 0.0f;

    // Debug logging for first few calls to understand the conversion
    static int logCount = 0;
    if (logCount < 4) {  // Log first 4 calls (one for each belt on first measurement)
        log_debug("measurementToXYPlane - measurement: " << measurement << ", zHeight: " << zHeight 
                 << ", beltExt: " << kinematics->getBeltEndExtension() << ", armLen: " << kinematics->getArmLength());
    }

    float lengthInXY = sqrt(measurement * measurement - zHeight * zHeight);
    float result = lengthInXY + kinematics->getBeltEndExtension() + kinematics->getArmLength();
    
    if (logCount < 4) {
        log_debug("measurementToXYPlane - lengthInXY: " << lengthInXY << ", result: " << result);
        logCount++;
    }
    
    return result;  //Add the belt end extension and arm length to get the actual distance
}

//Takes an XY plane distance, subtracts the belt end extension and arm length, then calculates the angled belt measurement.
float Calibration::measurementFromXYPlane(float xyPlaneDistance, float zHeight) {
    auto kinematics = getKinematics();
    if (!kinematics)
        return 0.0f;

    float lengthInXY =
        xyPlaneDistance - kinematics->getBeltEndExtension() - kinematics->getArmLength();  //Subtract the belt end extension and arm length
    return sqrt(lengthInXY * lengthInXY + zHeight * zHeight);                              //Calculate the angled belt length
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
        log_info("Center coordinates updated in MaslowKinematics: X=" << kinematics->getCenterX() << " Y=" << kinematics->getCenterY());
    }
}

// True if all axis were zeroed
bool Calibration::all_axis_homed() {
    return axis_homed[0] && axis_homed[1] && axis_homed[2] && axis_homed[3];
}

// True if all axis were extended
bool Calibration::allAxisExtended() {
    return extendedTL && extendedTR && extendedBL && extendedBR;
}

bool Calibration::checkOverides() {
    if (TLIOveride || TRIOveride || BLIOveride || BRIOveride || TLOOveride || TROOveride || BLOOveride || BROOveride) {
        return true;
    }
    return false;
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
            Maslow.axisTL.fullIn();
        } else {
            TLIOveride = false;
            Maslow.axisTL.stop();
        }
    }
    if (BRIOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axisBR.fullIn();
        } else {
            BRIOveride = false;
            Maslow.axisBR.stop();
        }
    }
    if (TRIOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axisTR.fullIn();
        } else {
            TRIOveride = false;
            Maslow.axisTR.stop();
        }
    }
    if (BLIOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axisBL.fullIn();
        } else {
            BLIOveride = false;
            Maslow.axisBL.stop();
        }
    }
    if (TLOOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axisTL.fullOut();
        } else {
            TLOOveride = false;
            Maslow.axisTL.stop();
        }
    }
    if (BROOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axisBR.fullOut();
        } else {
            BROOveride = false;
            Maslow.axisBR.stop();
        }
    }
    if (TROOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axisTR.fullOut();
        } else {
            TROOveride = false;
            Maslow.axisTR.stop();
        }
    }
    if (BLOOveride) {
        log_info(int(millis() - overideTimer));
        if (millis() - overideTimer < 200) {
            Maslow.axisBL.fullOut();
        } else {
            BLOOveride = false;
            Maslow.axisBL.stop();
        }
    }
}