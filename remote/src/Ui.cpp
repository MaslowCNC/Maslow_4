#include "Ui.h"
#include "Debug.h"
#include "Link.h"
#include "Machine.h"
#include "Power.h"
#include "Views.h"

#include <Preferences.h>
#include <math.h>

namespace Ui {
    const lgfx::IFont* F_SMALL = &fonts::Font2;
    const lgfx::IFont* F_BTN   = &fonts::FreeSansBold9pt7b;
    const lgfx::IFont* F_BIG   = &fonts::FreeSansBold12pt7b;
}

namespace {
    using namespace Ui;

    const char* TAB_NAMES[] = { "Jog", "Job", "Setup", "Log", "More" };
    constexpr int TAB_COUNT = 5;

    View*              tabs[TAB_COUNT] = {};
    int                activeTab       = 0;
    std::vector<View*> modals;

    LGFX_Sprite topBar(&lcd);

    struct Timer {
        uint32_t              at;
        std::function<void()> fn;
    };
    std::vector<Timer> timers;

    // Touch tracking
    bool     touching     = false;
    int      pressCount   = 0;
    int      releaseCount = 0;
    int      downX = 0, downY = 0, lastX = 0, lastY = 0;
    uint32_t downAt       = 0;
    View*    downView     = nullptr;
    int      downButton   = -1;
    bool     holdFired    = false;
    bool     dragging     = false;
    bool     stopTouched  = false;
    bool     swallow      = false;  // touch that only woke the screen
    uint32_t lastPoll     = 0;

    // What the top bar last showed
    uint32_t    topChanges = 0xFFFFFFFF;
    Link::State topLink    = Link::State::Unpaired;
    uint32_t    topDrawn   = 0;
    bool        stopFlash  = false;

    uint32_t lastUpdate  = 0;
    uint32_t toastShown  = 0;
    bool     toastActive = false;

    constexpr int STOP_X = 176, STOP_W = 62;

    View* top() { return modals.empty() ? tabs[activeTab] : modals.back(); }

    uint16_t stateColor(const char* s) {
        if (!Machine::connected()) {
            return C::grey;
        }
        if (!strncmp(s, "Idle", 4)) return C::green;
        if (!strncmp(s, "Run", 3)) return C::blue;
        if (!strncmp(s, "Jog", 3)) return C::cyan;
        if (!strncmp(s, "Hold", 4)) return C::yellow;
        if (!strncmp(s, "Alarm", 5)) return C::red;
        if (!strncmp(s, "Door", 4)) return C::orange;
        if (!strncmp(s, "Home", 4)) return C::blue;
        return C::grey;
    }

    void drawTopBar() {
        auto& g = topBar;
        g.fillScreen(C::panel);
        g.setTextDatum(textdatum_t::middle_center);

        const char* state = Machine::connected() ? (Machine::st.state[0] ? Machine::st.state : "...") : "--";
        uint16_t    sc    = stateColor(state);
        g.fillRoundRect(3, 3, 70, 24, 5, sc);
        g.setFont(F_BTN);
        g.setTextColor(sc == C::yellow || sc == C::cyan ? TFT_BLACK : C::text);
        std::string st(state);
        size_t      colon = st.find(':');
        if (colon != std::string::npos) {
            st.resize(colon);
        }
        g.drawString(st.c_str(), 38, 16);

        g.setFont(F_SMALL);
        g.setTextDatum(textdatum_t::middle_left);
        uint16_t    dot;
        std::string mid;
        switch (Link::state()) {
            case Link::State::Connected:
                dot = C::green;
                mid = Machine::maslowStateName(Machine::maslowState);
                break;
            case Link::State::Searching:
                dot = C::orange;
                mid = "Searching";
                break;
            case Link::State::Pairing:
                dot = C::blue;
                mid = "Pairing";
                break;
            default:
                dot = C::red;
                mid = "Not paired";
                break;
        }
        g.fillCircle(83, 15, 4, dot);
        g.setTextColor(C::text);
        while (mid.size() > 3 && g.textWidth(mid.c_str()) > STOP_X - 94) {
            mid.pop_back();
        }
        g.drawString(mid.c_str(), 91, 16);

        g.fillRoundRect(STOP_X, 2, STOP_W, 26, 5, stopFlash ? C::orange : C::red);
        g.setFont(F_BTN);
        g.setTextColor(C::text);
        g.setTextDatum(textdatum_t::middle_center);
        g.drawString("STOP", STOP_X + STOP_W / 2, 16);
        g.pushSprite(0, 0);
        topDrawn = millis();
    }

