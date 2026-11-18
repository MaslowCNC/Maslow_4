#include "trip.h"
#include "link.h"
#include "config.h"

namespace {
    constexpr int SAMPLES = 1000;   // x 2 ms = 2 s, 56 KB
    TripSample    s_buf[SAMPLES];
    int           s_head    = 0;
    int           s_count   = 0;
    bool          s_frozen  = false;
    char          s_reason[80];
    uint32_t      s_trigger_ms = 0;

    bool     s_sending   = false;
    int      s_line      = 0;     // -3 title, -2 count, -1 header, 0.. samples, s_count = end
    uint32_t s_last_ms   = 0;
    uint32_t s_test_due  = 0;

    constexpr int LINK_RESERVE = 64;   // keep room for the status line

    int format(int idx, char* line, size_t size) {
        int n;
        if (idx == -3) {
            n = snprintf(line, size, "[TRIP] ===== trip recording: %s (at %lu.%03lus uptime) =====\n", s_reason,
                         (unsigned long)(s_trigger_ms / 1000), (unsigned long)(s_trigger_ms % 1000));
        } else if (idx == -2) {
            n = snprintf(line, size, "[TRIP] %d samples every 2 ms, t_ms is relative to the trigger (G474 spindle)\n",
                         s_count);
        } else if (idx == -1) {
            n = snprintf(line, size,
                         "TRIPCSV,t_ms,foc_loops,foc_max_us,z_phase_deg,z_target_deg,z_err_deg,vbus,"
                         "m1_en,m1_mode,m1_rpm,m1_orpm,m1_ia,m1_ib,m1_ic,m1_id,m1_iq,m1_imax,m1_v,"
                         "m2_en,m2_mode,m2_rpm,m2_orpm,m2_ia,m2_ib,m2_ic,m2_id,m2_iq,m2_imax,m2_v\n");
        } else if (idx < s_count) {
            int               oldest = (s_head - s_count + SAMPLES) % SAMPLES;
            const TripSample& s      = s_buf[(oldest + idx) % SAMPLES];
            n = snprintf(line, size, "TRIPCSV,%ld,%u,%u,%.1f,%.1f,%.1f,%.1f", (long)(s.t_ms - s_trigger_ms),
                         (unsigned)s.isr_count, (unsigned)s.isr_max_us, s.phase_deg10 / 10.0f, s.target_deg10 / 10.0f,
                         s.zerr_deg10 / 10.0f, s.vbus10 / 10.0f);
            for (int k = 0; k < 2 && n > 0 && n < (int)size; k++) {
                const TripMotorSample& m = s.m[k];
                n += snprintf(line + n, size - n, ",%u,%u,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f", m.enabled, m.mode,
                              m.rpm, m.obs_rpm, m.ia / 100.0f, m.ib / 100.0f, m.ic / 100.0f, m.id / 100.0f,
                              m.iq / 100.0f, m.imax / 100.0f, m.vmag / 100.0f);
            }
            if (n > 0 && n < (int)size - 1) {
                line[n++] = '\n';
                line[n]   = 0;
            }
        } else {
            n = snprintf(line, size, "[TRIP] ===== end of trip recording =====\n");
        }
        if (n < 0) n = 0;
        if (n >= (int)size) n = size - 1;
        return n;
    }

    void arm() {
        s_frozen = false;
        s_count  = 0;
        s_head   = 0;
    }
}

void tripPush(const TripSample& s) {
    if (s_frozen) return;
    s_buf[s_head] = s;
    s_head        = (s_head + 1) % SAMPLES;
    if (s_count < SAMPLES) s_count++;
}

void tripTrigger(const char* reason) {
    if (s_frozen) {
        linkEvent("MSG", "[TRIP] not recorded (%s): still holding the trip from %lu.%03lus", reason,
                  (unsigned long)(s_trigger_ms / 1000), (unsigned long)(s_trigger_ms % 1000));
        return;
    }
    s_frozen     = true;
    s_trigger_ms = millis();
    snprintf(s_reason, sizeof(s_reason), "%s", reason);
    tripRequestLinkDump();
}

void tripArmOnStart() {
    if (s_frozen && !s_sending) arm();
}

void tripRequestLinkDump() {
    if (s_sending) return;   // the XY board asks again once it has stopped arriving
    if (!s_frozen) {
        linkEvent("MSG", "[TRIP] nothing captured yet (recorder armed)");
        return;
    }
    s_sending = true;
    s_line    = -3;
    s_last_ms = 0;
}

void tripLinkTest() {
    // A moment later, so the request's own reply does not interleave with the recording.
    s_test_due = millis() + 100;
    if (!s_test_due) s_test_due = 1;
}

bool tripSending() { return s_sending; }

void tripService() {
    if (s_test_due && (int32_t)(millis() - s_test_due) >= 0) {
        s_test_due = 0;
        if (s_frozen && !s_sending) arm();
        tripTrigger("manual test (link T!)");
    }
    if (!s_sending) return;
    uint32_t now = millis();
    if (s_last_ms && now - s_last_ms < LINK_TRIP_LINE_MS) return;
    char line[400];
    int  n = format(s_line, line, sizeof(line));
    if (Link.availableForWrite() < n + LINK_RESERVE) return;
    Link.write(reinterpret_cast<const uint8_t*>(line), n);
    s_last_ms = now;
    if (s_line >= s_count) s_sending = false;
    else s_line++;
}
