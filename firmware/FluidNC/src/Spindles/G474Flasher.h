// Copyright (c) 2026 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
    Flashes the 36 V Unified Control Board's spindle MCU (STM32G474) from a .bin file on the XY
    board's filesystem, through the G474's ROM bootloader on the inter-board link (USART2, the
    same wires the spindle protocol uses).  ST application note AN3155 describes the protocol.

    GPIO2 (SPINDLE_BOOT_X, 1k into BOOT0) and GPIO1 (SPINDLE_RST_X, 1k into NRST) put the G474 in
    its bootloader; the link switches to 8E1 for the transfer and back to 8N1 after.  The flash is
    mass-erased, written in 256-byte blocks from 0x08000000, read back and compared, then the G474
    is reset with BOOT0 low so the new firmware starts.

    Non-blocking: SpindleBoard::update() calls service() every pass until it returns false, so the
    Maslow loop keeps running (an erase takes up to a few seconds, a 90 KB image ~20 s).  The
    object, with its 256-byte block buffer and the open file, exists only while flashing.
*/

#include "../Uart.h"

class FileStream;

namespace Spindles {
    class G474Flasher {
    public:
        G474Flasher(Uart* uart, const char* path);
        ~G474Flasher();

        // Advance the transfer.  Returns true while it is still running.
        bool service();

        bool        succeeded() const { return _state == State::Done; }
        const char* error() const { return _error; }

        // Reset the spindle MCU into its application (BOOT0 low, NRST pulsed).  Blocks ~20 ms.
        static void resetToApplication();

        static constexpr uint32_t FLASH_BASE = 0x08000000;
        static constexpr size_t   FLASH_SIZE = 128 * 1024;   // STM32G474RB

    private:
        enum class State : uint8_t {
            Open,
            ResetHold,
            BootWait,
            Sync,
            GetId,
            EraseCmd,
            EraseArg,
            WriteCmd,
            WriteAddr,
            WriteData,
            VerifyCmd,
            VerifyAddr,
            VerifyLen,
            VerifyData,
            Finish,
            Done,
            Failed,
        };

        void fail(const char* why);
        void restartApplication();
        void sendCommand(uint8_t cmd);
        void sendAddress(uint32_t addr);
        void expect(size_t n, uint32_t timeout_ms);   // wait for n bytes in _rx
        int  poll();   // 1 = expected bytes arrived, 0 = still waiting, -1 = timeout
        bool readBlock();   // next block of the file into _buf; false at the end
        void progress();

        Uart*       _uart;
        FileStream* _file = nullptr;
        char        _path[96];
        State       _state = State::Open;
        const char* _error = "";

        uint32_t _t0       = 0;   // state / wait start
        uint32_t _deadline = 0;
        size_t   _want     = 0;
        size_t   _have     = 0;
        uint8_t  _rx[8];
        int      _tries    = 0;

        size_t   _size     = 0;
        size_t   _offset   = 0;   // of the block in _buf
        size_t   _block    = 0;   // bytes in _buf (padded to 8)
        size_t   _verified = 0;   // bytes of the current read-back block compared so far
        int      _last_pct = -1;
        bool     _verifying = false;
        uint8_t  _buf[256];
    };
}