    void drawTabs() {
        lcd.fillRect(0, TABS_Y, W, H - TABS_Y, C::panel);
        lcd.setFont(F_BTN);
        lcd.setTextDatum(textdatum_t::middle_center);
        int w = W / TAB_COUNT;
        for (int i = 0; i < TAB_COUNT; ++i) {
            bool on = i == activeTab;
            if (on) {
                lcd.fillRoundRect(i * w + 2, TABS_Y + 3, w - 4, H - TABS_Y - 6, 5, C::select);
            }
            lcd.setTextColor(on ? C::text : C::dim);
            lcd.drawString(TAB_NAMES[i], i * w + w / 2, TABS_Y + (H - TABS_Y) / 2 + 1);
        }
    }

    void drawToast() {
        uint32_t    at;
        const char* t = Machine::currentToast(at);
        auto        lines = wrap(F_SMALL, t, W - 16);
        if (lines.size() > 3) {
            lines.resize(3);
        }
        int h = 8 + 16 * (int)lines.size();
        int y = TABS_Y - h - 4;
        lcd.fillRoundRect(4, y, W - 8, h, 6, C::orange);
        lcd.setFont(F_SMALL);
        lcd.setTextColor(TFT_BLACK);
        lcd.setTextDatum(textdatum_t::top_left);
        for (size_t i = 0; i < lines.size(); ++i) {
            lcd.drawString(lines[i].c_str(), 10, y + 4 + 16 * i);
        }
    }

    Button* buttonAt(View* v, int x, int y, int& index) {
        for (size_t i = 0; i < v->buttons.size(); ++i) {
            if (v->buttons[i].contains(x, y)) {
                index = (int)i;
                return &v->buttons[i];
            }
        }
        index = -1;
        return nullptr;
    }

    bool readTouch(int& x, int& y) {
        if (Debug::injectedTouch(x, y)) {
            return true;
        }
        int32_t tx, ty;
        if (lcd.getTouch(&tx, &ty)) {
            x = tx;
            y = ty;
            return x >= 0 && x < W && y >= 0 && y < H;
        }
        return false;
    }

    void touchDown(int x, int y) {
        downX = lastX = x;
        downY = lastY = y;
        downAt        = millis();
        holdFired     = false;
        dragging      = false;
        stopTouched   = false;
        downView      = nullptr;
        downButton    = -1;
        swallow       = false;
        bool wasOff   = Power::screenOff();
        Power::activity();
        // With the screen off a touch only turns it on, except STOP, which
        // works blind.
        if (wasOff && !(y < TOP_H && x >= STOP_X)) {
            swallow = true;
            return;
        }
        if (y < TOP_H) {
            if (x >= STOP_X) {
                // STOP acts on touch-down: no waiting for release.
                stopTouched = true;
                stopFlash   = true;
                drawTopBar();
                Machine::stop();
            }
            return;
        }
        View* v = top();
        if (!v->modal() && y >= TABS_Y) {
            return;
        }
        downView   = v;
        Button* b  = buttonAt(v, x, y, downButton);
        if (b) {
            v->drawButton(*b, true);
        }
    }

