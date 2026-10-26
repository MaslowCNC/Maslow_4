#include "trip_recorder.h"
#include "usb_console.h"
#include <stdarg.h>

// 1000 samples x ~2 ms housekeeping period = ~2 s of history (~96 KB of RAM, ~120 KB in the
// sync-sense build with the observer columns).
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

static bool s_arm_pending = false;  // re-arm requested while a dump was printing
static bool s_is_test     = false;  // the held recording came from a test trigger, not a fault

// Link test ("T!"): re-arm, then trigger once the buffer has refilled.  0 = none pending.
static uint32_t s_test_due_ms = 0;
static constexpr uint32_t TEST_FILL_MS = 2100;

// Dump progress: negative = header lines pending (-HEADER_LINES..-1), 0..s_count-1 = next sample
// line, s_count = footer pending.  The USB console and the inter-board link each have their own
// cursor: USB prints as fast as the host drains it, the link is paced (see LINK_LINE_INTERVAL_MS).
static constexpr int HEADER_LINES = 4;
static bool s_dumping   = false;
static int  s_dump_line = 0;

// The link copy goes to the XY board, which saves it to its flash as /spindle_trip.csv so the
// recording can be read over WiFi without the USB cable.  One line every 20 ms (~9 KB/s) stays
// under the 115200-baud link and the XY board's 256-byte UART RX buffer, and leaves room in the
// TX buffer for the status line (sendStatus wants 48 bytes free).  The whole dump takes ~21 s.
static constexpr uint32_t LINK_LINE_INTERVAL_MS = 20;
static constexpr int      LINK_STATUS_RESERVE   = 64;
static bool     s_link_dumping = false;
static int      s_link_line    = 0;
static uint32_t s_link_last_ms = 0;

