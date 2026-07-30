#pragma once

// On-demand WiFi + ArduinoOTA service for the spindle/Z controller board.
//
// The board normally keeps its radio off so the real-time motor control loop is
// undisturbed.  When the XY (FluidNC) board sends the 'W' link command (in response to
// the $Spindle/EnableOTA console command) it hands over its own WiFi credentials; the
// spindle then joins that network and runs an ArduinoOTA server so it can be reflashed
// wirelessly with:
//
//     pio run -e esp32-s3-devkitc-1-ota -t upload
//
// All WiFi/OTA work runs on a dedicated core-0 task, away from the motor control task
// pinned to core 1.

// True only while an OTA image is actively being written.  The motor control loop polls
// this and stands the drivers down until the board reboots into the new firmware.
extern volatile bool g_ota_flashing;

// True for the whole OTA window - from the moment WiFi is being brought up until the
// service tears back down (or the board reboots into new firmware).  The motor control
// loop disables both drivers while this is set so their current draw can't sag the 3.3V
// rail during the WiFi radio's power-up surge (which would otherwise hang the chip).
extern volatile bool g_ota_active;

// Set by the motor control loop once it has parked in its OTA standby branch (drivers
// off, no ADC reads).  otaTask waits for this before starting WiFi: the radio seizes the
// ADC when it powers up, and if an analogRead() is in flight on core 1 at that moment the
// conversion never completes and the core spins forever (interrupt-watchdog panic).
extern volatile bool g_motor_in_ota_standby;

// Bring up WiFi and the ArduinoOTA server using the given credentials.  Safe to call
// from the motor-control task (it just captures the credentials and starts a core-0
// task); a no-op if OTA is already active.
void requestSpindleOTA(const char* ssid, const char* pass);
