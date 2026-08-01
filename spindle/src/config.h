#pragma once

#include <Arduino.h>

// Motor parameters
const int   POLE_PAIRS = 1;
const float SUPPLY_VOLTAGE = 24.0f;

// Voltage limits
const float BASE_VOLTAGE = 1.3f;
const float MAX_VOLTAGE = 16.0f;

// PWM configuration
const long  PWM_FREQUENCY = 60000;   // 60 kHz
const float DEAD_ZONE = 0.02f;       // ~2% deadtime

// Fan PWM configuration
const long FAN_PWM_FREQUENCY = 25000;
const int  FAN_PWM_RESOLUTION_BITS = 8;
const int  FAN_PWM_CHANNEL = 7;
const uint8_t FAN_MIN_DUTY = 80;
const uint8_t FAN_MAX_DUTY = 255;
const int FAN_LEVEL_COUNT = 100;
const float FAN_PWM_RAMP_UNITS_PER_SEC = 320.0f;

// Velocity ramping
const float VELOCITY_RAMP_RATE = 20.0f;   // rad/s per second, used during calibration
const float SPINDLE_RAMP_RATE  = 200.0f;  // rad/s per second, spindle on/off spin-up/down rate

// Phase offset ramping
const float PHASE_OFFSET_STEP = 45.0f * PI / 180.0f;          // 45 deg per keypress
const float PHASE_OFFSET_RAMP_RATE = 400.0f * PI / 180.0f;    // 400 deg/s ramp

// Inter-board link (UART to FluidNC XY board)
const long    LINK_BAUD = 115200;          // baud rate for the XY <-> spindle link
const uint32_t LINK_STATUS_INTERVAL_MS = 50;  // how often to report status to the XY board
const int     MAX_COMMAND_RPM = 10000;     // clamp for spindle speed commands

// On-demand WiFi OTA.  The board normally keeps its radio off; when the XY board sends
// the 'W' link command (in response to $Spindle/EnableOTA) it joins the XY board's WiFi
// network and runs an ArduinoOTA server so it can be reflashed wirelessly with:
//   pio run -e esp32-s3-devkitc-1-ota -t upload
const char*    const OTA_HOSTNAME                = "maslow-spindle";  // mDNS -> maslow-spindle.local
const uint32_t       OTA_WIFI_CONNECT_TIMEOUT_MS = 20000;   // give up joining WiFi after 20 s
const uint32_t       OTA_IDLE_TIMEOUT_MS         = 300000;  // drop WiFi/OTA after 5 min idle
const int            OTA_RECEIVE_TIMEOUT_MS      = 15000;   // tolerate 15 s data gaps mid-flash

// Current filtering
const float CURRENT_FILTER_ALPHA = 0.001f;      // Slow filter for DC-equivalent telemetry
const float PROTECTION_FILTER_ALPHA = 0.1f;     // Fast filter for stall detection

// Overcurrent protection
const float OVERCURRENT_THRESHOLD = 5.0f;       // Phase-RMS trip point (A)
const float OVERCURRENT_THRESHOLD_CAL = 6.0f;   // Relaxed phase-RMS trip point during calibration
const int   OVERCURRENT_CONSECUTIVE_TRIPS = 100;

// Motor run timeout
const uint32_t MOTOR_RUN_DURATION = 6000000;     // 60 seconds in ms

// Z-axis (phase-offset) hold power-down.  When the spindle is not spinning, the two
// BLDC drivers only hold the Z position with current, which keeps them (and the
// cooling fan) energized even though nothing is moving.  Once the XY board reports the
// machine is idle (the 'D' command) and the phase ramp has settled to within this
// tolerance of its target, the drivers are powered down.
const float PHASE_MOVE_COMPLETE_EPS_RAD = 0.001f;   // within ~0.06 deg of target = move done

// Extra motor voltage applied while the relative phase between the two motors is
// changing (i.e. the Z axis is moving).  Changing the phase fights extra mechanical
// resistance in the Z drive, so give the drivers a little more headroom while moving.
const float Z_MOVE_VOLTAGE_BOOST = 1.0f;            // volts added during phase (Z) moves

