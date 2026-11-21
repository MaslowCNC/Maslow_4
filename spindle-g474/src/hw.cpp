#include "hw.h"
#include "board.h"
#include "config.h"
#include "link.h"   // BOOT_TRACE_POINT
#include <math.h>

// All peripheral handles are file-local.  HAL is used for initialisation only; the interrupt
// and the duty writes touch the registers directly.

namespace hw {
    const char* g_update_phase = "unknown";

    namespace {
        TIM_HandleTypeDef  htim[2];      // TIM1 = motor 1, TIM8 = motor 2
        TIM_HandleTypeDef  htimFan;      // TIM4 CH1 on PB6
        OPAMP_HandleTypeDef hopamp[6];
        ADC_HandleTypeDef  hadc[5];      // ADC1..ADC5
        COMP_HandleTypeDef hcomp[6];
        DAC_HandleTypeDef  hdac3, hdac4;

        TIM_TypeDef* const TIMS[2] = { TIM1, TIM8 };

        // ---------------------------------------------------------------- GPIO
        void gpioOut(GPIO_TypeDef* port, uint32_t pin, uint32_t af) {
            GPIO_InitTypeDef g = {};
            g.Pin       = pin;
            g.Mode      = GPIO_MODE_AF_PP;
            g.Pull      = GPIO_NOPULL;   // never a pull-up on a gate input (the board has 4.7k pull-downs)
            g.Speed     = GPIO_SPEED_FREQ_HIGH;
            g.Alternate = af;
            HAL_GPIO_Init(port, &g);
        }

        void gpioAnalog(GPIO_TypeDef* port, uint32_t pin) {
            GPIO_InitTypeDef g = {};
            g.Pin  = pin;
            g.Mode = GPIO_MODE_ANALOG;
            g.Pull = GPIO_NOPULL;
            HAL_GPIO_Init(port, &g);
        }

        // ---------------------------------------------------------------- PWM timers
        uint32_t deadtimeTicks() {
            // DTG[7:5] = 0xx: DT = DTG * tDTS, tDTS = 1/170 MHz = 5.9 ns, up to 127 ticks (747 ns)
            uint32_t t = (uint32_t)((uint64_t)DEADTIME_NS * TIMER_CLOCK_HZ / 1000000000ULL);
            return t > 127 ? 127 : t;
        }

