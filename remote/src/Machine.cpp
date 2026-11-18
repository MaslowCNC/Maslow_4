#include "Machine.h"
#include "Debug.h"
#include "Link.h"

#include <deque>
#include <map>

namespace Machine {
    Status st;
    int    maslowState = -1;
    bool   inches      = false;
    bool   homed = false, extended = false, calibrating = false;
    bool   mini = false, fixedZ = false;

    bool Status::jobRunning() const {
        return sdPercent >= 0 || !strncmp(state, "Run", 3) || !strncmp(state, "Hold", 4);
    }
}

namespace {
    using namespace Machine;

    constexpr uint32_t COMMAND_TIMEOUT = 10000;

    struct Pending {
        std::string line;
        Done        done;
        uint32_t    timeout = COMMAND_TIMEOUT;
    };
    std::deque<Pending> queue;
    bool                outstanding     = false;
    Pending             current;
    uint32_t            sentAt          = 0;
    int                 skipAcks        = 0;  // acks owed to sendNow() lines

    uint32_t changeCount = 0;

    std::vector<LogLine> logLines;
    uint32_t             logCount = 0;
    constexpr size_t     LOG_MAX  = 150;

    std::vector<FileEntry>   fileList;
    std::vector<std::string> dirStack;
    bool                     listing = false;

    std::map<std::string, std::string> settings;

    std::deque<Alert> alerts;

    std::string toastText;
    uint32_t    toastAt = 0;

    bool     wasConnected = false;
    uint32_t lastStatusPoll = 0;
    int      wcoBurst       = 0;
    uint32_t wcoBurstAt     = 0;

    // Stop (WebUI wsCommands.stop): 0x18, wait up to 1.5 s for the restart
    // banner, then $STOP.  ESP-NOW has no retransmission of its own, so 0x18 is
    // repeated every 500 ms until the banner comes, and $STOP is retried if it
    // gets no reply.
    bool     stopWaitBanner = false;
    int      stopResets     = 0;
    int      stopTries      = 0;
    uint32_t stopAt         = 0;
    uint32_t minfoAt        = 0;
    constexpr int      STOP_RESETS     = 3;
    constexpr uint32_t STOP_RESET_MS   = 500;
    constexpr uint32_t STOP_REPLY_MS   = 2000;
    constexpr int      STOP_TRIES      = 3;

    // Start (WebUI runFile): from $SD/Run until the job moves (Run or Hold).
    bool                      startPending = false;
    bool                      startAccepted = false;
    uint32_t                  startAt      = 0;
    uint32_t                  startReports = 0;
    std::function<void(bool)> startDone;
    constexpr uint32_t        START_WAIT_MS = 30000;  // a spindle spin-up or dwell can keep it Idle

    void finishStart(bool ok) {
        if (!startPending) {
            return;
        }
        startPending = false;
        auto d       = startDone;
        startDone    = nullptr;
        if (d) {
            d(ok);
        }
    }

    void changed() { ++changeCount; }

    bool startsWith(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }

    const char* errorText(int code) {
        switch (code) {
            case 1: return "Expected command letter";
            case 2: return "Bad number format";
            case 3: return "Invalid statement";
            case 8: return "Command requires idle state. Unlock machine?";
            case 9: return "G-code locked out (alarm or jog)";
            case 15: return "Jog target exceeds travel";
            case 20: return "Unsupported command";
            case 152: return "Configuration is invalid. maslow.yaml may be corrupt.";
            case 153: return "Configuration is invalid; config changes cannot be saved. Try restarting.";
            default: return "";
        }
    }

    void finishCurrent(bool ok, const char* err) {
        if (!outstanding) {
            return;
        }
        outstanding = false;
        Done d      = current.done;
        current     = {};
        if (d) {
            d(ok, err);
        }
    }

    void pump() {
        if (outstanding || queue.empty() || !Machine::connected()) {
            return;
        }
        current = queue.front();
        queue.pop_front();
        if (!Link::sendLine(current.line.c_str())) {
            Pending p = current;
            current   = {};
            if (p.done) {
                p.done(false, "Not connected");
            }
            return;
        }
        outstanding = true;
        sentAt      = millis();
    }

