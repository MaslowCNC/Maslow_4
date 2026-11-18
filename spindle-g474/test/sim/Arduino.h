// Host stand-in for <Arduino.h> and the few STM32 registers foc.cpp touches, so the real control
// code (src/foc.cpp) can run against a motor model on a PC.  See sim_main.cpp.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

struct TIM_TypeDef { volatile uint32_t SR; };
struct ADC_TypeDef { volatile uint32_t ISR, JDR1, JDR2; };
struct DWT_Type { volatile uint32_t CTRL, CYCCNT; };
struct CoreDebug_Type { volatile uint32_t DEMCR; };
extern TIM_TypeDef    sim_tim1, sim_tim8;
extern ADC_TypeDef    sim_adc[5];
extern DWT_Type       sim_dwt;
extern CoreDebug_Type sim_cd;
#define TIM1 (&sim_tim1)
#define TIM8 (&sim_tim8)
#define ADC1 (&sim_adc[0])
#define ADC2 (&sim_adc[1])
#define ADC3 (&sim_adc[2])
#define ADC4 (&sim_adc[3])
#define ADC5 (&sim_adc[4])
#define DWT (&sim_dwt)
#define CoreDebug (&sim_cd)
#define TIM_SR_BIF (1u << 7)
#define ADC_ISR_JEOC (1u << 5)
#define ADC_ISR_JEOS (1u << 6)
#define CoreDebug_DEMCR_TRCENA_Msk (1u << 24)
#define DWT_CTRL_CYCCNTENA_Msk 1u
extern uint32_t SystemCoreClock;
inline void __disable_irq() {}
inline void __enable_irq() {}
uint32_t millis();
