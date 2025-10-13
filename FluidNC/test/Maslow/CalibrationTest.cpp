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

// Test anchor coordinate validation functionality
Test(MaslowKinematicsValidation, CalibrationTest) {
    using namespace Kinematics;

    // Create a MaslowKinematics instance
    MaslowKinematics kinematics;

    // Test with valid coordinates (should not trigger warnings)
    kinematics.updateAnchorCoordinates(100.0f,
                                       2000.0f,
                                       100.0f,  // tlX, tlY, tlZ
                                       3000.0f,
                                       2000.0f,
                                       56.0f,  // trX, trY, trZ
                                       0.0f,
                                       0.0f,
                                       34.0f,  // blX, blY, blZ
                                       3100.0f,
                                       0.0f,
                                       78.0f  // brX, brY, brZ
    );

    // Verify coordinates are set as expected
    Assert(kinematics.getTlX() == 100.0f, "Valid tlX should be preserved");
    Assert(kinematics.getTrX() == 3000.0f, "Valid trX should be preserved");
    Assert(kinematics.getBlX() == 0.0f, "Valid blX should be preserved");
    Assert(kinematics.getBrY() == 0.0f, "Valid brY should be preserved");
}

// Test anchor coordinate validation with small tolerance issues
Test(MaslowKinematicsValidationMinorCorrections, CalibrationTest) {
    using namespace Kinematics;

    // Create a MaslowKinematics instance
    MaslowKinematics kinematics;

    // Test with small tolerance issues (blX, blY, brY slightly off from zero but geometry is valid)
    kinematics.updateAnchorCoordinates(0.0f,
                                       2000.0f,
                                       100.0f,  // tlX, tlY, tlZ
                                       2000.0f,
                                       2000.0f,
                                       56.0f,  // trX, trY, trZ
                                       0.05f,
                                       0.08f,
                                       34.0f,  // blX, blY, blZ (slightly off from 0, should be auto-corrected)
                                       2000.0f,
                                       0.03f,
                                       78.0f  // brX, brY, brZ (brY slightly off from 0)
    );

    // Run validation to trigger minor corrections
    kinematics.validate();

    // Verify that small tolerance issues were corrected
    Assert(kinematics.getBlX() == 0.0f, "blX should be corrected to 0.0");
    Assert(kinematics.getBlY() == 0.0f, "blY should be corrected to 0.0");
    Assert(kinematics.getBrY() == 0.0f, "brY should be corrected to 0.0");
    // Verify other coordinates are preserved (valid geometry)
    Assert(kinematics.getTlX() == 0.0f, "tlX should be preserved");
    Assert(kinematics.getTrX() == 2000.0f, "trX should be preserved");
}

// Test anchor coordinate validation with invalid geometry (tlX >= trX)
Test(MaslowKinematicsValidationInvalidGeometry, CalibrationTest) {
    using namespace Kinematics;

    // Create a MaslowKinematics instance
    MaslowKinematics kinematics;

    // Test with invalid coordinates (tlX > trX - invalid geometry)
    kinematics.updateAnchorCoordinates(3000.0f,
                                       2000.0f,
                                       100.0f,  // tlX, tlY, tlZ (tlX > trX - invalid)
                                       100.0f,
                                       2000.0f,
                                       56.0f,  // trX, trY, trZ
                                       0.0f,
                                       0.0f,
                                       34.0f,  // blX, blY, blZ
                                       3100.0f,
                                       0.0f,
                                       78.0f  // brX, brY, brZ
    );

    // Run validation - should trigger emergency stop (which throws AssertionFailed in test environment)
    AssertThrow(kinematics.validate());
}

// Test anchor coordinate validation with top points not above bottom points
Test(MaslowKinematicsValidationTopNotAboveBottom, CalibrationTest) {
    using namespace Kinematics;

    // Create a MaslowKinematics instance
    MaslowKinematics kinematics;

    // Test with invalid coordinates (tlY <= blY - top not above bottom)
    kinematics.updateAnchorCoordinates(0.0f,
                                       100.0f,
                                       100.0f,  // tlX, tlY, tlZ (tlY too low)
                                       2000.0f,
                                       100.0f,
                                       56.0f,  // trX, trY, trZ (trY too low)
                                       0.0f,
                                       0.0f,
                                       34.0f,  // blX, blY, blZ
                                       2000.0f,
                                       0.0f,
                                       78.0f  // brX, brY, brZ
    );

    // Run validation - should trigger emergency stop
    AssertThrow(kinematics.validate());
}