// --- Z-axis power-up homing (top-of-travel beam break) ---
// On power-up the Z axis raises until the top-of-travel beam is interrupted, which
// establishes the home position and confirms a tool is loaded.  The Z is driven by the
// relative phase between the two BLDC motors; the XY board uses 45 deg of phase per mm
// of Z travel (its phase_deg_per_mm), so the mm limit below is converted with the same
// constant.  If the Z raises further than Z_HOMING_MAX_MM without the beam breaking,
// no tool is loaded.
const float PHASE_DEG_PER_MM   = 45.0f;   // must match the XY board's phase_deg_per_mm
const float Z_HOMING_MAX_MM    = 70.0f;   // give up (no tool loaded) past this much travel
const float Z_HOMING_PHASE_DIR = +1.0f;   // +1 raises the Z; flip to -1 if homing drives it down
const float Z_HOMING_MAX_RAD =
    Z_HOMING_MAX_MM * PHASE_DEG_PER_MM * PI / 180.0f;  // travel limit as a phase offset (rad)

// --- Z-axis tool loading (reverse of homing) ---
// While homing has left the board in the "no tool loaded" state, the top-of-travel beam
// is monitored.  When the operator inserts a tool it interrupts the beam, which starts a
// tool-loading move: the Z is lowered (opposite the homing direction) until the beam is no
// longer broken, at which point a tool is considered loaded.  The lowering move gives up
// (leaving the tool not loaded) once it travels this far without the beam clearing.
const float Z_TOOL_LOAD_MAX_MM = 200.0f;  // give up lowering past this much travel
const float Z_TOOL_LOAD_MAX_RAD =
    Z_TOOL_LOAD_MAX_MM * PHASE_DEG_PER_MM * PI / 180.0f;  // travel limit as a phase offset (rad)

// --- Z-axis tool removal (raise to eject) ---
// Triggered on demand by the XY board's 'R' command (web UI "Remove Tool" button).  With a
// tool loaded the top-of-travel beam is interrupted; the Z raises (the homing direction)
// until the beam transitions from blocked (tool present) to clear (tool removed), at which
// point the board returns to the "no tool loaded" state.  The move gives up and stops once
// it travels this far without the beam clearing.
const float Z_TOOL_REMOVE_MAX_MM = 150.0f;  // give up raising past this much travel
const float Z_TOOL_REMOVE_MAX_RAD =
    Z_TOOL_REMOVE_MAX_MM * PHASE_DEG_PER_MM * PI / 180.0f;  // travel limit as a phase offset (rad)
// After the beam first clears the Z keeps raising for this long to confirm a clean removal;
// if the beam is interrupted again at any point during the window the timer restarts, so a
// tool flickering right on the edge of the beam is not mistaken for a completed removal.
const uint32_t Z_TOOL_REMOVE_CONFIRM_MS = 500;  // beam must stay clear this long to finish

// Calibration LUT
const int   CAL_LUT_SIZE = 140;                                     // 100, 200, ... 14000 RPM
const float CAL_TARGET_CURRENT = 3.5f;                              // Target phase-RMS current (A)
const float CAL_CHECKPOINT_STEP_RAD = 100.0f * 2.0f * PI / 60.0f;  // 100 RPM step in rad/s
const float CAL_MAX_RAD = 14000.0f * 2.0f * PI / 60.0f;            // 14000 RPM in rad/s
const uint32_t CAL_SETTLE_MS = 500;                                 // ms to wait after reaching speed
const float CAL_HUNT_VOLTAGE_MARGIN = 0.9f;                         // Max extra volts above seeded LUT at each step
const float CAL_RAMP_VOLT_PER_RAD = 0.0025f;                        // Open-loop spin-floor slope (volts per rad/s).
                                                                    // Kept BELOW the real current-limited curve so the hunt
                                                                    // can converge; spin-up between steps relies on the
                                                                    // previous step's voltage carried forward, not this floor.

// DRV8316 fault detection
const int OCP_CONSEC_LIMIT = 5;  // 5 x 100ms = 500ms persistent OCP -> disable

// Over-current auto-recovery.  Rather than latching a fault (which halts the machine and
// forces a power cycle), a transient over-current briefly stops the motors, lets the
// current settle, then automatically resumes the last commanded speed.  Only if faults
// keep recurring within the window do we give up and latch a real fault, so a genuinely
// stuck/broken condition is still protected.
const uint32_t FAULT_RECOVERY_COOLDOWN_MS  = 600;    // motors held off this long before resuming
const uint32_t FAULT_RECOVERY_WINDOW_MS    = 8000;   // sliding window for counting recoveries
const int      FAULT_RECOVERY_MAX_ATTEMPTS = 8;      // recoveries allowed in the window before latching