    void touchMove(int x, int y) {
        if (swallow) return;
        lastX = x;
        lastY = y;
        if (!downView || downView != top()) {
            return;
        }
        if (downButton >= 0) {
            Button& b = downView->buttons[downButton];
            if (b.onHold && !holdFired && millis() - downAt > 700) {
                holdFired = true;
                downView->drawButton(b, false);
                b.onHold();
            }
            return;
        }
        int dy = y - downY;
        if (dragging || abs(dy) > 10) {
            dragging = downView->drag(downX, downY, dy, false) || dragging;
        }
    }

    void touchUp() {
        if (swallow) {
            swallow = false;
            return;
        }
        if (stopTouched) {
            stopFlash = false;
            topChanges = 0xFFFFFFFF;  // redraw
            return;
        }
        // Long press on the top bar (left of STOP): recalibrate the touch panel,
        // in case the calibration is so far off nothing else can be reached.
        if (downY < TOP_H && downX < STOP_X && millis() - downAt > 4000) {
            calibrateTouch();
            return;
        }
        if (downY >= TABS_Y && !top()->modal() && downY < H) {
            int t = downX / (W / TAB_COUNT);
            if (t >= 0 && t < TAB_COUNT) {
                showTab(t);
            }
            return;
        }
        View* v = downView;
        if (!v || v != top()) {
            return;
        }
        if (downButton >= 0) {
            if (downButton >= (int)v->buttons.size()) {
                return;
            }
            Button b = v->buttons[downButton];  // copy: the handler may rebuild buttons
            v->drawButton(v->buttons[downButton], false);
            bool inside = lastX >= b.x - 12 && lastX < b.x + b.w + 12 && lastY >= b.y - 12 && lastY < b.y + b.h + 12;
            if (!holdFired && inside) {
                if (b.onTap) {
                    b.onTap();
                } else if (b.onHold) {
                    Machine::toast("Press and hold");
                }
            }
            return;
        }
        if (dragging) {
            v->drag(downX, downY, lastY - downY, true);
        } else {
            v->tap(downX, downY);
        }
    }

    void pollTouch() {
        int  x, y;
        bool p = readTouch(x, y);
        if (p) {
            releaseCount = 0;
            if (!touching) {
                if (++pressCount >= 2) {
                    touching = true;
                    touchDown(x, y);
                }
            } else {
                touchMove(x, y);
            }
        } else {
            pressCount = 0;
            if (touching && ++releaseCount >= 3) {
                touching = false;
                touchUp();
            }
        }
    }

    // ---- Dialogs ---------------------------------------------------------

    class Dialog : public View {
    public:
        std::string title;
        bool        modal() const override { return true; }
        void        drawFrame() {
            lcd.fillRect(0, TOP_H, W, H - TOP_H, C::bg);
            lcd.fillRect(0, TOP_H, W, 26, C::panel);
            lcd.setFont(F_BTN);
            lcd.setTextColor(C::text);
            lcd.setTextDatum(textdatum_t::middle_left);
            std::string t = title;
            while (t.size() > 3 && lcd.textWidth(t.c_str()) > W - 12) {
                t.pop_back();
            }
            lcd.drawString(t.c_str(), 6, TOP_H + 13);
        }
        void draw() override { drawFrame(); }
    };

    class MessageDialog : public Dialog {
    public:
        std::string           text;
        const char*           yes = nullptr;
        std::function<void()> onYes;
        std::vector<std::string> lines;
        int                   scroll = 0;
        int textTop() const { return TOP_H + 32; }
        int maxLines() const { return (H - 50 - textTop()) / 18; }
        void build() override {
            lcd.setFont(F_SMALL);
            lines = wrap(F_SMALL, text, W - 16);
            buttons.clear();
            if (yes) {
                buttons.push_back({ 4, H - 46, 114, 42, "Cancel", C::button, [] { pop(); } });
                auto cb = onYes;
                buttons.push_back({ 122, H - 46, 114, 42, yes, C::green, [cb] {
                                       pop();
                                       if (cb) cb();
                                   } });
            } else {
                buttons.push_back({ 60, H - 46, 120, 42, "OK", C::button, [] { pop(); } });
            }
        }
        void draw() override {
            drawFrame();
            lcd.setFont(F_SMALL);
            lcd.setTextColor(C::text);
            lcd.setTextDatum(textdatum_t::top_left);
            int n = maxLines();
            for (int i = 0; i < n && scroll + i < (int)lines.size(); ++i) {
                lcd.drawString(lines[scroll + i].c_str(), 8, textTop() + 18 * i);
            }
        }
        bool drag(int, int, int dy, bool done) override {
            if (done && (int)lines.size() > maxLines()) {
                scroll = constrain(scroll - dy / 18, 0, (int)lines.size() - maxLines());
                redraw();
            }
            return true;
        }
    };

