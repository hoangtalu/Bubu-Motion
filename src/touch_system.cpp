#include "touch_system.h"

#include <Arduino.h>
#include <Wire.h>

#include "board_pins.h"
#include "logger.h"
DEFINE_MODULE_LOGGER(TouchLog)

// Forward declarations for functions used in implementation
namespace TouchSystem {
  // These need to be in the header if used externally
  bool isTcaActive();
  bool hasRecentSample(uint32_t windowMs);
  uint32_t lastSampleTimestamp();
}

namespace {

constexpr uint8_t CST816_ADDR = 0x15;
constexpr uint8_t TCA6408_ADDR = 0x20;

// Hardware gesture IDs from CST816S datasheet
constexpr uint8_t HW_GESTURE_NONE = 0x00;
constexpr uint8_t HW_GESTURE_SWIPE_UP = 0x01;
constexpr uint8_t HW_GESTURE_SWIPE_DOWN = 0x02;
constexpr uint8_t HW_GESTURE_SWIPE_LEFT = 0x03;
constexpr uint8_t HW_GESTURE_SWIPE_RIGHT = 0x04;
constexpr uint8_t HW_GESTURE_SINGLE_CLICK = 0x05;
constexpr uint8_t HW_GESTURE_DOUBLE_CLICK = 0x0B;
constexpr uint8_t HW_GESTURE_LONG_PRESS = 0x0C;

volatile bool touchInterruptFlag = false;
volatile bool tcaInterruptFlag = false;

TouchPoint pendingEvent;
bool eventAvailable = false;

// Touch state tracking
struct TouchState {
  bool isDown = false;
  uint32_t downTime = 0;
  uint16_t downX = 0;
  uint16_t downY = 0;
  uint16_t currentX = 0;
  uint16_t currentY = 0;
  uint32_t lastReadTime = 0;
  uint8_t fingerCount = 0;
  bool longPressFired = false;
} touch;

// Gesture thresholds (tuned for 240x240 screen)
constexpr uint32_t LONG_PRESS_MS = 400;      
constexpr uint16_t TAP_MAX_DRIFT_PX = 35;    // Increased for easier taps
constexpr uint16_t SWIPE_MIN_DIST_PX = 40;   
constexpr uint32_t RELEASE_TIMEOUT_MS = 120; // Tighter timeout
constexpr uint32_t DEBOUNCE_MS = 20;         // Very short debounce

void IRAM_ATTR touchISR() {
  touchInterruptFlag = true;
  tcaInterruptFlag = true;
}

void resetCST816() {
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(50);
}

bool readTouchData(uint8_t& gestureID, uint8_t& fingerCount, uint16_t& x, uint16_t& y) {
  // CRITICAL: Read gesture and finger count from registers 0x01-0x02
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0x01);  // Start at gesture register
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(CST816_ADDR, static_cast<size_t>(1), true) != 1) {
    return false;
  }
  gestureID = Wire.read();

  // Read finger count
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0x02);
  Wire.endTransmission(false);
  if (Wire.requestFrom(CST816_ADDR, static_cast<size_t>(1), true) != 1) {
    return false;
  }
  fingerCount = Wire.read() & 0x0F;

  // Validate finger count
  if (fingerCount == 0 || fingerCount > 2) {
    return false;
  }

  // Read X coordinate (registers 0x03-0x04)
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0x03);
  Wire.endTransmission(false);
  if (Wire.requestFrom(CST816_ADDR, static_cast<size_t>(1), true) != 1) {
    return false;
  }
  uint8_t x_h = Wire.read();

  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0x04);
  Wire.endTransmission(false);
  if (Wire.requestFrom(CST816_ADDR, static_cast<size_t>(1), true) != 1) {
    return false;
  }
  uint8_t x_l = Wire.read();

  // Read Y coordinate (registers 0x05-0x06)
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0x05);
  Wire.endTransmission(false);
  if (Wire.requestFrom(CST816_ADDR, static_cast<size_t>(1), true) != 1) {
    return false;
  }
  uint8_t y_h = Wire.read();

  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0x06);
  Wire.endTransmission(false);
  if (Wire.requestFrom(CST816_ADDR, static_cast<size_t>(1), true) != 1) {
    return false;
  }
  uint8_t y_l = Wire.read();

  // Reconstruct 12-bit coordinates
  int raw_x = ((x_h & 0x0F) << 8) | x_l;
  int raw_y = ((y_h & 0x0F) << 8) | y_l;

  // Sanity check
  if (raw_x > 500 || raw_y > 500 || raw_x < 0 || raw_y < 0) {
    return false;
  }

  // Map to screen coordinates (EXACTLY as working code does it)
  int mapped_x = 0xFF - ((x_h << 8) | x_l) & 0x0FFF;
  int mapped_y = ((y_h << 8) | y_l) & 0x0FFF;

  // Apply display rotation=1 (90°) to align touch with screen orientation
  int rot_x = mapped_y;
  int rot_y = 239 - mapped_x;

  // Boundary check
  if (rot_x < 0 || rot_x >= 240 || rot_y < 0 || rot_y >= 240) {
    return false;
  }

  x = static_cast<uint16_t>(rot_x);
  y = static_cast<uint16_t>(rot_y);

  return true;
}

