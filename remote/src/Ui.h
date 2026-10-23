// Small retained-mode touch UI for the 240x320 portrait display.
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

    constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    namespace C {
        constexpr uint16_t bg       = rgb(16, 18, 22);
        constexpr uint16_t panel    = rgb(34, 38, 46);
        constexpr uint16_t button   = rgb(58, 66, 80);
        constexpr uint16_t text     = rgb(240, 240, 240);
        constexpr uint16_t dim      = rgb(150, 156, 168);
        constexpr uint16_t green    = rgb(46, 150, 70);
        constexpr uint16_t red      = rgb(200, 40, 36);
        constexpr uint16_t yellow   = rgb(230, 180, 30);
        constexpr uint16_t blue     = rgb(40, 110, 200);
        constexpr uint16_t orange   = rgb(230, 120, 20);
        constexpr uint16_t grey     = rgb(90, 92, 98);
        constexpr uint16_t cyan     = rgb(60, 190, 210);
        constexpr uint16_t select   = rgb(40, 80, 140);
    }

    extern const lgfx::IFont* F_SMALL;  // 16 px bitmap
    extern const lgfx::IFont* F_BTN;    // FreeSansBold 9pt
    extern const lgfx::IFont* F_BIG;    // FreeSansBold 12pt

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
    void drawArrow(LovyanGFX& g, int cx, int cy, Icon dir, int size, uint16_t color);
    void calibrateTouch();
    void setBrightness(uint8_t level);  // saved
    uint8_t brightness();
}
