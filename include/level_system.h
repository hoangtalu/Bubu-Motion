#pragma once
#include <Arduino.h>

namespace LevelSystem {

  // Enum for all features that can be unlocked.
  // This makes the system easily expandable for future OTA updates.
  enum FeatureID {
    // Idle Behaviors
    IDLE_JITTER,
    IDLE_GIGGLE,
    IDLE_JUDGING,
    IDLE_SPEED_FAST,

    // Emotions
    EMO_EXCITED,
    EMO_ANGRY1,
    EMO_LOVE,
    EMO_SAD1,
    EMO_HAPPY1,

    // Add new features before this line
    FEATURE_COUNT
  };

  // --- Public API ---

  // Initializes the system, loads data from NVS, and sets defaults on first boot.
  void begin();

  // Adds experience points and handles level-ups.
  void addXP(int amount);

  // --- Getters ---

  // Returns the current level.
  int getLevel();

  // Returns the current XP.
  int getXP();

  // Returns the XP needed for the next level-up.
  int getXPForNextLevel();

  // Checks if a specific feature is unlocked at the current level.
  bool isUnlocked(FeatureID feature);

} // namespace LevelSystem
