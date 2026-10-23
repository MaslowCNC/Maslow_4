// What we know about the Maslow, built from the lines it sends, and the
// command queue to it.  Mirrors the WebUI's tablet tab (tablet.js, maslow.js).

#pragma once

#include <Arduino.h>
#include <functional>
#include <string>
#include <vector>

namespace Machine {
    // Maslow calibration states (firmware Maslow.h, WebUI maslow.js).
    enum MaslowState : int {
        MS_UNKNOWN          = 0,
        MS_RETRACTING       = 1,
        MS_RETRACTED        = 2,
        MS_EXTENDING        = 3,
        MS_EXTENDED         = 4,
        MS_TAKING_SLACK     = 5,
        MS_FINDING_ANCHORS  = 6,
        MS_READY_TO_CUT     = 7,
        MS_RELEASE_TENSION  = 8,
        MS_ANCHORS_COMPUTING = 9,
    };

    // Same limits as the WebUI's Z safety check (tablet.js).
    constexpr float Z_MIN_SAFE_MM = 0;
    constexpr float Z_MAX_SAFE_MM = 72;

    struct Status {
        char     state[12]   = "";  // Idle, Run, Hold, Jog, Alarm, ...
        float    mpos[3]     = {};
        float    wco[3]      = {};
        bool     havePos     = false;
        bool     haveWco     = false;
        float    feed        = 0;
        float    spindle     = 0;
        int      ovr[3]      = { 100, 100, 100 };  // feed, rapid, spindle
        float    sdPercent   = -1;                 // -1: no file job
        char     sdFile[96]  = "";
        uint32_t updated     = 0;
        float    wpos(int axis) const { return mpos[axis] - wco[axis]; }
    };

    struct FileEntry {
        std::string path;  // relative to /sd, e.g. "dir/part.nc"
        uint32_t    size;
    };

    enum class LogKind : uint8_t { Info, Sent, Warn, Error, Ok };

    extern Status st;
    extern int    maslowState;  // -1 until known
    extern bool   inches;       // G20 modal
    extern bool   homed, extended, calibrating;

    void begin();
    void loop();

    bool connected();

    // Queued: sent one at a time, each waits for its ok/error.
    using Done = std::function<void(bool ok, const char* error)>;
    void send(const char* line, Done done = nullptr);
    // Bypasses the queue (STOP).
    void sendNow(const char* line);
    void realtime(uint8_t cmd);
    void clearQueue();

    // $STOP with the WebUI's retry until the machine reports Idle/Alarm.
    void stop();

    // Files on the SD card.
    void                          requestFiles();
    const std::vector<FileEntry>& files();
    bool                          filesLoading();

    // "$/Maslow_Park_X"-style settings: cached as they are read or set.
    void        requestSetting(const char* path);
    const char* setting(const char* path);  // nullptr until known
    void        setSetting(const char* path, const char* value, bool saveYaml);

    // Console log, wrapped for the display by the Log screen.
    struct LogLine {
        LogKind     kind;
        std::string text;
    };
    const std::vector<LogLine>& log();
    uint32_t                    logSerial();  // increases when lines are added
    void                        addLog(LogKind kind, const char* text);

    // Bumped whenever something the UI shows changes.
    uint32_t changes();

    // Unit helpers (display units follow the G20/G21 modal, like the WebUI).
    float       toDisplay(float mm);
    float       fromDisplay(float v);
    const char* unitName();

    const char* maslowStateName(int s);
    bool        stateIs(const char* name);  // prefix match ("Hold" matches "Hold:0")

    // Alerts the UI should pop up (calibration complete, tension warnings, ...).
    struct Alert {
        std::string title;
        std::string text;
        std::string confirmCommand;  // non-empty: offer Continue, which sends it
    };
    bool takeAlert(Alert& out);

    // Short transient message (errors, rejected actions).
    void        toast(const char* text);
    const char* currentToast(uint32_t& setAt);
}
