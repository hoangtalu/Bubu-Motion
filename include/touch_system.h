#ifndef TOUCH_SYSTEM_H
#define TOUCH_SYSTEM_H

#include <stdint.h>

// Touch gesture types
enum TouchGesture {
  TOUCH_NONE = 0,
  TOUCH_TAP,           // Quick touch & release
  TOUCH_LONG_PRESS,    // Held for 500ms+ (fires once)
  TOUCH_LONG,          // Reserved/legacy
  TOUCH_SWIPE_UP,
  TOUCH_SWIPE_DOWN,
  TOUCH_SWIPE_LEFT,
  TOUCH_SWIPE_RIGHT
};

struct TouchPoint {
  uint16_t x;
  uint16_t y;
  TouchGesture gesture;
  uint32_t duration;
};

namespace TouchSystem {

// Initialize touch system (I2C, TCA6408, CST816)
void begin();

// Must be called regularly in loop() to process touch events
void update();

// Check if a new tap event is available
bool available();

// Get the most recent tap event (clears the pending flag)
TouchPoint get();

// Initialize LVGL input device driver (optional)
void lvgl_init();

// Get last known touch coordinates (even if not currently pressed)
TouchPoint getLastPoint();

// Get total number of touch-down events
uint32_t getTouchDownCount();

// Get total number of final processed events
uint32_t getFinalEventCount();

// Check if touch is currently pressed
bool isTouchPressed();

// Consume a TCA6408 interrupt edge (shared line).
bool consumeTcaInterrupt();

// Additional utility functions (from implementation)
bool isTcaActive();
bool hasRecentSample(uint32_t windowMs = 100);
uint32_t lastSampleTimestamp();

}  // namespace TouchSystem

#endif  // TOUCH_SYSTEM_H
