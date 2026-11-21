// Copyright (c) 2026 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

// CPU fault report.  This MCU has no console but the link to the XY board, so a hard fault (and the
// bus/usage/memory faults, which escalate to it) turns both bridges off, then writes one ERR line
// with the stacked registers straight into USART2 by polling - the interrupt-driven Serial cannot
// run inside the fault handler.  The XY board copies it into its log.  Afterwards the CPU waits:
// the independent watchdog resets it once it is running, and before that (during init)
// $Spindle/G474=RESET does.

#include <Arduino.h>

namespace {
    void putc_polled(char c) {
        uint32_t t0 = DWT->CYCCNT;
        while (!(USART2->ISR & USART_ISR_TXE_TXFNF)) {
            if (DWT->CYCCNT - t0 > 170000) return;   // ~1 ms at 170 MHz: never hang on a dead UART
        }
        USART2->TDR = (uint8_t)c;
    }

    void puts_polled(const char* s) {
        while (*s) putc_polled(*s++);
    }

    void puthex(uint32_t v) {
        static const char digits[] = "0123456789abcdef";
        for (int shift = 28; shift >= 0; shift -= 4) putc_polled(digits[(v >> shift) & 0xF]);
    }

    void field(const char* name, uint32_t v) {
        puts_polled(name);
        puthex(v);
    }
}

// frame = the exception stack frame: r0, r1, r2, r3, r12, lr, pc, xpsr.
extern "C" void spindleFaultReport(const uint32_t* frame, uint32_t exc_return) {
    // Outputs off first: clearing MOE puts every gate input in its idle (off) state.
    TIM1->BDTR &= ~TIM_BDTR_MOE;
    TIM8->BDTR &= ~TIM_BDTR_MOE;
    // Kept across the watchdog reset that follows; the next boot reports it.
    TAMP->BKP3R = 0xFA170000u;
    TAMP->BKP4R = frame[6];
    TAMP->BKP5R = frame[5];
    TAMP->BKP6R = SCB->CFSR;
    TAMP->BKP7R = SCB->HFSR;
    // Stop the interrupt-driven transmitter so it cannot interleave with this report.
    USART2->CR1 &= ~(USART_CR1_TXEIE_TXFNFIE | USART_CR1_TCIE);

    puts_polled("\nERR:spindle CPU fault - motors off;");
    field(" pc=", frame[6]);
    field(" lr=", frame[5]);
    field(" psr=", frame[7]);
    field(" cfsr=", SCB->CFSR);
    field(" hfsr=", SCB->HFSR);
    field(" mmfar=", SCB->MMFAR);
    field(" bfar=", SCB->BFAR);
    field(" exc=", exc_return);
    field(" r0=", frame[0]);
    field(" r1=", frame[1]);
    puts_polled(" (addr2line -e firmware.elf <pc>)\n");
    for (;;) {
    }
}

extern "C" __attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile(
        "tst lr, #4      \n"
        "ite eq          \n"
        "mrseq r0, msp   \n"
        "mrsne r0, psp   \n"
        "mov r1, lr      \n"
        "b spindleFaultReport \n");
}