        bool initPwmTimer(int m, const char** why) {
            TIM_HandleTypeDef& h = htim[m];
            h.Instance               = TIMS[m];
            h.Init.Prescaler         = 0;
            h.Init.CounterMode       = TIM_COUNTERMODE_CENTERALIGNED1;   // compare flags while counting down
            h.Init.Period            = PWM_ARR;
            h.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
            h.Init.RepetitionCounter = 1;                                 // one update per PWM period
            h.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
            if (HAL_TIM_PWM_Init(&h) != HAL_OK) { *why = "TIM PWM init"; return false; }

            // TRGO2 = OC4REF: channel 4 in PWM mode 2 goes active as the counter passes
            // ARR - SAMPLE_LEAD_TICKS on its way up, i.e. just before the peak, when every low side of
            // BOTH bridges is on.  All five
            // ADCs start their injected conversion on TIM1's TRGO2 rising edge.
            TIM_MasterConfigTypeDef mc = {};
            mc.MasterOutputTrigger  = TIM_TRGO_RESET;
            mc.MasterOutputTrigger2 = TIM_TRGO2_OC4REF;
            mc.MasterSlaveMode      = TIM_MASTERSLAVEMODE_DISABLE;
            if (HAL_TIMEx_MasterConfigSynchronization(&h, &mc) != HAL_OK) { *why = "TIM master config"; return false; }

            TIM_OC_InitTypeDef oc = {};
            oc.OCMode       = TIM_OCMODE_PWM1;          // high side on while CNT < CCR (around the valley)
            oc.Pulse        = 0;                        // 0 = low side on all period
            oc.OCPolarity   = TIM_OCPOLARITY_HIGH;
            oc.OCNPolarity  = TIM_OCNPOLARITY_HIGH;     // DRV8300 MODE open: non-inverted INL
            oc.OCFastMode   = TIM_OCFAST_DISABLE;
            oc.OCIdleState  = TIM_OCIDLESTATE_RESET;    // MOE = 0: INH low ...
            oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;   // ... and INL low: every FET off, motor coasts
            const uint32_t ch[3] = { TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3 };
            for (int i = 0; i < 3; i++) {
                if (HAL_TIM_PWM_ConfigChannel(&h, &oc, ch[i]) != HAL_OK) { *why = "TIM channel"; return false; }
            }
            oc.OCMode = TIM_OCMODE_PWM2;
            oc.Pulse  = PWM_ARR - SAMPLE_LEAD_TICKS;
            if (HAL_TIM_PWM_ConfigChannel(&h, &oc, TIM_CHANNEL_4) != HAL_OK) { *why = "TIM channel 4"; return false; }

            TIM_BreakDeadTimeConfigTypeDef bd = {};
            bd.OffStateRunMode  = TIM_OSSR_ENABLE;
            bd.OffStateIDLEMode = TIM_OSSI_ENABLE;      // MOE = 0 drives the idle levels (low), not Hi-Z
            bd.LockLevel        = TIM_LOCKLEVEL_OFF;
            bd.DeadTime         = deadtimeTicks();
            bd.BreakState       = TIM_BREAK_ENABLE;
            bd.BreakPolarity    = TIM_BREAKPOLARITY_HIGH;
            bd.BreakFilter      = 0x9;                  // fDTS/8, N = 8: ~0.38 us of a steady trip
            bd.BreakAFMode      = TIM_BREAK_AFMODE_INPUT;
            bd.Break2State      = TIM_BREAK2_DISABLE;
            bd.Break2Polarity   = TIM_BREAK2POLARITY_HIGH;
            bd.Break2Filter     = 0;
            bd.Break2AFMode     = TIM_BREAK_AFMODE_INPUT;
            bd.AutomaticOutput  = TIM_AUTOMATICOUTPUT_DISABLE;   // a break stays latched until firmware acts
            if (HAL_TIMEx_ConfigBreakDeadTime(&h, &bd) != HAL_OK) { *why = "TIM break/dead time"; return false; }

            // Comparators into the break input: motor 1 = COMP1/2/4, motor 2 = COMP6/7/5.
            const uint32_t src[2][3] = {
                { TIM_BREAKINPUTSOURCE_COMP1, TIM_BREAKINPUTSOURCE_COMP2, TIM_BREAKINPUTSOURCE_COMP4 },
                { TIM_BREAKINPUTSOURCE_COMP6, TIM_BREAKINPUTSOURCE_COMP7, TIM_BREAKINPUTSOURCE_COMP5 },
            };
            for (int i = 0; i < 3; i++) {
                TIMEx_BreakInputConfigTypeDef bi = {};
                bi.Source   = src[m][i];
                bi.Enable   = TIM_BREAKINPUTSOURCE_ENABLE;
                bi.Polarity = TIM_BREAKINPUTSOURCE_POLARITY_HIGH;
                if (HAL_TIMEx_ConfigBreakInput(&h, TIM_BREAKINPUT_BRK, &bi) != HAL_OK) { *why = "TIM break input"; return false; }
            }
            // The BKIN pin is not wired: keep its input off so a floating pad cannot trip the timer.
            TIMS[m]->AF1 &= ~TIM1_AF1_BKINE;

            // Output enables on, MOE still off: with OSSI the six outputs are now actively low.
            TIM_TypeDef* t = TIMS[m];
            t->CCER |= TIM_CCER_CC1E | TIM_CCER_CC1NE | TIM_CCER_CC2E | TIM_CCER_CC2NE | TIM_CCER_CC3E | TIM_CCER_CC3NE;
            t->BDTR &= ~TIM_BDTR_MOE;
            t->SR = 0;
            return true;
        }

