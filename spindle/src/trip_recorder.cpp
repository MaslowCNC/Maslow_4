#include "trip_recorder.h"
#include <stdarg.h>

// 1000 samples x ~2 ms housekeeping period = ~2 s of history (~70 KB of RAM).
static constexpr int TRIP_REC_SAMPLES = 1000;
// Samples kept after the trigger, so the decay after the motors are stopped is visible too.
static constexpr int TRIP_REC_POST_SAMPLES = 50;

enum RecState : uint8_t { REC_ARMED, REC_POST_TRIGGER, REC_FROZEN };

static TripSample s_buf[TRIP_REC_SAMPLES];
static int        s_head  = 0;  // next slot to write
static int        s_count = 0;  // valid samples in the buffer
static RecState   s_state = REC_ARMED;
static int        s_post_remaining = 0;
static uint32_t   s_trigger_ms = 0;
static char       s_reason[96] = "";

// Dump progress: negative = header lines pending (-HEADER_LINES..-1), 0..s_count-1 = next sample
// line, s_count = footer pending.
static constexpr int HEADER_LINES = 4;
static bool s_dumping   = false;
static int  s_dump_line = 0;

void tripRecorderPush(const TripSample& s) {
    if (s_state == REC_FROZEN) return;

    s_buf[s_head] = s;
    s_head        = (s_head + 1) % TRIP_REC_SAMPLES;
    if (s_count < TRIP_REC_SAMPLES) s_count++;

    if (s_state == REC_POST_TRIGGER && --s_post_remaining <= 0) {
        s_state     = REC_FROZEN;
        s_dumping   = true;
        s_dump_line = -HEADER_LINES;
    }
}

// Print a short status line now, without blocking (dropped if the USB TX buffer is full).
static void notice(const char* fmt, ...) {
    if (!Serial) return;
    char    line[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
    if (Serial.availableForWrite() >= n) Serial.write(reinterpret_cast<const uint8_t*>(line), n);
}

void tripRecorderTrigger(const char* reason) {
    uint32_t now = millis();
    if (s_state != REC_ARMED) {
        // Say so, rather than silently keeping the older recording.
        notice("[TRIP] NOT recorded (%s at %lu.%03lus): still holding the trip from %lu.%03lus - "
               "send TRIP to print it, REARM to clear\n",
               reason ? reason : "", (unsigned long)(now / 1000), (unsigned long)(now % 1000),
               (unsigned long)(s_trigger_ms / 1000), (unsigned long)(s_trigger_ms % 1000));
        return;
    }
    notice("[TRIP] captured: %s at %lu.%03lus uptime - recording prints in ~0.1 s\n",
           reason ? reason : "", (unsigned long)(now / 1000), (unsigned long)(now % 1000));
    s_state          = REC_POST_TRIGGER;
    s_post_remaining = TRIP_REC_POST_SAMPLES;
    s_trigger_ms     = now;
    strncpy(s_reason, reason ? reason : "", sizeof(s_reason) - 1);
    s_reason[sizeof(s_reason) - 1] = '\0';
}

void tripRecorderArm() {
    s_state   = REC_ARMED;
    s_head    = 0;
    s_count   = 0;
    s_dumping = false;
}

void tripRecorderRequestDump() {
    if (s_state != REC_FROZEN) {
        if (Serial) Serial.println(F("[TRIP] nothing captured yet (recorder armed)"));
        return;
    }
    s_dumping   = true;
    s_dump_line = -HEADER_LINES;
}

// Write a line only if it fits in the USB TX FIFO right now, so the housekeeping task never
// blocks.  Returns false (line not sent, retry next pass) if it does not fit.  The USB CDC TX ring
// buffer is only 256 bytes, so every line written here must stay well under that or it can never
// fit and the dump stalls forever - hence the header is split over several short lines.
static bool tryWrite(const char* line, int n) {
    if (n <= 0) return true;
    if (Serial.availableForWrite() < n) return false;
    Serial.write(reinterpret_cast<const uint8_t*>(line), n);
    return true;
}

void tripRecorderService() {
    if (!s_dumping || !Serial) return;

    char line[200];
    // A handful of lines per pass keeps each pass short; the whole dump takes a few seconds.
    for (int budget = 8; budget > 0 && s_dumping; budget--) {
        int n;
        if (s_dump_line == -4) {
            n = snprintf(line, sizeof(line), "\n[TRIP] ===== trip recording: %s (at %lu.%03lus uptime) =====\n",
                         s_reason, (unsigned long)(s_trigger_ms / 1000), (unsigned long)(s_trigger_ms % 1000));
        } else if (s_dump_line == -3) {
            n = snprintf(line, sizeof(line),
                         "[TRIP] %d samples, t_ms is relative to the trigger.  TRIP reprints, REARM clears.\n",
                         s_count);
        } else if (s_dump_line == -2) {
            n = snprintf(line, sizeof(line),
                         "TRIPCSV,t_ms,foc_loops,foc_max_us,z_phase_deg,z_target_deg,"
                         "m1_en,m1_rpm,m1_vlim,m1_ia,m1_ib,m1_ic,m1_prot,m1_nf_edges,m1_nf_low,");
        } else if (s_dump_line == -1) {
            n = snprintf(line, sizeof(line),
                         "m2_en,m2_rpm,m2_vlim,m2_ia,m2_ib,m2_ic,m2_prot,m2_nf_edges,m2_nf_low\n");
        } else if (s_dump_line < s_count) {
            int               oldest = (s_head - s_count + TRIP_REC_SAMPLES) % TRIP_REC_SAMPLES;
            const TripSample& s      = s_buf[(oldest + s_dump_line) % TRIP_REC_SAMPLES];
            const TripMotorSample& a = s.m[0];
            const TripMotorSample& b = s.m[1];
            n = snprintf(line, sizeof(line),
                         "TRIPCSV,%ld,%u,%u,%.2f,%.2f,"
                         "%u,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%u,%u,"
                         "%u,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%u,%u\n",
                         (long)(s.t_ms - s_trigger_ms), (unsigned)s.foc_loops, (unsigned)s.foc_max_us,
                         s.phase_cur * 180.0f / PI, s.phase_tgt * 180.0f / PI,
                         (unsigned)a.enabled, a.rpm, a.vlim, a.ia, a.ib, a.ic, a.prot,
                         (unsigned)a.nf_edges, (unsigned)a.nf_low,
                         (unsigned)b.enabled, b.rpm, b.vlim, b.ia, b.ib, b.ic, b.prot,
                         (unsigned)b.nf_edges, (unsigned)b.nf_low);
        } else {
            n = snprintf(line, sizeof(line), "[TRIP] ===== end of trip recording =====\n");
        }

        if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
        if (!tryWrite(line, n)) return;  // FIFO full - resume on the next pass

        if (s_dump_line >= s_count) {
            s_dumping = false;
        } else {
            s_dump_line++;
        }
    }
}
