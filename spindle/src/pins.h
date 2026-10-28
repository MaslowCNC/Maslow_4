#pragma once

// ------------------- Pin Map -------------------

// Motor Driver U14 (MP6541A) - HSx/LSx inputs, active high
#define INHA 18
#define INLA 9
#define INHB 8
#define INLB 10
#define INHC 3
#define INLC 14

// Motor Driver U15 (MP6541A)
#define INHA2 21
#define INLA2 4
#define INHB2 47
#define INLB2 35
#define INHC2 48
#define INLC2 36

// Current Sense ADC - Motor 1 (U14)
#define CURA 5
#define CURB 6
#define CURC 7

// Current Sense ADC - Motor 2 (U15)
#define CURA2 15
#define CURB2 16
#define CURC2 17

// Driver fault / sleep control.  The MP6541A has no SPI: nFAULT is an open-drain output
// (5.1k pull-up on the board) and nSLEEP - shared by BOTH drivers - is the only way to power
// the gate drivers down.  nSLEEP has an internal pull-down, so the drivers sleep out of reset.
#define DRV_NFAULT1 11    // U14 nFAULT
#define DRV_NFAULT2 12    // U15 nFAULT
#define DRV_NSLEEP  13    // U14 + U15 nSLEEP (shared)

// Vacuum / board cooling fan
#define FAN_PWM_PIN 40

// Inter-board link UART (to FluidNC XY board)
// Spindle GPIO39 (RX) <-> XY GPIO15 (TX)
// Spindle GPIO38 (TX) <-> XY GPIO16 (RX)
#define LINK_RX_PIN 39
#define LINK_TX_PIN 38

// Z-axis homing beam break (top-of-travel detector)
// IR LED emitter driven by GPIO1 (through 100R), phototransistor collector read on GPIO2
// (10k pull-up to 3V3, emitter to GND): the pin reads LOW when the beam reaches the
// detector and HIGH when the beam is interrupted.
#define BEAM_LED_PIN      1
#define BEAM_DETECT_PIN   2
