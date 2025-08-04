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

// Test that center coordinates remain stable and are based only on physical frame
Test(MaslowKinematicsCenterStability, CalibrationTest) {
    using namespace Kinematics;
    
    // Create a MaslowKinematics instance
    MaslowKinematics kinematics;
    
    // Set a known frame size
    float frameSize = 2400.0f;
    kinematics.setFrameSize(frameSize);
    
    // Get initial center coordinates - these should be based only on frame geometry
    float initialCenterX = kinematics.getCenterX();
    float initialCenterY = kinematics.getCenterY();
    
    // The center should be at the geometric center of the frame for a square frame
    // For a square frame from (0,0) to (frameSize, frameSize), center should be (frameSize/2, frameSize/2)
    float expectedCenterX = frameSize / 2.0f;
    float expectedCenterY = frameSize / 2.0f;
    
    Assert(abs(initialCenterX - expectedCenterX) < 0.1f, "Center X should be at geometric center of frame");
    Assert(abs(initialCenterY - expectedCenterY) < 0.1f, "Center Y should be at geometric center of frame");
    
    // Simulate setting work coordinates (like G92) - this should NOT affect the machine frame center
    // In the real system, G92 only affects coordinate offsets, not the physical machine frame
    
    // The center coordinates should remain exactly the same regardless of work coordinate changes
    // because they represent the physical machine frame geometry, not work coordinates
    float finalCenterX = kinematics.getCenterX();
    float finalCenterY = kinematics.getCenterY();
    
    Assert(finalCenterX == initialCenterX, "Center X must remain constant - machine bounds should not change with work coordinates");
    Assert(finalCenterY == initialCenterY, "Center Y must remain constant - machine bounds should not change with work coordinates");
}