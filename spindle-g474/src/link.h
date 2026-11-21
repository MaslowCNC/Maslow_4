#pragma once
// The inter-board link to the XY ESP32-S3 (USART2, PA2 TX / PA3 RX, 115200 8N1).  It is the only
// console this board has (no USB): events go out as "MSG:", "WARN:" and "ERR:" lines, which the XY
// board copies into its own log.

#include <Arduino.h>

extern HardwareSerial Link;

// Send "<level>:<text>\n" if the whole line fits in the TX buffer now; dropped otherwise (never
// blocks).  level is "MSG", "WARN" or "ERR", as the ESP32 spindle firmware.
void linkEvent(const char* level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// Send a raw line (with its newline) if it fits.
bool linkWriteIfRoom(const char* line, int n);

// Boot trace (builds with -D BOOT_TRACE, i.e. g474-bench): one "MSG:boot: <stage>" line per init
// step, each sent out completely before the next step starts, so the last line the XY board logs
// names the step a hang or fault happened in.  Compiles to nothing otherwise.
void bootTrace(const char* stage);
#ifdef BOOT_TRACE
#define BOOT_TRACE_POINT(stage) bootTrace(stage)
#else
#define BOOT_TRACE_POINT(stage) ((void)0)
#endif