    void sendStopLine() {
        stopWaitBanner = false;
        if (outstanding && current.line != "$STOP") {
            finishCurrent(false, "Reset");  // the reset dropped it
        }
        if (++stopTries > STOP_TRIES) {
            Machine::toast("No reply to $STOP");
            return;
        }
        Pending p { "$STOP", [](bool ok, const char* err) {
                       if (!ok && err && !strcmp(err, "Timeout")) {
                           sendStopLine();
                       }
                   }, STOP_REPLY_MS };
        queue.push_front(p);
        pump();
        minfoAt = millis() + 1000;
    }

    // Splits "a,b,c" into up to n floats.
    int parseFloats(const char* s, float* out, int n) {
        int i = 0;
        while (i < n && *s) {
            out[i++] = strtof(s, (char**)&s);
            if (*s != ',') {
                break;
            }
            ++s;
        }
        return i;
    }

    void parseStatus(const char* line) {
        // <Idle|MPos:1,2,3|FS:0,0|Ov:100,100,100|WCO:0,0,0|SD:12.3,/sd/x.nc>
        std::string s(line + 1);
        if (!s.empty() && s.back() == '>') {
            s.pop_back();
        }
        bool  sawSD = false, sawWPos = false, sawStopped = false;
        float wpos[3] = {};
        size_t start = 0;
        int    field = 0;
        while (start <= s.size()) {
            size_t      bar = s.find('|', start);
            std::string f   = s.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
            if (field == 0) {
                strlcpy(st.state, f.c_str(), sizeof(st.state));
            } else if (startsWith(f.c_str(), "MPos:")) {
                parseFloats(f.c_str() + 5, st.mpos, 3);
                st.havePos = true;
            } else if (startsWith(f.c_str(), "WPos:")) {
                parseFloats(f.c_str() + 5, wpos, 3);
                sawWPos = true;
            } else if (startsWith(f.c_str(), "WCO:")) {
                parseFloats(f.c_str() + 4, st.wco, 3);
                st.haveWco = true;
            } else if (startsWith(f.c_str(), "FS:")) {
                float fs[2] = {};
                parseFloats(f.c_str() + 3, fs, 2);
                st.feed    = fs[0];
                st.spindle = fs[1];
            } else if (startsWith(f.c_str(), "F:")) {
                st.feed = strtof(f.c_str() + 2, nullptr);
            } else if (startsWith(f.c_str(), "Ov:")) {
                float o[3] = { 100, 100, 100 };
                parseFloats(f.c_str() + 3, o, 3);
                for (int i = 0; i < 3; ++i) {
                    st.ovr[i] = (int)o[i];
                }
            } else if (startsWith(f.c_str(), "SD:")) {
                // SD:<pct>,<file>[,<line>]
                sawSD        = true;
                st.sdPercent = strtof(f.c_str() + 3, nullptr);
                size_t      comma = f.find(',');
                std::string file  = comma == std::string::npos ? "" : f.substr(comma + 1);
                size_t      last  = file.rfind(',');
                if (last != std::string::npos && last + 1 < file.size() &&
                    file.find_first_not_of("0123456789", last + 1) == std::string::npos) {
                    file.resize(last);
                }
                strlcpy(st.sdFile, file.c_str(), sizeof(st.sdFile));
            } else if (startsWith(f.c_str(), "STOPPED:")) {
                // STOPPED:<line>,<file> (the file may contain commas)
                sawStopped     = true;
                st.stoppedLine = atoi(f.c_str() + 8);
                size_t comma   = f.find(',');
                strlcpy(st.stoppedFile, comma == std::string::npos ? "" : f.c_str() + comma + 1, sizeof(st.stoppedFile));
            } else if (startsWith(f.c_str(), "MS:")) {
                int m = atoi(f.c_str() + 3);
                if (m >= 0 && m <= MS_TENSIONED) {
                    maslowState = m;
                }
            } else if (startsWith(f.c_str(), "BL:")) {
                // BL:tl,tr,bl,br; "?" for a belt whose length is unknown
                int         known = 0;
                const char* p     = f.c_str() + 3;
                for (int i = 0; i < 4 && *p; ++i) {
                    if (*p != '?') ++known;
                    p = strchr(p, ',');
                    if (!p) break;
                    ++p;
                }
                st.beltsKnown = known;
            }
            if (bar == std::string::npos) {
                break;
            }
            start = bar + 1;
            ++field;
        }
        if (sawWPos) {
            for (int i = 0; i < 3; ++i) {
                st.mpos[i] = wpos[i] + st.wco[i];
            }
            st.havePos = true;
        }
        if (!sawSD) {
            st.sdPercent = -1;
            st.sdFile[0] = 0;
        }
        if (!sawStopped) {
            st.stoppedLine    = 0;
            st.stoppedFile[0] = 0;
        }
        st.updated = millis();
        ++st.reports;
        if (startPending) {
            if (stateIs("Run") || stateIs("Hold")) {
                finishStart(true);
            } else if (stateIs("Alarm")) {
                finishStart(false);
            } else if (startAccepted && stateIs("Idle") && st.sdPercent < 0 && st.reports - startReports >= 3) {
                finishStart(true);  // a file too short to see running
            }
        }
        changed();
    }