        void initPwmPins() {
            // Motor 1 (TIM1): PC0-PC2 CH1-3 AF2, PA11/PA12 CH1N/CH2N AF6, PB15 CH3N AF4.
            gpioOut(GPIOC, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2, GPIO_AF2_TIM1);
            gpioOut(GPIOA, GPIO_PIN_11 | GPIO_PIN_12, GPIO_AF6_TIM1);
            gpioOut(GPIOB, GPIO_PIN_15, GPIO_AF4_TIM1);
            // Motor 2 (TIM8): PC6-PC8 CH1-3, PC10-PC12 CH1N-3N, all AF4.
            gpioOut(GPIOC, GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12, GPIO_AF4_TIM8);
        }

        // Make the update event (where the preloaded duties load) land on the valley, so duties
        // written by the interrupt just after a peak sample are in force for the whole next period
        // and its peak sample.  With RCR = 1 the parity of the repetition counter decides which
        // extreme carries the update; a single RCR = 0 update shifts it by half a period.
        bool alignUpdateToValley(TIM_TypeDef* t) {
            for (int attempt = 0; attempt < 4; attempt++) {
                t->SR = ~TIM_SR_UIF;
                uint32_t t0 = micros();
                while (!(t->SR & TIM_SR_UIF)) {
                    if (micros() - t0 > 1000) return false;
                }
                uint32_t cnt = t->CNT;
                bool     up  = !(t->CR1 & TIM_CR1_DIR);
                if (cnt < PWM_ARR / 4 && up) return true;   // just past the valley: right phase
                // Shift by one half period.
                t->RCR = 0;
                t->SR  = ~TIM_SR_UIF;
                t0     = micros();
                while (!(t->SR & TIM_SR_UIF)) {
                    if (micros() - t0 > 1000) return false;
                }
                t->RCR = 1;
            }
            return false;
        }

        // ---------------------------------------------------------------- OPAMP PGAs
        bool initOpamps(const char** why) {
            OPAMP_TypeDef* const inst[6] = { OPAMP1, OPAMP2, OPAMP3, OPAMP4, OPAMP5, OPAMP6 };
            // VINP index per OPAMP, from the HAL's own table (stm32g4xx_hal_opamp.h):
            //   OPAMP1 IO0 = PA1 (ISA1), OPAMP2 IO0 = PA7 (ISB1), OPAMP3 IO0 = PB0 (ISC1),
            //   OPAMP4 IO2 = PB11 (ISA2), OPAMP5 IO0 = PB14 (ISB2), OPAMP6 IO2 = PB13 (ISC2).
            const uint32_t vinp[6] = { OPAMP_NONINVERTINGINPUT_IO0, OPAMP_NONINVERTINGINPUT_IO0, OPAMP_NONINVERTINGINPUT_IO0,
                                       OPAMP_NONINVERTINGINPUT_IO2, OPAMP_NONINVERTINGINPUT_IO0, OPAMP_NONINVERTINGINPUT_IO2 };
            for (int i = 0; i < 6; i++) {
                OPAMP_HandleTypeDef& h = hopamp[i];
                h.Instance                    = inst[i];
                h.Init.PowerMode              = OPAMP_POWERMODE_HIGHSPEED;
                h.Init.Mode                   = OPAMP_PGA_MODE;
                h.Init.NonInvertingInput      = vinp[i];
                h.Init.InternalOutput         = ENABLE;   // to the ADC; the VOUT pins stay free (OPAMP1 VOUT = PA2 = link TX)
                h.Init.TimerControlledMuxmode = OPAMP_TIMERCONTROLLEDMUXMODE_DISABLE;
                h.Init.PgaConnect             = OPAMP_PGA_CONNECT_INVERTINGINPUT_NO;
                h.Init.PgaGain                = OPAMP_PGA_GAIN_16_OR_MINUS_15;
                h.Init.UserTrimming           = OPAMP_TRIMMING_FACTORY;
                if (HAL_OPAMP_Init(&h) != HAL_OK || HAL_OPAMP_Start(&h) != HAL_OK) { *why = "OPAMP"; return false; }
            }
            return true;
        }

