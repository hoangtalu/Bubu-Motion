#include "eye_game.h"

#include <Arduino.h>
#include <math.h>

namespace EyeGame {

namespace {

enum EyeColorType {
  EYE_COL_GREEN = 0,
  EYE_COL_RED,
  EYE_COL_BLUE,
  EYE_COL_PURPLE,
  EYE_COL_YELLOW,
  EYE_COL_CYAN,
  EYE_COL_COUNT
};

struct State {
  bool running = false;
  CareSystem::StatId rewardStat = CareSystem::STAT_MOOD;
  Config cfg;
  EyeColorType leftColor = EYE_COL_RED;
  EyeColorType rightColor = EYE_COL_BLUE;
  uint16_t leftColor565 = 0;
  uint16_t rightColor565 = 0;
  uint32_t nextChangeMs = 0;
  uint16_t rounds = 0;
  uint8_t score = 0;
  GameResult lastResult = GAME_NONE;
};

State state;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t colorFromType(EyeColorType type) {
  switch (type) {
    case EYE_COL_GREEN:  return rgb565(0, 255, 0);
    case EYE_COL_RED:    return rgb565(255, 0, 0);
    case EYE_COL_BLUE:   return rgb565(0, 0, 255);
    case EYE_COL_PURPLE: return rgb565(128, 0, 255);
    case EYE_COL_YELLOW: return rgb565(255, 255, 0);
    case EYE_COL_CYAN:   return rgb565(0, 255, 255);
    default:             return rgb565(255, 255, 255);
  }
}

EyeColorType randomColorType() {
  uint8_t roll = static_cast<uint8_t>(random(0, 100));
  if (roll < 25) {
    return EYE_COL_GREEN;
  }
  EyeColorType pool[] = {EYE_COL_RED, EYE_COL_BLUE, EYE_COL_PURPLE, EYE_COL_YELLOW, EYE_COL_CYAN};
  size_t poolSize = sizeof(pool) / sizeof(pool[0]);
  return pool[random(0, poolSize)];
}

void refreshColor565(uint32_t nowMs) {
  (void)nowMs;
  state.leftColor565 = colorFromType(state.leftColor);
  state.rightColor565 = colorFromType(state.rightColor);
}

void applyReward() {
  int reward = static_cast<int>(state.score) * static_cast<int>(state.cfg.rewardPerHit);
  if (reward == 0) return;

  switch (state.rewardStat) {
    case CareSystem::STAT_HUNGER:      CareSystem::addHunger(reward); break;
    case CareSystem::STAT_MOOD:        CareSystem::addMood(reward); break;
    case CareSystem::STAT_ENERGY:      CareSystem::addEnergy(reward); break;
    case CareSystem::STAT_CLEANLINESS: CareSystem::addCleanliness(reward); break;
  }
}

void finishGame(GameResult result) {
  state.running = false;
  state.lastResult = result;

  if (result == GAME_FINISH_NORMAL) {
    applyReward();
  }
}

void applyWrongTapPenalty() {
  CareSystem::addMood(state.cfg.wrongTapMoodDelta);
  CareSystem::addEnergy(state.cfg.wrongTapEnergyDelta);
}

void scheduleNextChange() {
  // Change eye colors every 1-2 seconds; plasma removed.
  uint32_t interval = static_cast<uint32_t>(random(1000, 2001));
  state.nextChangeMs = millis() + interval;

  state.leftColor = randomColorType();
  state.rightColor = randomColorType();
  refreshColor565(millis());
  state.rounds++;

  if (state.rounds >= state.cfg.maxRounds) {
    finishGame(GAME_FINISH_NORMAL);
  }
}

}  // namespace

void configure(const Config &cfg) {
  state.cfg = cfg;
}

void start(CareSystem::StatId rewardStat) {
  state.running = true;
  state.rewardStat = rewardStat;
  state.rounds = 0;
  state.score = 0;
  state.lastResult = GAME_NONE;
  scheduleNextChange();
}

void stop() {
  if (!state.running) return;
  // Reward any accumulated points on manual stop
  applyReward();
  state.running = false;
  state.lastResult = GAME_FINISH_NORMAL;
}

void update() {
  if (!state.running) return;

  if (millis() >= state.nextChangeMs) {
    scheduleNextChange();
  }
}

void handleTap(int x, int y) {
  if (!state.running) return;

  bool isLeft = (x < 120);
  EyeColorType tappedColor = isLeft ? state.leftColor : state.rightColor;

  if (tappedColor == EYE_COL_GREEN) {
    state.score++;
    scheduleNextChange();
  } else {
    applyReward();
    applyWrongTapPenalty();
    finishGame(GAME_FINISH_WRONG_TAP);
  }
}

bool isRunning() {
  return state.running;
}

uint8_t getScore() {
  return state.score;
}

GameResult getLastResult() {
  return state.lastResult;
}

uint8_t getRewardPerHit() {
  return state.cfg.rewardPerHit;
}

uint16_t getLeftColor565() {
  return state.leftColor565;
}

uint16_t getRightColor565() {
  return state.rightColor565;
}

bool isLeftPlasma() {
  return false;
}

bool isRightPlasma() {
  return false;
}

uint32_t getLeftPlasmaSeed() {
  return 0;
}

uint32_t getRightPlasmaSeed() {
  return 0;
}

}  // namespace EyeGame
