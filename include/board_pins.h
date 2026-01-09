#pragma once

// TFT 8-bit I80 bus to GC9A01
constexpr int PIN_LCD_D0 = 10;
constexpr int PIN_LCD_D1 = 11;
constexpr int PIN_LCD_D2 = 12;
constexpr int PIN_LCD_D3 = 13;
constexpr int PIN_LCD_D4 = 14;
constexpr int PIN_LCD_D5 = 15;
constexpr int PIN_LCD_D6 = 16;
constexpr int PIN_LCD_D7 = 17;

constexpr int PIN_LCD_DC  = 18;
constexpr int PIN_LCD_CS  = 2;
constexpr int PIN_LCD_WR  = 3;
constexpr int PIN_LCD_RD  = -1;   // not used
constexpr int PIN_LCD_RST = 21;
constexpr int PIN_LCD_BL  = 42;   // backlight PWM

// USB pins just for reference (no code yet):
// D- = 19, D+ = 20

// I2C bus
constexpr int PIN_I2C_SDA = 8;
constexpr int PIN_I2C_SCL = 9;

// Touch and IO expander
constexpr int PIN_TOUCH_RST = 0;
constexpr int PIN_TCA_INT   = 45;