        // ---------------------------------------------------------------- ADCs
        bool initAdc(int idx, ADC_TypeDef* inst, const char** why) {
            ADC_HandleTypeDef& h = hadc[idx];
            h.Instance                   = inst;
            h.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;   // 42.5 MHz, fixed latency to the trigger
            h.Init.Resolution            = ADC_RESOLUTION_12B;
            h.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
            h.Init.GainCompensation      = 0;
            h.Init.ScanConvMode          = ADC_SCAN_ENABLE;            // ADC5 converts two injected ranks
            h.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
            h.Init.LowPowerAutoWait      = DISABLE;
            h.Init.ContinuousConvMode    = DISABLE;
            h.Init.NbrOfConversion       = 1;
            h.Init.DiscontinuousConvMode = DISABLE;
            h.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
            h.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
            h.Init.DMAContinuousRequests = DISABLE;
            h.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
            h.Init.OversamplingMode      = DISABLE;
            if (HAL_ADC_Init(&h) != HAL_OK) { *why = "ADC init"; return false; }
            if (HAL_ADCEx_Calibration_Start(&h, ADC_SINGLE_ENDED) != HAL_OK) { *why = "ADC calibration"; return false; }
            return true;
        }

        bool injected(int idx, uint32_t channel, uint32_t rank, uint32_t nranks, const char** why) {
            ADC_InjectionConfTypeDef j = {};
            j.InjectedChannel               = channel;
            j.InjectedRank                  = rank;
            j.InjectedSamplingTime          = ADC_SAMPLETIME_12CYCLES_5;   // 0.29 us
            j.InjectedSingleDiff            = ADC_SINGLE_ENDED;
            j.InjectedOffsetNumber          = ADC_OFFSET_NONE;
            j.InjectedOffset                = 0;
            j.InjectedNbrOfConversion       = nranks;
            j.InjectedDiscontinuousConvMode = DISABLE;
            j.AutoInjectedConv              = DISABLE;
            j.QueueInjectedContext          = DISABLE;
            j.ExternalTrigInjecConv         = ADC_EXTERNALTRIGINJEC_T1_TRGO2;
            j.ExternalTrigInjecConvEdge     = ADC_EXTERNALTRIGINJECCONV_EDGE_RISING;
            j.InjecOversamplingMode         = DISABLE;
            if (HAL_ADCEx_InjectedConfigChannel(&hadc[idx], &j) != HAL_OK) { *why = "ADC injected channel"; return false; }
            return true;
        }

        bool regularSampleTime(uint32_t channel, uint32_t time, const char** why) {
            ADC_ChannelConfTypeDef c = {};
            c.Channel      = channel;
            c.Rank         = ADC_REGULAR_RANK_1;
            c.SamplingTime = time;
            c.SingleDiff   = ADC_SINGLE_ENDED;
            c.OffsetNumber = ADC_OFFSET_NONE;
            c.Offset       = 0;
            if (HAL_ADC_ConfigChannel(&hadc[1], &c) != HAL_OK) { *why = "ADC2 regular channel"; return false; }
            return true;
        }

        // ---------------------------------------------------------------- comparators
        uint32_t dacCodeForAmps(float amps) {
            float v = VINP_ZERO_V + amps * VINP_V_PER_A;
            float code = v / ADC_VREF * 4095.0f;
            if (code < 0) code = 0;
            if (code > 4095) code = 4095;
            return (uint32_t)code;
        }

