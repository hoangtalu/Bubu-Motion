#include "tca6408.h"
#include <Arduino.h>
#include <Wire.h>
#include "logger.h"
DEFINE_MODULE_LOGGER(TcaLog)

namespace {
constexpr uint8_t TCA6408_ADDR = 0x20;
constexpr uint8_t REG_INPUT = 0x00;
constexpr uint8_t REG_CONFIG = 0x03;
}

namespace TCA6408 {

bool begin() {
  // Test communication first
  Wire.beginTransmission(TCA6408_ADDR);
  Wire.write(REG_CONFIG);
  Wire.write(0x55); // Test pattern
  if (Wire.endTransmission(true) != 0) {
    TcaLog::println("[TCA6408] begin() failed at endTransmission 1");
    return false;
  }
  delay(10);
  
  // Read back test pattern
  Wire.beginTransmission(TCA6408_ADDR);
  Wire.write(REG_CONFIG);
  if (Wire.endTransmission(false) != 0) {
    TcaLog::println("[TCA6408] begin() failed at endTransmission 2");
    return false;
  }
  if (Wire.requestFrom(TCA6408_ADDR, (uint8_t)1) != 1) {
    TcaLog::println("[TCA6408] begin() failed at requestFrom");
    return false;
  }
  uint8_t test = Wire.read();
  
  if (test != 0x55) {
    TcaLog::printf("[TCA6408] begin() failed, test read %02X, expected 55\n", test);
    return false;
  }
  
  // Configure all pins as inputs
  Wire.beginTransmission(TCA6408_ADDR);
  Wire.write(REG_CONFIG);
  Wire.write(0xFF);
  bool success = (Wire.endTransmission(true) == 0);
  if (success) {
    TcaLog::println("[TCA6408] begin() OK");
  } else {
    TcaLog::println("[TCA6408] begin() failed to set config to FF");
  }
  return success;
}

bool readInputs(uint8_t& value) {
  Wire.beginTransmission(TCA6408_ADDR);
  Wire.write(REG_INPUT);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(TCA6408_ADDR, (uint8_t)1) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

}  // namespace TCA6408