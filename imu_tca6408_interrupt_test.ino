/*
  QMI8658 IMU + TCA6408 Interrupt Test (ESP32-S3)

  Wiring notes:
    - IMU INT     -> TCA6408 P0 (change TCA_IMU_INT_PIN if needed)
    - TCA6408 INT -> ESP32 PIN_TCA_INT (open-drain, active LOW)
    - SDA/SCL     -> PIN_I2C_SDA / PIN_I2C_SCL
    - Use INPUT_PULLUP on PIN_TCA_INT

  Expected Serial output example:
    ACC: x=0.12 y=-0.03 z=0.98
    GYR: x=1.2 y=0.4 z=-0.8
    [INT] IMU interrupt fired via TCA6408, inputs=0xFE changed=0x01 IMU_PIN=1 INT_PIN=0
*/

#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"

// I2C addresses
static constexpr uint8_t IMU_ADDR = 0x6B;   // QMI8658 default; change to 0x6A if needed
static constexpr uint8_t TCA_ADDR = 0x20;   // TCA6408 default

// TCA6408 registers
static constexpr uint8_t TCA_REG_INPUT   = 0x00;
static constexpr uint8_t TCA_REG_OUTPUT  = 0x01;
static constexpr uint8_t TCA_REG_POLARITY = 0x02;
static constexpr uint8_t TCA_REG_CONFIG  = 0x03;

// IMU pin routed into TCA6408
static constexpr uint8_t TCA_IMU_INT_PIN = 0;  // P0

// QMI8658 registers (per QMI8658 datasheet)
static constexpr uint8_t QMI_REG_WHO_AM_I     = 0x00;
static constexpr uint8_t QMI_REG_REVISION     = 0x01;
static constexpr uint8_t QMI_REG_CTRL1        = 0x02;
static constexpr uint8_t QMI_REG_CTRL2        = 0x03;
static constexpr uint8_t QMI_REG_CTRL7        = 0x08;
static constexpr uint8_t QMI_REG_CTRL8        = 0x09;
static constexpr uint8_t QMI_REG_INT1_CTRL    = 0x0C;
static constexpr uint8_t QMI_REG_INT_STATUS   = 0x2D;
static constexpr uint8_t QMI_REG_ACCEL_X_L    = 0x35;

// CTRL1/CTRL2 fields (ODR + range)
static constexpr uint8_t QMI_ODR_104HZ = 0x05;
static constexpr uint8_t QMI_ACCEL_RANGE_4G = (0x01 << 4);
static constexpr uint8_t QMI_GYRO_RANGE_500DPS = (0x01 << 4);
// CTRL7: enable accel + gyro
static constexpr uint8_t QMI_ENABLE_ACCEL_GYRO = 0x03;
// INT1: enable accel + gyro data-ready
static constexpr uint8_t QMI_INT1_DRDY = 0x03;
// CTRL8: INT1 active-low, open-drain, latch (per datasheet)
static constexpr uint8_t QMI_INT_ACTIVE_LOW_OD_LATCH = 0x06;

// Scale factors for +/-4g and +/-500 dps
static constexpr float ACC_LSB_PER_G = 8192.0f;
static constexpr float GYR_LSB_PER_DPS = 65.5f;

volatile bool tcaInterruptFired = false;
uint8_t lastTcaInputs = 0xFF;

static void IRAM_ATTR onTcaInterrupt() {
  tcaInterruptFired = true;
}

static bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return (Wire.endTransmission(true) == 0);
}

static bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t& val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, static_cast<uint8_t>(1)) != 1) return false;
  val = Wire.read();
  return true;
}

