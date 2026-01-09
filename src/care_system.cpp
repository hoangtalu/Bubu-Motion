#include "care_system.h"
#include "level_system.h"
#include <Preferences.h>

// 0–100 range
static const int STAT_MIN = 0;
static const int STAT_MAX = 100;

// “Need attention” band
static const int ATTENTION_MIN = 20;
static const int ATTENTION_MAX = 39;

// Decay schedule (minutes per -1)
static const uint8_t HUNGER_DECAY_MIN      = 6;   // -1 / 6 min
static const uint8_t MOOD_DECAY_MIN        = 8;   // -1 / 8 min
static const uint8_t ENERGY_DECAY_MIN      = 5;   // -1 / 5 min
static const uint8_t CLEANLINESS_DECAY_MIN = 10;  // -1 / 10 min
static const uint32_t SAVE_INTERVAL_MS = 10UL * 60UL * 1000UL;
static const int DEFAULT_STAT_VALUE = 30;

// We tick every 60s and accumulate minutes
static const uint32_t DECAY_TICK_MS = 60UL * 1000UL;

namespace CareSystem {

  static int hunger      = 80;
  static int mood        = 80;
  static int energy      = 80;
  static int cleanliness = 80;

  static uint32_t lastDecayMs    = 0;
  static uint32_t hungerAccMin   = 0;
  static uint32_t moodAccMin     = 0;
  static uint32_t energyAccMin   = 0;
  static uint32_t cleanAccMin    = 0;
  static uint32_t lastSaveMs     = 0;
  static bool decaySuspended     = false;
  static Preferences prefs;
  static bool prefsReady         = false;

  static int clampStat(int v) {
    if (v < STAT_MIN) return STAT_MIN;
    if (v > STAT_MAX) return STAT_MAX;
    return v;
  }

  static void applySnapshot(bool hasSnapshot) {
    if (hasSnapshot) {
      hunger      = prefs.getInt("h", DEFAULT_STAT_VALUE);
      mood        = prefs.getInt("m", DEFAULT_STAT_VALUE);
      energy      = prefs.getInt("e", DEFAULT_STAT_VALUE);
      cleanliness = prefs.getInt("c", DEFAULT_STAT_VALUE);
    } else {
      hunger = mood = energy = cleanliness = DEFAULT_STAT_VALUE;
    }
    hunger      = clampStat(hunger);
    mood        = clampStat(mood);
    energy      = clampStat(energy);
    cleanliness = clampStat(cleanliness);
  }

  static void saveSnapshot() {
    if (!prefsReady) return;
    prefs.putBool("has", true);
    prefs.putInt("h", hunger);
    prefs.putInt("m", mood);
    prefs.putInt("e", energy);
    prefs.putInt("c", cleanliness);
  }

  static void applyDecay(uint32_t minutes) {
    if (minutes == 0) return;

    hungerAccMin   += minutes;
    moodAccMin     += minutes;
    energyAccMin   += minutes;
    cleanAccMin    += minutes;

    // Hunger
    if (HUNGER_DECAY_MIN > 0 && hungerAccMin >= HUNGER_DECAY_MIN) {
      uint32_t steps = hungerAccMin / HUNGER_DECAY_MIN;
      hunger -= static_cast<int>(steps);
      hungerAccMin -= steps * HUNGER_DECAY_MIN;
    }

    // Mood
    if (MOOD_DECAY_MIN > 0 && moodAccMin >= MOOD_DECAY_MIN) {
      uint32_t steps = moodAccMin / MOOD_DECAY_MIN;
      mood -= static_cast<int>(steps);
      moodAccMin -= steps * MOOD_DECAY_MIN;
    }

    // Energy
    if (ENERGY_DECAY_MIN > 0 && energyAccMin >= ENERGY_DECAY_MIN) {
      uint32_t steps = energyAccMin / ENERGY_DECAY_MIN;
      energy -= static_cast<int>(steps);
      energyAccMin -= steps * ENERGY_DECAY_MIN;
    }

    // Cleanliness
    if (CLEANLINESS_DECAY_MIN > 0 && cleanAccMin >= CLEANLINESS_DECAY_MIN) {
      uint32_t steps = cleanAccMin / CLEANLINESS_DECAY_MIN;
      cleanliness -= static_cast<int>(steps);
      cleanAccMin -= steps * CLEANLINESS_DECAY_MIN;
    }

    hunger      = clampStat(hunger);
    mood        = clampStat(mood);
    energy      = clampStat(energy);
    cleanliness = clampStat(cleanliness);
  }

