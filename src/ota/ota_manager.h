#pragma once
#include <Arduino.h>

namespace BubuOTA {
  void begin();
  void runOnce();   // blocking update, just like your old code
  void runManual(); // manual trigger, can be called multiple times
  bool wasRollback();
}
