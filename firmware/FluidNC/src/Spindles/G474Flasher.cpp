// Copyright (c) 2026 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "G474Flasher.h"

#include "../FileStream.h"
#include "../Logging.h"
#include "../NutsBolts.h"  // delay_ms
#include "Driver/fluidnc_gpio.h"

#include <cstring>

namespace Spindles {
    namespace {
        // The XY ESP32-S3's lines into the G474 (36 V Unified Control Board, through 1k each).
        constexpr pinnum_t PIN_SPINDLE_RST  = 1;   // SPINDLE_RST_X -> NRST (10k pull-up, 100 nF)
        constexpr pinnum_t PIN_SPINDLE_BOOT = 2;   // SPINDLE_BOOT_X -> BOOT0 (10k pull-down)

        constexpr uint8_t ACK  = 0x79;
        constexpr uint8_t NACK = 0x1F;

        constexpr uint8_t CMD_GET_ID       = 0x02;
        constexpr uint8_t CMD_READ_MEMORY  = 0x11;
        constexpr uint8_t CMD_WRITE_MEMORY = 0x31;
        constexpr uint8_t CMD_EXT_ERASE    = 0x44;

        constexpr uint16_t G47X_PID = 0x469;   // STM32G47x/G48x (AN2606)

        void driveLow(pinnum_t pin) {
            gpio_write(pin, false);
            gpio_mode(pin, false, true, false, false, false);
            gpio_write(pin, false);
        }
        void driveHigh(pinnum_t pin) {
            gpio_write(pin, true);
            gpio_mode(pin, false, true, false, false, false);
            gpio_write(pin, true);
        }
        void release(pinnum_t pin) {
            gpio_mode(pin, true, false, false, false, false);   // input, no pull: the board's resistors decide
        }
    }

    G474Flasher::G474Flasher(Uart* uart, const char* path) : _uart(uart) {
        strncpy(_path, path, sizeof(_path) - 1);
        _path[sizeof(_path) - 1] = '\0';
    }

    G474Flasher::~G474Flasher() {
        delete _file;
    }

    void G474Flasher::resetToApplication() {
        driveLow(PIN_SPINDLE_BOOT);
        driveLow(PIN_SPINDLE_RST);
        delay_ms(20);
        release(PIN_SPINDLE_RST);
        delay_ms(5);   // BOOT0 is sampled as NRST rises; keep it driven low until then
        release(PIN_SPINDLE_BOOT);
    }

    void G474Flasher::fail(const char* why) {
        _error = why;
        _state = State::Failed;
        // Let whatever is in the flash run (BOOT0 low).  After a failed write that is nothing
        // useful, and $Spindle/G474=FLASH can simply be run again: the ROM bootloader is always there.
        restartApplication();
    }

    // The link back in its normal mode (8N1) and its RX empty BEFORE the G474 leaves reset: its first
    // line (the reset reason) starts a few ms after NRST rises, and read in 8E1 it is garbage.
    void G474Flasher::restartApplication() {
        _uart->restoreMode();
        _uart->flushRx();
        resetToApplication();
    }

    // The calibration could not be read before the erase: it will be lost, but the update goes on - a
    // stuck flash would leave SWD as the only way in (e.g. after a flash-mode change scrambles the
    // old contents).
    void G474Flasher::skipCalibration(const char* why) {
        log_warn("Spindle flash: could not read the stored calibration (" << why
                                                                          << "); flashing without keeping it - run #GCAL and #GSAVE after");
        _cal_present = false;
        sendCommand(CMD_EXT_ERASE);
        expect(1, 1000);
        _state = State::EraseCmd;
    }

    void G474Flasher::sendCommand(uint8_t cmd) {
        uint8_t b[2] = { cmd, (uint8_t)~cmd };
        _uart->flushRx();
        _uart->write(b, 2);
    }

