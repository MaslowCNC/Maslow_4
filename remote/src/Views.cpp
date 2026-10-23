#include "Views.h"
#include "Debug.h"
#include "Link.h"
#include "Machine.h"

#include <Preferences.h>

using namespace Ui;
using Machine::st;

namespace {
    // ---- Local preferences ----------------------------------------------

    struct JogPrefs {
        uint8_t xyIndex = 2, zIndex = 2;
        float   xyFeed = 2500, zFeed = 300;  // mm/min, the WebUI defaults
    } jogPrefs;

    void loadJogPrefs() {
        Preferences p;
        p.begin("ui", true);
        jogPrefs.xyIndex = p.getUChar("xyi", 2);
        jogPrefs.zIndex  = p.getUChar("zi", 2);
        jogPrefs.xyFeed  = p.getFloat("fxy", 2500);
        jogPrefs.zFeed   = p.getFloat("fz", 300);
        p.end();
    }

    void saveJogPrefs() {
        Preferences p;
        p.begin("ui", false);
        p.putUChar("xyi", jogPrefs.xyIndex);
        p.putUChar("zi", jogPrefs.zIndex);
        p.putFloat("fxy", jogPrefs.xyFeed);
        p.putFloat("fz", jogPrefs.zFeed);
        p.end();
    }

    const float XY_STEPS_MM[] = { 0.1, 1, 10, 100, 500 };
    const float Z_STEPS_MM[]  = { 0.1, 0.5, 1, 5, 10 };
    const float XY_STEPS_IN[] = { 0.01, 0.1, 0.5, 4, 20 };
    const float Z_STEPS_IN[]  = { 0.005, 0.02, 0.05, 0.2, 0.5 };
    constexpr int STEP_COUNT  = 5;

    float xyStep() { return (Machine::inches ? XY_STEPS_IN : XY_STEPS_MM)[jogPrefs.xyIndex % STEP_COUNT]; }
    float zStep() { return (Machine::inches ? Z_STEPS_IN : Z_STEPS_MM)[jogPrefs.zIndex % STEP_COUNT]; }

    std::string num(float v) { return fmt(v, 4); }

    // ---- Safety checks (WebUI tablet.js) --------------------------------

    bool readyToMove() {
        if (Machine::maslowState == Machine::MS_READY_TO_CUT) {
            return true;
        }
        alert("Not ready to move",
              std::string("Maslow is not ready to move (state: ") + Machine::maslowStateName(Machine::maslowState) +
                  "). Belt lengths are unknown: apply tension first.");
        return false;
    }

    bool  zLowAck = false, zHighAck = false;
    float zLowAckAt = 0, zHighAckAt = 0;

    // Warn before a move that leaves machine Z outside 0..72 mm, like
    // checkZHomeAndProceed(): once acknowledged, only re-ask when it gets worse.
    void zCheck(float deltaMm, std::function<void()> action) {
        if (!st.havePos) {
            action();
            return;
        }
        float z = st.mpos[2] + deltaMm;
        if (z < Machine::Z_MIN_SAFE_MM) {
            if (zLowAck && z < zLowAckAt) zLowAck = false;
            if (!zLowAck) {
                char t[260];
                snprintf(t, sizeof(t),
                         "Machine Z (%.1f mm) %s the minimum safe position of %.0f mm. This may mean the Z position is "
                         "wrong after an alarm or power reset.\nMovement is still allowed (e.g. to release the Z "
                         "screws). Proceed?",
                         z, deltaMm < 0 ? "would go below" : "is below", Machine::Z_MIN_SAFE_MM);
                confirm("Low Z Position", t, "Proceed", [z, action] {
                    zLowAck   = true;
                    zLowAckAt = z;
                    action();
                });
                return;
            }
        } else {
            zLowAck = false;
        }
        if (z > Machine::Z_MAX_SAFE_MM) {
            if (zHighAck && z > zHighAckAt) zHighAck = false;
            if (!zHighAck) {
                char t[260];
                snprintf(t, sizeof(t),
                         "Machine Z (%.1f mm) %s the safe maximum of %.0f mm. This may mean the Z position is wrong "
                         "after an alarm or power reset.\nMovement is still allowed (e.g. to release the Z screws). "
                         "Proceed?",
                         z, deltaMm > 0 ? "would exceed" : "exceeds", Machine::Z_MAX_SAFE_MM);
                confirm("High Z Position", t, "Proceed", [z, action] {
                    zHighAck   = true;
                    zHighAckAt = z;
                    action();
                });
                return;
            }
        } else {
            zHighAck = false;
        }
        action();
    }

    std::string feedText(float mmPerMin) {
        return Machine::inches ? fmt(mmPerMin / 25.4f, 2) : fmt(mmPerMin, 0);
    }

    // $J=G91G21F2500X10Y-10 (units embedded so a stale modal can't scale the move)
    void jog(float dx, float dy, float dz) {
        std::string cmd = std::string("$J=G91") + (Machine::inches ? "G20" : "G21") + "F" +
                          feedText(dz != 0 ? jogPrefs.zFeed : jogPrefs.xyFeed);
        if (dx != 0) cmd += "X" + num(dx);
        if (dy != 0) cmd += "Y" + num(dy);
        if (dz != 0) cmd += "Z" + num(dz);
        Machine::send(cmd.c_str());
    }

    void moveXYHome() {
        if (!readyToMove()) return;
        zCheck(0, [] { Machine::send("G90 G0 X0Y0"); });
    }

    void moveZHome() {
        zCheck(st.wco[2] - st.mpos[2], [] { Machine::send("G90 G0 Z0"); });
    }

