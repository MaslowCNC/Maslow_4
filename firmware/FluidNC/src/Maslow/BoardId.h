#pragma once

/*
* Identifies which Maslow control board the firmware is running on from the BoardVersion divider
* on GPIO17, so one firmware can drive both the Maslow 4 and the Maslow Mini.
*
*   Maslow 4.1 control board      10k / 10k    ~1.65 V
*   Unified Control Board (Mini)  10k / 5.1k   ~1.11 V
*   Unified Control Board, 36 V   10k / 2.2k   ~0.60 V
*
* Every shipped board has the divider fitted, so a reading outside all the bands means a fault.
*/

namespace BoardId {
    enum class Board { Maslow41, Unified, Unified36V, Unknown };

    // Read the divider and classify it.  Call once at boot, before WiFi starts: GPIO17 is
    // ADC2_CH6, which the WiFi radio shares.
    void detect();

    Board       board();
    const char* boardName(Board board);
    int         millivolts();  // the boot reading, or -1 if the ADC could not be read

    // The Unified Control Boards wire the belt motors and encoders the opposite way round to the
    // Maslow 4.1, so both must be reversed in firmware for a command to turn a belt the same way.
    bool reversedBeltMotors();
}
