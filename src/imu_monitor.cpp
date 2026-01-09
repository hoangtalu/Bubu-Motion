#include "imu_monitor.h"

#include <Arduino.h>
#include <Wire.h>

#include "board_pins.h"
#include "logger.h"
#include "touch_system.h"

DEFINE_MODULE_LOGGER(ImuLog)

namespace {

constexpr uint8_t IMU_ADDR_PRIMARY = 0x6B;  // QMI8658 default; alt is 0x6A.
constexpr uint8_t IMU_ADDR_ALT = 0x6A;
constexpr uint8_t TCA_ADDR = 0x20;  // TCA6408 default.

// TCA6408 registers.
constexpr uint8_t TCA_REG_INPUT = 0x00;

// IMU pin routed into TCA6408 (update to match wiring).
constexpr uint8_t TCA_IMU_INT_PIN = 1;  // P1 (touch uses P0).

// QMI8658 registers.
constexpr uint8_t QMI_REG_WHO_AM_I = 0x00;
constexpr uint8_t QMI_REG_CTRL1 = 0x02;
constexpr uint8_t QMI_REG_CTRL2 = 0x03;
constexpr uint8_t QMI_REG_CTRL7 = 0x08;
constexpr uint8_t QMI_REG_CTRL8 = 0x09;
constexpr uint8_t QMI_REG_INT1_CTRL = 0x0C;
constexpr uint8_t QMI_REG_INT_STATUS = 0x2D;
constexpr uint8_t QMI_REG_ACCEL_X_L = 0x35;

// CTRL1/CTRL2 fields (ODR + range).
constexpr uint8_t QMI_ODR_104HZ = 0x05;
constexpr uint8_t QMI_ACCEL_RANGE_4G = (0x01 << 4);
constexpr uint8_t QMI_GYRO_RANGE_500DPS = (0x01 << 4);
// CTRL7: enable accel + gyro.
constexpr uint8_t QMI_ENABLE_ACCEL_GYRO = 0x03;
// INT1: enable accel + gyro data-ready.
constexpr uint8_t QMI_INT1_DRDY = 0x03;
// CTRL8: INT1 active-low, open-drain, latched until status read.
constexpr uint8_t QMI_INT_ACTIVE_LOW_OD_LATCH = 0x06;

// Scale factors for +/-4g and +/-500 dps.
constexpr float ACC_LSB_PER_G = 8192.0f;
constexpr float GYR_LSB_PER_DPS = 65.5f;

constexpr uint32_t SAMPLE_INTERVAL_MS = 100;
constexpr uint32_t IMU_RETRY_INTERVAL_MS = 2000;
constexpr bool IMU_LOG_SAMPLES = false;

bool imuReady = false;
uint8_t imuAddr = IMU_ADDR_PRIMARY;
uint8_t lastTcaInputs = 0xFF;
uint32_t lastSampleMs = 0;
uint32_t lastInitAttemptMs = 0;

bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return (Wire.endTransmission(true) == 0);
}

bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t& val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, static_cast<uint8_t>(1)) != 1) return false;
  val = Wire.read();
  return true;
}

bool i2cReadBytes(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  size_t got = Wire.requestFrom(addr, static_cast<uint8_t>(len));
  if (got != len) return false;
  for (size_t i = 0; i < len; ++i) {
    buf[i] = Wire.read();
  }
  return true;
}

bool readTcaInputs(uint8_t& value) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(TCA_REG_INPUT);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(TCA_ADDR, static_cast<uint8_t>(1)) != 1) return false;
  value = Wire.read();
  return true;
}

bool imuProbe(uint8_t addr, uint8_t& who) {
  if (!i2cReadReg(addr, QMI_REG_WHO_AM_I, who)) {
    return false;
  }
  return (who != 0x00 && who != 0xFF);
}

bool imuInitQmi8658() {
  uint8_t who = 0;
  if (!imuProbe(IMU_ADDR_PRIMARY, who)) {
    uint8_t altWho = 0;
    if (imuProbe(IMU_ADDR_ALT, altWho)) {
      imuAddr = IMU_ADDR_ALT;
      who = altWho;
    } else {
      ImuLog::println("[IMU] WHO_AM_I read failed");
      return false;
    }
  } else {
    imuAddr = IMU_ADDR_PRIMARY;
  }
  ImuLog::printf("[IMU] WHO_AM_I=0x%02X addr=0x%02X\n", who, imuAddr);

  if (!i2cWriteReg(imuAddr, QMI_REG_CTRL1, QMI_ODR_104HZ | QMI_ACCEL_RANGE_4G)) return false;
  if (!i2cWriteReg(imuAddr, QMI_REG_CTRL2, QMI_ODR_104HZ | QMI_GYRO_RANGE_500DPS)) return false;
  if (!i2cWriteReg(imuAddr, QMI_REG_CTRL7, QMI_ENABLE_ACCEL_GYRO)) return false;
  if (!i2cWriteReg(imuAddr, QMI_REG_CTRL8, QMI_INT_ACTIVE_LOW_OD_LATCH)) return false;
  if (!i2cWriteReg(imuAddr, QMI_REG_INT1_CTRL, QMI_INT1_DRDY)) return false;

  return true;
}