    class NumpadDialog : public Dialog {
    public:
        std::string                             value;
        std::function<void(const std::string&)> onOk;
        bool                                    fresh = true;  // first key replaces the initial value

        void drawValue() {
            lcd.fillRect(4, TOP_H + 30, W - 8, 36, C::panel);
            lcd.drawRect(4, TOP_H + 30, W - 8, 36, C::dim);
            lcd.setFont(F_BIG);
            lcd.setTextColor(fresh ? C::dim : C::text);
            lcd.setTextDatum(textdatum_t::middle_right);
            lcd.drawString(value.c_str(), W - 12, TOP_H + 49);
        }
        void key(char c) {
            if (fresh) {
                value.clear();
                fresh = false;
            }
            if (c == '-') {
                if (!value.empty() && value[0] == '-') {
                    value.erase(0, 1);
                } else {
                    value.insert(0, "-");
                }
            } else if (c == '.' && value.find('.') != std::string::npos) {
                return;
            } else if (value.size() < 12) {
                value += c;
            }
            drawValue();
        }
        void build() override {
            buttons.clear();
            const char* keys = "789456123-0.";
            for (int i = 0; i < 12; ++i) {
                int  col = i % 3, row = i / 3;
                char k   = keys[i];
                buttons.push_back({ (int16_t)(4 + col * 78), (int16_t)(TOP_H + 72 + row * 44), 76, 42, std::string(1, k),
                                    C::button, [this, k] { key(k); } });
                buttons.back().font = F_BIG;
            }
            int y = TOP_H + 72 + 4 * 44;
            buttons.push_back({ 4, (int16_t)y, 76, 42, "Del", C::button, [this] {
                                   if (fresh) {
                                       value.clear();
                                       fresh = false;
                                   } else if (!value.empty()) {
                                       value.pop_back();
                                   }
                                   drawValue();
                               } });
            buttons.push_back({ 82, (int16_t)y, 76, 42, "Cancel", C::button, [] { pop(); } });
            auto cb = onOk;
            buttons.push_back({ 160, (int16_t)y, 76, 42, "OK", C::green, [this, cb] {
                                   std::string v = value;
                                   pop();
                                   if (cb) cb(v);
                               } });
        }
        void draw() override {
            drawFrame();
            drawValue();
        }
    };

    class KeyboardDialog : public Dialog {
    public:
        std::string                             value;
        std::function<void(const std::string&)> onOk;
        int                                     layer = 0;  // 0 upper, 1 lower, 2 symbols

