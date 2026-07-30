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

// Bring up WiFi and the ArduinoOTA server using the given credentials.  Safe to call
// from the motor-control task (it just captures the credentials and starts a core-0
// task); a no-op if OTA is already active.
void requestSpindleOTA(const char* ssid, const char* pass);
