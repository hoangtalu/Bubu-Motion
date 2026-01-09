#pragma once
#include <LovyanGFX.hpp>
#include "board_pins.h"

class LGFX : public lgfx::LGFX_Device {
public:
  LGFX() {
    { // Bus config
      auto cfg = _bus_instance.config();
      cfg.port = 0;
      cfg.freq_write = 40000000;
      cfg.pin_wr  = PIN_LCD_WR;
      cfg.pin_rd  = PIN_LCD_RD;
      cfg.pin_rs  = PIN_LCD_DC;

      cfg.pin_d0  = PIN_LCD_D0;
      cfg.pin_d1  = PIN_LCD_D1;
      cfg.pin_d2  = PIN_LCD_D2;
      cfg.pin_d3  = PIN_LCD_D3;
      cfg.pin_d4  = PIN_LCD_D4;
      cfg.pin_d5  = PIN_LCD_D5;
      cfg.pin_d6  = PIN_LCD_D6;
      cfg.pin_d7  = PIN_LCD_D7;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { // Panel config
      auto cfg = _panel_instance.config();
      cfg.pin_cs   = PIN_LCD_CS;
      cfg.pin_rst  = PIN_LCD_RST;
      cfg.pin_busy = -1;

      cfg.memory_width  = 240;
      cfg.memory_height = 240;
      cfg.panel_width   = 240;
      cfg.panel_height  = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false; // use default order to correct color inversion
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;

      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }

private:
  lgfx::Bus_Parallel8 _bus_instance;
  lgfx::Panel_GC9A01  _panel_instance;
};

extern LGFX gfx;
