#include "battery_system.h"
#include "tca6408.h"
#include "logger.h"

#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <cmath>
DEFINE_MODULE_LOGGER(BatteryLog)

namespace {
  // Hardware: VBAT -> (100k + 100k) divider -> ADC1_CH0 (GPIO1)
  constexpr adc1_channel_t ADC_CH = ADC1_CHANNEL_0;  // GPIO1
  constexpr adc_atten_t ADC_ATTEN = ADC_ATTEN_DB_12;
  constexpr adc_bits_width_t ADC_WIDTH = ADC_WIDTH_BIT_12;

  constexpr float EMA_ALPHA = 0.10f;             // voltage smoothing
  constexpr uint32_t SAMPLE_INTERVAL_BATTERY_MS = 15000;
  constexpr uint32_t SAMPLE_INTERVAL_PLUGGED_MS = 5000;
  constexpr uint8_t PERCENT_STEP = 1;            // max % change per update
  constexpr size_t AVG_WINDOW = 8;
  constexpr uint8_t USB_DETECT_BIT = 2;          // TCA6408 input bit for USB detect.
  constexpr bool USB_DETECT_ACTIVE_LOW = true;  // USB detect line reads LOW when USB is present.
  constexpr bool USB_DETECT_LOG = true;
  constexpr bool BATTERY_STATUS_LOG = true;

  constexpr float FULL_VOLTAGE = 4.15f;
  constexpr float STABLE_DELTA = 0.005f;         // 5 mV
  constexpr float RISE_DELTA = 0.002f;           // 2 mV
  constexpr uint32_t FULL_HOLD_TIME_MS = 180000; // 3 minutes

  esp_adc_cal_characteristics_t adcChars;
  uint32_t lastSampleMs = 0;
  uint16_t samples[AVG_WINDOW] = {0};
  size_t sampleCount = 0;
  size_t sampleIndex = 0;
  BatteryStatus status{0.0f, 0, false, ChargingState::UNKNOWN};
  float vbatPrev = 0.0f;
  bool vbatPrevValid = false;
  uint32_t highHoldMs = 0;
  bool percentInit = false;
  uint8_t lastUsbInputs = 0;
  bool lastUsbInputsValid = false;
  bool lastUsbValid = false;
  bool lastUsbPresent = false;
  bool lastUsbPresentValid = false;

  uint8_t voltageToPercent(float vbat) {
    // Simple table with linear interpolation between points
    struct Point { float v; uint8_t p; };
    static const Point table[] = {
      {4.20f, 100},
      {4.10f,  90},
      {4.00f,  80},
      {3.90f,  65},
      {3.80f,  50},
      {3.70f,  35},
      {3.60f,  20},
      {3.50f,  10},
      {3.40f,   5},
      {3.30f,   0},
    };
    if (vbat >= table[0].v) return 100;
    if (vbat <= table[sizeof(table)/sizeof(table[0])-1].v) return 0;
    for (size_t i = 0; i + 1 < sizeof(table)/sizeof(table[0]); ++i) {
      const auto& hi = table[i];
      const auto& lo = table[i + 1];
      if (vbat <= hi.v && vbat >= lo.v) {
        float t = (vbat - lo.v) / (hi.v - lo.v);
        float pct = lo.p + t * (hi.p - lo.p);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        return static_cast<uint8_t>(pct + 0.5f);
      }
    }
    return 0;
  }
  float vbatFiltered = 0.0f;
  bool filterInit = false;

  bool readUsbPresent(bool& present, uint8_t& inputs) {
    if (!TCA6408::readInputs(inputs)) {
      return false;
    }
    bool bitSet = ((inputs & (1u << USB_DETECT_BIT)) != 0);
    present = USB_DETECT_ACTIVE_LOW ? !bitSet : bitSet;
    return true;
  }
}

namespace BatterySystem {

void begin() {
  adc1_config_width(ADC_WIDTH);
  adc1_config_channel_atten(ADC_CH, ADC_ATTEN);
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN, ADC_WIDTH, 1100, &adcChars);
  filterInit = false;
  percentInit = false;
  vbatPrevValid = false;
  highHoldMs = 0;
  lastUsbPresentValid = false;
  status.state = ChargingState::UNKNOWN;
  status.charging = false;
}