uint16_t calculateDistance(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
  int16_t dx = static_cast<int16_t>(x2) - static_cast<int16_t>(x1);
  int16_t dy = static_cast<int16_t>(y2) - static_cast<int16_t>(y1);
  // Manhattan distance (faster than sqrt, good enough for gesture detection)
  return static_cast<uint16_t>(abs(static_cast<int>(dx)) + abs(static_cast<int>(dy)));
}

void emitGesture(TouchGesture gesture, uint16_t x, uint16_t y, uint32_t duration) {
  pendingEvent.gesture = gesture;
  pendingEvent.x = x;
  pendingEvent.y = y;
  pendingEvent.duration = duration;
  eventAvailable = true;

  const char* gestureName[] = {
    "NONE", "TAP", "LONG_PRESS", "LONG", "SWIPE_UP", "SWIPE_DOWN", "SWIPE_LEFT", "SWIPE_RIGHT"
  };
  TouchLog::printf("[Touch] %s at (%d,%d) dur=%lums\n", 
                gestureName[gesture], x, y, duration);
}

void handleTouchDown(uint16_t x, uint16_t y, uint32_t now) {
  touch.isDown = true;
  touch.downTime = now;
  touch.downX = x;
  touch.downY = y;
  touch.currentX = x;
  touch.currentY = y;
  touch.lastReadTime = now;
  touch.longPressFired = false;
  
  TouchLog::printf("[Touch] DOWN at (%d,%d)\n", x, y);
}

void handleTouchMove(uint16_t x, uint16_t y, uint32_t now) {
  touch.currentX = x;
  touch.currentY = y;
  touch.lastReadTime = now;
}

void checkLongPress(uint32_t now) {
  if (touch.longPressFired || !touch.isDown) {
    return;
  }

  uint32_t heldDuration = now - touch.downTime;
  if (heldDuration < LONG_PRESS_MS) {
    return;
  }

  // Check if finger stayed relatively still
  uint16_t drift = calculateDistance(touch.downX, touch.downY, 
                                     touch.currentX, touch.currentY);
  
  TouchLog::printf("[Touch] Long press check: held=%lums, drift=%upx (max=%u)\n", 
                heldDuration, drift, TAP_MAX_DRIFT_PX);
  
  if (drift <= TAP_MAX_DRIFT_PX) {
    touch.longPressFired = true;
    emitGesture(TOUCH_LONG_PRESS, touch.downX, touch.downY, heldDuration);
    TouchLog::println("[Touch] *** LONG PRESS FIRED ***");
  } else {
    TouchLog::printf("[Touch] Long press rejected - too much drift (%u > %u)\n", 
                  drift, TAP_MAX_DRIFT_PX);
  }
}