        const char* rows(int layer, int r) {
            static const char* L[3][4] = {
                { "1234567890", "QWERTYUIOP", "ASDFGHJKL$", "ZXCVBNM=/." },
                { "1234567890", "qwertyuiop", "asdfghjkl$", "zxcvbnm=/." },
                { "1234567890", "$=/.-_:#?!", "[]()<>*%+,", "@&;'\"~^|\\ " },
            };
            return L[layer][r];
        }
        void drawValue() {
            lcd.fillRect(4, TOP_H + 30, W - 8, 32, C::panel);
            lcd.drawRect(4, TOP_H + 30, W - 8, 32, C::dim);
            lcd.setFont(F_SMALL);
            lcd.setTextColor(C::text);
            lcd.setTextDatum(textdatum_t::middle_left);
            std::string shown = value + "_";
            while (shown.size() > 1 && lcd.textWidth(shown.c_str()) > W - 18) {
                shown.erase(0, 1);
            }
            lcd.drawString(shown.c_str(), 9, TOP_H + 46);
        }
        void build() override {
            buttons.clear();
            for (int r = 0; r < 4; ++r) {
                const char* keys = rows(layer, r);
                for (int i = 0; keys[i]; ++i) {
                    char c = keys[i];
                    buttons.push_back({ (int16_t)(i * 24), (int16_t)(TOP_H + 68 + r * 40), 23, 38, std::string(1, c),
                                        C::button, [this, c] {
                                            if (value.size() < 120) {
                                                value += c;
                                                drawValue();
                                            }
                                        } });
                    buttons.back().font = F_SMALL;
                }
            }
            int y = TOP_H + 68 + 4 * 40;
            buttons.push_back({ 0, (int16_t)y, 47, 40, layer == 0 ? "abc" : "ABC", C::button, [this] {
                                   layer = layer == 0 ? 1 : 0;
                                   redraw();
                               } });
            buttons.push_back({ 48, (int16_t)y, 47, 40, "#+=", C::button, [this] {
                                   layer = layer == 2 ? 0 : 2;
                                   redraw();
                               } });
            buttons.push_back({ 96, (int16_t)y, 47, 40, "space", C::button, [this] {
                                   value += ' ';
                                   drawValue();
                               } });
            buttons.push_back({ 144, (int16_t)y, 47, 40, "Del", C::button, [this] {
                                   if (!value.empty()) value.pop_back();
                                   drawValue();
                               } });
            auto cb = onOk;
            buttons.push_back({ 192, (int16_t)y, 48, 40, "OK", C::green, [this, cb] {
                                   std::string v = value;
                                   pop();
                                   if (cb) cb(v);
                               } });
            buttons.push_back({ 170, (int16_t)(TOP_H + 2), 68, 22, "Cancel", C::grey, [] { pop(); } });
            buttons.back().font = F_SMALL;
        }
        void draw() override {
            drawFrame();
            drawValue();
        }
    };

    class MenuDialog : public Dialog {
    public:
        std::vector<std::string> items;
        std::function<void(int)> onPick;
        int                      scroll = 0;
        static constexpr int     ROW    = 35;
        int visible() const { return (H - 48 - (TOP_H + 30)) / ROW; }
        void build() override {
            buttons.clear();
            int n = visible();
            for (int i = 0; i < n && scroll + i < (int)items.size(); ++i) {
                int idx = scroll + i;
                auto cb = onPick;
                buttons.push_back({ 4, (int16_t)(TOP_H + 30 + i * ROW), W - 8, ROW - 3, items[idx], C::button, [idx, cb] {
                                       pop();
                                       if (cb) cb(idx);
                                   } });
            }
            if ((int)items.size() > n) {
                buttons.push_back({ 4, H - 44, 56, 40, "", C::button, [this] {
                                       scroll = max(0, scroll - visible());
                                       redraw();
                                   }, nullptr, ARROW_N });
                buttons.push_back({ 64, H - 44, 56, 40, "", C::button, [this] {
                                       scroll = min((int)items.size() - visible(), scroll + visible());
                                       redraw();
                                   }, nullptr, ARROW_S });
            }
            buttons.push_back({ 124, H - 44, 112, 40, "Cancel", C::grey, [] { pop(); } });
        }
    };
}

namespace Ui {
    // ---- View ------------------------------------------------------------

