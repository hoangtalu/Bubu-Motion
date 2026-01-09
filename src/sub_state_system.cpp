#include "sub_state_system.h"
#include "care_system.h"
#include "level_system.h"

namespace SubStateSystem {

// Thresholds
static constexpr int LOW_HUNGER        = 30;
static constexpr int LOW_MOOD          = 30;
static constexpr int LOW_ENERGY        = 25;
static constexpr int LOW_CLEANLINESS   = 25;
static constexpr int RECOVER_THRESHOLD = 80;
static constexpr int CALM_THRESHOLD    = 50;
static constexpr int PROMOTE_LOW_THRESHOLD = 30;

// Activation durations (ms) for "stays below" rules
static constexpr uint32_t PRIMARY_ACTIVATE_MS   = 20000;  // 20s continuous low
// change it in final version to 6 hours (add surprise factor after a long night sleep)
static constexpr uint32_t DEPRESSED_LONG_MS     = 60000;  // any one low very long

struct State {
  bool sub_irritable      = false;
  bool sub_sluggish       = false;
  bool sub_withdrawn      = false;
  bool sub_uncomfortable  = false;
  bool sub_depressed      = false;

  // timers for continuous-low tracking (0 = not currently low)
  uint32_t hungerLowSince      = 0;
  uint32_t energyLowSince      = 0;
  uint32_t moodLowSince        = 0;
  uint32_t cleanLowSince       = 0;
};

static State g;

void begin() {
  g = State{};
}

static bool sustainedLow(uint32_t sinceMs, uint32_t nowMs, uint32_t thresholdMs) {
  return (sinceMs != 0) && (nowMs - sinceMs >= thresholdMs);
}

static void resetAllPrimary(State& s) {
  s.sub_irritable = s.sub_sluggish = s.sub_withdrawn = s.sub_uncomfortable = false;
  s.hungerLowSince = s.energyLowSince = s.moodLowSince = s.cleanLowSince = 0;
}

void update(Snapshot& out) {
  uint32_t now = millis();

  // Read current stats
  int hunger      = CareSystem::getHunger();
  int energy      = CareSystem::getEnergy();
  int mood        = CareSystem::getMood();
  int cleanliness = CareSystem::getCleanliness();

  // Global happy override + recovery
  bool allHigh = hunger >= RECOVER_THRESHOLD &&
                 energy >= RECOVER_THRESHOLD &&
                 mood >= RECOVER_THRESHOLD &&
                 cleanliness >= RECOVER_THRESHOLD;
  bool allAboveCalm = hunger > CALM_THRESHOLD &&
                      energy > CALM_THRESHOLD &&
                      mood > CALM_THRESHOLD &&
                      cleanliness > CALM_THRESHOLD;
  if (allHigh) {
    resetAllPrimary(g);
    g.sub_depressed = false;
  }

  // Individual primary recovery (instant)
  if (hunger >= RECOVER_THRESHOLD) {
    g.sub_irritable = false;
    g.hungerLowSince = 0;
  }
  if (energy >= RECOVER_THRESHOLD) {
    g.sub_sluggish = false;
    g.energyLowSince = 0;
  }
  if (mood >= RECOVER_THRESHOLD) {
    g.sub_withdrawn = false;
    g.moodLowSince = 0;
  }
  if (cleanliness >= RECOVER_THRESHOLD) {
    g.sub_uncomfortable = false;
    g.cleanLowSince = 0;
  }

  // Track continuous low periods for activation
  auto trackLow = [&](int value, int lowThr, uint32_t& sinceTs) {
    if (value < lowThr) {
      if (sinceTs == 0) sinceTs = now;
    } else {
      sinceTs = 0;  // reset if value recovers above low threshold
    }
  };

  trackLow(hunger, LOW_HUNGER, g.hungerLowSince);
  trackLow(energy, LOW_ENERGY, g.energyLowSince);
  trackLow(mood, LOW_MOOD, g.moodLowSince);
  trackLow(cleanliness, LOW_CLEANLINESS, g.cleanLowSince);

  // Primary activations (only when staying low long enough)
  if (!g.sub_irritable && sustainedLow(g.hungerLowSince, now, PRIMARY_ACTIVATE_MS)) {
    g.sub_irritable = true;
  }
  if (!g.sub_sluggish && sustainedLow(g.energyLowSince, now, PRIMARY_ACTIVATE_MS)) {
    g.sub_sluggish = true;
  }
  if (!g.sub_withdrawn && sustainedLow(g.moodLowSince, now, PRIMARY_ACTIVATE_MS)) {
    g.sub_withdrawn = true;
  }
  if (!g.sub_uncomfortable && sustainedLow(g.cleanLowSince, now, PRIMARY_ACTIVATE_MS)) {
    g.sub_uncomfortable = true;
  }

  // Depressed rule: 2+ primaries OR any primary low for very long
  if (!allHigh) {
    int primaryCount = static_cast<int>(g.sub_irritable) +
                       static_cast<int>(g.sub_sluggish) +
                       static_cast<int>(g.sub_withdrawn) +
                       static_cast<int>(g.sub_uncomfortable);
    bool veryLongLow = sustainedLow(g.hungerLowSince, now, DEPRESSED_LONG_MS) ||
                       sustainedLow(g.energyLowSince, now, DEPRESSED_LONG_MS) ||
                       sustainedLow(g.moodLowSince, now, DEPRESSED_LONG_MS) ||
                       sustainedLow(g.cleanLowSince, now, DEPRESSED_LONG_MS);
    g.sub_depressed = (primaryCount >= 2) || veryLongLow;
  } else {
    g.sub_depressed = false;
  }

  // Build snapshot output
  out.sub_irritable = g.sub_irritable;
  out.sub_sluggish = g.sub_sluggish;
  out.sub_withdrawn = g.sub_withdrawn;
  out.sub_uncomfortable = g.sub_uncomfortable;
  out.sub_depressed = g.sub_depressed;

  // Happy override clears gates
  out.allowAllPositive = allHigh;
  out.forceCount = 0;
  for (int i = 0; i < EYE_EMO_COUNT; ++i) out.suppress[i] = false;
  out.suppressSpeedSlow = false;
  out.suppressSpeedNormal = false;
  out.suppressSpeedFast = false;

  if (allAboveCalm) {
    out.suppress[EYE_EMO_ANGRY1] = true;
    out.suppress[EYE_EMO_ANGRY2] = true;
    out.suppress[EYE_EMO_ANGRY3] = true;
    out.suppress[EYE_EMO_WORRIED1] = true;
    out.suppress[EYE_EMO_SAD1] = true;
    out.suppress[EYE_EMO_SAD2] = true;
  }

  if (!allHigh) {
    // Suppression stacking
    if (g.sub_depressed) {
      out.suppress[EYE_EMO_HAPPY1] = true;
      out.suppress[EYE_EMO_HAPPY2] = true;
      out.suppress[EYE_EMO_EXCITED] = true;
      out.suppress[EYE_EMO_LOVE] = true;
    }
    if (g.sub_sluggish) {
      out.suppress[EYE_EMO_EXCITED] = true;
    }
    if (g.sub_irritable) {
      out.suppress[EYE_EMO_HAPPY1] = true;
      out.suppress[EYE_EMO_HAPPY2] = true;
      out.suppress[EYE_EMO_LOVE] = true;
    }
    if (g.sub_withdrawn) {
      out.suppress[EYE_EMO_CURIOUS] = true;
      out.suppress[EYE_EMO_LOVE] = true;
    }
    if (g.sub_uncomfortable) {
      out.suppress[EYE_EMO_HAPPY1] = true;
      out.suppress[EYE_EMO_HAPPY2] = true;
    }

    // Forced emotions by priority
    if (g.sub_depressed) {
      out.forceCount = 3;
      out.forced[0] = EYE_EMO_SAD1;
      out.forced[1] = EYE_EMO_SAD2;
      out.forced[2] = EYE_EMO_TIRED;
    } else if (g.sub_sluggish) {
      out.forceCount = 1;
      out.forced[0] = EYE_EMO_TIRED;
    } else if (g.sub_irritable) {
      out.forceCount = 1;
      out.forced[0] = EYE_EMO_ANGRY1;
    } else if (g.sub_withdrawn) {
      out.forceCount = 2;
      out.forced[0] = EYE_EMO_SAD1;
      out.forced[1] = EYE_EMO_SAD2;
    } else if (g.sub_uncomfortable) {
      out.forceCount = 1;
      out.forced[0] = EYE_EMO_ANGRY1;
    }
  }

  if (allAboveCalm && out.forceCount > 0) {
    uint8_t writeIdx = 0;
    for (uint8_t i = 0; i < out.forceCount; ++i) {
      EyeEmotion emo = out.forced[i];
      if (emo == EYE_EMO_ANGRY1 ||
          emo == EYE_EMO_ANGRY2 ||
          emo == EYE_EMO_ANGRY3 ||
          emo == EYE_EMO_WORRIED1 ||
          emo == EYE_EMO_SAD1 ||
          emo == EYE_EMO_SAD2) {
        continue;
      }
      out.forced[writeIdx++] = emo;
    }
    out.forceCount = writeIdx;
  }

  int minStat = hunger;
  if (energy < minStat) minStat = energy;
  if (mood < minStat) minStat = mood;
  if (cleanliness < minStat) minStat = cleanliness;
  bool bandHigh = minStat > CALM_THRESHOLD;
  bool bandMid = (!bandHigh && minStat > PROMOTE_LOW_THRESHOLD);

  bool allow[EYE_EMO_COUNT] = {false};
  allow[EYE_EMO_IDLE] = true;
  if (bandHigh) {
    allow[EYE_EMO_HAPPY1] = true;
    allow[EYE_EMO_HAPPY2] = true;
    allow[EYE_EMO_CURIOUS] = true;
    allow[EYE_EMO_CURIOUS1] = true;
    allow[EYE_EMO_CURIOUS2] = true;
    allow[EYE_EMO_EXCITED] = true;
    allow[EYE_EMO_LOVE] = true;
  } else if (bandMid) {
    allow[EYE_EMO_WORRIED1] = true;
    allow[EYE_EMO_SAD1] = true;
    allow[EYE_EMO_SAD2] = true;
    allow[EYE_EMO_ANGRY1] = true;
    allow[EYE_EMO_ANGRY2] = true;
    allow[EYE_EMO_ANGRY3] = true;
  } else {
    allow[EYE_EMO_ANGRY1] = true;
    allow[EYE_EMO_ANGRY2] = true;
    allow[EYE_EMO_ANGRY3] = true;
    allow[EYE_EMO_SAD1] = true;
    allow[EYE_EMO_SAD2] = true;
  }

  for (int i = 0; i < EYE_EMO_COUNT; ++i) {
    if (!allow[i]) {
      out.suppress[i] = true;
    }
  }
  if (out.forceCount > 0) {
    uint8_t writeIdx = 0;
    for (uint8_t i = 0; i < out.forceCount; ++i) {
      EyeEmotion emo = out.forced[i];
      if (!allow[emo]) {
        continue;
      }
      out.forced[writeIdx++] = emo;
    }
    out.forceCount = writeIdx;
  }

  // Idle speed gating
  if (g.sub_sluggish || g.sub_depressed) {
    out.suppressSpeedFast = true;
    out.suppressSpeedNormal = true;
  }
  if (g.sub_withdrawn) {
    out.suppressSpeedFast = true;
  }
  if (g.sub_irritable) {
    out.suppressSpeedSlow = true;
    out.suppressSpeedNormal = true;
  }

  // --- Level System Gating (Final Override) ---
  // This is the final step. If a feature isn't unlocked, it gets suppressed
  // regardless of what the sub-state logic decided.
  if (!LevelSystem::isUnlocked(LevelSystem::EMO_EXCITED))   { out.suppress[EYE_EMO_EXCITED] = true; }
  if (!LevelSystem::isUnlocked(LevelSystem::EMO_ANGRY1))     { out.suppress[EYE_EMO_ANGRY1] = true; }
  if (!LevelSystem::isUnlocked(LevelSystem::EMO_LOVE))       { out.suppress[EYE_EMO_LOVE] = true; }
  // SAD1 and HAPPY1 are level 1, but we can still gate them for consistency.
  if (!LevelSystem::isUnlocked(LevelSystem::EMO_SAD1))       { out.suppress[EYE_EMO_SAD1] = true; }
  if (!LevelSystem::isUnlocked(LevelSystem::EMO_HAPPY1))     { out.suppress[EYE_EMO_HAPPY1] = true; }

  // Gate idle speeds. The specific animations (jitter, giggle) will be gated
  // in the DisplaySystem where they are chosen.
  if (!LevelSystem::isUnlocked(LevelSystem::IDLE_SPEED_FAST)) { out.suppressSpeedFast = true; }
}

}  // namespace SubStateSystem
