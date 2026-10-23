// LovyanGFX setup for the ESP32-2432S028R ("Cheap Yellow Display").
//
// The panel sits on HSPI (SCLK 14, MOSI 13, MISO 12, CS 15, DC 2) with the
// backlight on GPIO 21.  The XPT2046 touch controller has its own bus on
// VSPI (SCLK 25, MOSI 32, MISO 39, CS 33, PENIRQ 36).

#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;
    lgfx::Touch_XPT2046 _touch;

public:
    LGFX() {
        {
            auto cfg        = _bus.config();
            cfg.spi_host    = SPI2_HOST;  // HSPI
            cfg.spi_mode    = 0;
            cfg.freq_write  = 40000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = 14;
            cfg.pin_mosi    = 13;
            cfg.pin_miso    = 12;
            cfg.pin_dc      = 2;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg            = _panel.config();
            cfg.pin_cs          = 15;
            cfg.pin_rst         = -1;
            cfg.pin_busy        = -1;
            cfg.panel_width     = 240;
            cfg.panel_height    = 320;
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.readable        = true;
            cfg.invert          = false;
            cfg.rgb_order       = false;
            cfg.dlen_16bit      = false;
            cfg.bus_shared      = false;
            _panel.config(cfg);
        }
        {
            auto cfg        = _light.config();
            cfg.pin_bl      = 21;
            cfg.invert      = false;
            cfg.freq        = 12000;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg            = _touch.config();
            cfg.x_min           = 300;
            cfg.x_max           = 3900;
            cfg.y_min           = 3800;
            cfg.y_max           = 200;
            cfg.pin_int         = 36;
            cfg.bus_shared      = false;
            cfg.offset_rotation = 0;
            cfg.spi_host        = SPI3_HOST;  // VSPI
            cfg.freq            = 1000000;
            cfg.pin_sclk        = 25;
            cfg.pin_mosi        = 32;
            cfg.pin_miso        = 39;
            cfg.pin_cs          = 33;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

extern LGFX lcd;