static bool i2cReadBytes(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) {
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

static bool imuInitQmi8658() {
  uint8_t who = 0;
  if (!i2cReadReg(IMU_ADDR, QMI_REG_WHO_AM_I, who)) {
    Serial.println("[IMU] WHO_AM_I read failed");
    return false;
  }
  Serial.printf("[IMU] WHO_AM_I=0x%02X\n", who);

  // Configure accel: 104 Hz, +/-4g
  if (!i2cWriteReg(IMU_ADDR, QMI_REG_CTRL1, QMI_ODR_104HZ | QMI_ACCEL_RANGE_4G)) return false;
  // Configure gyro: 104 Hz, +/-500 dps
  if (!i2cWriteReg(IMU_ADDR, QMI_REG_CTRL2, QMI_ODR_104HZ | QMI_GYRO_RANGE_500DPS)) return false;
  // Enable accel + gyro
  if (!i2cWriteReg(IMU_ADDR, QMI_REG_CTRL7, QMI_ENABLE_ACCEL_GYRO)) return false;
  // INT1 output: active-low, open-drain, latched until status read
  if (!i2cWriteReg(IMU_ADDR, QMI_REG_CTRL8, QMI_INT_ACTIVE_LOW_OD_LATCH)) return false;
  // Route data-ready to INT1
  if (!i2cWriteReg(IMU_ADDR, QMI_REG_INT1_CTRL, QMI_INT1_DRDY)) return false;

  return true;
}

static bool imuReadAccelGyro(float& ax, float& ay, float& az, float& gx, float& gy, float& gz) {
  uint8_t buf[12];
  if (!i2cReadBytes(IMU_ADDR, QMI_REG_ACCEL_X_L, buf, sizeof(buf))) return false;
  // QMI8658 data is little-endian (L then H)
  int16_t rawAx = (int16_t)((buf[1] << 8) | buf[0]);
  int16_t rawAy = (int16_t)((buf[3] << 8) | buf[2]);
  int16_t rawAz = (int16_t)((buf[5] << 8) | buf[4]);
  int16_t rawGx = (int16_t)((buf[7] << 8) | buf[6]);
  int16_t rawGy = (int16_t)((buf[9] << 8) | buf[8]);
  int16_t rawGz = (int16_t)((buf[11] << 8) | buf[10]);

  ax = rawAx / ACC_LSB_PER_G;
  ay = rawAy / ACC_LSB_PER_G;
  az = rawAz / ACC_LSB_PER_G;
  gx = rawGx / GYR_LSB_PER_DPS;
  gy = rawGy / GYR_LSB_PER_DPS;
  gz = rawGz / GYR_LSB_PER_DPS;
  return true;
}

static bool tcaInit() {
  // All pins inputs
  if (!i2cWriteReg(TCA_ADDR, TCA_REG_CONFIG, 0xFF)) return false;
  // No polarity inversion (active-low stays low)
  if (!i2cWriteReg(TCA_ADDR, TCA_REG_POLARITY, 0x00)) return false;
  // Read input once to clear any pending interrupt
  uint8_t val = 0;
  if (!i2cReadReg(TCA_ADDR, TCA_REG_INPUT, val)) return false;
  lastTcaInputs = val;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  pinMode(PIN_TCA_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_TCA_INT), onTcaInterrupt, FALLING);

  Serial.println("[TEST] IMU + TCA6408 interrupt test starting...");
  if (!tcaInit()) {
    Serial.println("[TCA] Init failed");
  } else {
    Serial.println("[TCA] Init OK");
  }

  if (!imuInitQmi8658()) {
    Serial.println("[IMU] Init failed");
  } else {
    Serial.println("[IMU] Init OK");
  }
}

void loop() {
  static uint32_t lastPrintMs = 0;
  uint32_t now = millis();

  if (now - lastPrintMs >= 100) {
    lastPrintMs = now;
    float ax, ay, az, gx, gy, gz;
    if (imuReadAccelGyro(ax, ay, az, gx, gy, gz)) {
      Serial.printf("ACC: x=%.2f y=%.2f z=%.2f\n", ax, ay, az);
      Serial.printf("GYR: x=%.2f y=%.2f z=%.2f\n", gx, gy, gz);
    } else {
      Serial.println("[IMU] Read failed");
    }
  }

  if (tcaInterruptFired) {
    noInterrupts();
    tcaInterruptFired = false;
    interrupts();

    // Read TCA inputs to identify which pin(s) changed and clear the interrupt
    uint8_t inputs = 0;
    if (i2cReadReg(TCA_ADDR, TCA_REG_INPUT, inputs)) {
      uint8_t changed = inputs ^ lastTcaInputs;
      lastTcaInputs = inputs;
      int intPin = digitalRead(PIN_TCA_INT);
      bool imuPinChanged = (changed & (1 << TCA_IMU_INT_PIN)) != 0;
      Serial.printf("[INT] IMU interrupt fired via TCA6408, inputs=0x%02X changed=0x%02X IMU_PIN=%d INT_PIN=%d\n",
                    inputs, changed, imuPinChanged ? 1 : 0, intPin);
    } else {
      Serial.println("[INT] TCA input read failed");
    }

    // Read IMU INT_STATUS to clear the latched interrupt
    uint8_t status = 0;
    if (i2cReadReg(IMU_ADDR, QMI_REG_INT_STATUS, status)) {
      Serial.printf("[INT] IMU INT_STATUS=0x%02X\n", status);
    }
  }
}