        bool initDac(DAC_HandleTypeDef& h, DAC_TypeDef* inst, const char** why) {
            h.Instance = inst;
            if (HAL_DAC_Init(&h) != HAL_OK) { *why = "DAC init"; return false; }
            DAC_ChannelConfTypeDef c = {};
            c.DAC_HighFrequency           = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
            c.DAC_DMADoubleDataMode       = DISABLE;
            c.DAC_SignedFormat            = DISABLE;
            c.DAC_SampleAndHold           = DAC_SAMPLEANDHOLD_DISABLE;
            c.DAC_Trigger                 = DAC_TRIGGER_NONE;
            c.DAC_Trigger2                = DAC_TRIGGER_NONE;
            c.DAC_OutputBuffer            = DAC_OUTPUTBUFFER_DISABLE;
            c.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_INTERNAL;
            c.DAC_UserTrimming            = DAC_TRIMMING_FACTORY;
            for (uint32_t ch : { DAC_CHANNEL_1, DAC_CHANNEL_2 }) {
                if (HAL_DAC_ConfigChannel(&h, &c, ch) != HAL_OK) { *why = "DAC channel"; return false; }
                HAL_DAC_SetValue(&h, ch, DAC_ALIGN_12B_R, dacCodeForAmps(HW_TRIP_A));
                if (HAL_DAC_Start(&h, ch) != HAL_OK) { *why = "DAC start"; return false; }
            }
            return true;
        }

        bool initComparators(const char** why) {
            // INP = IO1 for all six (COMP1 PA1, COMP2 PA7, COMP4 PB0, COMP6 PB11, COMP7 PB14, COMP5 PB13);
            // INM = a DAC3/DAC4 channel the HAL allows for that comparator.  Order = motor 1 a,b,c,
            // motor 2 a,b,c, matching comparatorOutputs().
            COMP_TypeDef* const inst[6] = { COMP1, COMP2, COMP4, COMP6, COMP7, COMP5 };
            const uint32_t inm[6] = { COMP_INPUT_MINUS_DAC3_CH1, COMP_INPUT_MINUS_DAC3_CH2, COMP_INPUT_MINUS_DAC3_CH2,
                                      COMP_INPUT_MINUS_DAC4_CH2, COMP_INPUT_MINUS_DAC4_CH1, COMP_INPUT_MINUS_DAC4_CH1 };
            for (int i = 0; i < 6; i++) {
                COMP_HandleTypeDef& h = hcomp[i];
                h.Instance          = inst[i];
                h.Init.InputPlus    = COMP_INPUT_PLUS_IO1;
                h.Init.InputMinus   = inm[i];
                h.Init.OutputPol    = COMP_OUTPUTPOL_NONINVERTED;   // high = VINP above the threshold
                h.Init.Hysteresis   = COMP_HYSTERESIS_20MV;
                h.Init.BlankingSrce = COMP_BLANKINGSRC_NONE;
                h.Init.TriggerMode  = COMP_TRIGGERMODE_NONE;
                if (HAL_COMP_Init(&h) != HAL_OK || HAL_COMP_Start(&h) != HAL_OK) { *why = "COMP"; return false; }
            }
            return true;
        }

        // ---------------------------------------------------------------- slow analog
        struct Slow {
            uint32_t channel;
            float    filtered;
            bool     valid;
        };
        Slow     slow[3] = { { ADC_CHANNEL_1, 0, false }, { ADC_CHANNEL_5, 0, false }, { ADC_CHANNEL_11, 0, false } };
        int      slowIdx = 0;
        bool     slowBusy = false;

        float ntcCelsius(float frac) {
            // frac = Vntc / 3V3A; NTC to GND under a 10k pull-up.
            if (frac < 0.01f || frac > 0.99f) return NAN;
            float r = NTC_PULLUP * frac / (1.0f - frac);
            float invT = 1.0f / 298.15f + logf(r / NTC_R25) / NTC_B;
            return 1.0f / invT - 273.15f;
        }
    }

    // ==================================================================== public