    void View::drawButton(const Button& b, bool pressed) {
        uint16_t c = b.dimmed ? C::grey : b.color;
        if (pressed) {
            c = C::text;
        }
        lcd.fillRoundRect(b.x, b.y, b.w, b.h, 6, c);
        uint16_t fg = pressed ? TFT_BLACK : (b.dimmed ? C::dim : (c == C::yellow || c == C::cyan ? TFT_BLACK : C::text));
        if (b.icon != NONE) {
            int s = min(b.w, b.h) / (b.label.empty() ? 3 : 4);
            if (b.icon == HOME) {
                int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
                lcd.fillTriangle(cx - s, cy, cx + s, cy, cx, cy - s, fg);
                lcd.fillRect(cx - s * 2 / 3, cy, s * 4 / 3, s * 2 / 3 + 2, fg);
            } else {
                drawArrow(lcd, b.x + b.w / 2, b.y + b.h / 2 - (b.label.empty() ? 0 : 6), b.icon, s, fg);
            }
            if (!b.label.empty()) {
                lcd.setFont(F_SMALL);
                lcd.setTextColor(fg);
                lcd.setTextDatum(textdatum_t::bottom_center);
                lcd.drawString(b.label.c_str(), b.x + b.w / 2, b.y + b.h - 1);
            }
            return;
        }
        lcd.setFont(b.font ? b.font : F_BTN);
        lcd.setTextColor(fg);
        lcd.setTextDatum(textdatum_t::middle_center);
        // Multi-line labels split on '\n'.
        std::vector<std::string> lines;
        size_t start = 0;
        while (true) {
            size_t nl = b.label.find('\n', start);
            lines.push_back(b.label.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        int lh = lcd.fontHeight();
        int y0 = b.y + b.h / 2 - (lh * ((int)lines.size() - 1)) / 2;
        for (size_t i = 0; i < lines.size(); ++i) {
            lcd.drawString(lines[i].c_str(), b.x + b.w / 2, y0 + lh * (int)i + 1);
        }
    }

    void View::redraw() {
        build();
        draw();
        for (auto& b : buttons) {
            drawButton(b);
        }
    }

    Button* View::find(const char* label) {
        for (auto& b : buttons) {
            if (b.label == label) return &b;
        }
        return nullptr;
    }

    // ---- Public ----------------------------------------------------------

    void begin() {
        Preferences p;
        p.begin("ui", true);
        uint16_t cal[8];
        bool     haveCal = p.getBytes("touchcal", cal, sizeof(cal)) == sizeof(cal);
        uint8_t  bright  = p.getUChar("bright", 200);
        p.end();
        lcd.init();
        lcd.setRotation(0);
        if (haveCal) {
            lcd.setTouchCalibrate(cal);
        }
        setBrightness(bright);
        topBar.setColorDepth(16);
        topBar.createSprite(W, TOP_H);
        for (int i = 0; i < TAB_COUNT; ++i) {
            tabs[i] = makeTab(i);
        }
        lcd.fillScreen(C::bg);
        if (!haveCal && !Power::wokeFromSleep()) {
            // First boot: offer touch calibration; carry on with the defaults
            // if nobody touches the screen.
            lcd.setFont(F_BTN);
            lcd.setTextColor(C::text);
            lcd.setTextDatum(textdatum_t::middle_center);
            lcd.drawString("Touch the screen to", W / 2, 130);
            lcd.drawString("calibrate touch", W / 2, 152);
            lcd.setFont(F_SMALL);
            lcd.setTextColor(C::dim);
            lcd.drawString("(skips in 6 s)", W / 2, 180);
            uint32_t t0 = millis();
            int32_t  x, y;
            bool     touched = false;
            while (millis() - t0 < 6000) {
                if (lcd.getTouch(&x, &y)) {
                    touched = true;
                    break;
                }
                Debug::poll();
                delay(20);
            }
            if (touched) {
                while (lcd.getTouch(&x, &y)) delay(20);
                calibrateTouch();
                return;
            }
        }
        redrawAll();
    }

    void redrawAll() {
        lcd.fillScreen(C::bg);
        topChanges = 0xFFFFFFFF;
        drawTopBar();
        if (modals.empty()) {
            drawTabs();
            tabs[activeTab]->redraw();
        } else {
            modals.back()->redraw();
        }
        toastActive = false;
    }

    View* current() { return top(); }

    void showTab(int index) {
        while (!modals.empty()) {
            delete modals.back();
            modals.pop_back();
        }
        activeTab = index;
        lcd.fillRect(0, CONTENT_Y, W, CONTENT_H, C::bg);
        drawTabs();
        tabs[activeTab]->redraw();
        toastActive = false;
    }

    void push(View* modal) {
        modals.push_back(modal);
        modal->redraw();
        toastActive = false;
    }

    void pop() {
        if (modals.empty()) return;
        // Deleting the dialog while one of its own handlers runs would free the
        // lambda being executed, so defer the delete.
        View* v = modals.back();
        modals.pop_back();
        after(0, [v] { delete v; });
        if (modals.empty()) {
            lcd.fillRect(0, CONTENT_Y, W, H - CONTENT_Y, C::bg);
            drawTabs();
            tabs[activeTab]->redraw();
        } else {
            modals.back()->redraw();
        }
        toastActive = false;
    }

    void after(uint32_t ms, std::function<void()> fn) { timers.push_back({ millis() + ms, fn }); }

    void loop() {
        uint32_t now = millis();
        if (now - lastPoll >= 15) {
            lastPoll = now;
            pollTouch();
        }
        for (size_t i = 0; i < timers.size();) {
            if ((int32_t)(now - timers[i].at) >= 0) {
                auto fn = timers[i].fn;
                timers.erase(timers.begin() + i);
                fn();
            } else {
                ++i;
            }
        }
        if (touching) {
            return;  // keep drawing out of the way while a finger is down
        }
        uint32_t ch = Machine::changes();
        if (ch != topChanges || Link::state() != topLink || now - topDrawn > 1000) {
            topChanges = ch;
            topLink    = Link::state();
            drawTopBar();
        }
        if (now - lastUpdate >= 100) {
            lastUpdate = now;
            top()->update();
        }
        uint32_t    toastAt;
        const char* t = Machine::currentToast(toastAt);
        if (*t && toastAt != toastShown) {
            toastShown  = toastAt;
            toastActive = true;
            Power::wakeScreen();
            drawToast();
        } else if (toastActive && now - toastShown > 3500) {
            toastActive = false;
            View* v     = top();
            if (v->modal()) {
                v->redraw();
            } else {
                lcd.fillRect(0, CONTENT_Y, W, CONTENT_H, C::bg);
                v->redraw();
            }
        }
        Machine::Alert a;
        if (Machine::takeAlert(a)) {
            Power::wakeScreen();
            if (a.confirmCommand.empty()) {
                alert(a.title, a.text);
            } else {
                std::string cmd = a.confirmCommand;
                confirm(a.title, a.text, "Continue", [cmd] { Machine::send(cmd.c_str()); });
            }
        }
    }

    void alert(const std::string& title, const std::string& text) {
        auto d   = new MessageDialog();
        d->title = title;
        d->text  = text;
        push(d);
    }

    void confirm(const std::string& title, const std::string& text, const char* yes, std::function<void()> onYes) {
        auto d   = new MessageDialog();
        d->title = title;
        d->text  = text;
        d->yes   = yes;
        d->onYes = onYes;
        push(d);
    }

    void numpad(const std::string& title, const std::string& initial, std::function<void(const std::string&)> onOk) {
        auto d   = new NumpadDialog();
        d->title = title;
        d->value = initial;
        d->onOk  = onOk;
        push(d);
    }

    void keyboard(const std::string& title, const std::string& initial, std::function<void(const std::string&)> onOk) {
        auto d   = new KeyboardDialog();
        d->title = title;
        d->value = initial;
        d->onOk  = onOk;
        push(d);
    }

    void menu(const std::string& title, const std::vector<std::string>& items, std::function<void(int)> onPick) {
        auto d    = new MenuDialog();
        d->title  = title;
        d->items  = items;
        d->onPick = onPick;
        push(d);
    }

    std::vector<std::string> wrap(const lgfx::IFont* font, const std::string& text, int width) {
        lcd.setFont(font);
        std::vector<std::string> out;
        size_t                   pos = 0;
        while (pos < text.size()) {
            size_t nl  = text.find('\n', pos);
            size_t end = nl == std::string::npos ? text.size() : nl;
            std::string para = text.substr(pos, end - pos);
            while (!para.empty()) {
                if (lcd.textWidth(para.c_str()) <= width) {
                    out.push_back(para);
                    break;
                }
                // Longest prefix that fits, preferring a break at a space.
                size_t fit = 1;
                while (fit < para.size() && lcd.textWidth(para.substr(0, fit + 1).c_str()) <= width) {
                    ++fit;
                }
                size_t sp = para.rfind(' ', fit);
                size_t cut = (sp != std::string::npos && sp > fit / 2) ? sp : fit;
                out.push_back(para.substr(0, cut));
                para.erase(0, cut);
                while (!para.empty() && para[0] == ' ') para.erase(0, 1);
            }
            if (para.empty() && end > pos && out.empty()) out.push_back("");
            pos = end + 1;
            if (nl != std::string::npos && nl + 1 == text.size()) break;
        }
        return out;
    }

    std::string fmt(float v, int decimals) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.*f", decimals, v);
        std::string s(buf);
        if (s.find('.') != std::string::npos) {
            while (s.back() == '0') s.pop_back();
            if (s.back() == '.') s.pop_back();
        }
        if (s == "-0") s = "0";
        return s;
    }

    void drawArrow(LovyanGFX& g, int cx, int cy, Icon dir, int size, uint16_t color) {
        float a  = (dir - ARROW_N) * (float)M_PI / 4;  // 0 = up, clockwise
        float dx = sinf(a), dy = -cosf(a);
        float px = -dy, py = dx;  // perpendicular
        auto  P  = [&](float along, float across, int& x, int& y) {
            x = (int)lroundf(cx + dx * along + px * across);
            y = (int)lroundf(cy + dy * along + py * across);
        };
        int x0, y0, x1, y1, x2, y2, x3, y3;
        // head
        P(size, 0, x0, y0);
        P(-size * 0.1f, size * 0.85f, x1, y1);
        P(-size * 0.1f, -size * 0.85f, x2, y2);
        g.fillTriangle(x0, y0, x1, y1, x2, y2, color);
        // shaft: a quad as two triangles
        float hw = size * 0.28f;
        P(0, hw, x0, y0);
        P(0, -hw, x1, y1);
        P(-size, hw, x2, y2);
        P(-size, -hw, x3, y3);
        g.fillTriangle(x0, y0, x1, y1, x2, y2, color);
        g.fillTriangle(x1, y1, x2, y2, x3, y3, color);
    }

    void calibrateTouch() {
        uint16_t cal[8];
        lcd.fillScreen(C::bg);
        lcd.setFont(F_BTN);
        lcd.setTextColor(C::text);
        lcd.setTextDatum(textdatum_t::middle_center);
        lcd.drawString("Touch the arrow tips", W / 2, H / 2 - 12);
        lcd.drawString("in each corner", W / 2, H / 2 + 12);
        lcd.calibrateTouch(cal, C::text, C::bg, 24);
        Preferences p;
        p.begin("ui", false);
        p.putBytes("touchcal", cal, sizeof(cal));
        p.end();
        DLOG("UI touch calibration saved\n");
        redrawAll();
    }

    static uint8_t currentBrightness = 200;

    uint8_t brightness() { return currentBrightness; }

    void setBrightness(uint8_t level) {
        currentBrightness = level;
        lcd.setBrightness(level);
        Preferences p;
        p.begin("ui", false);
        if (p.getUChar("bright", 0) != level) p.putUChar("bright", level);
        p.end();
    }
}