    void parseModal(const char* line) {
        // [GC:G1 G54 G17 G20 G90 G94 M5 M9 T1 F6000 S1000]
        bool was = inches;
        if (strstr(line, " G20")) {
            inches = true;
        } else if (strstr(line, " G21")) {
            inches = false;
        }
        if (was != inches) {
            changed();
        }
    }

    bool jsonBool(const char* json, const char* key) {
        const char* p = strstr(json, key);
        if (!p) {
            return false;
        }
        p = strchr(p, ':');
        while (p && (*p == ':' || *p == ' ')) {
            ++p;
        }
        return p && startsWith(p, "true");
    }

    void parseFile(const char* line, bool isDir) {
        // "[FILE: <depth spaces>name|SIZE:123]" or "[DIR:<depth spaces>name]"
        const char* p     = line + (isDir ? 5 : 7);
        size_t      depth = 0;
        while (p[depth] == ' ') {
            ++depth;
        }
        std::string rest(p + depth);
        if (!rest.empty() && rest.back() == ']') {
            rest.pop_back();
        }
        if (isDir) {
            dirStack.resize(depth);
            dirStack.push_back(rest);
            return;
        }
        size_t      bar  = rest.find("|SIZE:");
        std::string name = rest.substr(0, bar);
        uint32_t    size = bar == std::string::npos ? 0 : strtoul(rest.c_str() + bar + 6, nullptr, 10);
        std::string path;
        for (size_t i = 0; i < depth && i < dirStack.size(); ++i) {
            path += dirStack[i] + "/";
        }
        fileList.push_back({ path + name, size });
    }

    void maybeAlert(const char* line) {
        static const struct {
            const char* prefix;
            const char* title;
            const char* confirm;
        } warnings[] = {
            { "[MSG:WARN: Maslow Apply Tension deviation warning:", "Apply Tension Warning", "" },
            { "[MSG:WARN: Maslow Apply Tension retraction warning:", "Apply Tension Warning", "$TKSLK" },
            { "[MSG:WARN: Maslow Z home reset warning:", "Z Home Reset", "" },
            { "[MSG:WARN: Maslow Zm invalid warning:", "Z Position Invalid", "" },
        };
        for (auto& w : warnings) {
            if (startsWith(line, w.prefix)) {
                std::string text(line + strlen(w.prefix));
                if (!text.empty() && text.back() == ']') {
                    text.pop_back();
                }
                while (!text.empty() && text[0] == ' ') {
                    text.erase(0, 1);
                }
                alerts.push_back({ w.title, text, w.confirm });
                return;
            }
        }
        // Apply Tension couldn't confirm the position against the saved anchors (state
        // TENSIONED): Find Anchors measures them again (WebUI showApplyTensionFailedMessage).
        if (startsWith(line, "[MSG:ERR: Apply Tension failed:") || startsWith(line, "[MSG:ERR: Center point deviation over")) {
            std::string detail(line + 10);
            if (!detail.empty() && detail.back() == ']') {
                detail.pop_back();
            }
            Alert a { "Couldn't confirm position",
                      "The belts don't fit the saved anchors, which may have moved. Find Anchors measures them again "
                      "(no need to retract). Lower Z fully first.\n" +
                          detail,
                      "$CAL", "Find Anchors" };
            alerts.push_back(a);
            return;
        }
        if (startsWith(line, "[MSG:INFO: Calibration complete")) {
            alerts.push_back({ "Calibration complete",
                               "You do not need to calibrate again unless your frame changes size. Consider backing "
                               "up maslow.yaml.",
                               "" });
        }
    }