    bool init(const char** why) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_SYSCFG_CLK_ENABLE();   // OPAMPs and comparators
        __HAL_RCC_TIM1_CLK_ENABLE();
        __HAL_RCC_TIM8_CLK_ENABLE();
        __HAL_RCC_TIM4_CLK_ENABLE();
        __HAL_RCC_ADC12_CLK_ENABLE();
        __HAL_RCC_ADC345_CLK_ENABLE();
        __HAL_RCC_DAC3_CLK_ENABLE();
        __HAL_RCC_DAC4_CLK_ENABLE();

        // A debugger halt stops the PWM timers, which turns the bridges off.
        __HAL_DBGMCU_FREEZE_TIM1();
        __HAL_DBGMCU_FREEZE_TIM8();
        __HAL_DBGMCU_FREEZE_IWDG();

        // Timers first, so the gate pins are driven low the moment they become timer outputs.
        for (int m = 0; m < 2; m++) {
            if (!initPwmTimer(m, why)) return false;
        }
        BOOT_TRACE_POINT("PWM timers");
        initPwmPins();
        BOOT_TRACE_POINT("PWM pins");

        // Sense pins: analog (OPAMP VINP and COMP INP share them).  Slow analog too.
        gpioAnalog(GPIOA, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7);
        gpioAnalog(GPIOB, GPIO_PIN_0 | GPIO_PIN_11 | GPIO_PIN_13 | GPIO_PIN_14);
        gpioAnalog(GPIOC, GPIO_PIN_4 | GPIO_PIN_5);
        BOOT_TRACE_POINT("analog pins");

        if (!initOpamps(why)) return false;
        BOOT_TRACE_POINT("OPAMPs");

        ADC_TypeDef* const adcs[5] = { ADC1, ADC2, ADC3, ADC4, ADC5 };
        for (int i = 0; i < 5; i++) {
            if (!initAdc(i, adcs[i], why)) return false;
            BOOT_TRACE_POINT(i == 0 ? "ADC1" : i == 1 ? "ADC2" : i == 2 ? "ADC3" : i == 3 ? "ADC4" : "ADC5");
        }
        // Motor 1: OPAMP1 -> ADC1, OPAMP2 -> ADC2, OPAMP3 -> ADC3.  Motor 2: OPAMP4 + OPAMP5 -> ADC5
        // (two ranks), OPAMP6 -> ADC4.
        if (!injected(0, ADC_CHANNEL_VOPAMP1, ADC_INJECTED_RANK_1, 1, why)) return false;
        if (!injected(1, ADC_CHANNEL_VOPAMP2, ADC_INJECTED_RANK_1, 1, why)) return false;
        if (!injected(2, ADC_CHANNEL_VOPAMP3_ADC3, ADC_INJECTED_RANK_1, 1, why)) return false;
        if (!injected(3, ADC_CHANNEL_VOPAMP6, ADC_INJECTED_RANK_1, 1, why)) return false;
        if (!injected(4, ADC_CHANNEL_VOPAMP4, ADC_INJECTED_RANK_1, 2, why)) return false;
        if (!injected(4, ADC_CHANNEL_VOPAMP5, ADC_INJECTED_RANK_2, 2, why)) return false;
        // ADC2's regular group reads VBUS (IN1) and the NTCs (IN5, IN11) between the injected samples.
        // Sampling times can only be set while nothing converts, so set them all now.
        if (!regularSampleTime(ADC_CHANNEL_5, ADC_SAMPLETIME_247CYCLES_5, why)) return false;
        if (!regularSampleTime(ADC_CHANNEL_11, ADC_SAMPLETIME_247CYCLES_5, why)) return false;
        if (!regularSampleTime(ADC_CHANNEL_1, ADC_SAMPLETIME_47CYCLES_5, why)) return false;
        BOOT_TRACE_POINT("ADC channels");

        if (!initDac(hdac3, DAC3, why)) return false;
        if (!initDac(hdac4, DAC4, why)) return false;
        BOOT_TRACE_POINT("DACs");
        delayMicroseconds(20);   // DAC settling before the comparators can see a threshold
        if (!initComparators(why)) return false;
        BOOT_TRACE_POINT("comparators");

