// Copyright (c) 2026 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#include "cal.h"

#include <Arduino.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

namespace cal {
    namespace {
        volatile bool s_reading = false;   // load() is reading the record: an ECC NMI is expected there
        volatile bool s_ecc_hit = false;   // ... and it happened

        uint32_t crc32(const void* data, size_t n) {
            const uint8_t* p   = static_cast<const uint8_t*>(data);
            uint32_t       crc = 0xFFFFFFFFu;
            while (n--) {
                crc ^= *p++;
                for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
            }
            return ~crc;
        }

        bool plausible(const Record& r) {
            for (int k = 0; k < 2; k++)
                for (int p = 0; p < 3; p++)
                    if (!(r.gain[k][p] > 0.5f && r.gain[k][p] < 2.0f)) return false;
            return r.r > 0.05f && r.r < 2.0f && r.l > 1e-6f && r.l < 1e-3f && r.flux > 1e-3f && r.flux < 0.05f;
        }

        bool dualBank() {
            return (FLASH->OPTR & FLASH_OPTR_DBANK) != 0;
        }

        // The page holding ADDR: 2 KB pages per bank (dual bank) or 4 KB pages (single bank).  In dual
        // bank mode bank 2 starts at 0x08040000 on these parts whatever the flash size: the HAL's
        // FLASH_BANK_SIZE (size / 2) would put it at 0x08010000 on a 128 KB G474, and erasing "bank 2
        // page 31" left board 1's record alone; erasing bank 1 page 63 cleared it with the image intact.
        void pageOf(uint32_t* bank, uint32_t* page) {
            constexpr uint32_t BANK2_OFFSET = 0x40000;
            uint32_t           off          = ADDR - FLASH_BASE;
            if (dualBank()) {
                *bank = off >= BANK2_OFFSET ? FLASH_BANK_2 : FLASH_BANK_1;
                *page = (off % BANK2_OFFSET) / 2048;
            } else {
                *bank = FLASH_BANK_1;
                *page = off / 4096;
            }
        }

        bool erased() {
            for (size_t i = 0; i < sizeof(Record) / 4; i++)
                if (reinterpret_cast<const volatile uint32_t*>(ADDR)[i] != 0xFFFFFFFFu) return false;
            return true;
        }

        void flushDataCache() {
            __HAL_FLASH_DATA_CACHE_DISABLE();
            __HAL_FLASH_DATA_CACHE_RESET();
            __HAL_FLASH_DATA_CACHE_ENABLE();
        }

