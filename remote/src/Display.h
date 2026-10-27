// LovyanGFX setup for the supported boards, picked by build flag.
//
// 2.8" ESP32-2432S028R ("Cheap Yellow Display", env cyd): ILI9341 240x320 on
// HSPI (SCLK 14, MOSI 13, MISO 12, CS 15, DC 2), backlight GPIO 21.  The
// XPT2046 touch controller has its own bus on VSPI (SCLK 25, MOSI 32,
// MISO 39, CS 33, PENIRQ 36).  RGB LED on 4/16/17, active low.
//
// 4.0" ESP32-32E display (ELEGOO / lcdwiki E32R40T, env cyd4, BOARD_CYD4):
// ST7796 320x480 on HSPI (same pins), backlight GPIO 27.  The XPT2046 shares
// the panel's bus (CS 33, PENIRQ 36).  RGB LED on 22/16/17, active low.
//
// 4.0" ESP32-S3 IPS display (Hosyond / lcdwiki ES3C40P, env es3c40p,
// BOARD_ES3C40P): ST7796S 320x480 on SPI2 (SCLK 12, MOSI 11, MISO 13, CS 10,
// DC 46), backlight GPIO 45.  FT6336U capacitive touch on I2C (SDA 16,
// SCL 15, INT 17, RST 18, address 0x38).  One WS2812 LED on GPIO 42.

#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#if defined(BOARD_ES3C40P)
constexpr int  LCD_W = 320, LCD_H = 480;
constexpr int  PIN_BACKLIGHT = 45;
constexpr int  PIN_TOUCH_IRQ = 17;  // FT6336U INT, low while touched
constexpr bool TOUCH_CAPACITIVE = true;
constexpr int  PIN_LED_WS2812 = 42;
#define LARGE_PANEL
#elif defined(BOARD_CYD4)
constexpr int  LCD_W = 320, LCD_H = 480;
constexpr int  PIN_BACKLIGHT = 27;
constexpr int  PIN_TOUCH_IRQ = 36;
constexpr bool TOUCH_CAPACITIVE = false;
constexpr int  PIN_LED_R = 22, PIN_LED_G = 16, PIN_LED_B = 17;
#define LARGE_PANEL
#else
constexpr int  LCD_W = 240, LCD_H = 320;
constexpr int  PIN_BACKLIGHT = 21;
constexpr int  PIN_TOUCH_IRQ = 36;
constexpr bool TOUCH_CAPACITIVE = false;
constexpr int  PIN_LED_R = 4, PIN_LED_G = 16, PIN_LED_B = 17;
#endif

class LGFX : public lgfx::LGFX_Device {
#if defined(BOARD_ES3C40P) || defined(BOARD_CYD4)
    lgfx::Panel_ST7796 _panel;
#else
    lgfx::Panel_ILI9341 _panel;
#endif
    lgfx::Bus_SPI   _bus;
    lgfx::Light_PWM _light;
#ifdef BOARD_ES3C40P
    lgfx::Touch_FT5x06 _touch;
#else
    lgfx::Touch_XPT2046 _touch;
#endif

public:
    LGFX() {
        {
            auto cfg        = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 40000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
#ifdef BOARD_ES3C40P
            cfg.pin_sclk = 12;
            cfg.pin_mosi = 11;
            cfg.pin_miso = 13;
            cfg.pin_dc   = 46;
#else
            cfg.pin_sclk = 14;
            cfg.pin_mosi = 13;
            cfg.pin_miso = 12;
            cfg.pin_dc   = 2;
#endif
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg            = _panel.config();
            cfg.pin_rst         = -1;
            cfg.pin_busy        = -1;
            cfg.panel_width     = LCD_W;
            cfg.panel_height    = LCD_H;
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.readable        = true;
            cfg.rgb_order       = false;
            cfg.dlen_16bit      = false;
#ifdef BOARD_ES3C40P
            cfg.pin_cs     = 10;
            cfg.invert     = true;  // IPS panel
            cfg.bus_shared = false;
#else
            cfg.pin_cs     = 15;
            cfg.invert     = false;
            cfg.bus_shared = defined_cyd4();
#endif
            _panel.config(cfg);
        }
        {
            auto cfg        = _light.config();
            cfg.pin_bl      = PIN_BACKLIGHT;
            cfg.invert      = false;
            cfg.freq        = 12000;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg = _touch.config();
#ifdef BOARD_ES3C40P
            cfg.x_min           = 0;
            cfg.x_max           = LCD_W - 1;
            cfg.y_min           = 0;
            cfg.y_max           = LCD_H - 1;
            cfg.pin_int         = PIN_TOUCH_IRQ;
            cfg.pin_rst         = 18;
            cfg.bus_shared      = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port        = 0;
            cfg.i2c_addr        = 0x38;
            cfg.pin_sda         = 16;
            cfg.pin_scl         = 15;
            cfg.freq            = 400000;
#else
            cfg.x_min           = 300;
            cfg.x_max           = 3900;
            cfg.y_min           = 3800;
            cfg.y_max           = 200;
            cfg.pin_int         = PIN_TOUCH_IRQ;
            cfg.offset_rotation = 0;
            cfg.freq            = 1000000;
            cfg.pin_cs          = 33;
#ifdef BOARD_CYD4
            cfg.bus_shared = true;
            cfg.spi_host   = SPI2_HOST;
            cfg.pin_sclk   = 14;
            cfg.pin_mosi   = 13;
            cfg.pin_miso   = 12;
#else
            cfg.bus_shared = false;
            cfg.spi_host   = SPI3_HOST;  // VSPI
            cfg.pin_sclk   = 25;
            cfg.pin_mosi   = 32;
            cfg.pin_miso   = 39;
#endif
#endif
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }

private:
    static constexpr bool defined_cyd4() {
#ifdef BOARD_CYD4
        return true;  // the XPT2046 shares the panel's bus
#else
        return false;
#endif
    }
};

extern LGFX lcd;

// The status LED: three active-low pins, or one WS2812.
inline void boardLed(bool r, bool g, bool b) {
#ifdef BOARD_ES3C40P
    rgbLedWrite(PIN_LED_WS2812, r ? 40 : 0, g ? 40 : 0, b ? 40 : 0);
#else
    static bool init = false;
    if (!init) {
        init = true;
        pinMode(PIN_LED_R, OUTPUT);
        pinMode(PIN_LED_G, OUTPUT);
        pinMode(PIN_LED_B, OUTPUT);
    }
    digitalWrite(PIN_LED_R, !r);
    digitalWrite(PIN_LED_G, !g);
    digitalWrite(PIN_LED_B, !b);
#endif
}
