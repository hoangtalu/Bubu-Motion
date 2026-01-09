#pragma once
#include <Arduino.h>

void DisplaySystem_begin();
void DisplaySystem_update();

enum EyeEmotion {
  EYE_EMO_IDLE = 0,
  EYE_EMO_CURIOUS,
  EYE_EMO_ANGRY1,
  EYE_EMO_LOVE,
  EYE_EMO_TIRED,
  EYE_EMO_EXCITED,
  EYE_EMO_ANGRY2,
  EYE_EMO_ANGRY3,
  EYE_EMO_WORRIED1,
  EYE_EMO_CURIOUS1,
  EYE_EMO_CURIOUS2,
  EYE_EMO_SAD1,
  EYE_EMO_SAD2,
  EYE_EMO_HAPPY1,
  EYE_EMO_HAPPY2,
  EYE_EMO_COUNT
};

EyeEmotion DisplaySystem_getEmotion();
void DisplaySystem_setEmotion(EyeEmotion emotion);
void DisplaySystem_setEmotionWeight(EyeEmotion emotion, uint16_t weight);
EyeEmotion DisplaySystem_pickNextEmotionWeighted();
void DisplaySystem_startExcitedNow();
void DisplaySystem_startSleep();
bool DisplaySystem_isHatching();

// Notify display system of user interaction (touch/gesture) for idle visual logic
void DisplaySystem_notifyUserInteraction(uint32_t nowMs);
