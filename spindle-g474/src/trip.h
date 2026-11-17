#pragma once
// Trip recorder: the last ~2 s of drive state, sampled every 2 ms by the main loop, frozen when a
// fault stops the motors and sent to the XY board over the link in the format its SpindleBoard
// saves to /spindle_trip.csv ("[TRIP] ===== trip recording: ..." ... "[TRIP] ===== end ...").
// The columns differ from the ESP32 board's (no voltage LUT, no nFAULT; dq currents, observer
// speed and the closed-loop Z error instead); the XY board saves whatever the header names.

#include <Arduino.h>

struct TripMotorSample {
    uint8_t enabled;
    uint8_t mode;      // 0 off, 1 open loop, 2 closed loop
    int16_t rpm;       // commanded, signed (motor frame)
    int16_t obs_rpm;   // observer
    int16_t ia, ib, ic, id, iq, imax, vmag;   // x100 (A, V)
};

struct TripSample {
    uint32_t        t_ms;
    uint16_t        isr_count;    // FOC interrupts since the previous sample (40 expected)
    uint16_t        isr_max_us;
    int16_t         phase_deg10;  // phase offset (Z), deg x10
    int16_t         target_deg10;
    int16_t         zerr_deg10;   // closed loop: angle-sum error, deg x10
    uint16_t        vbus10;       // V x10
    TripMotorSample m[2];
};

void tripPush(const TripSample& s);
void tripTrigger(const char* reason);   // freeze and send over the link
void tripArmOnStart();                  // a spindle start re-arms once the last recording is out
void tripRequestLinkDump();             // "T"
void tripLinkTest();                    // "T!": record a test one now
void tripService();                     // main loop: send pending lines
bool tripSending();