  void begin() {
    prefsReady = prefs.begin("care_stats", false);
    if (prefsReady) {
      bool hasSnapshot = prefs.getBool("has", false);
      applySnapshot(hasSnapshot);
      if (!hasSnapshot) {
        saveSnapshot();
      }
    } else {
      hunger = mood = energy = cleanliness = DEFAULT_STAT_VALUE;
    }

    lastDecayMs  = millis();
    hungerAccMin = moodAccMin = energyAccMin = cleanAccMin = 0;
    lastSaveMs = lastDecayMs;
  }

  void update() {
    uint32_t now = millis();
    if (lastDecayMs == 0) {
      lastDecayMs = now;
      return;
    }
    if (decaySuspended) {
      lastDecayMs = now;
      lastSaveMs = now;
      return;
    }

    uint32_t elapsed = now - lastDecayMs;
    if (elapsed >= DECAY_TICK_MS) {
      uint32_t minutes = elapsed / DECAY_TICK_MS;
      lastDecayMs += minutes * DECAY_TICK_MS;
      applyDecay(minutes);
    }

    if (prefsReady && lastSaveMs != 0 && (now - lastSaveMs) >= SAVE_INTERVAL_MS) {
      saveSnapshot();
      lastSaveMs = now;
    }
  }

  void setDecaySuspended(bool suspended) {
    if (decaySuspended == suspended) return;
    decaySuspended = suspended;
    uint32_t now = millis();
    lastDecayMs = now;
    lastSaveMs = now;
  }

  // --- modifiers ---
  void addHunger(int v) {
    int oldValue = hunger;
    hunger = clampStat(hunger + v);
    if (v > 0 && oldValue < STAT_MAX) {
      int recovered = hunger - oldValue;
      int xp = recovered / 10;
      if (xp > 0) LevelSystem::addXP(xp);
    }
  }
  void addMood(int v) {
    int oldValue = mood;
    mood = clampStat(mood + v);
    if (v > 0 && oldValue < STAT_MAX) {
      int recovered = mood - oldValue;
      int xp = recovered / 10;
      if (xp > 0) LevelSystem::addXP(xp);
    }
  }
  void addEnergy(int v) {
    int oldValue = energy;
    energy = clampStat(energy + v);
    if (v > 0 && oldValue < STAT_MAX) {
      int recovered = energy - oldValue;
      int xp = recovered / 10;
      if (xp > 0) LevelSystem::addXP(xp);
    }
  }
  void addCleanliness(int v) {
    int oldValue = cleanliness;
    cleanliness = clampStat(cleanliness + v);
    if (v > 0 && oldValue < STAT_MAX) {
      int recovered = cleanliness - oldValue;
      int xp = recovered / 10;
      if (xp > 0) LevelSystem::addXP(xp);
    }
  }

  // --- getters ---
  int getHunger()      { return hunger; }
  int getMood()        { return mood; }
  int getEnergy()      { return energy; }
  int getCleanliness() { return cleanliness; }

  // --- attention flags ---
  bool needsAttention() {
    auto inBand = [](int v) {
      return (v >= ATTENTION_MIN && v <= ATTENTION_MAX);
    };
    return inBand(hunger) || inBand(mood) || inBand(energy) || inBand(cleanliness);
  }

  bool isCritical() {
    return hunger == 0 || mood == 0 || energy == 0 || cleanliness == 0;
  }

}  // namespace CareSystem