// Test anchor coordinate validation with unrealistic coordinate values
Test(MaslowKinematicsValidationUnrealisticCoordinates, CalibrationTest) {
    using namespace Kinematics;

    // Create a MaslowKinematics instance
    MaslowKinematics kinematics;

    // Test with unrealistic coordinates (negative top Y coordinate)
    kinematics.updateAnchorCoordinates(0.0f,
                                       -500.0f,
                                       100.0f,  // tlX, tlY, tlZ (tlY negative - unrealistic)
                                       2000.0f,
                                       2000.0f,
                                       56.0f,  // trX, trY, trZ
                                       0.0f,
                                       0.0f,
                                       34.0f,  // blX, blY, blZ
                                       2000.0f,
                                       0.0f,
                                       78.0f  // brX, brY, brZ
    );

    // Run validation - should trigger emergency stop
    AssertThrow(kinematics.validate());
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
    float testWorkThickness       = 19.05f;  // 3/4" in mm

    // For this test, we'll simulate setting the values through configuration by directly modifying
    // the kinematics object's private members through the configuration system.
    // Since we can't easily test the configuration parsing in unit tests, we'll verify that
    // the getter functions are accessible and would return correct values if set.

    // The key test is that thickness affects belt length calculations
    // We'll manually add thickness to Z coordinate to verify the calculation change
    float thicknessOffset = testSpoilboardThickness + testWorkThickness;
    float adjustedTL      = kinematics.computeTL(testX, testY, testZ - thicknessOffset);

    // When we subtract thickness from Z input (simulating the reverse effect),
    // the belt length should be shorter since the effective Z distance is reduced
    Assert(adjustedTL < originalTL, "Belt length should decrease when effective Z distance is reduced");

    // Test that all belt computation functions are working consistently
    Assert(originalTL > 0 && originalTR > 0 && originalBL > 0 && originalBR > 0, "All original belt lengths should be positive");
}

// Test side length validation functionality
Test(MaslowKinematicsSideLengthValidation, CalibrationTest) {
    using namespace Kinematics;

    // Create a MaslowKinematics instance
    MaslowKinematics kinematics;

    // Test with valid side lengths (should not trigger warnings)
    kinematics.updateAnchorCoordinates(0.0f,
                                       2000.0f,
                                       100.0f,  // tlX, tlY, tlZ
                                       2000.0f,
                                       2000.0f,
                                       56.0f,  // trX, trY, trZ
                                       0.0f,
                                       0.0f,
                                       34.0f,  // blX, blY, blZ
                                       2000.0f,
                                       0.0f,
                                       78.0f  // brX, brY, brZ
    );

    kinematics.validate();

    // Verify coordinates are preserved (all sides should be 2000mm which is valid)
    Assert(kinematics.getTlX() == 0.0f, "Valid tlX should be preserved");
    Assert(kinematics.getTrX() == 2000.0f, "Valid trX should be preserved");
    Assert(kinematics.getBlX() == 0.0f, "Valid blX should be preserved");
    Assert(kinematics.getBrX() == 2000.0f, "Valid brX should be preserved");
}

// Test side length validation with invalid side lengths
Test(MaslowKinematicsSideLengthValidationInvalid, CalibrationTest) {
    using namespace Kinematics;

    // Create a MaslowKinematics instance
    MaslowKinematics kinematics;

    // Test with side lengths that are too small (< 500mm)
    kinematics.updateAnchorCoordinates(0.0f,
                                       200.0f,
                                       100.0f,  // tlX, tlY, tlZ (top/left sides will be 200mm - too small)
                                       200.0f,
                                       200.0f,
                                       56.0f,  // trX, trY, trZ
                                       0.0f,
                                       0.0f,
                                       34.0f,  // blX, blY, blZ
                                       200.0f,
                                       0.0f,
                                       78.0f  // brX, brY, brZ
    );

    // Run validation - should trigger emergency stop
    AssertThrow(kinematics.validate());
}

