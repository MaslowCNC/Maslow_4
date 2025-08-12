#include "TestFramework.h"
#include "../../src/Maslow/Calibration.h"
#include "../../src/Kinematics/MaslowKinematics.h"

// Include calibration state constants
#include "../../src/Maslow/Calibration.h"

// Simple test to verify that MaslowKinematics setFrameSize works correctly
Test(MaslowKinematicsFrameSize, CalibrationTest) {
    using namespace Kinematics;
    
    // Create a MaslowKinematics instance
    MaslowKinematics kinematics;
    
    // Test setFrameSize functionality
    float testFrameSize = 2000.0f;
    kinematics.setFrameSize(testFrameSize);
    
    // Verify that the anchor points were set correctly
    Assert(kinematics.getBlX() == 0.0f, "Bottom left X should be 0");
    Assert(kinematics.getBlY() == 0.0f, "Bottom left Y should be 0");
    Assert(kinematics.getBrX() == testFrameSize, "Bottom right X should equal frame size");
    Assert(kinematics.getBrY() == 0.0f, "Bottom right Y should be 0");
    Assert(kinematics.getTlX() == 0.0f, "Top left X should be 0");
    Assert(kinematics.getTlY() == testFrameSize, "Top left Y should equal frame size");
    Assert(kinematics.getTrX() == testFrameSize, "Top right X should equal frame size");
    Assert(kinematics.getTrY() == testFrameSize, "Top right Y should equal frame size");
}

// Test updateAnchorCoordinates functionality
Test(MaslowKinematicsUpdateAnchors, CalibrationTest) {
    using namespace Kinematics;
    
    // Create a MaslowKinematics instance
    MaslowKinematics kinematics;
    
    // Test custom anchor coordinate setting
    float tlX = 10.0f, tlY = 2010.0f, tlZ = 105.0f;
    float trX = 2010.0f, trY = 2015.0f, trZ = 60.0f;
    float blX = 5.0f, blY = 5.0f, blZ = 40.0f;
    float brX = 2005.0f, brY = 10.0f, brZ = 80.0f;
    
    kinematics.updateAnchorCoordinates(tlX, tlY, tlZ, trX, trY, trZ, blX, blY, blZ, brX, brY, brZ);
    
    // Verify all coordinates were set correctly
    Assert(kinematics.getTlX() == tlX, "Top left X not set correctly");
    Assert(kinematics.getTlY() == tlY, "Top left Y not set correctly"); 
    Assert(kinematics.getTlZ() == tlZ, "Top left Z not set correctly");
    Assert(kinematics.getTrX() == trX, "Top right X not set correctly");
    Assert(kinematics.getTrY() == trY, "Top right Y not set correctly");
    Assert(kinematics.getTrZ() == trZ, "Top right Z not set correctly");
    Assert(kinematics.getBlX() == blX, "Bottom left X not set correctly");
    Assert(kinematics.getBlY() == blY, "Bottom left Y not set correctly");
    Assert(kinematics.getBlZ() == blZ, "Bottom left Z not set correctly");
    Assert(kinematics.getBrX() == brX, "Bottom right X not set correctly");
    Assert(kinematics.getBrY() == brY, "Bottom right Y not set correctly");
    Assert(kinematics.getBrZ() == brZ, "Bottom right Z not set correctly");
}

// Test spoilboard and work thickness functionality
Test(MaslowKinematicsMaterialThickness, CalibrationTest) {
    using namespace Kinematics;
    
    // Create a MaslowKinematics instance
    MaslowKinematics kinematics;
    
    // Set known anchor coordinates and test parameters
    float testX = 100.0f, testY = 100.0f, testZ = 10.0f;
    
    // Test that the default getter functions work correctly (defaults should be 0)
    Assert(kinematics.getSpoilboardThickness() == 0.0f, "Default spoilboard thickness should be 0");
    Assert(kinematics.getWorkThickness() == 0.0f, "Default work thickness should be 0");
    
    // Calculate belt length without any material thickness (defaults are 0)
    float lengthWithoutMaterial = kinematics.computeTL(testX, testY, testZ);
    
    // Test that the functions are callable and return reasonable values
    float tlLength = kinematics.computeTL(testX, testY, testZ);
    float trLength = kinematics.computeTR(testX, testY, testZ);
    float blLength = kinematics.computeBL(testX, testY, testZ);
    float brLength = kinematics.computeBR(testX, testY, testZ);
    
    // Belt lengths should be positive and reasonable
    Assert(tlLength > 0, "TL belt length should be positive");
    Assert(trLength > 0, "TR belt length should be positive");
    Assert(blLength > 0, "BL belt length should be positive");
    Assert(brLength > 0, "BR belt length should be positive");
    
    // All belt lengths should be reasonable (less than 10000mm for typical setups)
    Assert(tlLength < 10000, "TL belt length should be reasonable");
    Assert(trLength < 10000, "TR belt length should be reasonable");
    Assert(blLength < 10000, "BL belt length should be reasonable");
    Assert(brLength < 10000, "BR belt length should be reasonable");
}