    void park() {
        if (Machine::maslowState != Machine::MS_READY_TO_CUT) {
            Machine::toast("Park needs the Ready to Cut state");
            return;
        }
        auto get = [](const char* k, float def) {
            const char* v = Machine::setting(k);
            return v ? strtof(v, nullptr) : def;
        };
        float z = get("Maslow_Park_Z", 2.0f), x = get("Maslow_Park_X", 0), y = get("Maslow_Park_Y", 0);
        // Park values are mm: embed G21 in each move (tablet.js does the same).
        Machine::send(("G21 G90 G0 Z" + num(z)).c_str());
        Machine::send(("G21 G53 G0 Y" + num(y) + " X" + num(x)).c_str());
        if (Machine::inches) Machine::send("G20");
    }

    void homeMenu() {
        menu("Home", { "Define XY home here", "Set XY home position", "Set Z home here", "Set Z home value",
                       "Go to XY home", "Go to Z home", "Park" },
             [](int i) {
                 switch (i) {
                     case 0:
                         confirm("Define XY Home", "Make the current position the X0 Y0 origin of the job?", "Set", [] {
                             Machine::send("G10 L20 P0 X0");
                             Machine::send("G10 L20 P0 Y0");
                         });
                         break;
                     case 1: {
                         // Values are where the origin should be, in machine coordinates.
                         std::string u = Machine::unitName();
                         numpad("Home X (machine, " + u + ")", num(Machine::toDisplay(st.wco[0])), [u](const std::string& xs) {
                             numpad("Home Y (machine, " + u + ")", num(Machine::toDisplay(st.wco[1])), [xs](const std::string& ys) {
                                 float x = strtof(xs.c_str(), nullptr), y = strtof(ys.c_str(), nullptr);
                                 float mx = Machine::toDisplay(st.mpos[0]), my = Machine::toDisplay(st.mpos[1]);
                                 Machine::send(("G10 L20 P0 X" + num(mx - x) + " Y" + num(my - y)).c_str());
                             });
                         });
                         break;
                     }
                     case 2:
                         Machine::send("G10 L20 P0 Z0");
                         break;
                     case 3:
                         numpad("Z home (machine, mm, 0-72)", fmt(st.wco[2], 2), [](const std::string& zs) {
                             float z = constrain(strtof(zs.c_str(), nullptr), Machine::Z_MIN_SAFE_MM, Machine::Z_MAX_SAFE_MM);
                             Machine::send(("G21 G10 L20 P0 Z" + num(st.mpos[2] - z)).c_str());
                             if (Machine::inches) Machine::send("G20");
                         });
                         break;
                     case 4:
                         moveXYHome();
                         break;
                     case 5:
                         moveZHome();
                         break;
                     case 6:
                         park();
                         break;
                 }
             });
    }

    // ---- Jog --------------------------------------------------------------

    class JogView : public View {
        LGFX_Sprite dro { &lcd };
        uint32_t    drawn = 0xFFFFFFFF;
        bool        lastReady = false;
        bool        lastInches = false;
        static constexpr int DRO_Y = 32, DRO_H = 66, ROW = 22;

    public:
        JogView() {
            dro.setColorDepth(16);
            dro.createSprite(W, DRO_H);
        }

        void build() override {
            buttons.clear();
            bool ready = Machine::maslowState == Machine::MS_READY_TO_CUT;
            lastReady  = ready;
            lastInches = Machine::inches;
            static const Icon icons[3][3] = { { ARROW_NW, ARROW_N, ARROW_NE }, { ARROW_W, HOME, ARROW_E }, { ARROW_SW, ARROW_S, ARROW_SE } };
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    int dx = c - 1, dy = 1 - r;
                    Button b { (int16_t)(2 + c * 58), (int16_t)(100 + r * 50), 56, 48, "" };
                    b.icon   = icons[r][c];
                    b.dimmed = !ready;
                    if (dx == 0 && dy == 0) {
                        b.onTap = moveXYHome;
                    } else {
                        b.onTap = [dx, dy] {
                            if (!readyToMove()) return;
                            float s = xyStep();
                            zCheck(0, [dx, dy, s] { jog(dx * s, dy * s, 0); });
                        };
                    }
                    buttons.push_back(b);
                }
            }
            std::string zs = num(zStep());
            Button zUp { 178, 100, 60, 48, "Z+" };
            zUp.icon  = ARROW_N;
            zUp.color = C::select;
            zUp.onTap = [] {
                float s = zStep();
                zCheck(Machine::fromDisplay(s), [s] { jog(0, 0, s); });
            };
            buttons.push_back(zUp);
            buttons.push_back({ 178, 150, 60, 48, "Z step\n" + zs, C::panel, [this] {
                                   jogPrefs.zIndex = (jogPrefs.zIndex + 1) % STEP_COUNT;
                                   saveJogPrefs();
                                   redraw();
                               } });
            buttons.back().font = F_SMALL;
            Button zDn { 178, 200, 60, 48, "Z-" };
            zDn.icon  = ARROW_S;
            zDn.color = C::select;
            zDn.onTap = [] {
                float s = zStep();
                zCheck(-Machine::fromDisplay(s), [s] { jog(0, 0, -s); });
            };
            buttons.push_back(zDn);

