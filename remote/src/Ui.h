// Small retained-mode touch UI for portrait displays.
//
// Layouts are written for a 240x320 logical screen.  Gfx maps them onto the
// physical panel (240x320 on the 2.8" board, 320x480 on the 4"), and the 4"
// board uses larger fonts, so one layout serves both.
//
// Layout: a 30 px top bar (machine state, Maslow state, link, STOP) that is
// always visible, a content area, and a 34 px tab bar.  Dialogs cover the
// content and tab bar but never the STOP button.

#pragma once

#include "Display.h"

#include <functional>
#include <string>
#include <vector>

namespace Ui {
    constexpr int W         = 240;
    constexpr int H         = 320;
    constexpr int TOP_H     = 30;
    constexpr int TABS_Y    = 286;
    constexpr int CONTENT_Y = TOP_H;
    constexpr int CONTENT_H = TABS_Y - TOP_H;

    // Drawing in logical coordinates on the panel or a sprite.
    class Gfx {
        LovyanGFX* g;

    public:
        explicit Gfx(LovyanGFX& target) : g(&target) {}
        static int px(int x) { return (x * LCD_W + (x >= 0 ? 120 : -120)) / 240; }
        static int py(int y) { return (y * LCD_H + (y >= 0 ? 160 : -160)) / 320; }
        static int pr(int r) { return min(px(r), py(r)); }  // radii and sizes
        static int lx(int x) { return x * 240 / LCD_W; }    // physical -> logical
        static int ly(int y) { return y * 320 / LCD_H; }
        LovyanGFX& raw() { return *g; }

