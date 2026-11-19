#include "TestFramework.h"
#include "../../src/Kinematics/MaslowKinematics.h"
#include "../../src/Maslow/Maslow.h"

// Belt stretch compensation: AE = 178900 N (belt spec), Find Anchors pull 156.96 N

Test(MaslowBeltStretchConversions, BeltStretchTest) {
    using namespace Kinematics;
    MaslowKinematics kinematics;
    kinematics.init();

    kinematics.setBeltStretch(178900.0f, 156.96f);
    float g = kinematics.paidOutToGeometric(1700.0f, 156.96f);
    Assert(fabs(g - 1700.0f * (1.0f + 156.96f / 178900.0f)) < 0.001f, "Paid-out belt spans F*L/AE more");
    Assert(fabs(kinematics.geometricToPaidOut(g, 156.96f) - 1700.0f) < 0.001f, "geometricToPaidOut undoes paidOutToGeometric");

    kinematics.setBeltStretch(0.0f, 156.96f);
    Assert(kinematics.paidOutToGeometric(1700.0f, 156.96f) == 1700.0f, "Stiffness 0 turns compensation off");
    Assert(kinematics.geometricToPaidOut(1700.0f, 156.96f) == 1700.0f, "Stiffness 0 turns compensation off");
}

// The tensions assumed while cutting must balance on the sled, or the sled would settle off target
Test(MaslowBeltStretchTensionsBalance, BeltStretchTest) {
    using namespace Kinematics;
    MaslowKinematics kinematics;
    kinematics.init();
    kinematics.setBeltStretch(178900.0f, 156.96f);
    Maslow.calibration.orientation = HORIZONTAL;

    const float points[3][2] = { { 0.0f, 0.0f }, { 600.0f, 300.0f }, { -900.0f, -400.0f } };
    for (const auto& p : points) {
        float tension[ARM_COUNT];
        kinematics.estimateBeltTensions(p[0], p[1], tension);
        float fx = 0.0f, fy = 0.0f;
        for (int i = 0; i < ARM_COUNT; i++) {
            float dx  = kinematics.getAnchorCoord(i, Coord_X) - (p[0] + kinematics.getCenterX());
            float dy  = kinematics.getAnchorCoord(i, Coord_Y) - (p[1] + kinematics.getCenterY());
            float len = sqrtf(dx * dx + dy * dy);
            fx += tension[i] * dx / len;
            fy += tension[i] * dy / len;
            Assert(tension[i] > 0.0f, "Every belt stays in tension");
        }
        Assert(fabs(fx) < 0.01f && fabs(fy) < 0.01f, "Belt tensions balance on a horizontal frame");
    }
}

// Inverse then forward kinematics must give the same point back, or gc_sync_position would move the job
Test(MaslowBeltStretchRoundTrip, BeltStretchTest) {
    using namespace Kinematics;
    MaslowKinematics kinematics;
    kinematics.init();
    Maslow.scaleX                   = 1.0f;
    Maslow.scaleY                   = 1.0f;
    Maslow.calibration.orientation  = HORIZONTAL;
    Maslow.calibration.currentState = READY_TO_CUT;

    float cartesian[3] = { 700.0f, -350.0f, 0.0f };
    float plain[6], stretched[6];
    kinematics.setBeltStretch(0.0f, 156.96f);
    kinematics.transform_cartesian_to_motors(plain, cartesian);
    kinematics.setBeltStretch(178900.0f, 156.96f);
    kinematics.transform_cartesian_to_motors(stretched, cartesian);
    for (int i = 0; i < ARM_COUNT; i++) {
        Assert(stretched[i] < plain[i], "Compensation pays out less belt");
        Assert(plain[i] - stretched[i] < plain[i] * 300.0f / 178900.0f, "by no more than a 300 N stretch");
    }

    float back[3];
    kinematics.motors_to_cartesian(back, stretched, 6);
    Assert(fabs(back[0] - cartesian[0]) < 0.01f, "X matches after the round trip");
    Assert(fabs(back[1] - cartesian[1]) < 0.01f, "Y matches after the round trip");
}
