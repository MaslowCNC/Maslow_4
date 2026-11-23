#pragma once
// Pin map and fixed hardware facts of the 36 V Unified Control Board's spindle drive (sheet
// "Spindle Drive", U20 = STM32G474RBT3).  Every pin below was checked against the KiCad netlist
// exported from branch 36V-G474-spindle (kicad-cli sch export netlist, Unified Control Board.kicad_sch)
// and the alternate functions against the Arduino core's PeripheralPins.c / ST's HAL headers.
// BRINGUP.md lists the check.
//
//  Net            G474 pin       Use
//  INHA1/B1/C1    PC0/PC1/PC2    TIM1_CH1..3   (AF2)  motor 1 high-side inputs, U21 DRV8300 (4.7k pull-downs)
//  INLA1/B1       PA11/PA12      TIM1_CH1N/2N  (AF6)  motor 1 low-side inputs
//  INLC1          PB15           TIM1_CH3N     (AF4)
//  INHA2/B2/C2    PC6/PC7/PC8    TIM8_CH1..3   (AF4)  motor 2 high-side inputs, U22 DRV8300
//  INLA2/B2/C2    PC10/PC11/PC12 TIM8_CH1N..3N (AF4)  motor 2 low-side inputs
//  ISA1/B1/C1     PA1/PA7/PB0    OPAMP1/2/3 VINP0, COMP1/2/4 INP (IO1)   5 mOhm low-side shunts, motor 1
//  ISA2           PB11           OPAMP4 VINP2, COMP6 INP (IO1)           motor 2 phase A
//  ISB2           PB14           OPAMP5 VINP0, COMP7 INP (IO1)           motor 2 phase B
//  ISC2           PB13           OPAMP6 VINP2, COMP5 INP (IO1)           motor 2 phase C
//  VBUS_SENSE     PA0            ADC12_IN1      100k / 6.8k divider (52 V full scale)
//  TEMP1/TEMP2    PC4/PC5        ADC2_IN5/IN11  10k pull-up from 3V3A to an NCP18XH103 NTC (B 3380)
//  BoardCOM1      PA2            USART2_TX (AF7) -> XY ESP32-S3 IO16 (its RX)
//  BoardCOM2      PA3            USART2_RX (AF7) <- XY ESP32-S3 IO15 (its TX)
//  BEAMBREAK      PA5            input, phototransistor with a 10k pull-up: HIGH = beam interrupted
//  DETECTORLED    PB7            output, IR emitter through 100R (on = beam active)
//  SPINDLE_LED    PA15           output, green LED4 through 1k
//  VAC_CONTROL    PB6            TIM4_CH1 (AF2) -> CN10 pin 1 (pin 2 is VCC, 36 V)
//  SWDIO/SWCLK    PA13/PA14      TC2030 J1
//  SPINDLE_NRST   PG10/NRST      10k up, 100 nF, SW2, and the XY board's IO1 through 1k
//  SPINDLE_BOOT0  PB8/BOOT0      10k down, SW3 up, and the XY board's IO2 through 1k
//
// The DRV8300 has no fault output and no enable pin: the timers' MOE bit is the only off switch,
// and the six comparators trip the timers' break inputs in hardware.

#include <Arduino.h>

// ---- Shunt sense chain (Spindle Drive sheet note) ----
// VINP = 3V3A * 1k/32.6k + 0.969 * I * 5 mOhm; the OPAMP's PGA multiplies by 16 into the ADC.
// 1.62 V at 0 A, 77.5 mV/A, about +-21 A of ADC range.  The zero is measured at boot.
constexpr float SHUNT_OHMS          = 0.005f;
constexpr float SENSE_DIVIDER       = 31.6f / 32.6f;   // the 1k / 31.6k bias network's attenuation
constexpr float PGA_GAIN            = 16.0f;
constexpr float ADC_VREF            = 3.3f;            // VREF+ = 3V3A
constexpr float ADC_FULL_SCALE      = 4096.0f;
constexpr float SENSE_V_PER_A       = SHUNT_OHMS * SENSE_DIVIDER * PGA_GAIN;          // 0.0775 V/A
constexpr float SENSE_A_PER_LSB     = ADC_VREF / ADC_FULL_SCALE / SENSE_V_PER_A;      // ~0.0104 A
// A positive phase current (out of the bridge into the motor) flows up through the low-side
// shunt while the low side conducts, so the shunt node goes NEGATIVE: the reading falls.
// Verify with "#ALIGN" at bring-up (BRINGUP.md step 4).
constexpr float SENSE_SIGN          = -1.0f;
// The comparators see VINP itself (before the PGA): 0.101 V at 0 A and 4.85 mV/A.
constexpr float VINP_ZERO_V         = ADC_VREF / 32.6f;
constexpr float VINP_V_PER_A        = SHUNT_OHMS * SENSE_DIVIDER;