        void fillScreen(uint16_t c) { g->fillScreen(c); }
        void fillRect(int x, int y, int w, int h, uint16_t c) {
            g->fillRect(px(x), py(y), px(x + w) - px(x), py(y + h) - py(y), c);
        }
        void drawRect(int x, int y, int w, int h, uint16_t c) {
            g->drawRect(px(x), py(y), px(x + w) - px(x), py(y + h) - py(y), c);
        }
        void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
            g->fillRoundRect(px(x), py(y), px(x + w) - px(x), py(y + h) - py(y), pr(r), c);
        }
        void drawFastHLine(int x, int y, int w, uint16_t c) { g->drawFastHLine(px(x), py(y), px(x + w) - px(x), c); }
        void fillCircle(int x, int y, int r, uint16_t c) { g->fillCircle(px(x), py(y), pr(r), c); }
        void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
            g->fillTriangle(px(x0), py(y0), px(x1), py(y1), px(x2), py(y2), c);
        }
        void setFont(const lgfx::IFont* f) { g->setFont(f); }
        void setTextColor(uint16_t fg) { g->setTextColor(fg); }
        void setTextDatum(textdatum_t d) { g->setTextDatum(d); }
        void drawString(const char* s, int x, int y) { g->drawString(s, px(x), py(y)); }
        int  textWidth(const char* s) { return (g->textWidth(s) * 240 + LCD_W - 1) / LCD_W; }
        int  fontHeight() { return ly(g->fontHeight()); }
    };
    extern Gfx gfx;  // the panel

    constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    // The Maslow WebUI's palette (tablet.css, tablettab.html, maslow.js).
    namespace C {
        constexpr uint16_t bg        = rgb(255, 255, 255);  // page
        constexpr uint16_t panel     = rgb(248, 248, 248);  // navbar
        constexpr uint16_t border    = rgb(231, 231, 231);  // navbar border, list rules
        constexpr uint16_t button    = rgb(182, 159, 203);  // #b69fcb lavender buttons
        constexpr uint16_t buttonAlt = rgb(157, 136, 192);  // #9d88c0 alternate jog cells
        constexpr uint16_t cream     = rgb(242, 240, 228);  // #f2f0e4 info cells
        constexpr uint16_t text      = rgb(51, 51, 51);     // #333 body text
        constexpr uint16_t dim       = rgb(122, 122, 122);  // #7a7a7a labels
        constexpr uint16_t white     = rgb(255, 255, 255);
        constexpr uint16_t green     = rgb(74, 168, 92);    // #4aa85c status, run, available
        constexpr uint16_t red       = rgb(205, 101, 76);   // #cd654c stop
        constexpr uint16_t orange    = rgb(245, 177, 41);   // #f5b129 release tension
        constexpr uint16_t yellow    = orange;
        constexpr uint16_t blue      = rgb(51, 122, 183);   // #337ab7 links, upload
        constexpr uint16_t cyan      = blue;                // accent values
        constexpr uint16_t grey      = rgb(160, 160, 160);  // #a0a0a0 unavailable
        constexpr uint16_t select    = blue;
        constexpr uint16_t warnText  = rgb(176, 120, 0);
        // State banner backgrounds (maslow.js updateDynamicButtons)
        constexpr uint16_t bannerRed    = rgb(248, 215, 218);
        constexpr uint16_t bannerBlue   = rgb(207, 226, 255);
        constexpr uint16_t bannerGreen  = rgb(209, 231, 221);
        constexpr uint16_t bannerYellow = rgb(255, 243, 205);
    }

    // Dark text on light backgrounds, white on the coloured ones.
    constexpr bool isLight(uint16_t c) {
        return ((c >> 11) & 0x1F) * 299 / 31 + ((c >> 5) & 0x3F) * 587 / 63 + (c & 0x1F) * 114 / 31 > 800;
    }

    extern const lgfx::IFont* F_SMALL;  // body text
    extern const lgfx::IFont* F_BTN;    // button labels (FreeSans, like the WebUI's Arial)
    extern const lgfx::IFont* F_BTN_SMALL;  // labels that do not fit F_BTN
    extern const lgfx::IFont* F_BOLD;   // titles
    extern const lgfx::IFont* F_BIG;    // position readout

    enum Icon : uint8_t { NONE = 0, ARROW_N, ARROW_NE, ARROW_E, ARROW_SE, ARROW_S, ARROW_SW, ARROW_W, ARROW_NW, HOME };

    struct Button {
        int16_t               x, y, w, h;
        std::string           label;
        uint16_t              color  = C::button;
        std::function<void()> onTap  = nullptr;
        std::function<void()> onHold = nullptr;  // fires after a 0.7 s press instead of onTap
        Icon                  icon   = NONE;
        bool                  dimmed = false;    // drawn greyed; still tappable
        const lgfx::IFont*    font   = nullptr;
        uint16_t              fg     = 0;  // label/icon colour; 0 picks by background
        bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    };

    class View {
    public:
        std::vector<Button> buttons;
        virtual ~View() = default;

        virtual void build() {}   // create buttons; called before every full draw
        virtual void draw() {}    // background and static parts (buttons are drawn after)
        virtual void update() {}  // ~10 Hz: redraw what changed
        virtual void tap(int x, int y) {}  // tap that missed every button
        // Vertical drag that started at (x, y) on no button; dy is the total
        // movement so far.  Return false to ignore drags there.
        virtual bool drag(int x, int y, int dy, bool done) { return false; }
        virtual bool modal() const { return false; }

        void drawButton(const Button& b, bool pressed = false);
        void redraw();  // build + draw + buttons
        Button* find(const char* label);
        // Give buttons[from..] one font: F_BTN, or F_BTN_SMALL if any label overflows.
        void fitFont(size_t from = 0);
    };

    void begin();
    void loop();

    void showTab(int index);
    void push(View* modal);  // takes ownership
    void pop();              // closes and deletes the top modal
    void redrawAll();
    View* current();

    // Run fn once after ms (on the UI loop).
    void after(uint32_t ms, std::function<void()> fn);

    // Dialogs
    void alert(const std::string& title, const std::string& text);
    void confirm(const std::string& title, const std::string& text, const char* yes, std::function<void()> onYes);
    void numpad(const std::string& title, const std::string& initial, std::function<void(const std::string&)> onOk);
    void keyboard(const std::string& title, const std::string& initial, std::function<void(const std::string&)> onOk);
    void menu(const std::string& title, const std::vector<std::string>& items, std::function<void(int)> onPick);

    // Helpers
    std::vector<std::string> wrap(const lgfx::IFont* font, const std::string& text, int width);
    std::string fmt(float v, int decimals);  // trims trailing zeros
    uint16_t darken(uint16_t c);
    void     dashedRule(int y);  // dashed separator across the screen
    void drawArrow(Gfx& g, int cx, int cy, Icon dir, int size, uint16_t color);
    void calibrateTouch();
    void setBrightness(uint8_t level);  // saved
    uint8_t brightness();
}
