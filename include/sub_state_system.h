#pragma once

#include <Arduino.h>
#include "display_system.h"

namespace SubStateSystem {

// Snapshot of sub-states and the resulting emotion rules for this tick.
struct Snapshot {
  bool sub_irritable;
  bool sub_sluggish;
  bool sub_withdrawn;
  bool sub_uncomfortable;
  bool sub_depressed;

  // Emotion gating
  bool allowAllPositive;                // happy override rule
  uint8_t forceCount;                   // number of forced emotions (0-4)
  EyeEmotion forced[4];                 // ordered force set
  bool suppress[EYE_EMO_COUNT];         // per-emotion suppression flags

  // Idle speed gating
  bool suppressSpeedSlow;
  bool suppressSpeedNormal;
  bool suppressSpeedFast;
};

void begin();
void update(Snapshot& out);  // call once per main update loop

}  // namespace SubStateSystem
