#include "TestFramework.h"
#include "../../src/Kinematics/MaslowKinematics.h"
#include "../../src/Maslow/Maslow.h"

using Kinematics::MaslowKinematics;

static float anchorDistance(const MaslowKinematics& k, int a, int b) {
    float dx = k.getAnchorCoord(a, Coord_X) - k.getAnchorCoord(b, Coord_X);
    float dy = k.getAnchorCoord(a, Coord_Y) - k.getAnchorCoord(b, Coord_Y);
    return sqrtf(dx * dx + dy * dy);
}

Test(MaslowBeltStretchConversions, BeltStretchTest) {
    MaslowKinematics kinematics;
    kinematics.init();

    kinematics.setBeltStretchCompensation(true);
    float span = kinematics.spanAtPull(1700.0f);
    Assert(fabs(span - 1700.0f * (1.0f + 156.96f / 178900.0f)) < 0.001f, "A pulled belt spans F*L/AE more than paid out");
    Assert(fabs(kinematics.paidOutAtPull(span) - 1700.0f) < 0.001f, "paidOutAtPull undoes spanAtPull");

    kinematics.setBeltStretchCompensation(false);
    Assert(kinematics.spanAtPull(1700.0f) == 1700.0f, "Off: measurements unchanged");
}

// The tensions assumed while cutting must balance on the sled, or the sled would settle off target
Test(MaslowBeltStretchTensionsBalance, BeltStretchTest) {
    MaslowKinematics kinematics;
    kinematics.init();
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

// Turning it on moves anchors found without it outward; turning it off again puts them back
Test(MaslowBeltStretchAnchorSync, BeltStretchTest) {
    MaslowKinematics kinematics;
    kinematics.init();
    const float diagonal = anchorDistance(kinematics, _TL, _BR);
    const float tlX = kinematics.getTlX(), tlY = kinematics.getTlY(), brX = kinematics.getBrX();

    kinematics.setBeltStretchCompensation(false);
    Assert(!kinematics.syncAnchorsToBeltStretch(), "Anchors found without compensation, setting off: nothing to do");

    kinematics.setBeltStretchCompensation(true);
    Assert(!kinematics.stretchActive(), "The setting alone doesn't change cutting");
    Assert(kinematics.syncAnchorsToBeltStretch(), "Turning it on moves the anchors");
    Assert(kinematics.stretchActive(), "and cutting compensates from then on");
    Assert(!kinematics.syncAnchorsToBeltStretch(), "Only once");
    float grown = anchorDistance(kinematics, _TL, _BR) - diagonal;
    Assert(grown > 1.0f && grown < 4.0f, "The diagonal grows by about F/AE of its belt lengths");
    Assert(kinematics.getBlX() == 0.0f && kinematics.getBlY() == 0.0f && kinematics.getBrY() == 0.0f,
           "BL stays at 0, 0 and BR on the X axis");

    kinematics.setBeltStretchCompensation(false);
    Assert(kinematics.syncAnchorsToBeltStretch(), "Turning it off moves them back");
    Assert(fabs(kinematics.getTlX() - tlX) < 0.001f && fabs(kinematics.getTlY() - tlY) < 0.001f && fabs(kinematics.getBrX() - brX) < 0.001f,
           "to where they were");
}

// Inverse then forward kinematics must give the same point back, or gc_sync_position would move the job
Test(MaslowBeltStretchRoundTrip, BeltStretchTest) {
    MaslowKinematics kinematics;
    kinematics.init();
    Maslow.scaleX                   = 1.0f;
    Maslow.scaleY                   = 1.0f;
    Maslow.calibration.orientation  = HORIZONTAL;
    Maslow.calibration.currentState = READY_TO_CUT;
    kinematics.setBeltStretchCompensation(true);
    kinematics.syncAnchorsToBeltStretch();

    float cartesian[3] = { 700.0f, -350.0f, 0.0f };
    float motors[6];
    kinematics.transform_cartesian_to_motors(motors, cartesian);
    float back[3];
    kinematics.motors_to_cartesian(back, motors, 6);
    Assert(fabs(back[0] - cartesian[0]) < 0.01f, "X matches after the round trip");
    Assert(fabs(back[1] - cartesian[1]) < 0.01f, "Y matches after the round trip");
}