bool imuReadAccelGyro(float& ax, float& ay, float& az, float& gx, float& gy, float& gz) {
  uint8_t buf[12] = {};
  for (uint8_t i = 0; i < sizeof(buf); ++i) {
    if (!i2cReadReg(imuAddr, static_cast<uint8_t>(QMI_REG_ACCEL_X_L + i), buf[i])) {
      return false;
    }
  }
  // QMI8658 data is little-endian (L then H).
  int16_t rawAx = static_cast<int16_t>((buf[1] << 8) | buf[0]);
  int16_t rawAy = static_cast<int16_t>((buf[3] << 8) | buf[2]);
  int16_t rawAz = static_cast<int16_t>((buf[5] << 8) | buf[4]);
  int16_t rawGx = static_cast<int16_t>((buf[7] << 8) | buf[6]);
  int16_t rawGy = static_cast<int16_t>((buf[9] << 8) | buf[8]);
  int16_t rawGz = static_cast<int16_t>((buf[11] << 8) | buf[10]);

  ax = rawAx / ACC_LSB_PER_G;
  ay = rawAy / ACC_LSB_PER_G;
  az = rawAz / ACC_LSB_PER_G;
  gx = rawGx / GYR_LSB_PER_DPS;
  gy = rawGy / GYR_LSB_PER_DPS;
  gz = rawGz / GYR_LSB_PER_DPS;
  return true;
}

}  // namespace

namespace ImuMonitor {

void begin() {
  ImuLog::println("[IMU] Monitor init...");
  lastInitAttemptMs = millis();
  imuReady = imuInitQmi8658();
  if (!imuReady) {
    ImuLog::println("[IMU] Init failed");
  } else {
    ImuLog::println("[IMU] Init OK");
  }

  uint8_t inputs = 0;
  if (readTcaInputs(inputs)) {
    lastTcaInputs = inputs;
  } else {
    ImuLog::println("[TCA] Input read failed");
  }
}

void update(uint32_t nowMs) {
  if (!imuReady && (nowMs - lastInitAttemptMs) >= IMU_RETRY_INTERVAL_MS) {
    lastInitAttemptMs = nowMs;
    imuReady = imuInitQmi8658();
    if (imuReady) {
      ImuLog::println("[IMU] Init OK");
    }
  }

  if (imuReady && (nowMs - lastSampleMs) >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = nowMs;
    float ax, ay, az, gx, gy, gz;
    if (imuReadAccelGyro(ax, ay, az, gx, gy, gz)) {
      if (IMU_LOG_SAMPLES) {
        ImuLog::printf("ACC: x=%.2f y=%.2f z=%.2f\n", ax, ay, az);
        ImuLog::printf("GYR: x=%.2f y=%.2f z=%.2f\n", gx, gy, gz);
      }
    } else {
      ImuLog::println("[IMU] Read failed");
    }
  }

  if (TouchSystem::consumeTcaInterrupt()) {
    uint8_t inputs = 0;
    if (readTcaInputs(inputs)) {
      uint8_t changed = static_cast<uint8_t>(inputs ^ lastTcaInputs);
      lastTcaInputs = inputs;
      int intPin = digitalRead(PIN_TCA_INT);
      bool imuPinChanged = (changed & (1 << TCA_IMU_INT_PIN)) != 0;
      ImuLog::printf("[INT] IMU interrupt fired via TCA6408, inputs=0x%02X changed=0x%02X IMU_PIN=%d INT_PIN=%d\n",
                     inputs, changed, imuPinChanged ? 1 : 0, intPin);
    } else {
      ImuLog::println("[INT] TCA input read failed");
    }

    if (imuReady) {
      uint8_t status = 0;
      if (i2cReadReg(imuAddr, QMI_REG_INT_STATUS, status)) {
        ImuLog::printf("[INT] IMU INT_STATUS=0x%02X\n", status);
      }
    }
  }
}

}  // namespace ImuMonitor
