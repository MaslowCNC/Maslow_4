#pragma once

// ------------------- Pin Map -------------------

// Motor Driver U1
#define INHA 18
#define INLA 9
#define INHB 8
#define INLB 10
#define INHC 3
#define INLC 14
#define NFAULT  _NC
#define EN_GATE _NC

// Motor Driver U13
#define INHA2 21
#define INLA2 4
#define INHB2 47
#define INLB2 35
#define INHC2 48
#define INLC2 36

// Current Sense ADC - Motor 1 (U1)
#define CURA 5
#define CURB 6
#define CURC 7

// Current Sense ADC - Motor 2 (U13)
#define CURA2 15
#define CURB2 16
#define CURC2 17

// SPI Bus
#define SPI_MOSI_PIN 11
#define SPI_SCK_PIN  12
#define SPI_MISO_PIN 13
#define SPI_CS_PIN   43   // U1
#define SPI_CS_PIN2  44   // U13

// Vacuum / board cooling fan
#define FAN_PWM_PIN 40

// Inter-board link UART (to FluidNC XY board)
// Spindle GPIO39 (RX) <-> XY GPIO15 (TX)
// Spindle GPIO38 (TX) <-> XY GPIO16 (RX)
#define LINK_RX_PIN 39
#define LINK_TX_PIN 38

// Z-axis homing beam break (top-of-travel detector)
// IR LED emitter driven by GPIO1, phototransistor/detector read on GPIO45.
#define BEAM_LED_PIN      1
#define BEAM_DETECT_PIN   45