            buttons.push_back({ 2, 252, 112, 32, "XY step " + num(xyStep()), C::panel, [this] {
                                   jogPrefs.xyIndex = (jogPrefs.xyIndex + 1) % STEP_COUNT;
                                   saveJogPrefs();
                                   redraw();
                               } });
            buttons.push_back({ 118, 252, 54, 32, Machine::unitName(), C::panel, [] {
                                   Machine::send(Machine::inches ? "G21" : "G20");
                                   Machine::send("$G");
                               } });
            buttons.push_back({ 176, 252, 62, 32, "Home", C::button, homeMenu });
        }

        void drawDro() {
            dro.fillScreen(C::bg);
            bool ok = Machine::connected() && st.havePos;
            const char* names = "XYZ";
            for (int i = 0; i < 3; ++i) {
                int y = i * ROW + ROW / 2 + 1;
                dro.setFont(F_BIG);
                dro.setTextDatum(textdatum_t::middle_left);
                dro.setTextColor(C::cyan);
                dro.drawString(std::string(1, names[i]).c_str(), 6, y);
                char w[16], m[16];
                int  dec = Machine::inches ? 3 : 2;
                if (ok) {
                    snprintf(w, sizeof(w), "%.*f", dec, Machine::toDisplay(st.wpos(i)));
                    snprintf(m, sizeof(m), "M %.*f", dec, Machine::toDisplay(st.mpos[i]));
                } else {
                    strcpy(w, "--");
                    strcpy(m, "--");
                }
                dro.setTextDatum(textdatum_t::middle_right);
                dro.setTextColor(st.haveWco || !ok ? C::text : C::yellow);
                dro.drawString(w, 162, y);
                dro.setFont(F_SMALL);
                dro.setTextColor(C::dim);
                dro.drawString(m, 236, y);
            }
            dro.pushSprite(0, DRO_Y);
        }

        void draw() override { drawDro(); }

        void update() override {
            if (lastReady != (Machine::maslowState == Machine::MS_READY_TO_CUT) || lastInches != Machine::inches) {
                redraw();
                return;
            }
            if (drawn != Machine::changes()) {
                drawn = Machine::changes();
                drawDro();
            }
        }

        void tap(int x, int y) override {
            if (y < DRO_Y || y >= DRO_Y + DRO_H || !st.havePos) return;
            int         axis = (y - DRO_Y) / ROW;
            const char* n    = axis == 0 ? "X" : axis == 1 ? "Y" : "Z";
            numpad(std::string("Set work ") + n + " (" + Machine::unitName() + ")", fmt(Machine::toDisplay(st.wpos(axis)), 3),
                   [n](const std::string& v) {
                       Machine::send((std::string("G10 L20 P0 ") + n + num(strtof(v.c_str(), nullptr))).c_str());
                   });
        }
    };

    // ---- Job --------------------------------------------------------------

    class JobView : public View {
        int         scroll = 0;
        std::string selected;
        uint32_t    drawn = 0xFFFFFFFF;
        std::string lastStart;
        int         lastOvr = -1;
        static constexpr int LIST_Y = 58, ROW = 24, ROWS = 5, LIST_W = 206;

        std::string startLabel() {
            if (Machine::stateIs("Run")) return "Pause";
            if (Machine::stateIs("Hold")) return "Resume";
            return "Start";
        }

    public:
        void build() override {
            buttons.clear();
            buttons.push_back({ 166, 31, 72, 25, "Refresh", C::button, [] { Machine::requestFiles(); } });
            buttons.back().font = F_SMALL;
            buttons.push_back({ 210, LIST_Y, 28, ROW * ROWS / 2 - 1, "", C::panel, [this] {
                                   scroll = max(0, scroll - ROWS);
                                   drawList();
                               }, nullptr, ARROW_N });
            buttons.push_back({ 210, LIST_Y + ROW * ROWS / 2 + 1, 28, ROW * ROWS / 2 - 1, "", C::panel, [this] {
                                   int n  = (int)Machine::files().size();
                                   scroll = max(0, min(n - ROWS, scroll + ROWS));
                                   drawList();
                               }, nullptr, ARROW_S });

            std::string s = startLabel();
            lastStart     = s;
            Button start { 2, 208, 150, 40, s };
            if (s == "Pause") {
                start.color = C::orange;
                start.onTap = [] { Machine::realtime('!'); };
            } else if (s == "Resume") {
                start.color = C::green;
                start.onTap = [] { Machine::realtime('~'); };
            } else {
                bool ok      = !selected.empty() && Machine::stateIs("Idle") &&
                          Machine::maslowState == Machine::MS_READY_TO_CUT;
                start.color  = C::green;
                start.dimmed = !ok;
                start.onTap  = [this] { startJob(); };
            }
            buttons.push_back(start);
            buttons.push_back({ 156, 208, 82, 40, "Delete", C::button, [this] {
                                   if (selected.empty()) {
                                       Machine::toast("Select a file first");
                                       return;
                                   }
                                   std::string f = selected;
                                   confirm("Delete file", "Delete " + f + " from the SD card?", "Delete", [this, f] {
                                       Machine::send(("$SD/Delete=/" + f).c_str());
                                       selected.clear();
                                       Machine::requestFiles();
                                   });
                               } });
            buttons.back().dimmed = selected.empty();

            lastOvr = st.ovr[0];
            buttons.push_back({ 2, 252, 52, 32, "F-", C::button, [] { Machine::realtime(0x92); } });
            buttons.push_back({ 58, 252, 124, 32, "Feed " + std::to_string(st.ovr[0]) + "%", C::panel,
                                [] { Machine::realtime(0x90); } });
            buttons.push_back({ 186, 252, 52, 32, "F+", C::button, [] { Machine::realtime(0x91); } });
        }

        void startJob() {
            if (selected.empty()) {
                Machine::toast("Select a file first");
                return;
            }
            if (Machine::maslowState != Machine::MS_READY_TO_CUT) {
                Machine::toast("Maslow is not ready to cut");
                return;
            }
            if (!Machine::stateIs("Idle")) {
                Machine::toast("Machine is not idle");
                return;
            }
            std::string f = selected;
            confirm("Start job", "Run " + f + "?", "Start", [f] {
                Machine::send(("$SD/Run=/" + f).c_str());
                // tablet.js sends a cycle start 1.5 s after the run command.
                after(1500, [] { Machine::realtime('~'); });
            });
        }

        void drawHeader() {
            lcd.fillRect(0, CONTENT_Y, 164, 27, C::bg);
            lcd.setFont(F_BTN);
            lcd.setTextColor(C::text);
            lcd.setTextDatum(textdatum_t::middle_left);
            std::string h = "SD card";
            if (Machine::filesLoading()) {
                h += " ...";
            } else if (!Machine::files().empty()) {
                h += " (" + std::to_string(Machine::files().size()) + ")";
            }
            lcd.drawString(h.c_str(), 4, 44);
        }

        void drawList() {
            auto& files = Machine::files();
            lcd.fillRect(0, LIST_Y, LIST_W, ROW * ROWS, C::panel);
            lcd.setFont(F_SMALL);
            lcd.setTextDatum(textdatum_t::middle_left);
            if (files.empty()) {
                lcd.setTextColor(C::dim);
                lcd.drawString(Machine::connected() ? (Machine::filesLoading() ? "Loading..." : "No files - tap Refresh")
                                                    : "Not connected",
                               8, LIST_Y + ROW / 2);
                return;
            }
            scroll = max(0, min(scroll, (int)files.size() - ROWS));
            for (int i = 0; i < ROWS && scroll + i < (int)files.size(); ++i) {
                auto& f  = files[scroll + i];
                int   y  = LIST_Y + i * ROW;
                bool  on = f.path == selected;
                if (on) lcd.fillRect(0, y, LIST_W, ROW, C::select);
                lcd.setTextColor(C::text);
                std::string name = f.path;
                char        size[12];
                if (f.size >= 1000000) snprintf(size, sizeof(size), "%.1fM", f.size / 1e6);
                else snprintf(size, sizeof(size), "%uk", (unsigned)((f.size + 999) / 1000));
                int room = LIST_W - 12 - lcd.textWidth(size) - 6;
                while (name.size() > 2 && lcd.textWidth(name.c_str()) > room) name.pop_back();
                lcd.drawString(name.c_str(), 4, y + ROW / 2);
                lcd.setTextColor(C::dim);
                lcd.setTextDatum(textdatum_t::middle_right);
                lcd.drawString(size, LIST_W - 4, y + ROW / 2);
                lcd.setTextDatum(textdatum_t::middle_left);
                lcd.drawFastHLine(0, y + ROW - 1, LIST_W, C::bg);
            }
        }

        void drawInfo() {
            int y = LIST_Y + ROW * ROWS + 2;
            lcd.fillRect(0, y, W, 204 - y + 2, C::bg);
            lcd.setFont(F_SMALL);
            lcd.setTextDatum(textdatum_t::middle_left);
            if (st.sdPercent >= 0) {
                int bw = W - 8;
                lcd.drawRect(4, y + 2, bw, 20, C::dim);
                lcd.fillRect(5, y + 3, (int)((bw - 2) * st.sdPercent / 100), 18, C::blue);
                char t[40];
                snprintf(t, sizeof(t), "%.1f%%", st.sdPercent);
                lcd.setTextColor(C::text);
                lcd.setTextDatum(textdatum_t::middle_center);
                lcd.drawString(t, W / 2, y + 12);
            } else {
                lcd.setTextColor(selected.empty() ? C::dim : C::text);
                std::string s = selected.empty() ? "No file selected" : selected;
                while (s.size() > 2 && lcd.textWidth(s.c_str()) > W - 8) s.pop_back();
                lcd.drawString(s.c_str(), 4, y + 12);
            }
        }

        void draw() override {
            if (Machine::connected() && Machine::files().empty() && !Machine::filesLoading()) {
                Machine::requestFiles();
            }
            drawHeader();
            drawList();
            drawInfo();
        }

        void update() override {
            if (startLabel() != lastStart || st.ovr[0] != lastOvr) {
                redraw();
                return;
            }
            if (drawn != Machine::changes()) {
                drawn = Machine::changes();
                drawHeader();
                drawList();
                drawInfo();
                // Start button enablement depends on state too.
                bool ok = !selected.empty() && Machine::stateIs("Idle") && Machine::maslowState == Machine::MS_READY_TO_CUT;
                if (lastStart == "Start" && buttons.size() > 3 && buttons[3].dimmed == ok) {
                    buttons[3].dimmed = !ok;
                    drawButton(buttons[3]);
                }
            }
        }

        void tap(int x, int y) override {
            if (x >= LIST_W || y < LIST_Y || y >= LIST_Y + ROW * ROWS) return;
            int i = scroll + (y - LIST_Y) / ROW;
            auto& files = Machine::files();
            if (i < (int)files.size()) {
                selected = files[i].path == selected ? "" : files[i].path;
                redraw();
            }
        }

        bool drag(int x, int y, int dy, bool done) override {
            if (x >= LIST_W || y < LIST_Y || y >= LIST_Y + ROW * ROWS) return false;
            if (done) {
                int n  = (int)Machine::files().size();
                scroll = max(0, min(n - ROWS, scroll - dy / ROW));
                drawList();
            }
            return true;
        }
    };

    // ---- Setup (Maslow calibration popup + optional settings) ---------------

    void showSettings();

    class SetupView : public View {
        int      shownState = -2;
        uint32_t shownLog   = 0;
        static constexpr int GRID_Y = 64;

        bool finding() const {
            return Machine::maslowState == Machine::MS_FINDING_ANCHORS ||
                   Machine::maslowState == Machine::MS_ANCHORS_COMPUTING;
        }

    public:
        void build() override {
            buttons.clear();
            int s      = Machine::maslowState;
            shownState = s;
            if (finding()) {
                return;  // the log takes the space; STOP stays in the top bar
            }
            // Which actions are "available" (green) per state, as updateDynamicButtons() does.
            bool retract = s == 0 || s == 2 || s == 4 || s == 7 || s < 0;
            bool extend  = s == 0 || s == 2 || s == 4;
            bool tense   = s == 4;
            bool cal     = s == 4;
            struct Def {
                const char*           label;
                bool                  on;
                std::function<void()> fn;
            };
            Def defs[] = {
                { "Retract All", retract, [] { Machine::send("$ALL"); } },
                { "Extend All", extend, [] { Machine::send("$EXT"); } },
                { "Apply\nTension", tense, [] { Machine::send("$TKSLK"); } },
                { "Release\nTension", true, [] { Machine::send("$CMP"); } },
                { "Find\nAnchors", cal, [] {
                     confirm("Find Anchors", "Please confirm Z is fully lowered to continue.", "Continue",
                             [] { Machine::send("$CAL"); });
                 } },
                { "Set Z-Stop", true, [] { Machine::send("$SETZSTOP"); } },
                { "Test", true, [] { Machine::send("$TEST"); } },
                { "Park", s == Machine::MS_READY_TO_CUT, park },
                { "Unlock", Machine::stateIs("Alarm"), [] {
                     Machine::send("$X", [](bool ok, const char*) {
                         if (ok) alert("Alarm cleared", "Check the machine position before moving it.");
                     });
                 } },
                { "Settings", true, showSettings },
            };
            for (int i = 0; i < 10; ++i) {
                Button b { (int16_t)(2 + (i % 2) * 119), (int16_t)(GRID_Y + (i / 2) * 44), 117, 42, defs[i].label };
                b.color  = i < 5 ? C::green : C::button;
                b.dimmed = i < 5 ? !defs[i].on : (i == 7 || i == 8) && !defs[i].on;
                b.onTap  = defs[i].fn;
                buttons.push_back(b);
            }
        }

        void drawBanner() {
            int      s = Machine::maslowState;
            uint16_t c = C::grey;
            if (Machine::connected()) {
                if (s == 0 || s < 0) c = C::red;
                else if (s == 2 || s == 7) c = C::green;
                else if (s == 4) c = C::yellow;
                else c = C::blue;
            }
            lcd.fillRoundRect(2, 32, W - 4, 28, 6, c);
            lcd.setFont(F_BTN);
            lcd.setTextColor(c == C::yellow ? TFT_BLACK : C::text);
            lcd.setTextDatum(textdatum_t::middle_center);
            std::string t = Machine::connected() ? std::string("State: ") + Machine::maslowStateName(s) : "Not connected";
            lcd.drawString(t.c_str(), W / 2, 47);
        }

        void drawLogTail() {
            lcd.fillRect(0, GRID_Y, W, TABS_Y - GRID_Y, C::panel);
            auto&                    log = Machine::log();
            std::vector<std::string> lines;
            for (int i = (int)log.size() - 1; i >= 0 && lines.size() < 14; --i) {
                auto w = wrap(F_SMALL, log[i].text, W - 8);
                lines.insert(lines.begin(), w.begin(), w.end());
            }
            int maxRows = (TABS_Y - GRID_Y - 4) / 16;
            int first   = max(0, (int)lines.size() - maxRows);
            lcd.setFont(F_SMALL);
            lcd.setTextColor(C::text);
            lcd.setTextDatum(textdatum_t::top_left);
            for (int i = first; i < (int)lines.size(); ++i) {
                lcd.drawString(lines[i].c_str(), 4, GRID_Y + 2 + (i - first) * 16);
            }
        }

        void draw() override {
            lcd.fillRect(0, CONTENT_Y, W, CONTENT_H, C::bg);
            drawBanner();
            if (finding()) {
                drawLogTail();
                shownLog = Machine::logSerial();
            }
        }

        void update() override {
            static bool wasAlarm = false;
            bool        alarm    = Machine::stateIs("Alarm");
            if (shownState != Machine::maslowState || alarm != wasAlarm) {
                wasAlarm = alarm;
                redraw();
            } else if (finding() && shownLog != Machine::logSerial()) {
                shownLog = Machine::logSerial();
                drawLogTail();
            }
        }
    };

    // ---- Settings (Configuration + optional settings popups) -------------

    struct SettingDef {
        const char* label;
        const char* key;    // $/<key>, or a WiFi setting
        bool        text;   // keyboard rather than numpad
    };
    const SettingDef SETTINGS[] = {
        { "Work area X", "Maslow_Work_Area_X", false },
        { "Work area Y", "Maslow_Work_Area_Y", false },
        { "Center offset X", "Maslow_Work_Area_Center_Offset_X", false },
        { "Center offset Y", "Maslow_Work_Area_Center_Offset_Y", false },
        { "Park X", "Maslow_Park_X", false },
        { "Park Y", "Maslow_Park_Y", false },
        { "Park Z", "Maslow_Park_Z", false },
        { "Scale X", "Maslow_Scale_X", false },
        { "Scale Y", "Maslow_Scale_Y", false },
        { "Work thickness", "Maslow_workThickness", false },
        { "Spoilboard thick.", "Maslow_spoilboardThickness", false },
        { "Belt retract limit", "Maslow_Apply_Tension_Belt_Retraction_Limit", false },
        { "Extend distance", "Maslow_Extend_Dist", false },
        { "Retraction force", "Maslow_Retract_Current_Threshold", false },
        { "Calib. threshold", "Maslow_Acceptable_Calibration_Threshold", false },
        { "Allow tension limit", "Maslow_Apply_Tension_Allow_Limiting", true },
        { "WiFi SSID", "Sta/SSID", true },
        { "WiFi password", "Sta/Password", true },
    };
    constexpr int SETTING_COUNT = sizeof(SETTINGS) / sizeof(SETTINGS[0]);

    bool isWifi(const char* key) { return !strncmp(key, "Sta/", 4); }

    void requestAllSettings() {
        for (auto& s : SETTINGS) {
            if (!strcmp(s.key, "Sta/Password")) continue;  // write-only
            if (isWifi(s.key)) {
                Machine::send((std::string("$") + s.key).c_str());
            } else {
                Machine::requestSetting(s.key);
            }
        }
    }

    class SettingsView : public View {
        int      scroll = 0;
        uint32_t drawn  = 0xFFFFFFFF;
        static constexpr int LIST_Y = 58, ROW = 32, ROWS = 7;

    public:
        bool modal() const override { return true; }

        void build() override {
            buttons.clear();
            buttons.push_back({ 2, 286, 56, 32, "", C::button, [this] {
                                   scroll = max(0, scroll - ROWS);
                                   drawList();
                               }, nullptr, ARROW_N });
            buttons.push_back({ 62, 286, 56, 32, "", C::button, [this] {
                                   scroll = min(SETTING_COUNT - ROWS, scroll + ROWS);
                                   drawList();
                               }, nullptr, ARROW_S });
            buttons.push_back({ 122, 286, 56, 32, "Reload", C::button, [] { requestAllSettings(); } });
            buttons.back().font = F_SMALL;
            buttons.push_back({ 182, 286, 56, 32, "Back", C::grey, [] { pop(); } });
        }

        void drawList() {
            lcd.fillRect(0, LIST_Y, W, ROW * ROWS, C::bg);
            lcd.setFont(F_SMALL);
            for (int i = 0; i < ROWS && scroll + i < SETTING_COUNT; ++i) {
                auto& s = SETTINGS[scroll + i];
                int   y = LIST_Y + i * ROW;
                lcd.fillRoundRect(2, y + 1, W - 4, ROW - 2, 4, C::panel);
                lcd.setTextColor(C::text);
                lcd.setTextDatum(textdatum_t::middle_left);
                lcd.drawString(s.label, 8, y + ROW / 2);
                const char* v = Machine::setting(s.key);
                std::string shown = !strcmp(s.key, "Sta/Password") ? "(set)" : v ? v : "...";
                while (shown.size() > 2 && lcd.textWidth(shown.c_str()) > 90) shown.pop_back();
                lcd.setTextColor(v ? C::cyan : C::dim);
                lcd.setTextDatum(textdatum_t::middle_right);
                lcd.drawString(shown.c_str(), W - 8, y + ROW / 2);
            }
        }

        void draw() override {
            lcd.fillRect(0, CONTENT_Y, W, H - CONTENT_Y, C::bg);
            lcd.fillRect(0, CONTENT_Y, W, 26, C::panel);
            lcd.setFont(F_BTN);
            lcd.setTextColor(C::text);
            lcd.setTextDatum(textdatum_t::middle_left);
            lcd.drawString("Maslow settings", 6, CONTENT_Y + 13);
            lcd.setFont(F_SMALL);
            lcd.setTextColor(C::dim);
            lcd.setTextDatum(textdatum_t::middle_right);
            lcd.drawString("mm", W - 6, CONTENT_Y + 13);
            drawList();
        }

        void update() override {
            if (drawn != Machine::changes()) {
                drawn = Machine::changes();
                drawList();
            }
        }

        void tap(int x, int y) override {
            if (y < LIST_Y || y >= LIST_Y + ROW * ROWS) return;
            int i = scroll + (y - LIST_Y) / ROW;
            if (i >= SETTING_COUNT) return;
            const SettingDef& s   = SETTINGS[i];
            const char*       cur = Machine::setting(s.key);
            std::string       key = s.key;
            auto apply = [key](const std::string& v) {
                if (v.empty()) return;
                if (isWifi(key.c_str())) {
                    Machine::send(("$" + key + "=" + v).c_str());
                    if (key != "Sta/Password") Machine::send(("$" + key).c_str());
                    Machine::toast("WiFi changes apply after the Maslow restarts");
                } else {
                    // Same as the WebUI popups: set, then save maslow.yaml ($CO).
                    Machine::setSetting(key.c_str(), v.c_str(), true);
                }
            };
            std::string initial = (cur && key != "Sta/Password") ? cur : "";
            if (s.text) keyboard(s.label, initial, apply);
            else numpad(s.label, initial, apply);
        }

        bool drag(int, int y, int dy, bool done) override {
            if (y < LIST_Y || y >= LIST_Y + ROW * ROWS) return false;
            if (done) {
                scroll = max(0, min(SETTING_COUNT - ROWS, scroll - dy / ROW));
                drawList();
            }
            return true;
        }
    };

    void showSettings() {
        if (Machine::connected()) requestAllSettings();
        push(new SettingsView());
    }

    // ---- Log (WebUI messages + command line) -----------------------------

    struct WrappedLine {
        Machine::LogKind kind;
        std::string      text;
    };

    class LogView : public View {
        std::vector<WrappedLine> lines;
        uint32_t                 wrappedSerial = 0;
        size_t                   wrappedCount  = 0;
        int                      back          = 0;  // rows scrolled up from the bottom
        static constexpr int     TOP = 32, ROW = 16, ROWS = 13;

        void rewrap() {
            auto& log = Machine::log();
            if (wrappedCount > log.size() || Machine::logSerial() - wrappedSerial > log.size()) {
                lines.clear();
                wrappedCount = 0;
            }
            // The log drops its oldest lines in blocks; start over when that happens.
            size_t fresh = Machine::logSerial() - wrappedSerial;
            size_t start = fresh > log.size() ? 0 : log.size() - fresh;
            if (fresh > log.size()) lines.clear();
            for (size_t i = start; i < log.size(); ++i) {
                std::string t = log[i].text;
                if (log[i].kind == Machine::LogKind::Sent) t = "> " + t;
                for (auto& w : wrap(F_SMALL, t, W - 8)) lines.push_back({ log[i].kind, w });
            }
            if (lines.size() > 400) lines.erase(lines.begin(), lines.begin() + (lines.size() - 400));
            wrappedSerial = Machine::logSerial();
            wrappedCount  = log.size();
        }

        void drawLines() {
            rewrap();
            lcd.fillRect(0, TOP, W, ROW * ROWS + 4, C::panel);
            lcd.setFont(F_SMALL);
            lcd.setTextDatum(textdatum_t::top_left);
            int maxBack = max(0, (int)lines.size() - ROWS);
            back        = constrain(back, 0, maxBack);
            int first   = max(0, (int)lines.size() - ROWS - back);
            for (int i = 0; i < ROWS && first + i < (int)lines.size(); ++i) {
                auto&    l = lines[first + i];
                uint16_t c = C::text;
                switch (l.kind) {
                    case Machine::LogKind::Sent: c = C::cyan; break;
                    case Machine::LogKind::Warn: c = C::yellow; break;
                    case Machine::LogKind::Error: c = rgb(255, 110, 100); break;
                    default: break;
                }
                lcd.setTextColor(c);
                lcd.drawString(l.text.c_str(), 4, TOP + 2 + i * ROW);
            }
            if (back > 0) {
                lcd.fillRoundRect(W - 46, TOP + 2, 42, 16, 4, C::orange);
                lcd.setTextColor(TFT_BLACK);
                lcd.setTextDatum(textdatum_t::middle_center);
                lcd.drawString("more", W - 25, TOP + 10);
            }
        }

    public:
        void build() override {
            buttons.clear();
            buttons.push_back({ 2, 252, 44, 32, "", C::button, [this] {
                                   back += ROWS - 1;
                                   drawLines();
                               }, nullptr, ARROW_N });
            buttons.push_back({ 50, 252, 44, 32, "", C::button, [this] {
                                   back = max(0, back - (ROWS - 1));
                                   drawLines();
                               }, nullptr, ARROW_S });
            buttons.push_back({ 98, 252, 68, 32, "Send", C::green, [] {
                                   keyboard("Command", "", [](const std::string& c) {
                                       if (!c.empty()) Machine::send(c.c_str());
                                   });
                               } });
            buttons.push_back({ 170, 252, 68, 32, "Quick", C::button, [] {
                                   static const std::vector<std::string> cmds = {
                                       "$MINFO", "$GSTATE", "$G", "$I", "$#", "$X", "$SD/List", "$Heap/Show", "$EL",
                                   };
                                   menu("Quick commands", cmds, [](int i) { Machine::send(cmds[i].c_str()); });
                               } });
        }

        void draw() override { drawLines(); }

        void update() override {
            if (wrappedSerial != Machine::logSerial()) {
                drawLines();
            }
        }

        bool drag(int, int y, int dy, bool done) override {
            if (y >= TOP + ROW * ROWS) return false;
            if (done) {
                back += dy / ROW;
                drawLines();
            }
            return true;
        }
    };

    // ---- Overrides ---------------------------------------------------------

    class OverridesView : public View {
        int shown[3] = { -1, -1, -1 };

    public:
        bool modal() const override { return true; }
        void build() override {
            buttons.clear();
            const uint8_t cmds[3][3] = { { 0x92, 0x90, 0x91 }, { 0x97, 0x96, 0x95 }, { 0x9B, 0x99, 0x9A } };
            const char*   labels[3][3] = { { "-10%", "100%", "+10%" }, { "25%", "50%", "100%" }, { "-10%", "100%", "+10%" } };
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    uint8_t cmd = cmds[r][c];
                    buttons.push_back({ (int16_t)(4 + c * 78), (int16_t)(84 + r * 74), 76, 40, labels[r][c], C::button,
                                        [cmd] { Machine::realtime(cmd); } });
                }
            }
            buttons.push_back({ 60, 282, 120, 34, "Close", C::grey, [] { pop(); } });
        }
        void draw() override {
            lcd.fillRect(0, CONTENT_Y, W, H - CONTENT_Y, C::bg);
            lcd.fillRect(0, CONTENT_Y, W, 26, C::panel);
            lcd.setFont(F_BTN);
            lcd.setTextColor(C::text);
            lcd.setTextDatum(textdatum_t::middle_left);
            lcd.drawString("Overrides", 6, CONTENT_Y + 13);
            drawValues();
        }
        void drawValues() {
            const char* names[3] = { "Feed", "Rapid", "Spindle" };
            lcd.setFont(F_BTN);
            for (int r = 0; r < 3; ++r) {
                shown[r] = st.ovr[r];
                lcd.fillRect(0, 60 + r * 74, W, 22, C::bg);
                lcd.setTextColor(C::text);
                lcd.setTextDatum(textdatum_t::middle_left);
                lcd.drawString(names[r], 6, 71 + r * 74);
                lcd.setTextColor(C::cyan);
                lcd.setTextDatum(textdatum_t::middle_right);
                lcd.drawString((std::to_string(st.ovr[r]) + "%").c_str(), W - 6, 71 + r * 74);
            }
        }
        void update() override {
            if (shown[0] != st.ovr[0] || shown[1] != st.ovr[1] || shown[2] != st.ovr[2]) drawValues();
        }
    };

    // ---- Pairing -------------------------------------------------------------

    class PairView : public View {
        std::string shownStatus;
        Link::State shownState = Link::State::Unpaired;

    public:
        bool modal() const override { return true; }
        void build() override {
            buttons.clear();
            shownState = Link::state();
            if (Link::state() == Link::State::Pairing) {
                buttons.push_back({ 4, 278, 114, 40, "Cancel", C::button, [] { Link::cancelPairing(); } });
            } else {
                buttons.push_back({ 4, 278, 114, 40, "Pair", C::green, [] { Link::startPairing(); } });
            }
            buttons.push_back({ 122, 278, 114, 40, "Close", C::grey, [] { pop(); } });
        }
        void draw() override {
            lcd.fillRect(0, CONTENT_Y, W, H - CONTENT_Y, C::bg);
            lcd.fillRect(0, CONTENT_Y, W, 26, C::panel);
            lcd.setFont(F_BTN);
            lcd.setTextColor(C::text);
            lcd.setTextDatum(textdatum_t::middle_left);
            lcd.drawString("Pair with a Maslow", 6, CONTENT_Y + 13);
            std::string text =
                "1. In the Maslow WebUI terminal, send $EP. Pairing stays open for 60 seconds.\n"
                "2. Tap Pair below, near the Maslow.";
            auto lines = wrap(F_SMALL, text, W - 12);
            lcd.setFont(F_SMALL);
            lcd.setTextDatum(textdatum_t::top_left);
            for (size_t i = 0; i < lines.size(); ++i) lcd.drawString(lines[i].c_str(), 6, 64 + 18 * i);
            int y = 64 + 18 * (int)lines.size() + 10;
            lcd.setTextColor(C::dim);
            if (*Link::hostname()) {
                lcd.drawString((std::string("Paired: ") + Link::hostname()).c_str(), 6, y);
                lcd.drawString(Link::peerMac(), 6, y + 18);
            } else {
                lcd.drawString("Not paired yet", 6, y);
            }
            lcd.drawString((std::string("This remote: ") + Link::ourMac()).c_str(), 6, y + 36);
            drawStatus();
        }
        void drawStatus() {
            shownStatus = Link::state() == Link::State::Pairing ? Link::pairingStatus() : Link::stateName();
            lcd.fillRect(0, 236, W, 36, C::bg);
            lcd.setFont(F_BTN);
            lcd.setTextColor(Link::state() == Link::State::Connected ? C::green : C::yellow);
            lcd.setTextDatum(textdatum_t::middle_center);
            lcd.drawString(shownStatus.c_str(), W / 2, 254);
        }
        void update() override {
            if (shownState != Link::state()) {
                redraw();
                return;
            }
            std::string s = Link::state() == Link::State::Pairing ? Link::pairingStatus() : Link::stateName();
            if (s != shownStatus) drawStatus();
        }
    };

    // ---- More ----------------------------------------------------------------

    void macrosMenu() {
        // FluidNC config macros (macro0..macro3 in maslow.yaml).
        menu("Run macro", { "Macro 0", "Macro 1", "Macro 2", "Macro 3" }, [](int i) {
            confirm("Run macro", "Run macro " + std::to_string(i) + "?", "Run",
                    [i] { Machine::send(("$Macros/Run=" + std::to_string(i)).c_str()); });
        });
    }

    void jogSpeeds() {
        menu("Jog speeds (mm/min)",
             { "XY: " + fmt(jogPrefs.xyFeed, 0), "Z: " + fmt(jogPrefs.zFeed, 0), "Reset to 2500 / 300" }, [](int i) {
                 if (i == 2) {
                     jogPrefs.xyFeed = 2500;
                     jogPrefs.zFeed  = 300;
                     saveJogPrefs();
                     return;
                 }
                 numpad(i == 0 ? "XY jog speed (mm/min)" : "Z jog speed (mm/min)", fmt(i == 0 ? jogPrefs.xyFeed : jogPrefs.zFeed, 0),
                        [i](const std::string& v) {
                            float f = strtof(v.c_str(), nullptr);
                            if (f <= 0) return;
                            (i == 0 ? jogPrefs.xyFeed : jogPrefs.zFeed) = f;
                            saveJogPrefs();
                        });
             });
    }

    void linkInfo() {
        char t[400];
        snprintf(t, sizeof(t),
                 "State: %s\nMachine: %s\nMachine MAC: %s\nThis remote: %s\nChannel: %u\nSignal: %d dBm\n"
                 "Last reply: %u ms ago\nPackets sent %u, received %u, failed %u",
                 Link::stateName(), *Link::hostname() ? Link::hostname() : "-", Link::peerMac(), Link::ourMac(),
                 Link::channel(), Link::rssi(), (unsigned)(millis() - Link::lastRxMs()), (unsigned)Link::txPackets(),
                 (unsigned)Link::rxPackets(), (unsigned)Link::txFailures());
        alert("Link", t);
    }

    class MoreView : public View {
    public:
        void build() override {
            buttons.clear();
            struct Def {
                const char*           label;
                std::function<void()> fn;
                uint16_t              color;
            };
            static uint8_t brightness = 200;
            Def defs[] = {
                { "Unlock ($X)", [] { Machine::send("$X"); }, C::button },
                { "Spindle off", [] { Machine::send("M5"); }, C::button },
                { "Overrides", [] { push(new OverridesView()); }, C::button },
                { "Macros", macrosMenu, C::button },
                { "Jog speeds", jogSpeeds, C::button },
                { "Brightness", [] {
                     brightness = brightness >= 255 ? 40 : brightness >= 200 ? 255 : brightness >= 120 ? 200 : 120;
                     setBrightness(brightness);
                 }, C::button },
                { "Calibrate\ntouch", [] { calibrateTouch(); }, C::button },
                { "Link info", linkInfo, C::button },
                { "Pair\nmachine", showPairing, C::select },
                { "Forget\nmachine", [] {
                     confirm("Forget machine", "Remove the pairing? You will need $EP on the Maslow to pair again.",
                             "Forget", [] {
                                 Link::forget();
                                 redrawAll();
                             });
                 }, C::button },
            };
            for (int i = 0; i < 10; ++i) {
                buttons.push_back({ (int16_t)(2 + (i % 2) * 119), (int16_t)(33 + (i / 2) * 49), 117, 45, defs[i].label,
                                    defs[i].color, defs[i].fn });
            }
        }
        void draw() override {
            lcd.fillRect(0, CONTENT_Y, W, CONTENT_H, C::bg);
            lcd.setFont(&fonts::Font0);
            lcd.setTextColor(C::dim);
            lcd.setTextDatum(textdatum_t::bottom_center);
            lcd.drawString("Maslow remote " __DATE__, W / 2, TABS_Y - 2);
        }
    };
}

Ui::View* makeTab(int index) {
    loadJogPrefs();
    switch (index) {
        case 0: return new JogView();
        case 1: return new JobView();
        case 2: return new SetupView();
        case 3: return new LogView();
        default: return new MoreView();
    }
}

void showPairing() { push(new PairView()); }