    void handleLine(const char* line) {
        if (line[0] == '<') {
            parseStatus(line);
            return;
        }
        if (startsWith(line, "Grbl ")) {
            // Restart banner: the reset dropped every unread line, so nothing
            // sent before it will be answered.
            skipAcks = 0;
            if (outstanding) {
                finishCurrent(false, "Reset");
            }
            if (stopWaitBanner) {
                sendStopLine();
            }
            addLog(LogKind::Info, line);
            return;
        }
        if (stopWaitBanner && (!strcmp(line, "ok") || startsWith(line, "error:"))) {
            return;  // a stray reply to a line taken before the reset (protocol.md)
        }
        if (!strcmp(line, "ok")) {
            if (skipAcks > 0) {
                --skipAcks;
            } else {
                finishCurrent(true, nullptr);
            }
            return;
        }
        if (startsWith(line, "error:")) {
            int         code  = atoi(line + 6);
            const char* extra = errorText(code);
            char        msg[120];
            snprintf(msg, sizeof(msg), "%s%s%s", line, *extra ? " - " : "", extra);
            addLog(LogKind::Error, msg);
            if (skipAcks > 0) {
                --skipAcks;
            } else {
                std::string cmd = current.line;
                finishCurrent(false, msg);
                char t[160];
                snprintf(t, sizeof(t), "%s: %s", cmd.c_str(), msg);
                toast(t);
            }
            return;
        }
        if (startsWith(line, "[GC:")) {
            parseModal(line);
            return;
        }
        if (startsWith(line, "[FILE:")) {
            parseFile(line, false);
            return;
        }
        if (startsWith(line, "[DIR:")) {
            parseFile(line, true);
            return;
        }
        if (startsWith(line, "MINFO: ")) {
            homed       = jsonBool(line, "\"homed\"");
            extended    = jsonBool(line, "\"extended\"");
            calibrating = jsonBool(line, "\"calibrationInProgress\"");
            changed();
            addLog(LogKind::Info, line);
            return;
        }
        if (startsWith(line, "[MSG:INFO: Current state:")) {
            int s = atoi(line + strlen("[MSG:INFO: Current state:"));
            if (s >= 0 && s <= MS_TENSIONED) {
                maslowState = s;
                changed();
            }
            addLog(LogKind::Info, line);
            return;
        }
        if (startsWith(line, "[MSG:INFO: Heartbeat")) {
            return;
        }
        if (startsWith(line, "[MSG:INFO: Board: ")) {
            bool m = startsWith(line + 18, "Unified");
            if (m != mini) {
                mini = m;
                changed();
            }
        }
        // Setting echoes: "$/Maslow_Park_X=0.000" (config) or "$Sta/SSID=name" (NVS)
        if (line[0] == '$') {
            const char* eq = strchr(line, '=');
            if (eq) {
                const char* key = line[1] == '/' ? line + 2 : line + 1;
                std::string k(key, eq - key);
                settings[k] = eq + 1;
                if (!strcasecmp(k.c_str(), "kinematics/MaslowKinematics/fixedZ")) {
                    fixedZ = !strcasecmp(eq + 1, "true");
                }
                changed();
            }
        }
        if (startsWith(line, "ALARM:")) {
            char t[64];
            snprintf(t, sizeof(t), "%s - unlock in Setup", line);
            toast(t);
        }
        maybeAlert(line);
        LogKind k = LogKind::Info;
        if (startsWith(line, "[MSG:ERR") || startsWith(line, "ALARM")) {
            k = LogKind::Error;
        } else if (startsWith(line, "[MSG:WARN")) {
            k = LogKind::Warn;
        }
        addLog(k, line);
    }

