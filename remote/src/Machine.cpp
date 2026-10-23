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
}

namespace {
    using namespace Machine;

    struct Pending {
        std::string line;
        Done        done;
    };
    std::deque<Pending> queue;
    bool                outstanding     = false;
    Pending             current;
    uint32_t            sentAt          = 0;
    int                 skipAcks        = 0;  // acks owed to sendNow() lines
    constexpr uint32_t  COMMAND_TIMEOUT = 10000;

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

    // $STOP retry (WebUI sendStopCommand): every 300 ms for up to ~10 s,
    // until the machine reports Idle or Alarm.
    bool     stopPending  = false;
    int      stopAttempts = 0;
    uint32_t stopAt       = 0;
    uint32_t minfoAt      = 0;

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
        bool  sawSD = false, sawWPos = false;
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
                sawSD        = true;
                st.sdPercent = strtof(f.c_str() + 3, nullptr);
                size_t comma = f.find(',');
                strlcpy(st.sdFile, comma == std::string::npos ? "" : f.c_str() + comma + 1, sizeof(st.sdFile));
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
        st.updated = millis();
        if (stopPending && (stateIs("Idle") || stateIs("Alarm"))) {
            stopPending = false;
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
            if (s >= 0 && s <= 9) {
                maslowState = s;
                changed();
            }
            addLog(LogKind::Info, line);
            return;
        }
        if (startsWith(line, "[MSG:INFO: Heartbeat")) {
            return;
        }
        // Setting echoes: "$/Maslow_Park_X=0.000" (config) or "$Sta/SSID=name" (NVS)
        if (line[0] == '$') {
            const char* eq = strchr(line, '=');
            if (eq) {
                const char* key = line[1] == '/' ? line + 2 : line + 1;
                settings[std::string(key, eq - key)] = eq + 1;
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
        send("$GSTATE");
        send("$MINFO");
        send("$G");
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
                stopPending = false;
                changed();
            }
        }
        if (!c) {
            return;
        }
        uint32_t now = millis();
        if (outstanding && (uint32_t)(now - sentAt) > COMMAND_TIMEOUT) {
            char t[120];
            snprintf(t, sizeof(t), "No reply to %s", current.line.c_str());
            toast(t);
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
        if (stopPending && (uint32_t)(now - stopAt) >= 300) {
            if (++stopAttempts > 33) {
                stopPending = false;
            } else {
                stopAt = now;
                sendNow("$STOP");
            }
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
        addLog(LogKind::Sent, "$STOP");
        clearQueue();
        stopPending  = true;
        stopAttempts = 0;
        stopAt       = millis();
        sendNow("$STOP");
        minfoAt = millis() + 1000;
    }

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