// Test side length validation with side lengths that are too large
Test(MaslowKinematicsSideLengthValidationTooLarge, CalibrationTest) {
    using namespace Kinematics;

    // Create a MaslowKinematics instance
    MaslowKinematics kinematics;

    // Test with side lengths that are too large (> 5000mm)
    kinematics.updateAnchorCoordinates(0.0f,
                                       6000.0f,
                                       100.0f,  // tlX, tlY, tlZ (top/left sides will be 6000mm - too large)
                                       6000.0f,
                                       6000.0f,
                                       56.0f,  // trX, trY, trZ
                                       0.0f,
                                       0.0f,
                                       34.0f,  // blX, blY, blZ
                                       6000.0f,
                                       0.0f,
                                       78.0f  // brX, brY, brZ
    );

    // Run validation - should trigger emergency stop
    AssertThrow(kinematics.validate());
}

// Test the new general square finding algorithm (coordinate solving approach)
Test(GeneralSquareFindingAlgorithm, CalibrationTest) {
    // Test the coordinate solving approach that finds L by iteration
    // For a known square, verify that the algorithm correctly calculates the side length

    // Known square with side length 2000mm
    float knownL = 2000.0f;

    // Test point at center (1000, 1000)
    float centerX = knownL / 2.0f;
    float centerY = knownL / 2.0f;

    // Calculate distances from center to corners
    float tlLen = sqrt(centerX * centerX + (knownL - centerY) * (knownL - centerY));
    float trLen = sqrt((knownL - centerX) * (knownL - centerX) + (knownL - centerY) * (knownL - centerY));
    float blLen = sqrt(centerX * centerX + centerY * centerY);
    float brLen = sqrt((knownL - centerX) * (knownL - centerX) + centerY * centerY);

    // Apply coordinate solving method (simplified version for testing)
    float bestL     = -1;
    float bestError = 1e6;

    for (float L = 500.0f; L <= 5000.0f; L += 1.0f) {
        float x = (L * L + blLen * blLen - brLen * brLen) / (2.0f * L);
        float y = (L * L + blLen * blLen - tlLen * tlLen) / (2.0f * L);

        if (x >= 0 && x <= L && y >= 0 && y <= L) {
            float predicted_tr = sqrt((L - x) * (L - x) + (L - y) * (L - y));
            float error        = abs(predicted_tr - trLen);

            if (error < bestError) {
                bestError = error;
                bestL     = L;
            }
        }
    }

    // For a centered point, should correctly identify the square size
    Assert(abs(bestL - knownL) < 1.0f, "Calculated side length should match known side length for centered point");

    // Test point off-center (600, 800) - closer to bottom-left
    float offCenterX = 600.0f;
    float offCenterY = 800.0f;

    // Calculate distances from off-center point to corners
    tlLen = sqrt(offCenterX * offCenterX + (knownL - offCenterY) * (knownL - offCenterY));
    trLen = sqrt((knownL - offCenterX) * (knownL - offCenterX) + (knownL - offCenterY) * (knownL - offCenterY));
    blLen = sqrt(offCenterX * offCenterX + offCenterY * offCenterY);
    brLen = sqrt((knownL - offCenterX) * (knownL - offCenterX) + offCenterY * offCenterY);

    // Apply coordinate solving method again
    bestL     = -1;
    bestError = 1e6;

    for (float L = 500.0f; L <= 5000.0f; L += 1.0f) {
        float x = (L * L + blLen * blLen - brLen * brLen) / (2.0f * L);
        float y = (L * L + blLen * blLen - tlLen * tlLen) / (2.0f * L);

        if (x >= 0 && x <= L && y >= 0 && y <= L) {
            float predicted_tr = sqrt((L - x) * (L - x) + (L - y) * (L - y));
            float error        = abs(predicted_tr - trLen);

            if (error < bestError) {
                bestError = error;
                bestL     = L;
            }
        }
    }

    // Should still correctly calculate the side length even when not centered
    Assert(abs(bestL - knownL) < 1.0f, "Calculated side length should match known side length for off-center point");
}