    void onConnected() {
        DLOG("MACHINE connected, requesting state\n");
        clearQueue();
        skipAcks = 0;
        st       = Status();
        mini     = false;
        fixedZ   = false;
        // The Maslow state is in every status report (MS) on current firmware;
        // $GSTATE is for older firmware, which only logs it.
        Link::sendRealtime('?');
        send("$GSTATE");
        send("$MINFO");
        send("$G");
        send("$MBOARD");
        requestSetting("kinematics/MaslowKinematics/fixedZ");
        requestSetting("Maslow_Park_X");
        requestSetting("Maslow_Park_Y");
        requestSetting("Maslow_Park_Z");
        // A status report carries WCO only every 10-30 reports; ask until it does.
        wcoBurst   = 40;
        wcoBurstAt = 0;
        changed();
    }
}

namespace Machine {
    void begin() {
        Link::onLine([](const char* line) { handleLine(line); });
    }

    bool connected() { return Link::state() == Link::State::Connected; }

    void injectLine(const char* line) { handleLine(line); }

    void loop() {
        bool c = connected();
        if (c != wasConnected) {
            wasConnected = c;
            if (c) {
                onConnected();
            } else {
                outstanding = false;
                std::deque<Pending> dropped;
                dropped.swap(queue);
                for (auto& p : dropped) {
                    if (p.done) {
                        p.done(false, "Not connected");
                    }
                }
                stopWaitBanner = false;
                finishStart(false);
                changed();
            }
        }
        if (!c) {
            return;
        }
        uint32_t now = millis();
        if (outstanding && (uint32_t)(now - sentAt) > current.timeout) {
            if (current.line != "$STOP") {
                char t[120];
                snprintf(t, sizeof(t), "No reply to %s", current.line.c_str());
                toast(t);
            }
            finishCurrent(false, "Timeout");
        }
        pump();

        if (wcoBurst > 0 && (uint32_t)(now - wcoBurstAt) >= 100) {
            wcoBurstAt = now;
            Link::sendRealtime('?');
            if (--wcoBurst == 0 || st.haveWco) {
                wcoBurst = 0;
            }
        }
        // Auto-reports only come while moving or on changes; poll at 1 Hz otherwise.
        if ((uint32_t)(now - st.updated) > 1000 && (uint32_t)(now - lastStatusPoll) > 1000) {
            lastStatusPoll = now;
            Link::sendRealtime('?');
        }
        if (stopWaitBanner && (uint32_t)(now - stopAt) >= STOP_RESET_MS) {
            if (stopResets < STOP_RESETS) {
                ++stopResets;
                stopAt = now;
                Link::sendRealtime(0x18);
            } else {
                sendStopLine();  // no banner (or $Start/Message changed): send it anyway
            }
        }
        if (startPending && (uint32_t)(now - startAt) > START_WAIT_MS) {
            finishStart(st.jobRunning());
        }
        if (minfoAt && (int32_t)(now - minfoAt) >= 0) {
            minfoAt = 0;
            sendNow("$MINFO");
        }
    }

    void send(const char* line, Done done) {
        if (!connected()) {
            toast("Not connected");
            if (done) {
                done(false, "Not connected");
            }
            return;
        }
        addLog(LogKind::Sent, line);
        queue.push_back({ line, done });
        pump();
    }

    void sendNow(const char* line) {
        if (Link::sendLine(line)) {
            ++skipAcks;
        }
    }

    void realtime(uint8_t cmd) {
        if (!Link::sendRealtime(cmd)) {
            toast("Not connected");
        }
    }