// ---- Bus voltage divider: 100k over 6.8k ----
constexpr float VBUS_DIVIDER        = (100.0f + 6.8f) / 6.8f;   // 15.7: 3.3 V at the pin = 51.8 V

// ---- NTCs: 10k pull-up to 3V3A, NCP18XH103F03RB to GND ----
constexpr float NTC_R25             = 10000.0f;
constexpr float NTC_B               = 3380.0f;
constexpr float NTC_PULLUP          = 10000.0f;

// ---- PWM ----
constexpr uint32_t TIMER_CLOCK_HZ   = 170000000;
#ifdef PWM_FREQUENCY_OVERRIDE_HZ   // bring-up experiments, e.g. moving the 3V3 buck's alias (BRINGUP.md)
constexpr uint32_t PWM_FREQUENCY_HZ = PWM_FREQUENCY_OVERRIDE_HZ;
#else
constexpr uint32_t PWM_FREQUENCY_HZ = 20000;
#endif
// Centre-aligned: the counter goes 0 -> ARR -> 0 once per PWM period.
constexpr uint32_t PWM_ARR          = TIMER_CLOCK_HZ / (2 * PWM_FREQUENCY_HZ);   // 4250
constexpr float    PWM_PERIOD_S     = 1.0f / PWM_FREQUENCY_HZ;
// The ADC trigger (TIM1 OC4REF on TRGO2) is high while CNT > ARR - SAMPLE_LEAD_TICKS: it rises
// 118 ns before the peak and stays high 235 ns, several ADC clocks (23.5 ns at 42.5 MHz), so the
// ADCs cannot miss it.  The 0.29 us sampling window then sits about centred on the peak.
constexpr uint32_t SAMPLE_LEAD_TICKS = 20;
// The DRV8300 (DT pin open) adds its own 200 ns.  The timer's dead time is on top of it, so a
// timer whose outputs are ever driven straight to FETs elsewhere is never left without one.
constexpr uint32_t DEADTIME_NS      = 100;

// ---- U9 (TPS54360B 3V3 buck) sync clock: PB4 = TIM3_CH1 (AF2), through 10-22 pF into U9 RT/CLK ----
// v1.1 has C165 + net U9_SYNC; a v1.0 board needs the bodge in REVISIONS.md (PB4 is unconnected without it).
// 500 kHz = exactly 25 clocks per 20 kHz PWM period (8500 / 340 ticks), started from TIM1's update event at
// the peak, so the buck's switching keeps a fixed phase against the current samples.  Rank 1 of every ADC samples
// at about +15 ticks from the peak, ADC5's rank 2 (motor 2 phase B) about 120 ticks later.
// Phase sweeps on board 2 (v1.0 + bodge, BRINGUP.md "Board 2"), sd of the six channels:
//  - 36 V, logic load only: quiet (rms 10-11 counts, unsynchronised 120) for a rising edge 10-40 ticks after the
//    peak, worse from 5 and 45, a plateau of 30-40 at 110-240 and worse than unsynchronised at 280-320.  U9 runs in
//    discontinuous mode there (short on-time, then the inductor current reaches zero and the switch node rings),
//    so more than its two edges matter.
//  - USB power (U9 from 4.6 V, on-time ~1.35 us): quiet at 30 and 140-190.
// 30 is inside both.  Check it again under motor load (the 36 V rail moves, the logic load does not).
constexpr uint32_t SYNC_PERIOD_TICKS = 340;   // 500 kHz at 170 MHz
constexpr uint32_t SYNC_HIGH_TICKS   = 17;    // 100 ns high pulse
constexpr uint32_t SYNC_RISE_TICKS   = 30;    // rising edge, ticks after the PWM peak (0 .. 339)
// The clock must divide the PWM period exactly (a PWM_FREQUENCY_OVERRIDE_HZ experiment build leaves it off).
constexpr bool     SYNC_AVAILABLE    = 2 * PWM_ARR == 25 * SYNC_PERIOD_TICKS;
// Start the clock at boot, before the current-sense zero is measured: once U9 is locked its ripple lands on the
// samples as a fixed offset (board 2 on USB power: up to ~100 counts on M1b), which the zero then takes out.
// A board without the capacitor (v1.0 unmodified) just toggles an unconnected pin.  #SYNC 0 turns it off.
constexpr bool SYNC_AT_BOOT = SYNC_AVAILABLE;
// U9's ripple offset follows its input voltage (board 2: booted on USB, then 36 V moved M1b's zero by ~55 counts),
// so with the clock running the zero is measured again when the bus moves this far, with the drive off.
constexpr float SYNC_REZERO_V = 4.0f;

// ---- Pins driven as plain GPIO ----
#define PIN_BEAM_DETECT   PA5
#define PIN_BEAM_LED      PB7
#define PIN_STATUS_LED    PA15