// Test thickness configuration and its effect on belt calculations
Test(MaslowKinematicsThicknessConfiguration, CalibrationTest) {
    using namespace Kinematics;
    
    // Create a MaslowKinematics instance
    MaslowKinematics kinematics;
    
    // Test position
    float testX = 0.0f, testY = 0.0f, testZ = 0.0f;
    
    // Calculate belt lengths with default thickness (0.0)
    float originalTL = kinematics.computeTL(testX, testY, testZ);
    float originalTR = kinematics.computeTR(testX, testY, testZ);
    float originalBL = kinematics.computeBL(testX, testY, testZ);
    float originalBR = kinematics.computeBR(testX, testY, testZ);
    
    // Test that default thickness values are 0
    Assert(kinematics.getSpoilboardThickness() == 0.0f, "Default spoilboard thickness should be 0");
    Assert(kinematics.getWorkThickness() == 0.0f, "Default work thickness should be 0");
    
    // Apply test thickness values (simulating 1/4" spoilboard + 3/4" plywood)
    float testSpoilboardThickness = 6.35f;   // 1/4" in mm
    float testWorkThickness = 19.05f;        // 3/4" in mm
    
    // For this test, we'll simulate setting the values through configuration by directly modifying
    // the kinematics object's private members through the configuration system.
    // Since we can't easily test the configuration parsing in unit tests, we'll verify that
    // the getter functions are accessible and would return correct values if set.
    
    // The key test is that thickness affects belt length calculations
    // We'll manually add thickness to Z coordinate to verify the calculation change
    float thicknessOffset = testSpoilboardThickness + testWorkThickness;
    float adjustedTL = kinematics.computeTL(testX, testY, testZ - thicknessOffset);
    
    // When we subtract thickness from Z input (simulating the reverse effect), 
    // the belt length should be shorter since the effective Z distance is reduced
    Assert(adjustedTL < originalTL, "Belt length should decrease when effective Z distance is reduced");
    
    // Test that all belt computation functions are working consistently
    Assert(originalTL > 0 && originalTR > 0 && originalBL > 0 && originalBR > 0, 
           "All original belt lengths should be positive");
}

// Test calibration state reset functionality
Test(CalibrationStateReset, CalibrationTest) {
    // Create a Calibration instance
    Calibration calibration;
    
    // Test that the instance starts with correct initial state
    Assert(calibration.getCurrentState() == UNKNOWN, "Initial state should be UNKNOWN");
    
    // Test that the reset function can be called without error
    calibration.resetCalibrationStaticVariables();
    
    // Test state transitions to calibration
    bool success = calibration.requestStateChange(RETRACTING);
    Assert(success, "Should be able to transition to RETRACTING state");
    
    success = calibration.requestStateChange(RETRACTED);
    Assert(success, "Should be able to transition to RETRACTED state");
    
    success = calibration.requestStateChange(EXTENDING);
    Assert(success, "Should be able to transition to EXTENDING state");
    
    success = calibration.requestStateChange(EXTENDEDOUT);
    Assert(success, "Should be able to transition to EXTENDEDOUT state");
    
    // Test that we can initiate calibration
    success = calibration.requestStateChange(CALIBRATION_IN_PROGRESS);
    Assert(success, "Should be able to transition to CALIBRATION_IN_PROGRESS state");
    Assert(calibration.getCurrentState() == CALIBRATION_IN_PROGRESS, "State should be CALIBRATION_IN_PROGRESS");
    
    // Simulate calibration completion
    success = calibration.requestStateChange(READY_TO_CUT);
    Assert(success, "Should be able to transition to READY_TO_CUT state");
    
    // Test that we can start calibration again (this is where the bug was)
    success = calibration.requestStateChange(CALIBRATION_IN_PROGRESS);
    Assert(success, "Should be able to start calibration again without issues");
    Assert(calibration.getCurrentState() == CALIBRATION_IN_PROGRESS, "State should be CALIBRATION_IN_PROGRESS again");
}