    void clearQueue() {
        std::deque<Pending> dropped;
        dropped.swap(queue);
        for (auto& p : dropped) {
            if (p.done) {
                p.done(false, "Cancelled");
            }
        }
    }

    void stop() {
        if (!connected()) {
            toast("Not connected");
            return;
        }
        addLog(LogKind::Sent, "Stop (0x18, then $STOP)");
        clearQueue();
        finishStart(false);
        // The reset drops every unread line; its banner fails the outstanding one.
        Link::sendRealtime(0x18);
        stopWaitBanner = true;
        stopResets     = 1;
        stopTries      = 0;
        stopAt         = millis();
    }

    void runJob(const std::string& line, std::function<void(bool)> done) {
        if (startPending) {
            return;  // one Start at a time: a second $SD/Run would run the file again
        }
        startPending  = true;
        startAccepted = false;
        startDone     = done;
        startAt      = millis();
        startReports = st.reports;
        changed();
        send(line.c_str(), [](bool ok, const char*) {
            if (!ok) {
                finishStart(false);
            } else {
                startAccepted = true;
                startReports  = st.reports;
                Link::sendRealtime('?');
            }
        });
    }

    bool starting() { return startPending; }

    void requestFiles() {
        fileList.clear();
        dirStack.clear();
        listing = true;
        changed();
        send("$SD/List", [](bool, const char*) {
            listing = false;
            changed();
        });
    }

    const std::vector<FileEntry>& files() { return fileList; }
    bool                          filesLoading() { return listing; }

    void requestSetting(const char* path) {
        std::string cmd = std::string("$/") + path;
        send(cmd.c_str());
    }

    const char* setting(const char* path) {
        auto it = settings.find(path);
        return it == settings.end() ? nullptr : it->second.c_str();
    }

    void setSetting(const char* path, const char* value, bool saveYaml) {
        std::string cmd = std::string("$/") + path + "=" + value;
        std::string key = path;
        std::string val = value;
        send(cmd.c_str(), [key, val](bool ok, const char*) {
            if (ok) {
                settings[key] = val;
                changed();
            }
        });
        if (saveYaml) {
            send("$CO");
        }
    }

    const std::vector<LogLine>& log() { return logLines; }
    uint32_t                    logSerial() { return logCount; }

    void addLog(LogKind kind, const char* text) {
        if (logLines.size() >= LOG_MAX) {
            logLines.erase(logLines.begin(), logLines.begin() + 10);
        }
        logLines.push_back({ kind, text });
        ++logCount;
    }

    uint32_t changes() { return changeCount; }

    float toDisplay(float mm) { return inches ? mm / 25.4f : mm; }
    float fromDisplay(float v) { return inches ? v * 25.4f : v; }
    const char* unitName() { return inches ? "in" : "mm"; }

    const char* maslowStateName(int s) {
        switch (s) {
            case MS_UNKNOWN: return "Unknown";
            case MS_RETRACTING: return "Retracting";
            case MS_RETRACTED: return "Retracted";
            case MS_EXTENDING: return "Extending";
            case MS_EXTENDED: return "Extended";
            case MS_TAKING_SLACK: return "Taking Slack";
            case MS_FINDING_ANCHORS: return "Finding Anchors";
            case MS_READY_TO_CUT: return "Ready to Cut";
            case MS_RELEASE_TENSION: return "Releasing Tension";
            case MS_ANCHORS_COMPUTING: return "Computing Anchors";
            case MS_TENSIONED: return "Tensioned";
            default: return "--";
        }
    }

    bool stateIs(const char* name) { return startsWith(st.state, name); }

    bool takeAlert(Alert& out) {
        if (alerts.empty()) {
            return false;
        }
        out = alerts.front();
        alerts.pop_front();
        return true;
    }

    void toast(const char* text) {
        toastText = text;
        toastAt   = millis();
        DLOG("TOAST %s\n", text);
        changed();
    }

    const char* currentToast(uint32_t& setAt) {
        setAt = toastAt;
        return toastText.c_str();
    }
}