    void G474Flasher::sendAddress(uint32_t addr) {
        uint8_t b[5] = { (uint8_t)(addr >> 24), (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr, 0 };
        b[4]         = b[0] ^ b[1] ^ b[2] ^ b[3];
        _uart->write(b, 5);
    }

    void G474Flasher::expect(size_t n, uint32_t timeout_ms) {
        _want     = n;
        _have     = 0;
        _deadline = millis() + timeout_ms;
    }

    int G474Flasher::poll() {
        while (_have < _want && _uart->available()) {
            int c = _uart->read();
            if (c < 0) {
                break;
            }
            _rx[_have++] = (uint8_t)c;
        }
        if (_have >= _want) {
            return 1;
        }
        return (int32_t)(millis() - _deadline) > 0 ? -1 : 0;
    }

    bool G474Flasher::readBlock() {
        _offset += _block;
        _block = 0;
        if (_offset >= _size) {
            return false;
        }
        size_t n = _size - _offset;
        if (n > sizeof(_buf)) {
            n = sizeof(_buf);
        }
        int got = _file->read(_buf, n);
        if (got != (int)n) {
            fail("could not read the firmware file");
            return false;
        }
        // The G4 programs 64-bit double words: pad the last block with erased bytes.
        size_t padded = (n + 7) & ~size_t(7);
        memset(_buf + n, 0xFF, padded - n);
        _block = padded;
        return true;
    }

    void G474Flasher::progress() {
        int pct = (int)((_offset + _block) * 100 / (_size ? _size : 1));
        if (pct / 10 != _last_pct / 10) {
            _last_pct = pct;
            log_info("Spindle flash: " << (_verifying ? "verified " : "written ") << pct << "%");
        }
    }

    bool G474Flasher::service() {
        uint32_t now = millis();
        int      r;
        switch (_state) {
            case State::Open:
                try {
                    _file = new FileStream(_path, "r", LocalFS);
                } catch (...) {
                    _file = nullptr;
                }
                if (!_file) {
                    fail("cannot open the firmware file");
                    break;
                }
                _size = _file->size();
                if (_size < 1024 || _size > IMAGE_MAX) {
                    fail("file size is not that of a G474 firmware image (1 KB .. 124 KB .bin; the last 4 KB hold "
                         "the calibration)");
                    break;
                }
                log_info("Spindle flash: " << _path << ", " << _size << " bytes; resetting the spindle MCU into its bootloader");
                driveHigh(PIN_SPINDLE_BOOT);
                driveLow(PIN_SPINDLE_RST);
                _t0    = now;
                _state = State::ResetHold;
                break;

            case State::ResetHold:
                if (now - _t0 >= 20) {
                    release(PIN_SPINDLE_RST);
                    _t0    = now;
                    _state = State::BootWait;
                }
                break;

            case State::BootWait:
                if (now - _t0 >= 150) {
                    // BOOT0 has been sampled; let its pull-down take it again so a later reset runs the app.
                    release(PIN_SPINDLE_BOOT);
                    _uart->changeMode(115200, UartData::Bits8, UartParity::Even, UartStop::Bits1);
                    _uart->flushRx();
                    _tries = 0;
                    uint8_t sync = 0x7F;
                    _uart->write(&sync, 1);
                    expect(1, 100);
                    _state = State::Sync;
                }
                break;

            case State::Sync:
                r = poll();
                if (r > 0 && (_rx[0] == ACK || _rx[0] == NACK)) {   // NACK: it was already synchronised
                    sendCommand(CMD_GET_ID);
                    expect(5, 500);
                    _state = State::GetId;
                } else if (r != 0) {
                    if (++_tries >= 10) {
                        fail("no answer from the spindle MCU's bootloader (check SPINDLE_RST_X/BOOT_X on GPIO1/2)");
                        break;
                    }
                    _uart->flushRx();
                    uint8_t sync = 0x7F;
                    _uart->write(&sync, 1);
                    expect(1, 100);
                }
                break;

            case State::GetId:
                r = poll();
                if (r < 0) {
                    fail("bootloader did not answer Get ID");
                } else if (r > 0) {
                    uint16_t pid = ((uint16_t)_rx[2] << 8) | _rx[3];
                    if (_rx[0] != ACK || _rx[1] != 1 || _rx[4] != ACK) {
                        fail("bad Get ID reply from the bootloader");
                    } else if (pid != G47X_PID) {
                        fail("the spindle MCU is not an STM32G47x (product ID mismatch)");
                    } else {
                        sendCommand(CMD_READ_MEMORY);   // the calibration record first
                        expect(1, 1000);
                        _state = State::CalReadCmd;
                    }
                }
                break;

            case State::CalReadCmd:
            case State::CalCheckCmd:
                r = poll();
                if (r < 0 || (r > 0 && _rx[0] != ACK)) {
                    if (_state == State::CalReadCmd) {
                        skipCalibration("Read Memory refused");
                    } else {
                        fail("Read Memory refused (read-out protection set? recover with SWD)");
                    }
                } else if (r > 0) {
                    sendAddress(CAL_ADDR);
                    expect(1, 1000);
                    _state = _state == State::CalReadCmd ? State::CalReadAddr : State::CalCheckAddr;
                }
                break;

            case State::CalReadAddr:
            case State::CalCheckAddr:
                r = poll();
                if (r < 0 || (r > 0 && _rx[0] != ACK)) {
                    if (_state == State::CalReadAddr) {
                        skipCalibration("read address refused");
                    } else {
                        fail("calibration read address refused");
                    }
                } else if (r > 0) {
                    uint8_t len[2] = { (uint8_t)(CAL_LEN - 1), (uint8_t)~(uint8_t)(CAL_LEN - 1) };
                    _uart->write(len, 2);
                    expect(1, 1000);
                    _state = _state == State::CalReadAddr ? State::CalReadLen : State::CalCheckLen;
                }
                break;

            case State::CalReadLen:
            case State::CalCheckLen:
                r = poll();
                if (r < 0 || (r > 0 && _rx[0] != ACK)) {
                    if (_state == State::CalReadLen) {
                        skipCalibration("read length refused");
                    } else {
                        fail("calibration read length refused");
                    }
                } else if (r > 0) {
                    _cal_have = 0;
                    _deadline = millis() + 1000;
                    _state    = _state == State::CalReadLen ? State::CalReadData : State::CalCheckData;
                }
                break;

            case State::CalReadData:
                while (_cal_have < CAL_LEN && _uart->available()) {
                    int c = _uart->read();
                    if (c < 0) {
                        break;
                    }
                    _cal[_cal_have++] = (uint8_t)c;
                }
                if (_cal_have >= CAL_LEN) {
                    uint32_t magic = (uint32_t)_cal[0] | ((uint32_t)_cal[1] << 8) | ((uint32_t)_cal[2] << 16) |
                                     ((uint32_t)_cal[3] << 24);
                    _cal_present = magic == CAL_MAGIC;
                    log_info("Spindle flash: " << (_cal_present ? "stored calibration found; it is kept across the erase"
                                                                : "no stored calibration to keep"));
                    sendCommand(CMD_EXT_ERASE);
                    expect(1, 1000);
                    _state = State::EraseCmd;
                } else if ((int32_t)(millis() - _deadline) > 0) {
                    skipCalibration("read timed out");
                }
                break;

            case State::CalWriteCmd:
                r = poll();
                if (r < 0 || (r > 0 && _rx[0] != ACK)) {
                    fail("image written and verified, but Write Memory was refused for the calibration - run #GCAL and "
                         "#GSAVE again");
                } else if (r > 0) {
                    sendAddress(CAL_ADDR);
                    expect(1, 1000);
                    _state = State::CalWriteAddr;
                }
                break;

            case State::CalWriteAddr:
                r = poll();
                if (r < 0 || (r > 0 && _rx[0] != ACK)) {
                    fail("calibration write address refused");
                } else if (r > 0) {
                    uint8_t n1 = (uint8_t)(CAL_LEN - 1);
                    uint8_t cs = n1;
                    for (size_t i = 0; i < CAL_LEN; i++) {
                        cs ^= _cal[i];
                    }
                    _uart->write(&n1, 1);
                    _uart->write(_cal, CAL_LEN);
                    _uart->write(&cs, 1);
                    expect(1, 2000);
                    _state = State::CalWriteData;
                }
                break;

            case State::CalWriteData:
                r = poll();
                if (r < 0 || (r > 0 && _rx[0] != ACK)) {
                    fail("image written and verified, but the calibration write-back failed - run #GCAL and #GSAVE again");
                } else if (r > 0) {
                    sendCommand(CMD_READ_MEMORY);   // read it back
                    expect(1, 1000);
                    _state = State::CalCheckCmd;
                }
                break;

            case State::CalCheckData: {
                bool mismatch = false;
                while (_cal_have < CAL_LEN && _uart->available()) {
                    int c = _uart->read();
                    if (c < 0) {
                        break;
                    }
                    if ((uint8_t)c != _cal[_cal_have]) {
                        mismatch = true;
                    }
                    _cal_have++;
                }
                if (mismatch) {
                    fail("image written and verified, but the calibration did not read back - run #GCAL and #GSAVE again");
                } else if (_cal_have >= CAL_LEN) {
                    _state = State::Finish;
                } else if ((int32_t)(millis() - _deadline) > 0) {
                    fail("calibration read-back timed out");
                }
                break;
            }

            case State::EraseCmd:
                r = poll();
                if (r < 0 || (r > 0 && _rx[0] != ACK)) {
                    fail("erase refused (read-out protection set? recover with SWD)");
                } else if (r > 0) {
                    const uint8_t mass[3] = { 0xFF, 0xFF, 0x00 };   // global mass erase
                    _uart->write(mass, 3);
                    expect(1, 30000);
                    log_info("Spindle flash: erasing");
                    _state = State::EraseArg;
                }
                break;

            case State::EraseArg:
                r = poll();
                if (r < 0 || (r > 0 && _rx[0] != ACK)) {
                    fail("mass erase failed");
                } else if (r > 0) {
                    _offset = 0;
                    _block  = 0;
                    if (readBlock()) {
                        sendCommand(CMD_WRITE_MEMORY);
                        expect(1, 1000);
                        _state = State::WriteCmd;
                    }
                }
                break;

            case State::WriteCmd:
            case State::VerifyCmd:
                r = poll();
                if (r < 0 || (r > 0 && _rx[0] != ACK)) {
                    fail(_state == State::WriteCmd ? "Write Memory refused" : "Read Memory refused");
                } else if (r > 0) {
                    sendAddress(FLASH_BASE + _offset);
                    expect(1, 1000);
                    _state = _state == State::WriteCmd ? State::WriteAddr : State::VerifyAddr;
                }
                break;

            case State::WriteAddr:
                r = poll();
                if (r < 0 || (r > 0 && _rx[0] != ACK)) {
                    fail("write address refused");
                } else if (r > 0) {
                    uint8_t n1 = (uint8_t)(_block - 1);
                    uint8_t cs = n1;
                    for (size_t i = 0; i < _block; i++) {
                        cs ^= _buf[i];
                    }
                    _uart->write(&n1, 1);
                    _uart->write(_buf, _block);
                    _uart->write(&cs, 1);
                    expect(1, 2000);
                    _state = State::WriteData;
                }
                break;

            case State::WriteData:
                r = poll();
                if (r < 0 || (r > 0 && _rx[0] != ACK)) {
                    fail("block write failed");
                } else if (r > 0) {
                    progress();
                    if (readBlock()) {
                        sendCommand(CMD_WRITE_MEMORY);
                        expect(1, 1000);
                        _state = State::WriteCmd;
                    } else if (_state != State::Failed) {
                        // Read everything back and compare with the file.
                        _verifying = true;
                        _last_pct  = -1;
                        _file->set_position(0);
                        _offset = 0;
                        _block  = 0;
                        if (readBlock()) {
                            sendCommand(CMD_READ_MEMORY);
                            expect(1, 1000);
                            _state = State::VerifyCmd;
                        }
                    }
                }
                break;

            case State::VerifyAddr:
                r = poll();
                if (r < 0 || (r > 0 && _rx[0] != ACK)) {
                    fail("read address refused");
                } else if (r > 0) {
                    uint8_t len[2] = { (uint8_t)(_block - 1), (uint8_t)~(uint8_t)(_block - 1) };
                    _uart->write(len, 2);
                    expect(1, 1000);
                    _state = State::VerifyLen;
                }
                break;

            case State::VerifyLen:
                r = poll();
                if (r < 0 || (r > 0 && _rx[0] != ACK)) {
                    fail("read length refused");
                } else if (r > 0) {
                    _verified = 0;
                    _deadline = millis() + 1000;
                    _state    = State::VerifyData;
                }
                break;

            case State::VerifyData:
                while (_verified < _block && _uart->available()) {
                    int c = _uart->read();
                    if (c < 0) {
                        break;
                    }
                    if ((uint8_t)c != _buf[_verified]) {
                        fail("verify mismatch: the flash does not hold the file");
                        return false;
                    }
                    _verified++;
                }
                if (_verified >= _block) {
                    progress();
                    if (readBlock()) {
                        sendCommand(CMD_READ_MEMORY);
                        expect(1, 1000);
                        _state = State::VerifyCmd;
                    } else if (_state != State::Failed) {
                        if (_cal_present) {
                            sendCommand(CMD_WRITE_MEMORY);   // put the calibration back
                            expect(1, 1000);
                            _state = State::CalWriteCmd;
                        } else {
                            _state = State::Finish;
                        }
                    }
                } else if ((int32_t)(millis() - _deadline) > 0) {
                    fail("read-back timed out");
                }
                break;

            case State::Finish:
                restartApplication();
                log_info("Spindle flash: " << _size << " bytes written and verified"
                                           << (_cal_present ? ", calibration restored" : "") << "; spindle MCU restarted");
                _state = State::Done;
                return false;

            case State::Done:
            case State::Failed:
                return false;
        }
        return _state != State::Done && _state != State::Failed;
    }
}
