#pragma once

#include <Arduino.h>
#include "hal/usb_serial_jtag_ll.h"

// Non-blocking USB console write for code that must never stall: write the whole line only if it
// fits in the 256-byte USB Serial/JTAG TX ring buffer, otherwise write nothing and return false
// (the caller retries later or drops the line).
//
// When it does not fit, also re-enable the TX-empty interrupt, exactly as HWCDC::write() does
// after queueing data.  Output has been seen to sit in the ring buffer while the motors run until
// some other write happened (a trip recording that printed only after the motors were restarted;
// LOADREF lines that never appeared during the sweep).  Writes that skip themselves for lack of
// room never call HWCDC::write(), so nothing would otherwise restart the drain.  Harmless if the
// interrupt is already enabled or there is nothing queued.
inline bool usbWriteIfRoom(const char* s, int n) {
    if (n <= 0) return true;
    if (!Serial) return false;
    if (Serial.availableForWrite() >= n) {
        Serial.write(reinterpret_cast<const uint8_t*>(s), n);
        return true;
    }
    usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
    return false;
}