void update() {
  uint32_t now = millis();
  bool usbPresent = false;
  uint8_t usbInputs = 0;
  bool usbValid = readUsbPresent(usbPresent, usbInputs);
  bool usbChanged = false;
  if (usbValid) {
    lastUsbValid = true;
    lastUsbInputs = usbInputs;
    lastUsbInputsValid = true;
    if (!lastUsbPresentValid) {
      lastUsbPresent = usbPresent;
      lastUsbPresentValid = true;
    } else if (usbPresent != lastUsbPresent) {
      usbChanged = true;
      lastUsbPresent = usbPresent;
    }
  } else {
    lastUsbValid = false;
  }
  if (USB_DETECT_LOG) {
    if (!usbValid) {
      if (lastUsbValid) {
        BatteryLog::println("[Battery] TCA6408 read failed");
      }
    } else if (!lastUsbInputsValid || usbInputs != lastUsbInputs) {
      BatteryLog::printf("[Battery] TCA inputs=0x%02X usbPresent=%d\n",
                         usbInputs, usbPresent ? 1 : 0);
      lastUsbInputs = usbInputs;
      lastUsbInputsValid = true;
    }
    lastUsbValid = usbValid;
  }
  uint32_t sampleInterval = usbValid && usbPresent
      ? SAMPLE_INTERVAL_PLUGGED_MS
      : SAMPLE_INTERVAL_BATTERY_MS;
  if (!usbChanged && (now - lastSampleMs < sampleInterval)) return;
  uint32_t dt = now - lastSampleMs;
  lastSampleMs = now;

  uint32_t raw = static_cast<uint32_t>(adc1_get_raw(ADC_CH));
  samples[sampleIndex] = static_cast<uint16_t>(raw);
  sampleIndex = (sampleIndex + 1) % AVG_WINDOW;
  if (sampleCount < AVG_WINDOW) sampleCount++;

  uint32_t sum = 0;
  for (size_t i = 0; i < sampleCount; ++i) sum += samples[i];
  uint32_t avgRaw = sum / sampleCount;
  uint32_t mv = esp_adc_cal_raw_to_voltage(avgRaw, &adcChars); // mV at ADC pin

  // Guard: no battery or floating ADC
  if (mv < 200) {
    status.voltage = 0.0f;
    status.percent = 0;
    status.charging = false;
    status.state = ChargingState::UNKNOWN;
    highHoldMs = 0;
    return;
  }

  // Divider compensation (calibratable)
  float vbat = (mv / 1000.0f) * 2.05f;

  // Initialize EMA on first valid read
  if (!filterInit) {
    vbatFiltered = vbat;
    filterInit = true;
  } else {
    vbatFiltered = vbatFiltered * (1.0f - EMA_ALPHA) + vbat * EMA_ALPHA;
  }

  float dv = 0.0f;
  if (vbatPrevValid) {
    dv = vbatFiltered - vbatPrev;
  }

  /* TODO: Fix charging detection. For now, disable it.
  // WORKAROUND: If the USB detection pin is unreliable, use voltage as a backup heuristic.
  // A voltage above 4.17V is a strong indicator of being on charger power.
  bool presumedCharging = (vbatFiltered > 4.17f);
  bool isPluggedIn = (usbValid && usbPresent) || presumedCharging;

  ChargingState nextState = ChargingState::UNKNOWN;
  if (!isPluggedIn) {
    nextState = ChargingState::ON_BATTERY;
    highHoldMs = 0;
  } else {
    bool rising = vbatPrevValid && (dv > RISE_DELTA);
    if (vbatFiltered >= FULL_VOLTAGE && fabsf(dv) < STABLE_DELTA) {
      if (highHoldMs < FULL_HOLD_TIME_MS) {
        highHoldMs += dt;
      }
    } else {
      highHoldMs = 0;
    }
    bool fullReady = (vbatFiltered >= FULL_VOLTAGE) &&
                     (fabsf(dv) < STABLE_DELTA) &&
                     (highHoldMs >= FULL_HOLD_TIME_MS);
    nextState = fullReady ? ChargingState::PLUGGED_IN_FULL
                          : ChargingState::PLUGGED_IN_CHARGING;
  }
  */
  ChargingState nextState = ChargingState::ON_BATTERY;

  status.state = nextState;
  status.charging = (nextState == ChargingState::PLUGGED_IN_CHARGING);

  // Convert filtered voltage to percent
  uint8_t targetPercent = voltageToPercent(vbatFiltered);

  if (!percentInit) {
    status.percent = targetPercent;
    percentInit = true;
  } else if (nextState == ChargingState::ON_BATTERY) {
    if (targetPercent < status.percent) {
      status.percent = max<uint8_t>(status.percent - PERCENT_STEP, targetPercent);
    }
  } else if (nextState == ChargingState::PLUGGED_IN_CHARGING) {
    if (targetPercent > status.percent) {
      status.percent = min<uint8_t>(status.percent + PERCENT_STEP, targetPercent);
    }
  } else if (nextState == ChargingState::PLUGGED_IN_FULL) {
    if (status.percent < 99) {
      status.percent = min<uint8_t>(status.percent + PERCENT_STEP, static_cast<uint8_t>(99));
    } else {
      status.percent = min<uint8_t>(status.percent, static_cast<uint8_t>(99));
    }
  }

  status.voltage = vbatFiltered;
  vbatPrev = vbatFiltered;
  vbatPrevValid = true;

  if (BATTERY_STATUS_LOG) {
    BatteryLog::printf("[Battery] vbat=%.3fV percent=%u state=%d usb=%d valid=%d inputs=0x%02X\n",
                       static_cast<double>(vbatFiltered),
                       status.percent,
                       static_cast<int>(status.state),
                       usbPresent ? 1 : 0,
                       usbValid ? 1 : 0,
                       usbInputs);
  }
}

BatteryStatus getStatus() {
  return status;
}

void getUsbDebug(uint8_t& inputs, bool& present, bool& valid) {
  inputs = lastUsbInputs;
  present = lastUsbPresent;
  valid = lastUsbValid && lastUsbInputsValid && lastUsbPresentValid;
}

}  // namespace BatterySystem