        // Fan: TIM4 CH1 on PB6, 25 kHz, off.
        htimFan.Instance               = TIM4;
        htimFan.Init.Prescaler         = 0;
        htimFan.Init.CounterMode       = TIM_COUNTERMODE_UP;
        htimFan.Init.Period            = TIMER_CLOCK_HZ / FAN_PWM_HZ - 1;
        htimFan.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
        htimFan.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
        if (HAL_TIM_PWM_Init(&htimFan) != HAL_OK) { *why = "fan timer"; return false; }
        TIM_OC_InitTypeDef oc = {};
        oc.OCMode     = TIM_OCMODE_PWM1;
        oc.Pulse      = 0;
        oc.OCPolarity = TIM_OCPOLARITY_HIGH;
        if (HAL_TIM_PWM_ConfigChannel(&htimFan, &oc, TIM_CHANNEL_1) != HAL_OK) { *why = "fan channel"; return false; }
        gpioOut(GPIOB, GPIO_PIN_6, GPIO_AF2_TIM4);
        HAL_TIM_PWM_Start(&htimFan, TIM_CHANNEL_1);
        BOOT_TRACE_POINT("fan timer");

        pinMode(PIN_BEAM_LED, OUTPUT);
        digitalWrite(PIN_BEAM_LED, HIGH);   // beam on for good, as the ESP32 board did
        pinMode(PIN_BEAM_DETECT, INPUT);    // 10k pull-up on the board
        pinMode(PIN_STATUS_LED, OUTPUT);
        digitalWrite(PIN_STATUS_LED, LOW);
        return true;
    }

    bool start(const char** why) {
        // Start the injected conversions (they wait for the trigger), interrupt on ADC5's end of
        // sequence: ADC5 converts two ranks, so it finishes last.
        for (int i = 0; i < 5; i++) {
            if (HAL_ADCEx_InjectedStart(&hadc[i]) != HAL_OK) { *why = "ADC injected start"; return false; }
        }
        BOOT_TRACE_POINT("ADC injected start");
        ADC5->ISR = ADC_ISR_JEOS | ADC_ISR_JEOC;
        ADC5->IER |= ADC_IER_JEOSIE;
        HAL_NVIC_SetPriority(ADC5_IRQn, 0, 0);

        // Start both PWM counters back to back (a few ns apart) so both bridges sample and switch in
        // step: the peak sample then sees neither bridge switching.
        __disable_irq();
        TIM1->CNT = 0;
        TIM8->CNT = 0;
        TIM1->CR1 |= TIM_CR1_CEN;
        TIM8->CR1 |= TIM_CR1_CEN;
        __enable_irq();

        BOOT_TRACE_POINT("PWM counters running");
        bool ok1 = alignUpdateToValley(TIM1);
        bool ok8 = alignUpdateToValley(TIM8);
        g_update_phase = (ok1 && ok8) ? "valley" : "NOT ALIGNED";
        if (!ok1 || !ok8) { *why = "PWM update event not on the valley"; return false; }

        HAL_NVIC_EnableIRQ(ADC5_IRQn);
        return true;
    }

    bool enableOutputs(int m) {
        TIM_TypeDef* t = TIMS[m];
        if (t->SR & TIM_SR_BIF) return false;
        t->BDTR |= TIM_BDTR_MOE;
        return (t->BDTR & TIM_BDTR_MOE) != 0;   // a break input still high keeps MOE clear
    }

    void disableOutputs(int m) {
        TIMS[m]->BDTR &= ~TIM_BDTR_MOE;
    }

    bool outputsEnabled(int m) {
        return (TIMS[m]->BDTR & TIM_BDTR_MOE) != 0;
    }

    void writeDuty(int m, uint16_t a, uint16_t b, uint16_t c) {
        TIM_TypeDef* t = TIMS[m];
        t->CCR1 = a;
        t->CCR2 = b;
        t->CCR3 = c;
    }

    bool breakLatched(int m) {
        return (TIMS[m]->SR & TIM_SR_BIF) != 0;
    }

    bool clearBreak(int m) {
        TIMS[m]->SR = ~TIM_SR_BIF;
        return !breakLatched(m);
    }

    uint8_t comparatorOutputs() {
        uint8_t bits = 0;
        for (int i = 0; i < 6; i++) {
            if (HAL_COMP_GetOutputLevel(&hcomp[i]) == COMP_OUTPUT_LEVEL_HIGH) bits |= 1u << i;
        }
        return bits;
    }

    void setHardwareTripAmps(float amps) {
        uint32_t code = dacCodeForAmps(amps);
        HAL_DAC_SetValue(&hdac3, DAC_CHANNEL_1, DAC_ALIGN_12B_R, code);
        HAL_DAC_SetValue(&hdac3, DAC_CHANNEL_2, DAC_ALIGN_12B_R, code);
        HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_1, DAC_ALIGN_12B_R, code);
        HAL_DAC_SetValue(&hdac4, DAC_CHANNEL_2, DAC_ALIGN_12B_R, code);
    }

    uint16_t rawCurrent(int m, int p) {
        if (m == 0) {
            switch (p) {
                case 0: return ADC1->JDR1;
                case 1: return ADC2->JDR1;
                default: return ADC3->JDR1;
            }
        }
        switch (p) {
            case 0: return ADC5->JDR1;
            case 1: return ADC5->JDR2;
            default: return ADC4->JDR1;
        }
    }

    void serviceSlowAnalog() {
        if (slowBusy) {
            if (!(ADC2->ISR & ADC_ISR_EOC)) return;   // still converting
            uint32_t raw = ADC2->DR;                  // reading DR clears EOC
            slowBusy = false;
            Slow& s = slow[slowIdx];
            float v = raw * (ADC_VREF / ADC_FULL_SCALE);
            if (!s.valid) {
                s.filtered = v;
                s.valid    = true;
            } else {
                s.filtered += (v - s.filtered) * 0.2f;
            }
            slowIdx = (slowIdx + 1) % 3;
        }
        if (ADC2->CR & ADC_CR_ADSTART) return;
        // One regular conversion of the next channel; injected samples pre-empt it as needed.
        ADC2->SQR1 = (slow[slowIdx].channel & 0x1F) << ADC_SQR1_SQ1_Pos;   // L = 0: one conversion
        ADC2->ISR  = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
        ADC2->CR |= ADC_CR_ADSTART;
        slowBusy = true;
    }

    float vbus() {
        return slow[0].valid ? slow[0].filtered * VBUS_DIVIDER : 0.0f;
    }

    float slowPinVolts(int i) {
        return (i >= 0 && i < 3 && slow[i].valid) ? slow[i].filtered : NAN;
    }

    float temperature(int n) {
        const Slow& s = slow[n == 0 ? 1 : 2];
        if (!s.valid) return NAN;
        return ntcCelsius(s.filtered / ADC_VREF);
    }

    void setFanDuty(uint8_t duty) {
        uint32_t arr = TIM4->ARR + 1;
        TIM4->CCR1   = (uint32_t)duty * arr / 255u;
    }

    void watchdogStart() {
        // LSI ~32 kHz / 32 = 1 kHz: reload = WATCHDOG_MS.
        IWDG->KR  = 0xCCCC;   // start
        IWDG->KR  = 0x5555;   // unlock PR/RLR
        IWDG->PR  = 3;        // /32
        IWDG->RLR = WATCHDOG_MS;
        uint32_t t0 = millis();
        while (IWDG->SR && millis() - t0 < 50) {}
        IWDG->KR = 0xAAAA;
    }

    void watchdogKick() {
        IWDG->KR = 0xAAAA;
    }
}