void tripRecorderPush(const TripSample& s) {
    if (s_state == REC_FROZEN) return;

    s_buf[s_head] = s;
    s_head        = (s_head + 1) % TRIP_REC_SAMPLES;
    if (s_count < TRIP_REC_SAMPLES) s_count++;

    if (s_state == REC_POST_TRIGGER && --s_post_remaining <= 0) {
        s_state        = REC_FROZEN;
        s_dumping      = true;
        s_dump_line    = -HEADER_LINES;
        s_link_dumping = true;
        s_link_line    = -HEADER_LINES;
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
    usbWriteIfRoom(line, n);
}

// Link writes that must not block: dropped if the TX buffer has no room.
static void linkNotice(const char* msg) {
    int n = strlen(msg);
    if (Serial1.availableForWrite() >= n) Serial1.write(reinterpret_cast<const uint8_t*>(msg), n);
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
    s_is_test        = reason && strncmp(reason, "manual test", 11) == 0;
    s_state          = REC_POST_TRIGGER;
    s_post_remaining = TRIP_REC_POST_SAMPLES;
    s_trigger_ms     = now;
    strncpy(s_reason, reason ? reason : "", sizeof(s_reason) - 1);
    s_reason[sizeof(s_reason) - 1] = '\0';
}

void tripRecorderArm() {
    s_arm_pending = false;
    s_test_due_ms = 0;
    s_state   = REC_ARMED;
    s_head    = 0;
    s_count   = 0;
    s_dumping = false;
    s_link_dumping = false;
}

void tripRecorderArmOnStart() {
    if (s_dumping || s_link_dumping) {
        s_arm_pending = true;
        return;
    }
    tripRecorderArm();
}

void tripRecorderRequestDump() {
    if (s_state != REC_FROZEN) {
        if (Serial) Serial.println(F("[TRIP] nothing captured yet (recorder armed)"));
        return;
    }
    s_dumping   = true;
    s_dump_line = -HEADER_LINES;
}

void tripRecorderRequestLinkDump() {
    const char* msg = nullptr;
    if (s_state != REC_FROZEN)  msg = "MSG:no trip recording captured yet (recorder armed)\n";
    else if (s_link_dumping)    msg = "MSG:trip recording is already being sent\n";
    if (msg) {
        linkNotice(msg);
        return;
    }
    s_link_dumping = true;
    s_link_line    = -HEADER_LINES;
}

void tripRecorderLinkTest() {
    if (s_state != REC_ARMED && !s_is_test) {
        linkNotice("WARN:trip test refused - holding a real trip recording ('T' re-sends it; a spindle start clears it)\n");
        return;
    }
    if (s_dumping || s_link_dumping) {
        linkNotice("MSG:trip test refused - a recording is still being sent\n");
        return;
    }
    tripRecorderArm();
    s_test_due_ms = millis() + TEST_FILL_MS;
    if (s_test_due_ms == 0) s_test_due_ms = 1;
    linkNotice("MSG:trip test: recording 2 s, then sending it\n");
}

// Format dump line `idx` (see s_dump_line) into `line`.  For the link the two CSV header halves
// are sent as one line (-2 carries both, -1 is never asked for); the USB console has to split it
// because its TX ring buffer is only 256 bytes.  Returns the length, truncated to fit.
// The sync-sense build adds the shadow observer's columns after each motor's own.  Short names keep
// the link's one-line header under the XY board's 400-byte line buffer and each USB half under 256.
#ifdef SYNC_CURRENT_SENSE
#define OBS_COLS(m) ",m" m "_ok,m" m "_ld,m" m "_ldmx,m" m "_orpm,m" m "_slip,m" m "_sn,m" m "_sf,m" m "_slo,m" m "_soff"
#else
#define OBS_COLS(m) ""
#endif

// One motor's columns, starting with the separating comma.  Returns the length, truncated to fit.
static int formatMotor(char* line, size_t size, const TripMotorSample& m) {
    int n = snprintf(line, size, ",%u,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%u,%u,%.2f,%.2f,%.0f",
                     (unsigned)m.enabled, m.rpm, m.vlim, m.ia, m.ib, m.ic, m.prot,
                     (unsigned)m.nf_edges, (unsigned)m.nf_low, m.ip, m.iq, m.lag);
#ifdef SYNC_CURRENT_SENSE
    if (n >= 0 && n < (int)size) {
        char ld[8], mx[8], rpm[8];
        if (m.load_deg == INT16_MIN) strcpy(ld, "nan"); else snprintf(ld, sizeof(ld), "%d", m.load_deg);
        if (m.load_max == INT16_MIN) strcpy(mx, "nan"); else snprintf(mx, sizeof(mx), "%d", m.load_max);
        if (m.obs_rpm == UINT16_MAX) strcpy(rpm, "nan"); else snprintf(rpm, sizeof(rpm), "%u", (unsigned)m.obs_rpm);
        n += snprintf(line + n, size - n, ",%u,%s,%s,%s,%u,%u,%u,%u,%u", (unsigned)m.obs_ok, ld, mx, rpm,
                      (unsigned)m.slips, (unsigned)m.smp_n, (unsigned)m.smp_full, (unsigned)m.smp_lo,
                      (unsigned)m.smp_off);
    }
#endif
    if (n < 0) n = 0;
    if (n >= (int)size) n = size - 1;
    return n;
}

static int formatLine(int idx, char* line, size_t size, bool link) {
    static const char HDR1[] = "TRIPCSV,t_ms,foc_loops,foc_max_us,z_phase_deg,z_target_deg,"
                               "m1_en,m1_rpm,m1_vlim,m1_ia,m1_ib,m1_ic,m1_prot,m1_nf_edges,m1_nf_low,m1_ip,m1_iq,m1_lag"
                               OBS_COLS("1") ",";
    static const char HDR2[] = "m2_en,m2_rpm,m2_vlim,m2_ia,m2_ib,m2_ic,m2_prot,m2_nf_edges,m2_nf_low,m2_ip,m2_iq,m2_lag"
                               OBS_COLS("2") "\n";
    int n;
    if (idx == -4) {
        // The USB copy starts with a blank line to set it apart; the link has no use for one.
        n = snprintf(line, size, "%s[TRIP] ===== trip recording: %s (at %lu.%03lus uptime) =====\n",
                     link ? "" : "\n", s_reason, (unsigned long)(s_trigger_ms / 1000), (unsigned long)(s_trigger_ms % 1000));
    } else if (idx == -3) {
        n = snprintf(line, size, "[TRIP] %d samples, t_ms is relative to the trigger.  TRIP reprints, REARM clears.\n",
                     s_count);
    } else if (idx == -2) {
        n = snprintf(line, size, "%s%s", HDR1, link ? HDR2 : "");
    } else if (idx == -1) {
        n = snprintf(line, size, "%s", HDR2);
    } else if (idx < s_count) {
        int                    oldest = (s_head - s_count + TRIP_REC_SAMPLES) % TRIP_REC_SAMPLES;
        const TripSample&      s      = s_buf[(oldest + idx) % TRIP_REC_SAMPLES];
        n = snprintf(line, size, "TRIPCSV,%ld,%u,%u,%.2f,%.2f",
                     (long)(s.t_ms - s_trigger_ms), (unsigned)s.foc_loops, (unsigned)s.foc_max_us,
                     s.phase_cur * 180.0f / PI, s.phase_tgt * 180.0f / PI);
        if (n < 0) n = 0;
        if (n >= (int)size - 1) n = size - 2;   // room for the newline
        for (int m = 0; m < 2; m++) n += formatMotor(line + n, size - 1 - n, s.m[m]);
        line[n++] = '\n';
        line[n]   = '\0';
    } else {
        n = snprintf(line, size, "[TRIP] ===== end of trip recording =====\n");
    }
    if (n < 0) n = 0;
    if (n >= (int)size) n = size - 1;
    return n;
}

// Both dumps finished: carry out a re-arm that was requested while they were printing.
static void finishDump() {
    if (!s_dumping && !s_link_dumping && s_arm_pending) {
        s_arm_pending = false;
        tripRecorderArm();
    }
}

// USB copy.  Write a line only if it fits in the USB TX FIFO right now, so the housekeeping task
// never blocks; a line that does not fit is retried next pass.  With no USB host the copy is
// dropped (TRIP reprints it later), so it cannot hold up a re-arm forever.
static void serviceUsb() {
    if (!s_dumping) return;
    if (!Serial) {
        s_dumping = false;
        return;
    }
    char line[256];
    // A handful of lines per pass keeps each pass short; the whole dump takes a few seconds.
    for (int budget = 8; budget > 0 && s_dumping; budget--) {
        int n = formatLine(s_dump_line, line, sizeof(line), false);
        if (!usbWriteIfRoom(line, n)) return;  // FIFO full - resume on the next pass
        if (s_dump_line >= s_count) s_dumping = false;
        else s_dump_line++;
    }
}

// Link copy, one line per LINK_LINE_INTERVAL_MS, never blocking.
static void serviceLink() {
    if (!s_link_dumping) return;
    uint32_t now = millis();
    if (now - s_link_last_ms < LINK_LINE_INTERVAL_MS) return;
    char line[400];
    int  n = formatLine(s_link_line, line, sizeof(line), true);
    if (Serial1.availableForWrite() < n + LINK_STATUS_RESERVE) return;  // retry next pass
    Serial1.write(reinterpret_cast<const uint8_t*>(line), n);
    s_link_last_ms = now;
    if (s_link_line >= s_count) s_link_dumping = false;
    else s_link_line = (s_link_line == -2) ? 0 : s_link_line + 1;  // -1 is folded into -2
}

void tripRecorderService() {
    if (s_test_due_ms && (int32_t)(millis() - s_test_due_ms) >= 0) {
        s_test_due_ms = 0;
        tripRecorderTrigger("manual test (link T!)");
    }
    serviceUsb();
    serviceLink();
    finishDump();
}
