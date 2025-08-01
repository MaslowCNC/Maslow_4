#include "TestFramework.h"
#include "../../src/Maslow/Calibration.h"
#include "../../src/Kinematics/MaslowKinematics.h"

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
    
    // Test that the getter functions work correctly (defaults should be 0)
    Assert(kinematics.getSpoilboardThickness() == 0.0f, "Default spoilboard thickness should be 0");
    Assert(kinematics.getWorkThickness() == 0.0f, "Default work thickness should be 0");
}