void handleTouchRelease(uint32_t now) {
  uint32_t duration = now - touch.downTime;
  
  // If long press already fired, don't emit another gesture
  if (touch.longPressFired) {
    TouchLog::printf("[Touch] RELEASE after long press (dur=%lums)\n", duration);
    touch.isDown = false;
    return;
  }

  // Ignore very short taps (debounce)
  if (duration < DEBOUNCE_MS) {
    TouchLog::printf("[Touch] IGNORED - too short (%lums)\n", duration);
    touch.isDown = false;
    return;
  }

  // Calculate motion
  int16_t deltaX = static_cast<int16_t>(touch.currentX) - static_cast<int16_t>(touch.downX);
  int16_t deltaY = static_cast<int16_t>(touch.currentY) - static_cast<int16_t>(touch.downY);
  uint16_t totalDrift = static_cast<uint16_t>(abs(static_cast<int>(deltaX)) + abs(static_cast<int>(deltaY)));

  TouchLog::printf("[Touch] Release analysis: dur=%lums, drift=%upx, dx=%d, dy=%d\n", 
                duration, totalDrift, deltaX, deltaY);

  TouchGesture gesture = TOUCH_TAP;
  uint16_t reportX = touch.downX;
  uint16_t reportY = touch.downY;

  if (totalDrift >= SWIPE_MIN_DIST_PX) {
    // Clear swipe detected
    int adx = abs(static_cast<int>(deltaX));
    int ady = abs(static_cast<int>(deltaY));
    if (adx > ady) {
      gesture = (deltaX > 0) ? TOUCH_SWIPE_RIGHT : TOUCH_SWIPE_LEFT;
    } else {
      gesture = (deltaY > 0) ? TOUCH_SWIPE_DOWN : TOUCH_SWIPE_UP;
    }
    // Report end position for swipes
    reportX = touch.currentX;
    reportY = touch.currentY;
    TouchLog::printf("[Touch] -> Classified as SWIPE (drift %u >= %u)\n", totalDrift, SWIPE_MIN_DIST_PX);
  } else {
    // Tap (with or without small drift)
    gesture = TOUCH_TAP;
    reportX = touch.downX;
    reportY = touch.downY;
    TouchLog::printf("[Touch] -> Classified as TAP (drift %u < %u)\n", totalDrift, SWIPE_MIN_DIST_PX);
  }

  emitGesture(gesture, reportX, reportY, duration);
  touch.isDown = false;
}

}  // anonymous namespace

namespace TouchSystem {

void begin() {
  TouchLog::println("[TouchSystem] Initializing...");
  
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  delay(10);
  
  // Initialize TCA6408 (IO expander) - all inputs
  Wire.beginTransmission(TCA6408_ADDR);
  Wire.write(0x03);  // Configuration register
  Wire.write(0xFF);  // All inputs
  Wire.endTransmission(true);
  delay(10);
  
  // Reset touch controller
  resetCST816();
  
  // Read chip ID
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0xA7);
  Wire.endTransmission(false);
  Wire.requestFrom(CST816_ADDR, static_cast<size_t>(1), true);
  uint8_t chipID = Wire.read();
  
  TouchLog::printf("[TouchSystem] Chip ID: 0x%02X", chipID);
  if (chipID == 0xB4) {
    TouchLog::println(" (CST816S) ✓");
  } else if (chipID == 0xB5) {
    TouchLog::println(" (CST816T) ✓");
  } else if (chipID == 0xB6) {
    TouchLog::println(" (CST816D) ✓");
  } else {
    TouchLog::println(" (Unknown)");
  }
  
