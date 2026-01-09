#pragma once
#include <Arduino.h>

enum class ChargingState {
  ON_BATTERY,
  PLUGGED_IN_CHARGING,
  PLUGGED_IN_FULL,
  UNKNOWN
};

struct BatteryStatus {
  float voltage;   // Volts
  uint8_t percent; // 0-100
  bool charging;   // true only when actively charging
  ChargingState state;
};

namespace BatterySystem {
  void begin();
  void update();
  BatteryStatus getStatus();
  void getUsbDebug(uint8_t& inputs, bool& present, bool& valid);
}
