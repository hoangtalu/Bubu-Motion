#pragma once
#include <Arduino.h>
#include "care_system.h"

namespace EyeGame {

  struct Config {
    uint16_t maxRounds           = 40;  // number of color changes before auto-end
    uint8_t  rewardPerHit        = 5;   // stat points per correct tap
    int8_t   wrongTapMoodDelta   = -10; // penalty on wrong tap
    int8_t   wrongTapEnergyDelta = -5;
  };

  enum GameResult {
    GAME_NONE,
    GAME_FINISH_NORMAL,
    GAME_FINISH_WRONG_TAP
  };

  void configure(const Config &cfg);             // for tuning at startup
  void start(CareSystem::StatId rewardStat = CareSystem::STAT_MOOD);
  void stop();                                   // user backed out, no reward/penalty

  void update();                                 // call each frame in loop()
  void handleTap(int x, int y);                  // TAP on eye area while game running

  bool       isRunning();
  uint8_t    getScore();
  GameResult getLastResult();
  uint8_t    getRewardPerHit();
  bool       isLeftPlasma();
  bool       isRightPlasma();
  uint32_t   getLeftPlasmaSeed();
  uint32_t   getRightPlasmaSeed();

  // For the eye renderer:
  uint16_t getLeftColor565();
  uint16_t getRightColor565();
}
