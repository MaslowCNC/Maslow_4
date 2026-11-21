// Copyright (c) 2026 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

// Board calibration kept in the last 2 KB of the G474's flash: both motors' per-phase current gains
// (#GCAL) and the observer constants (#OBS).  Loaded at boot; written only on request (#GSAVE), with
// the drive off (a flash erase stalls the CPU).  The XY board's flasher reads this record before its
// mass erase and writes it back after the new image (firmware/.../Spindles/G474Flasher.cpp), so a
// firmware update keeps it.  The board file reserves the last 4 KB so the image never grows into it.
#pragma once

#include <stdint.h>

namespace cal {
    constexpr uint32_t ADDR    = 0x0801F800;   // last 2 KB of the 128 KB flash
    constexpr uint32_t MAGIC   = 0x4C41434D;   // "MCAL" in memory order
    constexpr uint32_t VERSION = 1;

    // 48 bytes: six 64-bit double words, the flash's programming unit.  Keep the layout: the XY
    // board's flasher copies it byte for byte and checks only the magic.
    struct Record {
        uint32_t magic;
        uint32_t version;
        float    gain[2][3];   // per-phase current gains, motor 1 a b c, motor 2 a b c
        float    r, l, flux;   // observer constants (ohm, H, Wb)
        uint32_t crc;          // CRC-32 of everything above
    };
    static_assert(sizeof(Record) == 48, "the XY flasher copies 48 bytes");

    // A valid stored record (magic, version, CRC and plausible values), else false.  A damaged
    // double word (power lost while programming) reads as no record, not a fault.
    bool load(Record* out);

    // Erase the page, program the record, read it back.  why names the step that failed.
    bool save(const Record& rec, const char** why);

    // Erase the page (nominal gains after the next reset).
    bool clear(const char** why);

    // "dual bank, 2 KB pages" or "single bank, 4 KB pages" (FLASH_OPTR.DBANK), for the log.
    const char* bankMode();
}
