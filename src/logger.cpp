#include "logger.h"

#include <stdio.h>

namespace Logger {

void begin(uint32_t baud) {
  Serial.begin(baud);
}

void print(const char* msg) {
  Serial.print(msg);
}

void println(const char* msg) {
  Serial.println(msg);
}

void vprintf(const char* fmt, va_list args) {
  char buffer[512];
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  Serial.print(buffer);
}

void printf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

}  // namespace Logger