        bool erasePage(const char** why) {
            FLASH_EraseInitTypeDef e   = {};
            uint32_t               err = 0xFFFFFFFFu;
            e.TypeErase                = FLASH_TYPEERASE_PAGES;
            e.NbPages                  = 1;
            pageOf(&e.Banks, &e.Page);
            HAL_FLASH_Unlock();
            __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
            HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e, &err);
            HAL_FLASH_Lock();
            flushDataCache();
            if (st != HAL_OK) {
                *why = "page erase failed";
                return false;
            }
            if (!erased()) {
                *why = "page not blank after the erase";
                return false;
            }
            return true;
        }
    }

    namespace {
        // Runs from RAM with interrupts off: nothing here may touch the flash it erases.  Mass erase
        // (both banks), write OPTR, reload the option bytes (a system reset).  ~50 ms, inside the
        // 250 ms watchdog.
        __attribute__((section(".RamFunc"), noinline, long_call)) void ramWipeAndSetOptr(uint32_t optr) {
            while (FLASH->SR & FLASH_SR_BSY) {
            }
            FLASH->CR |= FLASH_CR_MER1 | FLASH_CR_MER2;
            FLASH->CR |= FLASH_CR_STRT;
            while (FLASH->SR & FLASH_SR_BSY) {
            }
            FLASH->CR &= ~(FLASH_CR_MER1 | FLASH_CR_MER2);
            FLASH->OPTR = optr;
            FLASH->CR |= FLASH_CR_OPTSTRT;
            while (FLASH->SR & FLASH_SR_BSY) {
            }
            FLASH->CR |= FLASH_CR_OBL_LAUNCH;
            for (;;) {
            }
        }
    }

    bool dualBankMode() {
        return dualBank();
    }

    bool setBankModeAndWipe(bool dual, const char** why) {
        uint32_t optr = FLASH->OPTR;
        uint32_t want = dual ? (optr | FLASH_OPTR_DBANK) : (optr & ~FLASH_OPTR_DBANK);
        if (want == optr) {
            *why = "already in that mode";
            return false;
        }
        if (HAL_FLASH_Unlock() != HAL_OK || HAL_FLASH_OB_Unlock() != HAL_OK) {
            HAL_FLASH_Lock();
            *why = "flash / option byte unlock failed";
            return false;
        }
        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
        TIM1->BDTR &= ~TIM_BDTR_MOE;   // bridges off for good measure (the caller checked the drive is off)
        TIM8->BDTR &= ~TIM_BDTR_MOE;
        __disable_irq();
        ramWipeAndSetOptr(want);   // does not return
        return true;
    }

    bool load(Record* out) {
        s_ecc_hit = false;
        s_reading = true;
        memcpy(out, reinterpret_cast<const void*>(ADDR), sizeof(Record));   // a damaged word raises an NMI here
        s_reading = false;
        if (s_ecc_hit) return false;
        if (out->magic != MAGIC || out->version != VERSION) return false;
        if (crc32(out, offsetof(Record, crc)) != out->crc) return false;
        return plausible(*out);
    }

    bool save(const Record& in, const char** why) {
        Record r  = in;
        r.magic   = MAGIC;
        r.version = VERSION;
        r.crc     = crc32(&r, offsetof(Record, crc));
        if (!plausible(r)) {
            *why = "values out of range";
            return false;
        }
        if (!erasePage(why)) return false;
        HAL_FLASH_Unlock();
        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
        bool ok = true;
        for (size_t i = 0; i < sizeof(Record) / 8 && ok; i++) {
            uint64_t dw;
            memcpy(&dw, reinterpret_cast<const uint8_t*>(&r) + 8 * i, 8);
            ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, ADDR + 8 * i, dw) == HAL_OK;
        }
        HAL_FLASH_Lock();
        flushDataCache();
        if (!ok) {
            *why = "programming failed";
            return false;
        }
        Record back;
        if (!load(&back) || memcmp(&back, &r, sizeof(Record)) != 0) {
            *why = "read-back mismatch";
            return false;
        }
        return true;
    }

    bool clear(const char** why) {
        return erasePage(why);
    }

    uint32_t crc32Range(uint32_t addr, uint32_t len) {
        return crc32(reinterpret_cast<const void*>(addr), len);
    }

    void erasePageOf(uint32_t* bank, uint32_t* page) {
        pageOf(bank, page);
    }

    const char* bankMode() {
        return dualBank() ? "dual bank, 2 KB pages" : "single bank, 4 KB pages";
    }
}

// A double ECC error while load() reads the record (power lost while it was being programmed) is
// expected: note it and let load() report no record.  Any other NMI (an ECC error elsewhere, the clock
// security system): bridges off and keep the record for the next boot's report, as the hard fault
// handler does; the watchdog then resets the chip.
extern "C" void NMI_Handler(void) {
    if ((FLASH->ECCR & FLASH_ECCR_ECCD) && cal::s_reading) {
        FLASH->ECCR |= FLASH_ECCR_ECCD;   // write 1 to clear
        cal::s_ecc_hit = true;
        return;
    }
    TIM1->BDTR &= ~TIM_BDTR_MOE;
    TIM8->BDTR &= ~TIM_BDTR_MOE;
    TAMP->BKP3R = 0xFA170000u;   // the fault record crumbsReport() prints at the next boot
    TAMP->BKP4R = 0x204E4D49u;   // "NMI " in place of the pc
    TAMP->BKP5R = 0;
    TAMP->BKP6R = FLASH->ECCR;
    TAMP->BKP7R = RCC->CIFR;
    for (;;) {
    }
}
