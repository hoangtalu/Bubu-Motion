#pragma once

#include <Arduino.h>
#include <stdarg.h>

namespace Logger {

void begin(uint32_t baud = 115200);
void print(const char* msg);
void println(const char* msg);
void vprintf(const char* fmt, va_list args);
void printf(const char* fmt, ...);

}  // namespace Logger

// Module logger guide:
// - MainLog: boot + memory reports (src/main.cpp)
// - DisplayLog: display init, eyes, gestures, layer transitions (src/display_system.cpp)
// - TouchLog: raw touch events + gesture classification (src/touch_system.cpp)
// - MenuLog: menu navigation + actions (src/menu_system.cpp)
// - WifiLog: Wi-Fi provisioning/connection state (src/wifi_service.cpp)
#define DEFINE_MODULE_LOGGER(Name)                      \
  namespace Name {                                      \
    inline void print(const char* msg) {                \
      Logger::print(msg);                               \
    }                                                   \
    inline void println(const char* msg) {              \
      Logger::println(msg);                             \
    }                                                   \
    inline void printf(const char* fmt, ...) {          \
      va_list args;                                     \
      va_start(args, fmt);                              \
      Logger::vprintf(fmt, args);                       \
      va_end(args);                                     \
    }                                                   \
  }
