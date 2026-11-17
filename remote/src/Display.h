// LovyanGFX setup for the remote's display board: the 4.0" ESP32-S3 IPS display
// (Hosyond / lcdwiki ES3C40P, ESP32-S3 with 16 MB flash and 8 MB PSRAM).
//
// ST7796S 320x480 on SPI2 (SCLK 12, MOSI 11, MISO 13, CS 10, DC 46), backlight
// GPIO 45.  FT6336U capacitive touch on I2C (SDA 16, SCL 15, INT 17, RST 18,
// address 0x38).  One WS2812 LED on GPIO 42.

#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

constexpr int LCD_W = 320, LCD_H = 480;
constexpr int PIN_BACKLIGHT  = 45;
constexpr int PIN_TOUCH_IRQ  = 17;  // FT6336U INT, low while touched
constexpr int PIN_LED_WS2812 = 42;

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;
    lgfx::Touch_FT5x06 _touch;

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
            cfg.pin_sclk    = 12;
            cfg.pin_mosi    = 11;
            cfg.pin_miso    = 13;
            cfg.pin_dc      = 46;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg            = _panel.config();
            cfg.pin_cs          = 10;
            cfg.pin_rst         = -1;
            cfg.pin_busy        = -1;
            cfg.panel_width     = LCD_W;
            cfg.panel_height    = LCD_H;
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.readable        = true;
            cfg.invert          = true;  // IPS panel
            cfg.rgb_order       = false;
            cfg.dlen_16bit      = false;
            cfg.bus_shared      = false;
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
            auto cfg            = _touch.config();
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
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

extern LGFX lcd;

// The status LED (one WS2812).
inline void boardLed(bool r, bool g, bool b) {
    rgbLedWrite(PIN_LED_WS2812, r ? 40 : 0, g ? 40 : 0, b ? 40 : 0);
}
