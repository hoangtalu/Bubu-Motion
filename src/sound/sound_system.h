#pragma once

#include <Arduino.h>

// Simple one-shot sound support.
// Example future hook (commented):
//   // In blink completion:
//   // if (!eye.popInProgress) SoundSystem::blinkClink();
namespace SoundSystem {
  void begin();              // init I2S / DAC
  void blinkClink();         // play the blink sound once (non-blocking)
  void eyeSwoosh(float strength); // play a short swoosh; strength 0..1 scales volume
  void eyeJitter(float strength); // very soft noise burst; strength 0..1 scales volume
  void happyPip(float strength);  // short stereo-ish pip; strength scales volume
  void mute(bool enabled);   // hard mute (OTA / critical ops)
}
