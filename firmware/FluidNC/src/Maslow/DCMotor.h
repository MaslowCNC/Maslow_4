// Copyright (c) 2024 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

/*!
 *  @file DCMotor.h
 *
 *  This is a library to interact with the TI DRV8873 chip via a peripheral PWM generator chip (TLC59711)
 *
 */

#ifndef DCMotor_H
#define DCMotor_H

enum direction { BACKWARD, FORWARD };

#include <Arduino.h>
#include "driver/adc.h"
#include "esp_adc_cal.h"

/*!
 *  @brief  Class that stores state and functions for interacting with
 *          DRV8873 Sensor
 */
class DCMotor {
public:
    DCMotor();
    void   begin(uint8_t forwardPin, uint8_t backwardPin, int readbackPin, int channel1, int channel2);
    void   forward(uint16_t speed);
    void   fullOut();
    void   backward(uint16_t speed);
    void   fullIn();
    void   halfIn();
    void   runAtSpeed(uint8_t direction, uint16_t speed);
    void   runAtPWM(long signed_speed);
    void   stop();
    void   highZ();
    double readCurrent();

    /** One-off probe of the current-sense channel, for $MADC.  Reports which ADC unit and
     *  channel this motor reads and what a real conversion does right now, so the ADC2
     *  guard in readCurrent() can be judged against the hardware instead of assumed. */
    void probeADC(int& unit, int& channel, bool& valid, int& raw, int& err, int& viaArduino);

private:
    // Current-sense channel through the IDF adc_oneshot driver (see DCMotor.cpp)
    adc_unit_t    _adcUnit    = ADC_UNIT_1;
    adc_channel_t _adcChannel = ADC_CHANNEL_0;
    bool          _adcValid   = false;
    int           _lastCurrentReading = 0;
    int     multisamples = 1;
    uint8_t _forward, _back;
    int     _readback;
    int     _maxSpeed = 1023;  //Absolute max is 1023
    int     _channel1;
    int     _channel2;
};

#endif