  // Configure interrupt control (register 0xFA)
  // Enable touch change interrupt (bit 5)
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0xFA);
  Wire.write(0x20);  // EnChange = 1
  Wire.endTransmission(true);
  delay(10);
  
  // Configure long press time if needed (register 0xEB)
  // Default is 100 (~1 second), we want 50 (~0.5 seconds)
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0xEB);
  Wire.write(50);
  Wire.endTransmission(true);
  delay(10);
  
  // Setup interrupt pin
  pinMode(PIN_TCA_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_TCA_INT), touchISR, FALLING);
  
  TouchLog::println("[TouchSystem] Ready!");
}

void update() {
  uint32_t now = millis();
  static uint32_t lastUpdate = 0;
  
  // Limit update rate to reduce I2C bus congestion
  if ((now - lastUpdate) < 5) {
    return;
  }
  lastUpdate = now;
  
  // Poll TCA6408 for touch interrupt status
  Wire.beginTransmission(TCA6408_ADDR);
  Wire.write(0x00);  // Input port register
  Wire.endTransmission(false);
  if (Wire.requestFrom(TCA6408_ADDR, static_cast<size_t>(1), true) == 1) {
    uint8_t tcaInput = Wire.read();
    // Bit 0 is touch interrupt (active low = touch present)
    if ((tcaInput & 0x01) == 0x00) {
      touchInterruptFlag = true;
    }
  }
  
  // If touch is down, always try to read new coordinates
  if (touch.isDown) {
    checkLongPress(now);
    
    // Try to read new coordinates
    uint8_t gestureID, fingerCount;
    uint16_t x, y;
    
    if (readTouchData(gestureID, fingerCount, x, y)) {
      if (fingerCount > 0) {
        // Update position (only log if significant movement)
        uint16_t dist = calculateDistance(touch.currentX, touch.currentY, x, y);
        if (dist > 5) {
          TouchLog::printf("[Touch] MOVE to (%d,%d), delta=(%d,%d)\n", 
                        x, y, 
                        (int)x - (int)touch.downX, 
                        (int)y - (int)touch.downY);
        }
        handleTouchMove(x, y, now);
      } else {
        // Finger lifted
        TouchLog::println("[Touch] Finger lifted -> RELEASE");
        handleTouchRelease(now);
        return;
      }
    } else {
      // Can't read data - check timeout
      if ((now - touch.lastReadTime) >= RELEASE_TIMEOUT_MS) {
        TouchLog::println("[Touch] Read timeout -> RELEASE");
        handleTouchRelease(now);
        return;
      }
    }
  }
  
  // Check for new touch down
  if (!touch.isDown && touchInterruptFlag) {
    touchInterruptFlag = false;
    
    uint8_t gestureID, fingerCount;
    uint16_t x, y;
    
    if (readTouchData(gestureID, fingerCount, x, y)) {
      if (fingerCount > 0) {
        handleTouchDown(x, y, now);
        TouchLog::printf("[Touch] DOWN at (%d,%d)\n", x, y);
      }
    }
  }
}

bool available() {
  return eventAvailable;
}

TouchPoint get() {
  eventAvailable = false;
  return pendingEvent;
}

void lvgl_init() {
  // LVGL integration if needed
}

// Compatibility functions
bool isTcaActive() {
  return false;
}

bool hasRecentSample(uint32_t windowMs) {
  return (millis() - touch.lastReadTime) < windowMs;
}

TouchPoint getLastPoint() {
  return pendingEvent;
}

uint32_t lastSampleTimestamp() {
  return touch.lastReadTime;
}

uint32_t getTouchDownCount() {
  static uint32_t count = 0;
  if (eventAvailable && pendingEvent.gesture != TOUCH_NONE) count++;
  return count;
}

uint32_t getFinalEventCount() {
  return getTouchDownCount();
}

bool isTouchPressed() {
  return touch.isDown;
}

bool consumeTcaInterrupt() {
  bool fired = false;
  noInterrupts();
  fired = tcaInterruptFlag;
  tcaInterruptFlag = false;
  interrupts();
  return fired;
}

}  // namespace TouchSystem
