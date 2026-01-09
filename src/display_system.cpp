// =====================================================
// Includes & Forward Declarations
// =====================================================
#include "display_system.h"
#include "lgfx_setup.hpp"
#include "board_pins.h"
#include "touch_system.h"
#include "menu_system.h"
#include "eye_game.h"
#include "wifi_service.h"
#include "care_system.h"
#include "battery_system.h"
#include "sub_state_system.h"
#include "sound/sound_system.h"

#include <lvgl.h>
#include <esp_random.h>
#include <Preferences.h>
#include <time.h>
#include <esp_heap_caps.h>
#include "logger.h"
DEFINE_MODULE_LOGGER(DisplayLog)

// =====================================================
// Forward Declarations (types only, no variables)
// =====================================================

struct EyeRuntime;
struct ClockRuntime;

enum class IdleVisualState {
  Eyes,
  Clock
};

// Forward declarations for eye rendering.
static void EyeRenderer_drawFrame(int16_t topOffset);
static void EyeRenderer_drawFrame(int16_t topOffset, float scale);
static void EyeRenderer_drawFrame(int16_t topOffset, float scale, uint8_t blinkMask);
static void Emotion_scheduleNextPick();
static void Feed_start(uint32_t nowMs);
static void Feed_end();
static void Clean_start(uint32_t nowMs);
static void Clean_update(uint32_t nowMs);
static void Sleep_start(uint32_t nowMs);
static void Sleep_update(uint32_t nowMs);
static void Sleep_end(uint32_t nowMs);

// =====================================================
// Display HAL & LVGL Setup
// =====================================================
LGFX gfx;

static lgfx::LGFX_Sprite eyeCanvasA(&gfx);
static lgfx::LGFX_Sprite eyeCanvasB(&gfx);
static lgfx::LGFX_Sprite* eyeCanvasActive = &eyeCanvasA;
static lgfx::LGFX_Sprite* eyeCanvasBack = &eyeCanvasB;
static lv_obj_t* lvCanvas = nullptr;
static constexpr uint16_t LVGL_BUF_W = 240;
static constexpr uint16_t LVGL_BUF_H = 140;
static lv_color_t* lvglBuf = nullptr;  // allocated in PSRAM when available
static lv_draw_buf_t lvglDrawBuf;
static lv_display_t* lvglDisplay = nullptr;

static int blChannel = 0;
static constexpr uint8_t BACKLIGHT_FULL = 255;
static constexpr uint8_t BACKLIGHT_SLEEP = 128;


// =====================================================
// Geometry & Rendering Constants
// =====================================================
static constexpr uint16_t SCREEN_WIDTH = 240;
static constexpr uint16_t SCREEN_HEIGHT = 240;
static constexpr uint8_t EYE_SIZE = 80;
static constexpr uint8_t EYE_RADIUS = 24;
static constexpr uint8_t GAP = 10;
static constexpr uint8_t CLOSED_HEIGHT = 6;
static constexpr int16_t TIRED_EYE_HEIGHT = 40;
static constexpr uint8_t LOW_BATT_THRESHOLD_PERCENT = 20;
static constexpr int16_t POWER_RING_RADIUS = static_cast<int16_t>(SCREEN_WIDTH / 6);
static constexpr int16_t POWER_RING_OFFSCREEN_PCT = 70;
static constexpr int16_t POWER_RING_CENTER_Y =
    static_cast<int16_t>((POWER_RING_RADIUS * (100 - 2 * POWER_RING_OFFSCREEN_PCT)) / 100);
static constexpr uint32_t HATCH_TOTAL_MS = 300000;
static constexpr uint32_t HATCH_PHASE1_MS = 60000;
static constexpr uint32_t HATCH_PHASE2_MS = 90000;
static constexpr uint32_t HATCH_PHASE3_MS = 120000;
static constexpr uint32_t HATCH_PHASE4_MS = 30000;
static constexpr int16_t HATCH_BASE_SIZE = 90;
static constexpr bool HATCH_FORCE_RESET_ON_BOOT = false; // change to "false" to preserve hatch state across reboots


static constexpr float POP_SCALES[] = {1.0f, 1.15f, 1.28f, 1.15f, 1.0f};
static constexpr uint16_t POP_FRAME_DELAY = 30;
static constexpr size_t POP_FRAME_COUNT = sizeof(POP_SCALES) / sizeof(POP_SCALES[0]);
static constexpr float MAX_EYE_SCALE = 1.3f;  // for clearing during pop/game
static constexpr uint32_t POP_ANGRY_WINDOW_MS = 10000;
static constexpr uint8_t POP_ANGRY_COUNT = 10;
static constexpr uint8_t BLINK_LEFT_MASK = 0x01;
static constexpr uint8_t BLINK_RIGHT_MASK = 0x02;
static constexpr float HAPPY_SCALE = 1.15f;  // bean-like slightly larger eyes
static constexpr uint32_t HAPPY_DURATION_MS = 2000;
static constexpr float HAPPY_BOUNCE_FREQ_HZ = 2.0f;
static constexpr float HAPPY_BOUNCE_AMPLITUDE = 8.0f;
static constexpr int32_t BOUNCE_BASE_Y = 0;
static constexpr int32_t BOUNCE_AMPL   = 8;  // small idle bob
static constexpr int16_t GIGGLE_OFFSET_PX = 35;
static constexpr uint32_t GIGGLE_DURATION_MS = 1000;
static constexpr uint8_t  GIGGLE_JITTER_AMP = 6;
// --- Idle Jitter State tuning ---
static constexpr uint32_t JITTER_DURATION_MS = 420;
static constexpr uint8_t  JITTER_AMP_PX      = 5;
// Logging toggles
static constexpr bool IDLE_LOGS = false;
static constexpr uint32_t EYE_COLOR_FADE_MS = 500;

// Clean animation tuning
static constexpr uint32_t CLEAN_ANIM_DURATION_MS = 5000;
static constexpr uint8_t CLEAN_RAIN_DROP_COUNT = 80;
static constexpr uint8_t CLEAN_RAIN_W_MIN = 1;
static constexpr uint8_t CLEAN_RAIN_W_MAX = 1.2;
static constexpr uint8_t CLEAN_RAIN_L_MIN = 6;
static constexpr uint8_t CLEAN_RAIN_L_MAX = 8;
static constexpr uint8_t CLEAN_RAIN_SPEED_MIN = 200;
static constexpr uint8_t CLEAN_RAIN_SPEED_MAX = 220;
static constexpr uint32_t CLEAN_RAIN_COLOR = 0x8FCBFF;
static constexpr uint32_t FEED_ANIM_DURATION_MS = 5000;

// =====================================================
// Runtime State (Globals)
// =====================================================

// =====================================================
// Global Motion (applied to all objects)
// =====================================================

struct GlobalMotion {
  float offX;
  float offY;

  float targetOffX;
  float targetOffY;

  int16_t jitterX;
  int16_t jitterY;
  uint8_t jitterAmp;     // pixels
  uint16_t jitterDecay;  // ms to decay to zero
  uint32_t jitterUntil;  // millis
};

static GlobalMotion gMotion = {0, 0, 0, 0, 0, 0, 0, 0, 0};

// Simple helpers (no behavior change unless called)
static inline void GlobalMotion_setOffset(float x, float y) {
  gMotion.offX = x;
  gMotion.offY = y;
}

static inline void GlobalMotion_kickJitter(uint8_t amp, uint16_t decayMs) {
  gMotion.jitterAmp = amp;
  gMotion.jitterDecay = decayMs;
  gMotion.jitterUntil = millis() + decayMs;
  // seed jitter immediately
  gMotion.jitterX = (int16_t)random(-amp, amp + 1);
  gMotion.jitterY = (int16_t)random(-amp, amp + 1);
}

static inline void GlobalMotion_update(uint32_t nowMs) {
  if (gMotion.jitterAmp == 0) return;
  if (nowMs >= gMotion.jitterUntil) {
    gMotion.jitterX = 0; gMotion.jitterY = 0; gMotion.jitterAmp = 0; return;
  }
  // gentle refresh; no accumulation
  gMotion.jitterX = (int16_t)random(-gMotion.jitterAmp, gMotion.jitterAmp + 1);
  gMotion.jitterY = (int16_t)random(-gMotion.jitterAmp, gMotion.jitterAmp + 1);
}

// =====================================================
// Visual Object Model (DATA ONLY – not wired yet)
// =====================================================
static void ReturnVisualToNeutral();
static void UpdateVisualInterpolation(uint32_t dtMs);
enum class ObjId : uint8_t {
  LeftEye,
  RightEye,
  COUNT
};

struct VisualObject {
  ObjId    id;

  // Neutral / base reference
  int16_t baseX;
  int16_t baseY;
  int16_t baseW;
  int16_t baseH;

  // Runtime (current)
  int16_t offsetX;
  int16_t offsetY;
  float   scaleX;
  float   scaleY;

  // Runtime (targets)
  int16_t targetOffsetX;
  int16_t targetOffsetY;
  float   targetScaleX;
  float   targetScaleY;

  // Shape-specific
  int16_t radius;

  // Render controls
  bool    visible;
  int8_t  z;
};

static VisualObject* g_visualObjects = nullptr;
struct EyeRuntime {
  int16_t topOffset;
  float scale;
  bool blinkInProgress;
  bool popQueued;
  bool popInProgress;
  uint32_t popStartMs;
  uint32_t popWindowStartMs;
  uint8_t popWindowCount;
  int32_t bounceOffset;
};
static EyeRuntime eye = {0, 1.0f, false, false, false, 0, 0, 0, 0};

struct EyeColorRuntime {
  float currentR;
  float currentG;
  float currentB;
  float targetR;
  float targetG;
  float targetB;
  uint32_t lastUpdateMs;
};
static EyeColorRuntime eyeColor = {255.0f, 255.0f, 255.0f, 255.0f, 255.0f, 255.0f, 0};

struct EmotionRuntime {
  EyeEmotion currentEmotion;
  bool excitedActive;
  uint32_t excitedStartMs;
  uint32_t excitedEndMs;
  bool happyActive;
  uint32_t happyStartMs;
  uint32_t happyEndMs;
  uint32_t angryStartMs;
  uint32_t angryEndMs;
  uint32_t tiredStartMs;
  uint32_t tiredEndMs;
  uint32_t worriedStartMs;
  uint32_t worriedEndMs;
  uint32_t curiousStartMs;
  uint32_t curiousEndMs;
  uint32_t sadStartMs;
  uint32_t sadEndMs;
  uint32_t sad2StartMs;
  uint32_t sad2EndMs;
  uint32_t happy1StartMs;
  uint32_t happy1EndMs;
  uint32_t happy2StartMs;
  uint32_t happy2EndMs;
  uint32_t nextEmotionPickMs;
  uint16_t weights[EYE_EMO_COUNT];
};
static EmotionRuntime emotionState = {
  EYE_EMO_IDLE,
  false,
  0,
  0,
  false,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  {
    1,  // IDLE
    0,  // CURIOUS
    0,  // ANGRY1
    0,  // LOVE
    1,  // TIRED
    0,  // EXCITED (disabled)
    0,  // ANGRY2
    0,  // ANGRY3
    0,  // WORRIED1
    0,  // CURIOUS1
    0,  // CURIOUS2
    0,  // SAD1
    0,  // SAD2
    1,  // HAPPY1
    1   // HAPPY2
  }
};
static SubStateSystem::Snapshot subState{};

struct DisplayRuntime {
  uint32_t lastLvglTickMs;
  bool canvasHidden;
};
static DisplayRuntime display = {0, false};

struct TouchRuntime {
  bool suppressMenuOpenUntilLift;
  bool blockGesturesUntilLift;
};
static TouchRuntime touchState = {false, false};

struct RainDrop {
  float x;
  float y;
  float speed;
  uint8_t width;
  uint8_t length;
};

struct CleanAnimRuntime {
  bool active;
  bool returnToStats;
  uint32_t startMs;
  uint32_t endMs;
  uint32_t lastUpdateMs;
  RainDrop drops[CLEAN_RAIN_DROP_COUNT];
};
static CleanAnimRuntime cleanAnim = {false, false, 0, 0, 0, {}};

struct SleepZ {
  bool active;
  float x;
  float y;
  float speed;
  float driftX;
  uint32_t startMs;
  uint32_t durationMs;
  uint8_t sizeIdx;
  int16_t rotation;  // 0.1 degree units
};

struct SleepAnimRuntime {
  bool active;
  uint32_t startMs;
  uint32_t lastUpdateMs;
  uint32_t nextSpawnMs;
  SleepZ zs[6];
};
static SleepAnimRuntime sleepAnim = {false, 0, 0, 0, {}};

struct HatchRuntime {
  bool active;
  uint8_t phase;
  uint32_t startMs;
  uint32_t phaseStartMs;
  uint32_t tapBobStartMs;
  uint32_t tapBobDurationMs;
  float tapBobAmp;
  uint32_t phase2BoostUntilMs;
  bool moving;
  uint32_t moveStartMs;
  uint32_t moveDurationMs;
  uint32_t stopUntilMs;
  float posX;
  float posY;
  float moveStartX;
  float moveStartY;
  float moveTargetX;
  float moveTargetY;
  uint32_t twitchStartMs;
  uint32_t twitchDurationMs;
  float twitchX;
  float twitchY;
  bool blinkStarted;
  uint32_t blinkStartMs;
};
static HatchRuntime hatch = {};

static constexpr int16_t SLEEP_EYE_HEIGHT_PX = 10;
static constexpr int16_t SLEEP_BOB_AMPLITUDE_PX = 10;
static constexpr uint32_t SLEEP_BOB_PERIOD_MS = 4000;
static constexpr uint32_t SLEEP_Z_SPAWN_MIN_MS = 500;
static constexpr uint32_t SLEEP_Z_SPAWN_MAX_MS = 1000;
static constexpr uint32_t SLEEP_Z_LIFE_MIN_MS = 2200;
static constexpr uint32_t SLEEP_Z_LIFE_MAX_MS = 4200;
static constexpr float SLEEP_Z_SPEED_MIN = 8.0f;
static constexpr float SLEEP_Z_SPEED_MAX = 18.0f;
static constexpr float SLEEP_Z_DRIFT_MIN = -4.0f;
static constexpr float SLEEP_Z_DRIFT_MAX = 4.0f;
static constexpr int16_t SLEEP_Z_SPAWN_JITTER_X = 12;
static constexpr int16_t SLEEP_Z_SPAWN_JITTER_Y = 8;


static constexpr uint32_t IDLE_CLOCK_TIMEOUT_MS = 600000;   // 10 minutes inactivity
static constexpr uint32_t CLOCK_REFRESH_MS = 1000;         // tick clock text
static bool tzConfigured = false;

struct ClockRuntime {
  lv_obj_t* timeLabel;
  lv_obj_t* dateLabel;
  uint32_t lastTouchMs;
  uint32_t lastClockUpdateMs;
  IdleVisualState state;
  bool timeValid;
  uint64_t storedEpoch;
  uint32_t storedMsRef;
};
static ClockRuntime clockRt = {nullptr, nullptr, 0, 0, IdleVisualState::Eyes, false, 0, 0};
static Preferences clockPrefs;
static Preferences hatchPrefs;
static bool hatchPrefsReady = false;

struct IdleLookRuntime {
  bool active;
  int16_t destX;
  int16_t destY;
};
// -------------------------------
// STEP 2 — Speed integration
// -------------------------------

enum class IdleMoveSpeed : uint8_t {
  Slow,
  Normal,
  Fast
};
// -------------------------------------------------
// Idle State Machine (runtime foundation)
// -------------------------------------------------
// Idle States
// ⚠️ RULE: Any state that uses GlobalMotion_kickJitter()
// is a NEGATIVE / IRRITATION cue
// EXCEPT: Excited1, Giggle
enum class IdleStateType : uint8_t {
  None,

  // --- Negative / Irritation (jitter-based) ---
  Blink,
  Wink,
  JitterBoth,
  JitterLeft,
  JitterRight,

  // --- Positive / Neutral ---
  HappyBounce,
  Excited1,   // exception: jitter allowed (energy release)
  Giggle,     // exception: jitter allowed (social/nervous)
  Judging,

  Expand,
  TopOffsetUp,
  TopOffsetDown
};

struct IdleStateRuntime {
  IdleStateType type;
  bool active;
  uint32_t startMs;
  uint32_t durationMs;
};

// -------------------------------------------------
// Non-blocking Blink Runtime
// -------------------------------------------------
struct BlinkRuntime {
  bool active;
  bool left;
  bool right;
  uint32_t startMs;
};

static BlinkRuntime blinkRt = {false, false, false, 0};

static constexpr uint32_t BLINK_CLOSE_MS = 60;
static constexpr uint32_t BLINK_HOLD_MS  = 40;
static constexpr uint32_t BLINK_OPEN_MS  = 120;
static constexpr int16_t  BLINK_OFFSET_PX = 50;

static IdleMoveSpeed idleMoveSpeed = IdleMoveSpeed::Normal;
// -------------------------------
static IdleLookRuntime idleLook = {false, 0, 0};
static uint32_t idleLookNextAt = 0;

static void Blink_start(uint32_t nowMs, bool left, bool right);
static void Wink_start(uint32_t nowMs, bool leftEye);
static bool Blink_update(uint32_t nowMs);
static bool blinkSoundPlayed = false;

static IdleStateRuntime idleState = {
  IdleStateType::None,
  false,
  0,
  0
};
static bool happyPipPlayed = false;

static IdleMoveSpeed IdleMove_pickSpeed(const SubStateSystem::Snapshot& snapshot) {
  bool allowSlow = !snapshot.suppressSpeedSlow;
  bool allowNormal = !snapshot.suppressSpeedNormal;
  bool allowFast = !snapshot.suppressSpeedFast;
  int allowCount = static_cast<int>(allowSlow) +
                   static_cast<int>(allowNormal) +
                   static_cast<int>(allowFast);
  if (allowCount <= 0) {
    return IdleMoveSpeed::Normal;
  }
  int pick = random(0, allowCount);
  if (allowSlow) {
    if (pick == 0) return IdleMoveSpeed::Slow;
    --pick;
  }
  if (allowNormal) {
    if (pick == 0) return IdleMoveSpeed::Normal;
    --pick;
  }
  return IdleMoveSpeed::Fast;
}

static inline bool Emotion_isActive() {
  if (emotionState.happyActive || emotionState.excitedActive) {
    return true;
  }
  uint32_t now = millis();
  if (emotionState.angryEndMs > 0 && now < emotionState.angryEndMs) return true;
  if (emotionState.tiredEndMs > 0 && now < emotionState.tiredEndMs) return true;
  if (emotionState.worriedEndMs > 0 && now < emotionState.worriedEndMs) return true;
  if (emotionState.curiousEndMs > 0 && now < emotionState.curiousEndMs) return true;
  if (emotionState.sadEndMs > 0 && now < emotionState.sadEndMs) return true;
  if (emotionState.sad2EndMs > 0 && now < emotionState.sad2EndMs) return true;
  if (emotionState.happy1EndMs > 0 && now < emotionState.happy1EndMs) return true;
  if (emotionState.happy2EndMs > 0 && now < emotionState.happy2EndMs) return true;
  return false;
}

static inline bool Emotion_isReady(uint32_t nowMs) {
  return !Emotion_isActive() && nowMs >= emotionState.nextEmotionPickMs;
}

static bool IdleBehavior_isSuppressed(IdleStateType type, const SubStateSystem::Snapshot& snapshot) {
  if (snapshot.sub_depressed || snapshot.sub_irritable) {
    if (type == IdleStateType::HappyBounce ||
        type == IdleStateType::Excited1 ||
        type == IdleStateType::Giggle ||
        type == IdleStateType::Judging) {
      return true;
    }
  }
  if (snapshot.sub_uncomfortable || snapshot.sub_sluggish) {
    if (type == IdleStateType::JitterLeft ||
        type == IdleStateType::JitterRight ||
        type == IdleStateType::JitterBoth) {
      return true;
    }
  }
  return false;
}

struct IdleBehaviorWeight {
  IdleStateType type;
  uint8_t weight;
};

static constexpr IdleBehaviorWeight kIdleBehaviorWeights[] = {
  {IdleStateType::Blink, 8},
  {IdleStateType::Wink, 8},
  {IdleStateType::JitterLeft, 1},
  {IdleStateType::JitterRight, 1},
  {IdleStateType::JitterBoth, 1},
  {IdleStateType::HappyBounce, 8},
  {IdleStateType::Judging, 8},
  {IdleStateType::Excited1, 8},
  {IdleStateType::Giggle, 1}
};

static IdleStateType IdleBehavior_pickWeighted(const SubStateSystem::Snapshot& snapshot) {
  uint16_t totalWeight = 0;
  for (size_t i = 0; i < sizeof(kIdleBehaviorWeights) / sizeof(kIdleBehaviorWeights[0]); ++i) {
    const auto& entry = kIdleBehaviorWeights[i];
    if (entry.weight == 0 || IdleBehavior_isSuppressed(entry.type, snapshot)) {
      continue;
    }
    totalWeight = static_cast<uint16_t>(totalWeight + entry.weight);
  }
  if (totalWeight == 0) {
    return IdleStateType::Blink;
  }
  uint16_t roll = static_cast<uint16_t>(random(0, static_cast<long>(totalWeight)));
  for (size_t i = 0; i < sizeof(kIdleBehaviorWeights) / sizeof(kIdleBehaviorWeights[0]); ++i) {
    const auto& entry = kIdleBehaviorWeights[i];
    if (entry.weight == 0 || IdleBehavior_isSuppressed(entry.type, snapshot)) {
      continue;
    }
    if (roll < entry.weight) {
      return entry.type;
    }
    roll = static_cast<uint16_t>(roll - entry.weight);
  }
  return IdleStateType::Blink;
}

static void IdleLook_pickNewDestination() {
  int16_t dx = static_cast<int16_t>(random(-5, 6));
  int16_t dy = static_cast<int16_t>(random(-5, 6));
  idleLook.destX = dx;
  idleLook.destY = dy;
  gMotion.targetOffX = dx;
  gMotion.targetOffY = dy;
  // Pick movement speed for this hop
  idleMoveSpeed = IdleMove_pickSpeed(subState);
  // Trigger swoosh on movement: softer for normal hops, stronger for fast hops, none for slow
  if (idleMoveSpeed == IdleMoveSpeed::Fast || idleMoveSpeed == IdleMoveSpeed::Normal) {
    float dist = sqrtf(static_cast<float>(dx * dx + dy * dy));
    float strength = dist / 28.0f; // normalize to ~1 at max range (~28px diagonal)
    if (strength > 1.0f) strength = 1.0f;
    if (idleMoveSpeed == IdleMoveSpeed::Normal) {
      strength *= 0.45f; // softer/shorter for normal moves
    }
    SoundSystem::eyeSwoosh(strength);
  }
  if (IDLE_LOGS) {
    DisplayLog::printf("[IdleLook] New destination picked: dx=%d dy=%d\n",
                       idleLook.destX,
                       idleLook.destY);
    DisplayLog::printf("[IdleLook] Speed=%s\n",
      idleMoveSpeed == IdleMoveSpeed::Slow   ? "SLOW" :
      idleMoveSpeed == IdleMoveSpeed::Fast   ? "FAST" : "NORMAL");
  }
  idleLook.active = true;
}

static bool IdleLook_reachedDestination() {
  return (abs(gMotion.offX - gMotion.targetOffX) <= 1) &&
         (abs(gMotion.offY - gMotion.targetOffY) <= 1);
}

static void IdleLook_update(uint32_t nowMs) {
  uint32_t now = nowMs;
  if (idleState.active) {
    return;  // state owns time, movement must stop
  }
  bool menuOpen = MenuSystem::isOpen() || MenuSystem::isFeeding() ||
                  MenuSystem::isConnectOpen() || MenuSystem::isMessageOpen() ||
                  MenuSystem::isBatteryOpen() || MenuSystem::isStatsOpen() ||
                  MenuSystem::isOptionsOpen() || MenuSystem::isGamesOpen();
  bool gameActive = EyeGame::isRunning() || MenuSystem::isGameActive();
  bool clockVisible = (clockRt.state == IdleVisualState::Clock) && !display.canvasHidden;
  bool blocked = menuOpen || gameActive || clockVisible || eye.popInProgress || sleepAnim.active;

  // If blocked, DO NOT move targets — but DO allow timing to continue
  if (blocked) {
    if (IDLE_LOGS) {
      DisplayLog::printf("[IdleLook] BLOCKED: menu=%d game=%d clockVisible=%d blink=%d pop=%d\n",
                         menuOpen,
                         gameActive,
                         clockVisible,
                         eye.blinkInProgress,
                         eye.popInProgress);
    }
    idleLook.active = false;
    if (idleLookNextAt == 0) {
      idleLookNextAt = now + static_cast<uint32_t>(random(2000, 4001));
    }
    return;
  }

  if (Emotion_isActive() || Emotion_isReady(now)) {
    if (idleLook.active && IdleLook_reachedDestination()) {
      idleLook.active = false;
      idleLookNextAt = now + static_cast<uint32_t>(random(2000, 4001));
    }
    return;
  }

  if (!idleLook.active) {
    if (idleLookNextAt == 0 || now >= idleLookNextAt) {
      IdleLook_pickNewDestination();
    }
    return;
  }

  if (IdleLook_reachedDestination()) {
    if (IDLE_LOGS) {
      DisplayLog::printf("[IdleLook] Destination reached: offX=%d offY=%d\n",
                         gMotion.offX,
                         gMotion.offY);
    }
    idleLook.active = false;
    idleLookNextAt = now + static_cast<uint32_t>(random(2000, 4001));

    // Start blink, wink, jitter, happy bounce, judging, excited1, or giggle as idle state
    idleState.type = IdleBehavior_pickWeighted(subState);
    idleState.active = true;
    idleState.startMs = now;
    idleState.durationMs = (idleState.type == IdleStateType::HappyBounce || idleState.type == IdleStateType::Judging)
                             ? HAPPY_DURATION_MS
                             : (idleState.type == IdleStateType::Giggle)
                                 ? GIGGLE_DURATION_MS
                             : (idleState.type == IdleStateType::Blink ||
                                idleState.type == IdleStateType::Wink)
                                 ? (BLINK_CLOSE_MS + BLINK_HOLD_MS + BLINK_OPEN_MS)
                             : (idleState.type == IdleStateType::Excited1)
                                 ? 2000
                                 : JITTER_DURATION_MS;
  }
}

static void Pop_start(uint32_t nowMs) {
  // Pop disabled while using eye taps to trigger test emotions.
  (void)nowMs;
#if 0
  if (eye.popInProgress) return;
  eye.popInProgress = true;
  eye.popQueued = false;
  eye.popStartMs = nowMs;

  if (eye.popWindowStartMs == 0 ||
      nowMs - eye.popWindowStartMs > POP_ANGRY_WINDOW_MS) {
    eye.popWindowStartMs = nowMs;
    eye.popWindowCount = 0;
  }
  if (eye.popWindowCount < 255) {
    eye.popWindowCount++;
  }
  if (eye.popWindowCount >= POP_ANGRY_COUNT) {
    DisplaySystem_setEmotion(EYE_EMO_ANGRY1);
    Emotion_scheduleNextPick();
    eye.popWindowStartMs = nowMs;
    eye.popWindowCount = 0;
  }
#endif
}

static void Emotion_triggerTest(uint32_t nowMs) {
  // Placeholder: swap this emotion for quick testing.
  (void)nowMs;
  DisplaySystem_setEmotion(EYE_EMO_TIRED);
  Emotion_scheduleNextPick();
}

static void Pop_update(uint32_t nowMs, bool render) {
  if (!eye.popInProgress) return;
  uint32_t elapsed = nowMs - eye.popStartMs;
  size_t frame = static_cast<size_t>(elapsed / POP_FRAME_DELAY);
  if (frame >= POP_FRAME_COUNT) {
    eye.popInProgress = false;
    eye.scale = 1.0f;
    return;
  }
  eye.scale = POP_SCALES[frame];
  if (render) {
    EyeRenderer_drawFrame(eye.topOffset, eye.scale, 0);
  }
}

static void Blink_start(uint32_t nowMs, bool left, bool right) {
  if (blinkRt.active) return;
  blinkRt.active = true;
  blinkRt.left = left;
  blinkRt.right = right;
  blinkRt.startMs = nowMs;
  eye.blinkInProgress = true;
  blinkSoundPlayed = false;
  if (IDLE_LOGS) {
    DisplayLog::printf("[Blink] Start: left=%d right=%d\n", left ? 1 : 0, right ? 1 : 0);
  }
}

static void Wink_start(uint32_t nowMs, bool leftEye) {
  if (blinkRt.active) return;
  blinkRt.active = true;
  blinkRt.left = leftEye;
  blinkRt.right = !leftEye;
  blinkRt.startMs = nowMs;
  eye.blinkInProgress = true;
}

static bool Blink_update(uint32_t nowMs) {
  if (!blinkRt.active) return false;

  uint32_t t = nowMs - blinkRt.startMs;
  int16_t offset = 0;

  if (t < BLINK_CLOSE_MS) {
    float p = static_cast<float>(t) / BLINK_CLOSE_MS;
    offset = static_cast<int16_t>(BLINK_OFFSET_PX * p);
  } else if (t < BLINK_CLOSE_MS + BLINK_HOLD_MS) {
    offset = BLINK_OFFSET_PX;
    if (!blinkSoundPlayed) {
      SoundSystem::blinkClink();
      blinkSoundPlayed = true;
    }
  } else if (t < BLINK_CLOSE_MS + BLINK_HOLD_MS + BLINK_OPEN_MS) {
    float p = static_cast<float>(t - BLINK_CLOSE_MS - BLINK_HOLD_MS) / BLINK_OPEN_MS;
    offset = static_cast<int16_t>(BLINK_OFFSET_PX * (1.0f - p));
  } else {
    blinkRt.active = false;
    eye.topOffset = 0;
    eye.blinkInProgress = false;
    blinkSoundPlayed = false;
    return true;
  }

  // Apply blink (both eyes for now)
  eye.topOffset = offset;
  return false;
}

// =====================================================
// Eye Hitboxes & Geometry Helpers
// =====================================================
// Eye hit boxes (centered on screen)
struct EyeBox {
  int16_t x, y, w, h;
};

static EyeBox leftEyeBox;
static EyeBox rightEyeBox;

static void Display_backlightInit() {
  ledcSetup(blChannel, 5000, 8);
  ledcAttachPin(PIN_LCD_BL, blChannel);
  ledcWrite(blChannel, BACKLIGHT_FULL);
}

static inline void Display_setBacklight(uint8_t level) {
  ledcWrite(blChannel, level);
}

// Flush LVGL draw buffer to the display via LovyanGFX
static void Display_lvglFlush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  if (area == nullptr || px_map == nullptr) {
    lv_display_flush_ready(disp);
    return;
  }

  const uint16_t w = static_cast<uint16_t>(area->x2 - area->x1 + 1);
  const uint16_t h = static_cast<uint16_t>(area->y2 - area->y1 + 1);

  gfx.startWrite();
  gfx.setAddrWindow(area->x1, area->y1, w, h);
  gfx.writePixels(reinterpret_cast<lgfx::rgb565_t*>(px_map), static_cast<size_t>(w) * h);
  gfx.endWrite();

  lv_display_flush_ready(disp);
}

static void Display_initLvglCanvas() {
  lv_init();

  lvglDisplay = lv_display_create(gfx.width(), gfx.height());
  lv_display_set_color_format(lvglDisplay, LV_COLOR_FORMAT_RGB565);

  // Allocate LVGL draw buffer in PSRAM ONLY (fail if unavailable)
  if (!lvglBuf) {
    size_t bufBytes = static_cast<size_t>(LVGL_BUF_W) * LVGL_BUF_H * sizeof(lv_color_t);
    lvglBuf = static_cast<lv_color_t*>(
        heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (lvglBuf) {
      DisplayLog::printf("[Display] LVGL buffer allocated in PSRAM (%u bytes)\n",
                    static_cast<unsigned>(bufBytes));
    } else {
      DisplayLog::println("[Display] FATAL: PSRAM alloc for LVGL buffer failed");
      while (true) { delay(1000); }
    }
  }

  lv_draw_buf_init(&lvglDrawBuf,
                   LVGL_BUF_W,
                   LVGL_BUF_H,
                   LV_COLOR_FORMAT_RGB565,
                   LV_STRIDE_AUTO,
                   lvglBuf,
                   static_cast<size_t>(LVGL_BUF_W) * LVGL_BUF_H * sizeof(lv_color_t));
  lv_display_set_draw_buffers(lvglDisplay, &lvglDrawBuf, nullptr);
  lv_display_set_flush_cb(lvglDisplay, Display_lvglFlush);
  // Ensure LVGL root background is black so fades don't show white
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
  lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
  
  // Enable LVGL timer for animations
  lv_timer_resume(lv_display_get_refr_timer(lvglDisplay));

  eyeCanvasA.setPsram(true);  // store sprite buffer in PSRAM if available
  eyeCanvasA.setColorDepth(16);
  eyeCanvasA.createSprite(gfx.width(), gfx.height());
  eyeCanvasB.setPsram(true);
  eyeCanvasB.setColorDepth(16);
  eyeCanvasB.createSprite(gfx.width(), gfx.height());
  eyeCanvasActive = &eyeCanvasA;
  eyeCanvasBack = &eyeCanvasB;
  
  lvCanvas = lv_canvas_create(lv_screen_active());
  lv_canvas_set_buffer(lvCanvas,
                       eyeCanvasActive->getBuffer(),
                       eyeCanvasActive->width(),
                       eyeCanvasActive->height(),
                       LV_COLOR_FORMAT_RGB565);
  lv_canvas_fill_bg(lvCanvas, lv_color_black(), LV_OPA_COVER);
  
  // Set canvas to background (Layer 0)
  lv_obj_move_background(lvCanvas);
}

// -----------------------------------------------------
// Clock UI helpers
// -----------------------------------------------------
static void Clock_createLabels() {
  // Container-less labels on root screen
  clockRt.timeLabel = lv_label_create(lv_screen_active());
  lv_obj_set_style_text_color(clockRt.timeLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(clockRt.timeLabel, &lv_font_montserrat_40, 0);
  lv_label_set_text(clockRt.timeLabel, "--:--");
  lv_obj_align(clockRt.timeLabel, LV_ALIGN_CENTER, 0, -5);
  lv_obj_set_style_opa(clockRt.timeLabel, LV_OPA_TRANSP, 0);

  clockRt.dateLabel = lv_label_create(lv_screen_active());
  lv_obj_set_style_text_color(clockRt.dateLabel, lv_color_white(), 0);
  lv_obj_set_style_text_font(clockRt.dateLabel, &lv_font_montserrat_vn_20, 0);
  lv_label_set_text(clockRt.dateLabel, "--/--/--");
  lv_obj_align_to(clockRt.dateLabel, clockRt.timeLabel, LV_ALIGN_OUT_BOTTOM_MID, -10, 6);
  lv_obj_set_style_opa(clockRt.dateLabel, LV_OPA_TRANSP, 0);
}

static void Clock_ensureTz() {
  if (tzConfigured) return;
  // Vietnam: UTC+7, no DST. POSIX TZ uses negative sign for east-of-UTC offsets.
  setenv("TZ", "ICT-7", 1);
  tzset();
  tzConfigured = true;
}

static void Clock_setOpacity(uint8_t opa) {
  if (clockRt.timeLabel) lv_obj_set_style_opa(clockRt.timeLabel, opa, 0);
  if (clockRt.dateLabel) lv_obj_set_style_opa(clockRt.dateLabel, opa, 0);
}

static void Clock_fadeTo(uint8_t targetOpa) {
  Clock_setOpacity(targetOpa);
}

static void Eyes_fadeTo(uint8_t targetOpa) {
  if (!lvCanvas) return;
  lv_obj_set_style_opa(lvCanvas, targetOpa, LV_PART_MAIN);
}

static void Clock_loadStored() {
  if (!clockPrefs.begin("clock", true)) return;
  clockRt.storedEpoch = clockPrefs.getULong64("epoch", 0);
  clockRt.storedMsRef = clockPrefs.getUInt("msref", 0);
  clockPrefs.end();
  if (clockRt.storedEpoch > 0) {
    clockRt.timeValid = true;
    // Use current uptime as reference so stored epoch advances correctly after reboot
    clockRt.storedMsRef = millis();
  }
}

static void Clock_store(time_t epoch, uint32_t msRef) {
  if (!clockPrefs.begin("clock", false)) return;
  clockPrefs.putULong64("epoch", (uint64_t)epoch);
  clockPrefs.putUInt("msref", msRef);
  clockPrefs.end();
}

static time_t Clock_now(uint32_t nowMs) {
  // Always prefer a fresh RTC/NTP reading when available
  Clock_ensureTz();
  time_t sysNow = time(nullptr);
  if (sysNow > 1600000000) {
    // If system time differs from stored, refresh the cached epoch
    int64_t drift = (int64_t)sysNow - (int64_t)clockRt.storedEpoch;
    if (!clockRt.timeValid || llabs(drift) > 5) {
      clockRt.timeValid = true;
      clockRt.storedEpoch = (uint64_t)sysNow;
      clockRt.storedMsRef = nowMs;
      Clock_store(sysNow, nowMs);
      return sysNow;
    }
  }

  if (clockRt.timeValid) {
    uint32_t deltaMs = nowMs - clockRt.storedMsRef;
    return (time_t)(clockRt.storedEpoch + (deltaMs / 1000));
  }
  return 0;
}

static void Clock_tryNtpSync(uint32_t nowMs) {
  static uint32_t lastAttemptMs = 0;
  if (wifiGetState() != WifiState::CONNECTED) return;
  if (nowMs - lastAttemptMs < 10000) return;
  lastAttemptMs = nowMs;
  Clock_ensureTz();
  // Use TZ-aware helper so SNTP updates honor the timezone
  configTzTime("ICT-7", "pool.ntp.org", "time.nist.gov");
}

static void Clock_updateLabels(uint32_t nowMs) {
  if (!clockRt.timeLabel || !clockRt.dateLabel) return;
  if (nowMs - clockRt.lastClockUpdateMs < CLOCK_REFRESH_MS) return;
  clockRt.lastClockUpdateMs = nowMs;

  time_t t = Clock_now(nowMs);
  if (t == 0) return;
  struct tm tmInfo;
  localtime_r(&t, &tmInfo);

  char buf[6];
  strftime(buf, sizeof(buf), "%H:%M", &tmInfo);
  lv_label_set_text(clockRt.timeLabel, buf);

  char dbuf[9];
  strftime(dbuf, sizeof(dbuf), "%d/%m/%y", &tmInfo);
  lv_label_set_text(clockRt.dateLabel, dbuf);
}

static void Clock_show() {
  Clock_fadeTo(LV_OPA_COVER);
  Eyes_fadeTo(LV_OPA_TRANSP);
  clockRt.state = IdleVisualState::Clock;
}

static void Clock_hide() {
  Clock_fadeTo(LV_OPA_TRANSP);
  Eyes_fadeTo(LV_OPA_COVER);
  clockRt.state = IdleVisualState::Eyes;
}

static void Clock_updateIdle(uint32_t nowMs) {
  if (sleepAnim.active) return;
  if (MenuSystem::isFeeding()) return;
  Clock_tryNtpSync(nowMs);
  // Prime time as soon as it becomes available (even while eyes are showing)
  if (!clockRt.timeValid) {
    Clock_now(nowMs);
  }
  if (clockRt.state == IdleVisualState::Eyes) {
    if (nowMs - clockRt.lastTouchMs >= IDLE_CLOCK_TIMEOUT_MS) {
      Clock_updateLabels(nowMs);
      Clock_show();
    }
  } else {
    Clock_updateLabels(nowMs);
  }
}

static void Display_setCanvasVisible(bool visible) {
  if (!lvCanvas) return;
  if (visible && display.canvasHidden) {
    lv_obj_clear_flag(lvCanvas, LV_OBJ_FLAG_HIDDEN);
    display.canvasHidden = false;
    DisplayLog::println("[Display] Canvas visible (eyes layer)");
  } else if (!visible && !display.canvasHidden) {
    lv_obj_add_flag(lvCanvas, LV_OBJ_FLAG_HIDDEN);
    display.canvasHidden = true;
    DisplayLog::println("[Display] Canvas hidden (overlay active)");
  }
}

static void Display_calculateEyeBoxes() {
  // Center eyes on screen perfectly
  const int cx = SCREEN_WIDTH / 2;   // 120
  const int cy = SCREEN_HEIGHT / 2;  // 120
  
  // Left eye: center at (cx - GAP/2 - EYE_SIZE/2, cy)
  leftEyeBox.x = cx - GAP / 2 - EYE_SIZE;
  leftEyeBox.y = cy - EYE_SIZE / 2;
  leftEyeBox.w = EYE_SIZE;
  leftEyeBox.h = EYE_SIZE;
  
  // Right eye: center at (cx + GAP/2 + EYE_SIZE/2, cy)
  rightEyeBox.x = cx + GAP / 2;
  rightEyeBox.y = cy - EYE_SIZE / 2;
  rightEyeBox.w = EYE_SIZE;
  rightEyeBox.h = EYE_SIZE;
  
  DisplayLog::printf("[Eyes] Screen center: (%d, %d)\n", cx, cy);
  DisplayLog::printf("[Eyes] Left: x=%d, y=%d, w=%d, h=%d (center: %d, %d)\n", 
                leftEyeBox.x, leftEyeBox.y, leftEyeBox.w, leftEyeBox.h,
                leftEyeBox.x + leftEyeBox.w/2, leftEyeBox.y + leftEyeBox.h/2);
  DisplayLog::printf("[Eyes] Right: x=%d, y=%d, w=%d, h=%d (center: %d, %d)\n", 
                rightEyeBox.x, rightEyeBox.y, rightEyeBox.w, rightEyeBox.h,
                rightEyeBox.x + rightEyeBox.w/2, rightEyeBox.y + rightEyeBox.h/2);
}

// =====================================================
// Eye Rendering (Pixel Drawing Only)
// =====================================================
static inline bool GetEyeObjectsRO(const VisualObject*& left,
                                   const VisualObject*& right) {
  if (!g_visualObjects) return false;
  left  = &g_visualObjects[(int)ObjId::LeftEye];
  right = &g_visualObjects[(int)ObjId::RightEye];
  if (!left->visible || !right->visible) return false;
  return true;
}

static void EyeRenderer_pushCanvas() {
  if (!eyeCanvasActive) return;
  eyeCanvasActive->pushSprite(0, 0);
  if (eyeCanvasBack) {
    lgfx::LGFX_Sprite* tmp = eyeCanvasActive;
    eyeCanvasActive = eyeCanvasBack;
    eyeCanvasBack = tmp;
  }
}

static lv_color_t EyeRenderer_lvColorFrom565(uint16_t c) {
  uint8_t r5 = (c >> 11) & 0x1F;
  uint8_t g6 = (c >> 5) & 0x3F;
  uint8_t b5 = c & 0x1F;
  uint8_t r = static_cast<uint8_t>((r5 * 255) / 31);
  uint8_t g = static_cast<uint8_t>((g6 * 255) / 63);
  uint8_t b = static_cast<uint8_t>((b5 * 255) / 31);
  return lv_color_make(r, g, b);
}

static void EyeColor_getTargetForEmotion(EyeEmotion emo, uint8_t& r, uint8_t& g, uint8_t& b) {
  r = 255;
  g = 255;
  b = 255;
  switch (emo) {
    case EYE_EMO_ANGRY1:
      r = 255; g = 120; b = 120;
      break;
    case EYE_EMO_ANGRY2:
      r = 255; g = 70; b = 70;
      break;
    case EYE_EMO_ANGRY3:
      r = 255; g = 30; b = 30;
      break;
    case EYE_EMO_HAPPY1:
      r = 255; g = 200; b = 80;
      break;
    case EYE_EMO_HAPPY2:
      r = 255; g = 230; b = 40;
      break;
    case EYE_EMO_SAD1:
    case EYE_EMO_SAD2:
      r = 80; g = 140; b = 255;
      break;
    default:
      break;
  }
}

static void EyeColor_update(uint32_t nowMs) {
  uint8_t targetR = 255;
  uint8_t targetG = 255;
  uint8_t targetB = 255;
  EyeColor_getTargetForEmotion(emotionState.currentEmotion, targetR, targetG, targetB);
  eyeColor.targetR = static_cast<float>(targetR);
  eyeColor.targetG = static_cast<float>(targetG);
  eyeColor.targetB = static_cast<float>(targetB);

  if (eyeColor.lastUpdateMs == 0) {
    eyeColor.currentR = eyeColor.targetR;
    eyeColor.currentG = eyeColor.targetG;
    eyeColor.currentB = eyeColor.targetB;
    eyeColor.lastUpdateMs = nowMs;
    return;
  }

  uint32_t dtMs = nowMs - eyeColor.lastUpdateMs;
  eyeColor.lastUpdateMs = nowMs;
  if (EYE_COLOR_FADE_MS == 0 || dtMs >= EYE_COLOR_FADE_MS) {
    eyeColor.currentR = eyeColor.targetR;
    eyeColor.currentG = eyeColor.targetG;
    eyeColor.currentB = eyeColor.targetB;
    return;
  }

  float alpha = static_cast<float>(dtMs) / static_cast<float>(EYE_COLOR_FADE_MS);
  if (alpha > 1.0f) alpha = 1.0f;
  eyeColor.currentR += (eyeColor.targetR - eyeColor.currentR) * alpha;
  eyeColor.currentG += (eyeColor.targetG - eyeColor.currentG) * alpha;
  eyeColor.currentB += (eyeColor.targetB - eyeColor.currentB) * alpha;
}

static lv_color_t EyeColor_getCurrentColor() {
  float r = eyeColor.currentR;
  float g = eyeColor.currentG;
  float b = eyeColor.currentB;
  if (r < 0.0f) r = 0.0f;
  if (g < 0.0f) g = 0.0f;
  if (b < 0.0f) b = 0.0f;
  if (r > 255.0f) r = 255.0f;
  if (g > 255.0f) g = 255.0f;
  if (b > 255.0f) b = 255.0f;
  return lv_color_make(static_cast<uint8_t>(r + 0.5f),
                       static_cast<uint8_t>(g + 0.5f),
                       static_cast<uint8_t>(b + 0.5f));
}

static void Clean_resetDrop(RainDrop& drop, bool randomizeY) {
  drop.width = static_cast<uint8_t>(random(CLEAN_RAIN_W_MIN, CLEAN_RAIN_W_MAX + 1));
  drop.length = static_cast<uint8_t>(random(CLEAN_RAIN_L_MIN, CLEAN_RAIN_L_MAX + 1));
  int16_t maxX = static_cast<int16_t>(SCREEN_WIDTH - drop.width);
  if (maxX < 0) maxX = 0;
  drop.x = static_cast<float>(random(0, maxX + 1));
  drop.speed = static_cast<float>(random(CLEAN_RAIN_SPEED_MIN, CLEAN_RAIN_SPEED_MAX + 1));
  if (randomizeY) {
    drop.y = -static_cast<float>(random(0, SCREEN_HEIGHT));
  } else {
    drop.y = -static_cast<float>(drop.length);
  }
}

static void Clean_updateRain(uint32_t nowMs) {
  if (!cleanAnim.active) return;
  if (cleanAnim.lastUpdateMs == 0) {
    cleanAnim.lastUpdateMs = nowMs;
    return;
  }
  uint32_t dtMs = nowMs - cleanAnim.lastUpdateMs;
  if (dtMs == 0) return;
  cleanAnim.lastUpdateMs = nowMs;
  float dt = static_cast<float>(dtMs) / 1000.0f;
  for (size_t i = 0; i < CLEAN_RAIN_DROP_COUNT; ++i) {
    RainDrop& drop = cleanAnim.drops[i];
    drop.y += drop.speed * dt;
    if (drop.y > SCREEN_HEIGHT) {
      Clean_resetDrop(drop, true);
    }
  }
}

static void Clean_start(uint32_t nowMs) {
  if (cleanAnim.active) return;
  cleanAnim.active = true;
  cleanAnim.returnToStats = MenuSystem::isStatsOpen();
  cleanAnim.startMs = nowMs;
  cleanAnim.endMs = nowMs + CLEAN_ANIM_DURATION_MS;
  cleanAnim.lastUpdateMs = nowMs;
  for (size_t i = 0; i < CLEAN_RAIN_DROP_COUNT; ++i) {
    Clean_resetDrop(cleanAnim.drops[i], true);
  }
  DisplaySystem_setEmotion(EYE_EMO_IDLE);
  happyPipPlayed = false;
  idleState.type = IdleStateType::HappyBounce;
  idleState.active = true;
  idleState.startMs = nowMs;
  idleState.durationMs = CLEAN_ANIM_DURATION_MS;
}

static void Clean_update(uint32_t nowMs) {
  if (!cleanAnim.active) return;
  if (nowMs < cleanAnim.endMs) return;
  cleanAnim.active = false;
  cleanAnim.lastUpdateMs = 0;
  CareSystem::addCleanliness(CareSystem::kCleanAnimBoost);
  DisplaySystem_setEmotion(EYE_EMO_IDLE);
  Emotion_scheduleNextPick();
  if (cleanAnim.returnToStats) {
    MenuSystem::showStats();
  }
}

static void Feed_start(uint32_t nowMs) {
  DisplaySystem_setEmotion(EYE_EMO_IDLE);
  happyPipPlayed = false;
  idleState.type = IdleStateType::HappyBounce;
  idleState.active = true;
  idleState.startMs = nowMs;
  idleState.durationMs = FEED_ANIM_DURATION_MS;
}

static void Feed_end() {
  if (idleState.active && idleState.type == IdleStateType::HappyBounce) {
    idleState.active = false;
    idleState.type = IdleStateType::None;
    eye.topOffset = 0;
    eye.scale = 1.0f;
  }
}

static void Sleep_start(uint32_t nowMs) {
  if (sleepAnim.active || cleanAnim.active) return;
  sleepAnim.active = true;
  sleepAnim.startMs = nowMs;
  sleepAnim.lastUpdateMs = nowMs;
  sleepAnim.nextSpawnMs = nowMs;
  for (auto& z : sleepAnim.zs) {
    z.active = false;
  }
  Display_setBacklight(BACKLIGHT_SLEEP);
  DisplaySystem_setEmotion(EYE_EMO_IDLE);
  idleState.active = false;
  idleState.type = IdleStateType::None;
  idleLook.active = false;
  eye.topOffset = 0;
  eye.scale = 1.0f;
  eye.blinkInProgress = false;
  blinkRt.active = false;
  gMotion.offX = 0.0f;
  gMotion.offY = 0.0f;
  gMotion.targetOffX = 0.0f;
  gMotion.targetOffY = 0.0f;
  gMotion.jitterX = 0;
  gMotion.jitterY = 0;
  gMotion.jitterAmp = 0;
}

static void Sleep_spawnZ(uint32_t nowMs) {
  SleepZ* slot = nullptr;
  for (auto& z : sleepAnim.zs) {
    if (!z.active) {
      slot = &z;
      break;
    }
  }
  if (!slot) return;

  int16_t centerX = SCREEN_WIDTH / 2;
  int16_t centerY = SCREEN_HEIGHT / 2;
  slot->active = true;
  slot->x = static_cast<float>(centerX + random(-SLEEP_Z_SPAWN_JITTER_X, SLEEP_Z_SPAWN_JITTER_X + 1));
  slot->y = static_cast<float>(centerY + random(-SLEEP_Z_SPAWN_JITTER_Y, SLEEP_Z_SPAWN_JITTER_Y + 1));
  slot->speed = static_cast<float>(random(static_cast<long>(SLEEP_Z_SPEED_MIN * 10),
                                          static_cast<long>(SLEEP_Z_SPEED_MAX * 10 + 1))) / 10.0f;
  slot->driftX = static_cast<float>(random(static_cast<long>(SLEEP_Z_DRIFT_MIN * 10),
                                           static_cast<long>(SLEEP_Z_DRIFT_MAX * 10 + 1))) / 10.0f;
  slot->startMs = nowMs;
  slot->durationMs = static_cast<uint32_t>(random(SLEEP_Z_LIFE_MIN_MS, SLEEP_Z_LIFE_MAX_MS + 1));
  slot->sizeIdx = static_cast<uint8_t>(random(0, 3));
  int16_t rotChoices[] = {-120, -60, 0, 60, 120};
  slot->rotation = rotChoices[random(0, 5)];
}

static void Sleep_end(uint32_t nowMs) {
  if (!sleepAnim.active) return;
  sleepAnim.active = false;
  sleepAnim.lastUpdateMs = 0;
  sleepAnim.nextSpawnMs = 0;
  for (auto& z : sleepAnim.zs) {
    z.active = false;
  }
  Display_setBacklight(BACKLIGHT_FULL);
  eye.topOffset = 0;
  eye.scale = 1.0f;
  gMotion.offX = 0.0f;
  gMotion.offY = 0.0f;
  gMotion.targetOffX = 0.0f;
  gMotion.targetOffY = 0.0f;
  gMotion.jitterX = 0;
  gMotion.jitterY = 0;
  gMotion.jitterAmp = 0;
  CareSystem::addEnergy(CareSystem::kSleepBoost);
  DisplaySystem_setEmotion(EYE_EMO_IDLE);
  Emotion_scheduleNextPick();
}

static void Sleep_update(uint32_t nowMs) {
  if (!sleepAnim.active) return;
  if (sleepAnim.nextSpawnMs == 0) {
    sleepAnim.nextSpawnMs = nowMs;
  }
  if (nowMs >= sleepAnim.nextSpawnMs) {
    Sleep_spawnZ(nowMs);
    sleepAnim.nextSpawnMs = nowMs + static_cast<uint32_t>(
      random(SLEEP_Z_SPAWN_MIN_MS, SLEEP_Z_SPAWN_MAX_MS + 1));
  }
  if (sleepAnim.lastUpdateMs == 0) {
    sleepAnim.lastUpdateMs = nowMs;
  }
  uint32_t dtMs = nowMs - sleepAnim.lastUpdateMs;
  sleepAnim.lastUpdateMs = nowMs;
  if (dtMs > 0) {
    float dt = static_cast<float>(dtMs) / 1000.0f;
    for (auto& z : sleepAnim.zs) {
      if (!z.active) continue;
      z.y -= z.speed * dt;
      z.x += z.driftX * dt;
      if (nowMs - z.startMs >= z.durationMs || z.y < -20.0f) {
        z.active = false;
      }
    }
  }
  float t = static_cast<float>(nowMs - sleepAnim.startMs);
  float phase = (t / static_cast<float>(SLEEP_BOB_PERIOD_MS)) * 2.0f * 3.1415926f;
  int16_t bob = static_cast<int16_t>(sinf(phase) * SLEEP_BOB_AMPLITUDE_PX);

  int baseH = EYE_SIZE;
  float scale = 1.0f;
  if (g_visualObjects) {
    const auto& o = g_visualObjects[(int)ObjId::LeftEye];
    baseH = o.baseH;
    scale = o.scaleY;
  }
  int scaledSize = static_cast<int>(baseH * scale);
  int16_t sleepTopOffset = static_cast<int16_t>(scaledSize - SLEEP_EYE_HEIGHT_PX);
  if (sleepTopOffset < 0) sleepTopOffset = 0;

  eye.topOffset = sleepTopOffset;
  eye.scale = 1.0f;
  gMotion.offX = 0.0f;
  gMotion.offY = static_cast<float>(bob);
  gMotion.targetOffX = 0.0f;
  gMotion.targetOffY = static_cast<float>(bob);
}

static void Sleep_drawZs(lv_layer_t* layer, uint32_t nowMs) {
  if (!sleepAnim.active) return;

  static const lv_font_t* kSleepFonts[] = {
    &lv_font_montserrat_vn_20,
    &lv_font_montserrat_vn_22,
    &lv_font_montserrat_vn_28
  };

  lv_draw_label_dsc_t label;
  lv_draw_label_dsc_init(&label);
  label.text = "Z";
  label.text_length = 1;
  label.align = LV_TEXT_ALIGN_CENTER;

  for (const auto& z : sleepAnim.zs) {
    if (!z.active) continue;
    uint32_t age = nowMs - z.startMs;
    if (age >= z.durationMs) continue;
    float p = static_cast<float>(age) / static_cast<float>(z.durationMs);
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    lv_opa_t opa = static_cast<lv_opa_t>((1.0f - p) * 255.0f);
    if (opa == 0) continue;

    label.font = kSleepFonts[z.sizeIdx % (sizeof(kSleepFonts) / sizeof(kSleepFonts[0]))];
    label.rotation = z.rotation;
    int16_t lineH = static_cast<int16_t>(label.font->line_height);
    int16_t box = static_cast<int16_t>(lineH + 8);
    int16_t half = static_cast<int16_t>(box / 2);
    int16_t x = static_cast<int16_t>(z.x + 0.5f);
    int16_t y = static_cast<int16_t>(z.y + 0.5f);

    label.color = lv_color_make(90, 120, 170);
    label.opa = static_cast<lv_opa_t>((opa * 2) / 3);
    lv_area_t shadow = {
      static_cast<int16_t>(x - half + 1),
      static_cast<int16_t>(y - half + 1),
      static_cast<int16_t>(x + half + 1),
      static_cast<int16_t>(y + half + 1)
    };
    lv_draw_label(layer, &label, &shadow);

    label.color = lv_color_make(220, 235, 255);
    label.opa = opa;
    lv_area_t area = {
      static_cast<int16_t>(x - half),
      static_cast<int16_t>(y - half),
      static_cast<int16_t>(x + half),
      static_cast<int16_t>(y + half)
    };
    lv_draw_label(layer, &label, &area);
  }
}

static float Hatch_clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

static float Hatch_smoothstep(float t) {
  t = Hatch_clamp01(t);
  return t * t * (3.0f - 2.0f * t);
}

static float Hatch_lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

static float Hatch_randomRange(float minV, float maxV) {
  float r = static_cast<float>(random(0, 10001)) / 10000.0f;
  return minV + (maxV - minV) * r;
}

static void Hatch_enterPhase(uint8_t phase, uint32_t nowMs) {
  hatch.phase = phase;
  hatch.phaseStartMs = nowMs;
  hatch.tapBobStartMs = 0;
  hatch.tapBobDurationMs = 0;
  hatch.tapBobAmp = 0.0f;
  hatch.phase2BoostUntilMs = 0;
  hatch.twitchStartMs = 0;
  hatch.twitchDurationMs = 0;
  hatch.twitchX = 0.0f;
  hatch.twitchY = 0.0f;
  if (phase == 3) {
    hatch.moving = false;
    hatch.stopUntilMs = nowMs + static_cast<uint32_t>(random(600, 1201));
  }
  if (phase == 4) {
    hatch.blinkStarted = false;
    hatch.blinkStartMs = 0;
  }
}

static void Hatch_start(uint32_t nowMs) {
  hatch.active = true;
  hatch.startMs = nowMs;
  hatch.posX = SCREEN_WIDTH / 2.0f;
  hatch.posY = SCREEN_HEIGHT / 2.0f;
  hatch.moving = false;
  hatch.moveStartMs = 0;
  hatch.moveDurationMs = 0;
  hatch.stopUntilMs = 0;
  hatch.blinkStarted = false;
  hatch.blinkStartMs = 0;
  Hatch_enterPhase(1, nowMs);
  Clock_setOpacity(LV_OPA_TRANSP);
  clockRt.state = IdleVisualState::Eyes;
  MenuSystem::close();
  Display_setCanvasVisible(true);
}

static void Hatch_finish(uint32_t nowMs) {
  hatch.active = false;
  if (hatchPrefsReady) {
    hatchPrefs.putBool("hatched", true);
  }
  clockRt.lastTouchMs = nowMs;
  EyeRenderer_drawFrame(0, 1.0f);
}

static float Hatch_tapBob(uint32_t nowMs) {
  if (hatch.tapBobDurationMs == 0) return 0.0f;
  if (nowMs < hatch.tapBobStartMs) return 0.0f;
  uint32_t elapsed = nowMs - hatch.tapBobStartMs;
  if (elapsed >= hatch.tapBobDurationMs) return 0.0f;
  float t = static_cast<float>(elapsed) / static_cast<float>(hatch.tapBobDurationMs);
  return sinf(t * 3.1415926f) * hatch.tapBobAmp;
}

static void Hatch_handleTap(uint32_t nowMs) {
  if (hatch.phase == 1) {
    hatch.tapBobStartMs = nowMs;
    hatch.tapBobDurationMs = 600;
    hatch.tapBobAmp = 4.0f;
  } else if (hatch.phase == 2) {
    hatch.tapBobStartMs = nowMs;
    hatch.tapBobDurationMs = 650;
    hatch.tapBobAmp = 5.5f;
    hatch.phase2BoostUntilMs = nowMs + 3500;
  } else if (hatch.phase == 3) {
    hatch.tapBobStartMs = nowMs;
    hatch.tapBobDurationMs = 500;
    hatch.tapBobAmp = 6.0f;
    hatch.twitchStartMs = nowMs;
    hatch.twitchDurationMs = 280;
    hatch.twitchX = Hatch_randomRange(-5.0f, 5.0f);
    hatch.twitchY = Hatch_randomRange(-4.0f, 4.0f);
  }
}

static void Hatch_updatePhase3(uint32_t nowMs) {
  const float baseX = SCREEN_WIDTH / 2.0f;
  const float baseY = SCREEN_HEIGHT / 2.0f;
  if (hatch.moving) {
    uint32_t elapsed = nowMs - hatch.moveStartMs;
    float t = hatch.moveDurationMs > 0
        ? static_cast<float>(elapsed) / static_cast<float>(hatch.moveDurationMs)
        : 1.0f;
    if (t >= 1.0f) {
      hatch.posX = hatch.moveTargetX;
      hatch.posY = hatch.moveTargetY;
      hatch.moving = false;
      hatch.stopUntilMs = nowMs + static_cast<uint32_t>(random(700, 1301));
    } else {
      float eased = Hatch_smoothstep(t);
      hatch.posX = Hatch_lerp(hatch.moveStartX, hatch.moveTargetX, eased);
      hatch.posY = Hatch_lerp(hatch.moveStartY, hatch.moveTargetY, eased);
    }
  } else if (nowMs >= hatch.stopUntilMs) {
    float angle = Hatch_randomRange(0.0f, 6.2831853f);
    float radius = Hatch_randomRange(20.0f, 30.0f);
    hatch.moveStartX = hatch.posX;
    hatch.moveStartY = hatch.posY;
    hatch.moveTargetX = baseX + cosf(angle) * radius;
    hatch.moveTargetY = baseY + sinf(angle) * radius;
    hatch.moveStartMs = nowMs;
    hatch.moveDurationMs = static_cast<uint32_t>(random(650, 1101));
    hatch.moving = true;
  }
}

static void Hatch_drawEgg(lv_layer_t* layer, float cx, float cy, float w, float h,
                          float radius, float lobe, bool glow) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = lv_color_make(245, 245, 245);
  dsc.bg_opa = LV_OPA_COVER;
  dsc.border_opa = LV_OPA_TRANSP;
  dsc.radius = static_cast<int16_t>(radius + 0.5f);

  if (glow) {
    lv_draw_rect_dsc_t glowDsc = dsc;
    glowDsc.bg_opa = 50;
    glowDsc.radius = static_cast<int16_t>(radius + 6);
    int16_t glowW = static_cast<int16_t>(w + 10);
    int16_t glowH = static_cast<int16_t>(h + 10);
    lv_area_t glowArea = {
      static_cast<int16_t>(cx - glowW / 2),
      static_cast<int16_t>(cy - glowH / 2),
      static_cast<int16_t>(cx + glowW / 2 - 1),
      static_cast<int16_t>(cy + glowH / 2 - 1)
    };
    lv_draw_rect(layer, &glowDsc, &glowArea);
  }

  if (lobe <= 0.01f) {
    lv_area_t area = {
      static_cast<int16_t>(cx - w / 2),
      static_cast<int16_t>(cy - h / 2),
      static_cast<int16_t>(cx + w / 2 - 1),
      static_cast<int16_t>(cy + h / 2 - 1)
    };
    lv_draw_rect(layer, &dsc, &area);
    return;
  }

  float circleSize = h;
  float circleRadius = circleSize / 2.0f;
  float offset = 8.0f + lobe * 8.0f;
  lv_area_t left = {
    static_cast<int16_t>(cx - offset - circleRadius),
    static_cast<int16_t>(cy - circleRadius),
    static_cast<int16_t>(cx - offset + circleRadius - 1),
    static_cast<int16_t>(cy + circleRadius - 1)
  };
  lv_area_t right = {
    static_cast<int16_t>(cx + offset - circleRadius),
    static_cast<int16_t>(cy - circleRadius),
    static_cast<int16_t>(cx + offset + circleRadius - 1),
    static_cast<int16_t>(cy + circleRadius - 1)
  };
  dsc.radius = static_cast<int16_t>(circleRadius);
  lv_draw_rect(layer, &dsc, &left);
  lv_draw_rect(layer, &dsc, &right);
}

static void Hatch_drawEyes(lv_layer_t* layer, float cx, float cy, float splitT,
                           float baseSize, float baseRadius, float blinkScale) {
  float finalOffset = (EYE_SIZE + GAP) * 0.5f;
  float offset = finalOffset * splitT;
  float size = baseSize;
  float height = baseSize * blinkScale;
  if (height < CLOSED_HEIGHT) height = CLOSED_HEIGHT;
  float radius = baseRadius;
  if (radius > height / 2.0f) radius = height / 2.0f;

  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = lv_color_make(245, 245, 245);
  dsc.bg_opa = LV_OPA_COVER;
  dsc.border_opa = LV_OPA_TRANSP;
  dsc.radius = static_cast<int16_t>(radius + 0.5f);

  int16_t w = static_cast<int16_t>(size + 0.5f);
  int16_t h = static_cast<int16_t>(height + 0.5f);
  int16_t leftCx = static_cast<int16_t>(cx - offset + 0.5f);
  int16_t rightCx = static_cast<int16_t>(cx + offset + 0.5f);
  int16_t cyi = static_cast<int16_t>(cy + 0.5f);

  lv_area_t left = {
    static_cast<int16_t>(leftCx - w / 2),
    static_cast<int16_t>(cyi - h / 2),
    static_cast<int16_t>(leftCx + w / 2 - 1),
    static_cast<int16_t>(cyi + h / 2 - 1)
  };
  lv_area_t right = {
    static_cast<int16_t>(rightCx - w / 2),
    static_cast<int16_t>(cyi - h / 2),
    static_cast<int16_t>(rightCx + w / 2 - 1),
    static_cast<int16_t>(cyi + h / 2 - 1)
  };
  lv_draw_rect(layer, &dsc, &left);
  lv_draw_rect(layer, &dsc, &right);
}

static void Hatch_render(uint32_t nowMs) {
  if (!lvCanvas) return;
  if (lvCanvas && eyeCanvasActive) {
    lv_canvas_set_buffer(lvCanvas,
                         eyeCanvasActive->getBuffer(),
                         eyeCanvasActive->width(),
                         eyeCanvasActive->height(),
                         LV_COLOR_FORMAT_RGB565);
  }
  lv_canvas_fill_bg(lvCanvas, lv_color_black(), LV_OPA_COVER);
  lv_layer_t layer;
  lv_canvas_init_layer(lvCanvas, &layer);

  float tapBob = Hatch_tapBob(nowMs);
  float centerX = hatch.posX;
  float centerY = hatch.posY;
  float bobOffset = 0.0f;

  uint32_t phaseElapsed = nowMs - hatch.phaseStartMs;

  if (hatch.phase == 1) {
    bobOffset = -tapBob;
    Hatch_drawEgg(&layer, centerX, centerY + bobOffset, HATCH_BASE_SIZE, HATCH_BASE_SIZE,
                  HATCH_BASE_SIZE / 2.0f, 0.0f, true);
  } else if (hatch.phase == 2) {
    float period = (hatch.phase2BoostUntilMs > nowMs) ? 2600.0f : 3600.0f;
    float autoBob = sinf(phaseElapsed * 2.0f * 3.1415926f / period) * 3.0f;
    bobOffset = autoBob - tapBob;
    Hatch_drawEgg(&layer, centerX, centerY + bobOffset, HATCH_BASE_SIZE, HATCH_BASE_SIZE,
                  HATCH_BASE_SIZE / 2.0f, 0.0f, false);
  } else if (hatch.phase == 3) {
    float t = Hatch_clamp01(static_cast<float>(phaseElapsed) / HATCH_PHASE3_MS);
    float deform = Hatch_smoothstep(t);
    float stretch = 1.0f + 0.12f * sinf(nowMs * 0.002f);
    float width = HATCH_BASE_SIZE * (1.0f + 0.18f * deform) * stretch;
    float height = HATCH_BASE_SIZE * (1.0f - 0.10f * deform);
    float radius = Hatch_lerp(HATCH_BASE_SIZE / 2.0f, HATCH_BASE_SIZE * 0.28f, deform);
    float lobe = Hatch_clamp01((t - 0.65f) / 0.35f);
    float autoBob = hatch.moving ? 0.0f : sinf(phaseElapsed * 2.0f * 3.1415926f / 3200.0f) * 3.5f;
    bobOffset = autoBob - tapBob;
    float twitchX = 0.0f;
    float twitchY = 0.0f;
    if (hatch.twitchDurationMs > 0 && nowMs >= hatch.twitchStartMs) {
      uint32_t twitchElapsed = nowMs - hatch.twitchStartMs;
      if (twitchElapsed < hatch.twitchDurationMs) {
        float k = 1.0f - (static_cast<float>(twitchElapsed) /
                          static_cast<float>(hatch.twitchDurationMs));
        twitchX = hatch.twitchX * k;
        twitchY = hatch.twitchY * k;
      }
    }
    Hatch_drawEgg(&layer,
                  centerX + twitchX,
                  centerY + bobOffset + twitchY,
                  width,
                  height,
                  radius,
                  lobe,
                  false);
  } else if (hatch.phase == 4) {
    float t = Hatch_clamp01(static_cast<float>(phaseElapsed) / HATCH_PHASE4_MS);
    float split = Hatch_smoothstep(t);
    float size = Hatch_lerp(static_cast<float>(HATCH_BASE_SIZE), static_cast<float>(EYE_SIZE), split);
    float radius = Hatch_lerp(static_cast<float>(HATCH_BASE_SIZE / 2.0f),
                              static_cast<float>(EYE_RADIUS),
                              split);
    float settle = sinf(t * 3.1415926f) * 4.0f * (1.0f - t);
    if (!hatch.blinkStarted && t > 0.55f) {
      hatch.blinkStarted = true;
      hatch.blinkStartMs = nowMs;
    }
    float blinkScale = 1.0f;
    if (hatch.blinkStarted) {
      uint32_t blinkElapsed = nowMs - hatch.blinkStartMs;
      const uint32_t blinkDuration = 450;
      if (blinkElapsed < blinkDuration) {
        float bt = static_cast<float>(blinkElapsed) / static_cast<float>(blinkDuration);
        blinkScale = 1.0f - 0.9f * sinf(bt * 3.1415926f);
      }
    }
    Hatch_drawEyes(&layer, centerX, centerY + settle, split, size, radius, blinkScale);
  }

  lv_canvas_finish_layer(lvCanvas, &layer);
}

static void Hatch_update(uint32_t nowMs) {
  if (!hatch.active) return;
  uint32_t elapsed = nowMs - hatch.startMs;
  if (elapsed >= HATCH_TOTAL_MS) {
    Hatch_finish(nowMs);
    return;
  }

  uint32_t p1End = HATCH_PHASE1_MS;
  uint32_t p2End = p1End + HATCH_PHASE2_MS;
  uint32_t p3End = p2End + HATCH_PHASE3_MS;
  uint8_t nextPhase = 4;
  if (elapsed < p1End) nextPhase = 1;
  else if (elapsed < p2End) nextPhase = 2;
  else if (elapsed < p3End) nextPhase = 3;

  if (nextPhase != hatch.phase) {
    Hatch_enterPhase(nextPhase, nowMs);
  }

  while (TouchSystem::available()) {
    TouchPoint touch = TouchSystem::get();
    if (touch.gesture == TOUCH_TAP) {
      Hatch_handleTap(nowMs);
    }
  }

  if (hatch.phase == 1 || hatch.phase == 2) {
    hatch.posX = SCREEN_WIDTH / 2.0f;
    hatch.posY = SCREEN_HEIGHT / 2.0f;
  } else if (hatch.phase == 3) {
    Hatch_updatePhase3(nowMs);
  } else if (hatch.phase == 4) {
    hatch.posX = SCREEN_WIDTH / 2.0f;
    hatch.posY = SCREEN_HEIGHT / 2.0f;
  }

  Hatch_render(nowMs);
}

static void EyeRenderer_drawFrame(int16_t topOffset) {
  EyeRenderer_drawFrame(topOffset, -1.0f, 0);
}

static void EyeRenderer_drawFrame(int16_t topOffset, float scale) {
  EyeRenderer_drawFrame(topOffset, scale, 0);
}

// blinkMask: bit0=close left, bit1=close right
static void EyeRenderer_drawFrame(int16_t topOffset, float scale, uint8_t blinkMask) {
  if (!lvCanvas) {
    return;
  }
  lv_color_t eyeColorNow = EyeColor_getCurrentColor();

  const VisualObject* oL = nullptr;
  const VisualObject* oR = nullptr;
  bool useObj = GetEyeObjectsRO(oL, oR);

  if (scale <= 0.0f) {
    scale = useObj ? oL->scaleY : eye.scale;   // neutral eyes now come from VisualObject
  }

  int cx = SCREEN_WIDTH / 2;
  int cy = SCREEN_HEIGHT / 2;

  int baseW = useObj ? oL->baseW : EYE_SIZE;
  int baseH = useObj ? oL->baseH : EYE_SIZE;

  int scaledSize = static_cast<int>(baseH * scale);
  if (scaledSize < baseH) {
    scaledSize = baseH;
  }
  
  int scaledHalf = scaledSize / 2;
  int scaledTop = useObj
    ? (oL->baseY + oL->offsetY)
    : (cy - scaledHalf);
  // Apply global motion Y before eye geometry is finalized
  const int16_t gmy = static_cast<int16_t>(gMotion.offY) + gMotion.jitterY;
  scaledTop += gmy;


  int scaledBottom = scaledTop + scaledSize;
  bool leftUsesOffset = (blinkMask == 0) || (blinkMask & BLINK_LEFT_MASK);
  bool rightUsesOffset = (blinkMask == 0) || (blinkMask & BLINK_RIGHT_MASK);

  int leftTop = scaledTop + (leftUsesOffset ? topOffset : 0);
  int rightTop = scaledTop + (rightUsesOffset ? topOffset : 0);
  
  if (leftTop > scaledBottom - CLOSED_HEIGHT) {
    leftTop = scaledBottom - CLOSED_HEIGHT;
  }
  if (rightTop > scaledBottom - CLOSED_HEIGHT) {
    rightTop = scaledBottom - CLOSED_HEIGHT;
  }

  int leftHeight = scaledBottom - leftTop;
  int rightHeight = scaledBottom - rightTop;
  // Happy bean: asym bulge/squash per eye to avoid perfect symmetry
  if (emotionState.happyActive && !blinkMask) {
    int8_t bulgeL = 6;
    int8_t bulgeR = 4;
    leftTop -= bulgeL / 2;
    leftHeight += bulgeL;
    rightTop -= bulgeR / 2;
    rightHeight += bulgeR;
    if (leftTop < 0) leftTop = 0;
    if (rightTop < 0) rightTop = 0;
    if (leftTop + leftHeight > scaledBottom) leftHeight = scaledBottom - leftTop;
    if (rightTop + rightHeight > scaledBottom) rightHeight = scaledBottom - rightTop;
  }
  if (emotionState.currentEmotion == EYE_EMO_TIRED) {
    // Tired: lift the bottom edge to leave a shorter eye height.
    if (leftHeight > TIRED_EYE_HEIGHT) leftHeight = TIRED_EYE_HEIGHT;
    if (rightHeight > TIRED_EYE_HEIGHT) rightHeight = TIRED_EYE_HEIGHT;
  }

  int eyeWidth = scaledSize;
  
  // Center eyes properly
  int leftX, rightX;
  if (useObj) {
    leftX  = oL->baseX + oL->offsetX + (oL->baseW - eyeWidth);
    rightX = oR->baseX + oR->offsetX;
  } else {
    leftX  = cx - GAP / 2 - eyeWidth;
    rightX = cx + GAP / 2;
  }

  // Apply global motion once (shared for all objects)
  const int16_t baseX = static_cast<int16_t>(gMotion.offX);
  const int16_t jx = gMotion.jitterX;
  // const int16_t gmy = static_cast<int16_t>(gMotion.offY) + gMotion.jitterY;

  switch (idleState.type) {
    case IdleStateType::JitterLeft:
      leftX  += baseX + jx;
      rightX += baseX;
      break;
    case IdleStateType::JitterRight:
      leftX  += baseX;
      rightX += baseX + jx;
      break;
    default:  // JitterBoth or any other state
      leftX  += baseX + jx;
      rightX += baseX + jx;
      break;
  }

  // Calculate radius per eye
  auto calcRadius = [&](int h, float s) {
    int baseR = useObj ? oL->radius : EYE_RADIUS;
    int r = (h <= CLOSED_HEIGHT) ? (CLOSED_HEIGHT / 2) : static_cast<int>(baseR * s);
    if (r > h / 2) r = h / 2;
    return r;
  };
  int radiusL = calcRadius(leftHeight, scale);
  int radiusR = calcRadius(rightHeight, scale);

  bool gameRunning = EyeGame::isRunning();
  bool useLGFX = gameRunning;

  if (useLGFX) {
    lgfx::LGFX_Sprite& canvas = *eyeCanvasActive;
    // Pure LovyanGFX path to avoid LVGL flicker during game
    // Clear only the full eye bounding boxes (max size) to avoid ghosting during blink/pop
    auto clearEye = [&](int centerX, int centerY, int maxSize) {
      int half = maxSize / 2;
      int lx = centerX - half - 2;
      int ly = centerY - half - 2;
      int rw = maxSize + 4;
      int rh = maxSize + 4;
      if (lx < 0) { rw += lx; lx = 0; }
      if (ly < 0) { rh += ly; ly = 0; }
      if (lx + rw > SCREEN_WIDTH) rw = SCREEN_WIDTH - lx;
      if (ly + rh > SCREEN_HEIGHT) rh = SCREEN_HEIGHT - ly;
      if (rw > 0 && rh > 0) {
        canvas.fillRect(lx, ly, rw, rh, lgfx::color565(0, 0, 0));
      }
    };
    int maxSize = static_cast<int>(EYE_SIZE * MAX_EYE_SCALE);
    int leftCenterX = (SCREEN_WIDTH / 2) - GAP / 2 - (EYE_SIZE / 2);
    int rightCenterX = (SCREEN_WIDTH / 2) + GAP / 2 + (EYE_SIZE / 2);
    clearEye(leftCenterX, cy, maxSize);
    clearEye(rightCenterX, cy, maxSize);
    uint16_t leftRaw = EyeGame::getLeftColor565();
    uint16_t rightRaw = EyeGame::getRightColor565();
    lgfx::rgb565_t leftColor = lgfx::color565((leftRaw >> 8) & 0xF8, (leftRaw >> 3) & 0xFC, (leftRaw << 3) & 0xF8);
    lgfx::rgb565_t rightColor = lgfx::color565((rightRaw >> 8) & 0xF8, (rightRaw >> 3) & 0xFC, (rightRaw << 3) & 0xF8);

    canvas.fillRoundRect(leftX, leftTop, eyeWidth, leftHeight, radiusL, leftColor);
    canvas.fillRoundRect(rightX, rightTop, eyeWidth, rightHeight, radiusR, rightColor);


    EyeRenderer_pushCanvas();
    return;
  }

  if (lvCanvas && eyeCanvasActive) {
    lv_canvas_set_buffer(lvCanvas,
                         eyeCanvasActive->getBuffer(),
                         eyeCanvasActive->width(),
                         eyeCanvasActive->height(),
                         LV_COLOR_FORMAT_RGB565);
  }

  // LVGL path for static eyes
  lv_canvas_fill_bg(lvCanvas, lv_color_black(), LV_OPA_COVER);
  lv_draw_rect_dsc_t rect;
  lv_draw_rect_dsc_init(&rect);
  rect.bg_color   = eyeColorNow;
  rect.bg_opa     = LV_OPA_COVER;
  rect.border_opa = LV_OPA_TRANSP;
  rect.radius = radiusL;

  lv_layer_t layer;
  lv_canvas_init_layer(lvCanvas, &layer);

  if (cleanAnim.active) {
    Clean_updateRain(millis());
    lv_draw_rect_dsc_t rain;
    lv_draw_rect_dsc_init(&rain);
    rain.bg_color = lv_color_hex(CLEAN_RAIN_COLOR);
    rain.bg_opa = LV_OPA_COVER;
    rain.border_opa = LV_OPA_TRANSP;
    rain.radius = 0;
    for (size_t i = 0; i < CLEAN_RAIN_DROP_COUNT; ++i) {
      const RainDrop& drop = cleanAnim.drops[i];
      int16_t x1 = static_cast<int16_t>(drop.x);
      int16_t y1 = static_cast<int16_t>(drop.y);
      int16_t x2 = static_cast<int16_t>(drop.x + drop.width - 1);
      int16_t y2 = static_cast<int16_t>(drop.y + drop.length - 1);
      if (x2 < 0 || y2 < 0 || x1 >= SCREEN_WIDTH || y1 >= SCREEN_HEIGHT) {
        continue;
      }
      if (x1 < 0) x1 = 0;
      if (y1 < 0) y1 = 0;
      if (x2 >= SCREEN_WIDTH) x2 = SCREEN_WIDTH - 1;
      if (y2 >= SCREEN_HEIGHT) y2 = SCREEN_HEIGHT - 1;
      lv_area_t area = {x1, y1, x2, y2};
      lv_draw_rect(&layer, &rain, &area);
    }
  }

  lv_area_t leftArea  = {leftX,  leftTop, leftX  + eyeWidth - 1, leftTop + leftHeight - 1};
  lv_area_t rightArea = {rightX, rightTop, rightX + eyeWidth - 1, rightTop + rightHeight - 1};

  lv_draw_rect(&layer, &rect, &leftArea);
  rect.radius = radiusR;
  lv_draw_rect(&layer, &rect, &rightArea);

  if (!cleanAnim.active && !sleepAnim.active) {
    int16_t eyeTop = (leftTop < rightTop) ? leftTop : rightTop;
    int16_t gapLeft = static_cast<int16_t>(leftX + eyeWidth);
    int16_t gapRight = static_cast<int16_t>(rightX - 1);
    int16_t gapWidth = static_cast<int16_t>(gapRight - gapLeft + 1);
    if (gapWidth > 0) {
      int16_t triWidth = static_cast<int16_t>(eyeWidth * 2.0f);
      if (triWidth > 0) {
        int16_t triHeight = 50;
        int16_t apexY = static_cast<int16_t>(eyeTop - 1);
        int16_t baseY = static_cast<int16_t>(apexY - triHeight);

        lv_draw_triangle_dsc_t tri;
        lv_draw_triangle_dsc_init(&tri);
        tri.color = lv_color_black();
        tri.opa = LV_OPA_COVER;

        struct TriPlacement {
          int16_t left;
          int16_t right;
          int16_t baseY;
          int16_t apexY;
        };

        struct TriPlacementApex {
          int16_t left;
          int16_t right;
          int16_t baseY;
          int16_t apexX;
          int16_t apexY;
        };

        auto drawTriangle = [&](const TriPlacement& pos) {
          int16_t center = static_cast<int16_t>((pos.left + pos.right) / 2);
          tri.p[0].x = pos.left;   tri.p[0].y = pos.baseY;
          tri.p[1].x = pos.right;  tri.p[1].y = pos.baseY;
          tri.p[2].x = center;     tri.p[2].y = pos.apexY;
          lv_draw_triangle(&layer, &tri);
        };

        auto drawTriangleApex = [&](const TriPlacementApex& pos) {
          tri.p[0].x = pos.left;   tri.p[0].y = pos.baseY;
          tri.p[1].x = pos.right;  tri.p[1].y = pos.baseY;
          tri.p[2].x = pos.apexX;  tri.p[2].y = pos.apexY;
          lv_draw_triangle(&layer, &tri);
        };

        uint32_t triNow = millis();
        const bool angryActive = (emotionState.angryEndMs > 0 && triNow < emotionState.angryEndMs);
        const bool tiredActive = (emotionState.tiredEndMs > 0 && triNow < emotionState.tiredEndMs);
        const bool worriedActive = (emotionState.worriedEndMs > 0 && triNow < emotionState.worriedEndMs);
        const bool curiousActive = (emotionState.curiousEndMs > 0 && triNow < emotionState.curiousEndMs);
        const bool sad1Active = (emotionState.sadEndMs > 0 && triNow < emotionState.sadEndMs);
        const bool sad2Active = (emotionState.sad2EndMs > 0 && triNow < emotionState.sad2EndMs);
        const bool happy1Active = (emotionState.happy1EndMs > 0 && triNow < emotionState.happy1EndMs);
        const bool happy2Active = (emotionState.happy2EndMs > 0 && triNow < emotionState.happy2EndMs);
        const bool showTopHalf = (angryActive &&
                                  (emotionState.currentEmotion == EYE_EMO_ANGRY1 ||
                                   emotionState.currentEmotion == EYE_EMO_ANGRY2 ||
                                   emotionState.currentEmotion == EYE_EMO_ANGRY3)) ||
                                 (curiousActive &&
                                  (emotionState.currentEmotion == EYE_EMO_CURIOUS1 ||
                                   emotionState.currentEmotion == EYE_EMO_CURIOUS2));
        const bool showTop = worriedActive ||
                             (emotionState.currentEmotion == EYE_EMO_SAD1 && sad1Active) ||
                             (emotionState.currentEmotion == EYE_EMO_SAD2 && sad2Active) ||
                             (emotionState.currentEmotion == EYE_EMO_TIRED && tiredActive);
        const bool showBottom = (emotionState.currentEmotion == EYE_EMO_HAPPY1 && happy1Active) ||
                                (emotionState.currentEmotion == EYE_EMO_HAPPY2 && happy2Active);
        float triK;
        switch (idleMoveSpeed) {
          case IdleMoveSpeed::Slow:   triK = 0.06f; break;
          case IdleMoveSpeed::Fast:   triK = 0.30f; break;
          default:                    triK = 0.15f; break;
        }
        if (triK > 0.25f) triK = 0.25f;

        auto smoothOffset = [&](float& current, float target) {
          current += (target - current) * triK;
        };
        auto roundOffset = [&](float value) -> int16_t {
          return static_cast<int16_t>(value >= 0.0f ? value + 0.5f : value - 0.5f);
        };

        static float triCenterOffsetYf = 0.0f;
        static float curiousTopHalfLOffsetYf = 0.0f;
        static float curiousTopHalfROffsetYf = 0.0f;
        static float worriedTopOffsetXf = 0.0f;
        static float sadTopOffsetXf = 0.0f;
        static float sad2TopOffsetXf = 0.0f;
        static float tiredTopOffsetXf = 0.0f;
        static float happy1BottomOffsetYf = 0.0f;

        float targetTriCenterOffsetY = 0.0f;
        if (emotionState.angryEndMs > 0 && triNow < emotionState.angryEndMs) {
          if (emotionState.currentEmotion == EYE_EMO_ANGRY1) {
            targetTriCenterOffsetY = 25.0f;
          } else if (emotionState.currentEmotion == EYE_EMO_ANGRY2) {
            targetTriCenterOffsetY = 35.0f;
          } else if (emotionState.currentEmotion == EYE_EMO_ANGRY3) {
            targetTriCenterOffsetY = 45.0f;
          }
        }
        smoothOffset(triCenterOffsetYf, targetTriCenterOffsetY);
        int16_t triCenterOffsetY = roundOffset(triCenterOffsetYf);

        int16_t centerX = static_cast<int16_t>(gapLeft + gapWidth / 2);
        int16_t triLeft = static_cast<int16_t>(centerX - triWidth / 2);
        int16_t triRight = static_cast<int16_t>(triLeft + triWidth - 1);
        int16_t triCenterBaseY = static_cast<int16_t>(baseY + triCenterOffsetY);
        int16_t triCenterApexY = static_cast<int16_t>(apexY + triCenterOffsetY);
        const int16_t splitX = centerX;
        float targetCuriousTopHalfLOffsetY = 0.0f;
        float targetCuriousTopHalfROffsetY = 0.0f;
        if (emotionState.curiousEndMs > 0 && triNow < emotionState.curiousEndMs) {
          if (emotionState.currentEmotion == EYE_EMO_CURIOUS1) {
            targetCuriousTopHalfLOffsetY = 30.0f;
          } else if (emotionState.currentEmotion == EYE_EMO_CURIOUS2) {
            targetCuriousTopHalfROffsetY = 30.0f;
          }
        }
        smoothOffset(curiousTopHalfLOffsetYf, targetCuriousTopHalfLOffsetY);
        smoothOffset(curiousTopHalfROffsetYf, targetCuriousTopHalfROffsetY);
        int16_t curiousTopHalfLOffsetY = roundOffset(curiousTopHalfLOffsetYf);
        int16_t curiousTopHalfROffsetY = roundOffset(curiousTopHalfROffsetYf);

        const TriPlacementApex topHalfL = {triLeft, splitX,
                                           static_cast<int16_t>(triCenterBaseY + curiousTopHalfLOffsetY),
                                           centerX,
                                           static_cast<int16_t>(triCenterApexY + curiousTopHalfLOffsetY)};
        const TriPlacementApex topHalfR = {splitX, triRight,
                                           static_cast<int16_t>(triCenterBaseY + curiousTopHalfROffsetY),
                                           centerX,
                                           static_cast<int16_t>(triCenterApexY + curiousTopHalfROffsetY)};
        if (showTopHalf) {
          drawTriangleApex(topHalfL);
          drawTriangleApex(topHalfR);
        }
        tri.color = lv_color_black();

        float targetWorriedTopOffsetX = 0.0f;
        float targetSadTopOffsetX = 0.0f;
        float targetSad2TopOffsetX = 0.0f;
        float targetTiredTopOffsetX = 0.0f;
        float targetHappy1BottomOffsetY = 0.0f;
        int16_t bottomCenterInset = 2;
        if (emotionState.currentEmotion == EYE_EMO_WORRIED1 &&
            emotionState.worriedEndMs > 0 &&
            triNow < emotionState.worriedEndMs) {
          targetWorriedTopOffsetX = 10.0f;
        }
        if (emotionState.currentEmotion == EYE_EMO_SAD1 &&
            emotionState.sadEndMs > 0 &&
            triNow < emotionState.sadEndMs) {
          targetSadTopOffsetX = 20.0f;
        }
        if (emotionState.currentEmotion == EYE_EMO_SAD2 &&
            emotionState.sad2EndMs > 0 &&
            triNow < emotionState.sad2EndMs) {
          targetSad2TopOffsetX = 30.0f;
        }
        if (emotionState.currentEmotion == EYE_EMO_TIRED &&
            emotionState.tiredEndMs > 0 &&
            triNow < emotionState.tiredEndMs) {
          targetTiredTopOffsetX = 30.0f;
        }
        if (emotionState.currentEmotion == EYE_EMO_HAPPY1 &&
            emotionState.happy1EndMs > 0 &&
            triNow < emotionState.happy1EndMs) {
          targetHappy1BottomOffsetY = -30.0f;
        } else if (emotionState.currentEmotion == EYE_EMO_HAPPY2 &&
                   emotionState.happy2EndMs > 0 &&
                   triNow < emotionState.happy2EndMs) {
          targetHappy1BottomOffsetY = -35.0f;
        }
        smoothOffset(worriedTopOffsetXf, targetWorriedTopOffsetX);
        smoothOffset(sadTopOffsetXf, targetSadTopOffsetX);
        smoothOffset(sad2TopOffsetXf, targetSad2TopOffsetX);
        smoothOffset(tiredTopOffsetXf, targetTiredTopOffsetX);
        smoothOffset(happy1BottomOffsetYf, targetHappy1BottomOffsetY);
        int16_t worriedTopOffsetX = roundOffset(worriedTopOffsetXf);
        int16_t sadTopOffsetX = roundOffset(sadTopOffsetXf);
        int16_t sad2TopOffsetX = roundOffset(sad2TopOffsetXf);
        int16_t tiredTopOffsetX = roundOffset(tiredTopOffsetXf);
        int16_t happy1BottomOffsetY = roundOffset(happy1BottomOffsetYf);

        int16_t sideApexY = static_cast<int16_t>(apexY + 30);
        int16_t sideBaseY = static_cast<int16_t>(baseY + 30);
        int16_t flippedApexY = static_cast<int16_t>(apexY + 120);
        int16_t flippedBaseY = static_cast<int16_t>(baseY + 120);

        int16_t rightTriLeft = static_cast<int16_t>(rightX + eyeWidth - 1 - 50 -
                                                    worriedTopOffsetX - sadTopOffsetX -
                                                    sad2TopOffsetX - tiredTopOffsetX);
        int16_t rightTriRight = static_cast<int16_t>(rightTriLeft + triWidth - 1);
        const TriPlacement triRightTop = {rightTriLeft, rightTriRight, sideBaseY, sideApexY};
        if (showTop) {
          drawTriangle(triRightTop);
        }

        int16_t leftTriRight = static_cast<int16_t>(leftX + 50 +
                                                    worriedTopOffsetX + sadTopOffsetX +
                                                    sad2TopOffsetX + tiredTopOffsetX);
        int16_t leftTriLeft = static_cast<int16_t>(leftTriRight - triWidth + 1);
        const TriPlacement triLeftTop = {leftTriLeft, leftTriRight, sideBaseY, sideApexY};
        if (showTop) {
          drawTriangle(triLeftTop);
        }

        int16_t rightTriFlipLeft = static_cast<int16_t>(rightX + eyeWidth - 1 - 50 - bottomCenterInset);
        int16_t rightTriFlipRight = static_cast<int16_t>(rightTriFlipLeft + triWidth - 1);
        const TriPlacement triRightBottom = {rightTriFlipLeft, rightTriFlipRight,
                                             static_cast<int16_t>(flippedApexY + happy1BottomOffsetY),
                                             static_cast<int16_t>(flippedBaseY + happy1BottomOffsetY)};
        if (showBottom) {
          drawTriangle(triRightBottom);
        }

        int16_t leftTriFlipRight = static_cast<int16_t>(leftX + 50 + bottomCenterInset);
        int16_t leftTriFlipLeft = static_cast<int16_t>(leftTriFlipRight - triWidth + 1);
        const TriPlacement triLeftBottom = {leftTriFlipLeft, leftTriFlipRight,
                                            static_cast<int16_t>(flippedApexY + happy1BottomOffsetY),
                                            static_cast<int16_t>(flippedBaseY + happy1BottomOffsetY)};
        if (showBottom) {
          drawTriangle(triLeftBottom);
        }
      }
    }
  }
  /* TODO: Fix charging indicator. For now, disable it.
  {
    BatteryStatus bs = BatterySystem::getStatus();
    const bool plugged = (bs.state == ChargingState::PLUGGED_IN_CHARGING) ||
                         (bs.state == ChargingState::PLUGGED_IN_FULL);
    const bool lowBattery = bs.percent < LOW_BATT_THRESHOLD_PERCENT;
    if (plugged || lowBattery) {
      lv_color_t ringColor = lv_color_make(230, 70, 70);
      if (bs.state == ChargingState::PLUGGED_IN_FULL) {
        ringColor = lv_color_make(70, 200, 90);
      } else if (bs.state == ChargingState::PLUGGED_IN_CHARGING) {
        ringColor = lv_color_make(240, 210, 60);
      }
      lv_draw_rect_dsc_t ring;
      lv_draw_rect_dsc_init(&ring);
      ring.bg_color = ringColor;
      ring.bg_opa = LV_OPA_COVER;
      ring.border_opa = LV_OPA_TRANSP;
      ring.radius = POWER_RING_RADIUS;
      const int16_t cx = static_cast<int16_t>(SCREEN_WIDTH / 2);
      lv_area_t ringArea = {
        static_cast<int16_t>(cx - POWER_RING_RADIUS),
        static_cast<int16_t>(POWER_RING_CENTER_Y - POWER_RING_RADIUS),
        static_cast<int16_t>(cx + POWER_RING_RADIUS - 1),
        static_cast<int16_t>(POWER_RING_CENTER_Y + POWER_RING_RADIUS - 1)
      };
      lv_draw_rect(&layer, &ring, &ringArea);
    }
  }
  */
  if (sleepAnim.active) {
    Sleep_drawZs(&layer, millis());
  }
  lv_canvas_finish_layer(lvCanvas, &layer);
}



static EyeEmotion Emotion_pickAllowedWeighted(const SubStateSystem::Snapshot& ss) {
  // If forced emotions exist, choose deterministically from that set
  if (ss.forceCount > 0) {
    // Alternate within the force set for a bit of variation while staying deterministic
    static uint8_t forceIdx = 0;
    if (forceIdx >= ss.forceCount) forceIdx = 0;
    EyeEmotion chosen = ss.forced[forceIdx];
    forceIdx = (forceIdx + 1) % ss.forceCount;
    return chosen;
  }

  // Build weighted pool excluding suppressed emotions
  uint32_t total = 0;
  uint16_t tempWeights[EYE_EMO_COUNT];
  for (int i = 0; i < EYE_EMO_COUNT; ++i) {
    if (ss.suppress[i]) {
      tempWeights[i] = 0;
    } else {
      tempWeights[i] = emotionState.weights[i];
      total += tempWeights[i];
    }
  }
  if (total == 0) return EYE_EMO_IDLE;
  uint32_t r = static_cast<uint32_t>(random(0, total));
  for (int i = 0; i < EYE_EMO_COUNT; ++i) {
    if (r < tempWeights[i]) return static_cast<EyeEmotion>(i);
    r -= tempWeights[i];
  }
  return EYE_EMO_IDLE;
}

static void Emotion_scheduleNextPick();

static void Emotion_triggerNow() {
  SubStateSystem::update(subState);
  EyeEmotion next = Emotion_pickAllowedWeighted(subState);
  DisplaySystem_setEmotion(next);
  Emotion_scheduleNextPick();
}


static void Emotion_scheduleNextPick() {
  emotionState.nextEmotionPickMs = millis() + static_cast<uint32_t>(random(7000, 15001));  // 7-15s
}



static bool Touch_isTapOnEyes(const TouchPoint& pt) {
  DisplayLog::printf("[HitTest] Touch at x=%u, y=%u\n", pt.x, pt.y);
  DisplayLog::printf("[HitTest] Left eye box: x=%d-%d, y=%d-%d\n", 
                leftEyeBox.x, leftEyeBox.x + leftEyeBox.w,
                leftEyeBox.y, leftEyeBox.y + leftEyeBox.h);
  DisplayLog::printf("[HitTest] Right eye box: x=%d-%d, y=%d-%d\n", 
                rightEyeBox.x, rightEyeBox.x + rightEyeBox.w,
                rightEyeBox.y, rightEyeBox.y + rightEyeBox.h);
  
  // Check left eye
  if (pt.x >= leftEyeBox.x && pt.x <= leftEyeBox.x + leftEyeBox.w &&
      pt.y >= leftEyeBox.y && pt.y <= leftEyeBox.y + leftEyeBox.h) {
    DisplayLog::println(">>> LEFT EYE HIT! <<<");
    return true;
  }
  
  // Check right eye
  if (pt.x >= rightEyeBox.x && pt.x <= rightEyeBox.x + rightEyeBox.w &&
      pt.y >= rightEyeBox.y && pt.y <= rightEyeBox.y + rightEyeBox.h) {
    DisplayLog::println(">>> RIGHT EYE HIT! <<<");
    return true;
  }
  
  DisplayLog::println(">>> MISS - Outside eyes <<<");
  return false;
}



static bool EyeAnim_updateHappy(uint32_t nowMs) {
  if (!emotionState.happyActive) return false;
  if (nowMs >= emotionState.happyEndMs) {
    emotionState.happyActive = false;
    emotionState.currentEmotion = EYE_EMO_IDLE;
    eye.scale = 1.0f;
    eye.topOffset = 0;
    Emotion_scheduleNextPick();
    EyeRenderer_drawFrame(0, 1.0f);
    return false;
  }
  float t = (nowMs - emotionState.happyStartMs) / static_cast<float>(HAPPY_DURATION_MS);  // 0..1
  float phase = 2.0f * 3.1415926f * HAPPY_BOUNCE_FREQ_HZ * t;
  float s = sinf(phase);
  float eased = s * s * s;  // soft bounce
  int8_t offset = static_cast<int8_t>(HAPPY_BOUNCE_AMPLITUDE * eased);
  int16_t top = static_cast<int16_t>(eye.topOffset + offset);
  EyeRenderer_drawFrame(top, HAPPY_SCALE);
  return true;
}




// =====================================================
// Display System Lifecycle (begin / update)
// =====================================================
void DisplaySystem_begin() {
  Logger::begin(115200);
  delay(200);

  gfx.init();
  gfx.setRotation(1);
  Display_backlightInit();

  Display_initLvglCanvas();
  display.lastLvglTickMs = millis();
  Display_calculateEyeBoxes();

  // ---------------------------------------------------
  // Initialize visual objects (DATA ONLY, not rendered yet)
  // ---------------------------------------------------
  if (!g_visualObjects) {
    size_t objCount = static_cast<size_t>(ObjId::COUNT);
    g_visualObjects = static_cast<VisualObject*>(
        heap_caps_malloc(sizeof(VisualObject) * objCount,
                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (!g_visualObjects) {
      // Fallback to internal RAM if PSRAM fails
      g_visualObjects = static_cast<VisualObject*>(
          malloc(sizeof(VisualObject) * objCount));
    }
  }

  if (g_visualObjects) {
    // Base eye layout matches current on-screen eyes
    g_visualObjects[(int)ObjId::LeftEye] = {
      ObjId::LeftEye,
      leftEyeBox.x, leftEyeBox.y, leftEyeBox.w, leftEyeBox.h,
      0, 0, 1.0f, 1.0f,
      0, 0, 1.0f, 1.0f,
      EYE_RADIUS,
      true, 0
    };

    g_visualObjects[(int)ObjId::RightEye] = {
      ObjId::RightEye,
      rightEyeBox.x, rightEyeBox.y, rightEyeBox.w, rightEyeBox.h,
      0, 0, 1.0f, 1.0f,
      0, 0, 1.0f, 1.0f,
      EYE_RADIUS,
      true, 0
    };
  }

  // Set all targets to neutral at startup (baseline)
  ReturnVisualToNeutral();

  Clock_createLabels();
  Clock_setOpacity(LV_OPA_TRANSP);
  lv_obj_set_style_opa(lvCanvas, LV_OPA_COVER, 0);
  clockRt.lastTouchMs = millis();
  Clock_loadStored();
  
  TouchSystem::begin();
  MenuSystem::begin();
  randomSeed(esp_random());
  SubStateSystem::begin();
  hatchPrefsReady = hatchPrefs.begin("bubu", false);
  bool alreadyHatched = false;
  if (hatchPrefsReady) {
    if (HATCH_FORCE_RESET_ON_BOOT) {
      hatchPrefs.putBool("hatched", false);
    }
    alreadyHatched = hatchPrefs.getBool("hatched", false);
  }
  if (!alreadyHatched) {
    Hatch_start(millis());
  } else {
    hatch.active = false;
  }

  // Sync neutral eye scale into VisualObject before first draw
  g_visualObjects[(int)ObjId::LeftEye].scaleY  = eye.scale;
  g_visualObjects[(int)ObjId::RightEye].scaleY = eye.scale;

  if (hatch.active) {
    Hatch_render(millis());
  } else {
    EyeRenderer_drawFrame(0);
  }

  
  DisplayLog::println("==========================================");
  DisplayLog::println("🤖 Robot ready! Touch the eyes to pop! 🤖");
  DisplayLog::println("   Tap anywhere else to open menu");
  DisplayLog::println("==========================================");
}

// Forward declarations for game/idle helpers
static void updateGameEyes(bool gameRunning);
static void updateIdleBlinkAndEmotion(bool higherLayerActive, bool gameRunning);

void DisplaySystem_update() {
  // Keep LVGL tick running so LVGL timers/invalidations advance
  uint32_t nowMs = millis();
  Clean_update(nowMs);
  Sleep_update(nowMs);
  EyeColor_update(nowMs);
  // (Blink_update is now state-driven; removed from here)
  GlobalMotion_update(nowMs);
  static bool prevLayerVisible = true;
  uint32_t elapsed = nowMs - display.lastLvglTickMs;
  if (elapsed > 0) {
    lv_tick_inc(elapsed);
    display.lastLvglTickMs = nowMs;
  }
  UpdateVisualInterpolation(elapsed);
  TouchSystem::update();
  if (hatch.active) {
    Display_setCanvasVisible(true);
    Clock_setOpacity(LV_OPA_TRANSP);
    Hatch_update(nowMs);
    lv_timer_handler();
    return;
  }
  MenuSystem::render();
  static bool prevGameRunning = false;
  bool gameRunning = EyeGame::isRunning();
  if (prevGameRunning && !gameRunning) {
    MenuSystem::handleGameFinished();
  }
  prevGameRunning = gameRunning;

  // Suspend eyes (Layer 0) whenever higher layers are active
  bool gameActive = MenuSystem::isGameActive() || EyeGame::isRunning();
  bool feedActive = MenuSystem::isFeeding();
  bool higherLayerActive = MenuSystem::isOpen() || MenuSystem::isConnectOpen() ||
                           MenuSystem::isMessageOpen() || MenuSystem::isBatteryOpen() ||
                           MenuSystem::isStatsOpen() || MenuSystem::isOptionsOpen() ||
                           MenuSystem::isGamesOpen();
  static bool feedWasActive = false;
  if (feedActive && !feedWasActive) {
    Feed_start(nowMs);
  } else if (!feedActive && feedWasActive) {
    Feed_end();
  }
  feedWasActive = feedActive;
  bool layerVisible = gameActive || !higherLayerActive || feedActive;
  Display_setCanvasVisible(layerVisible);
  // ---------------------------------------------------
  // STEP 1: Idle destination movement render tick
  // This makes idle destination motion visible.
  // ---------------------------------------------------
  if (layerVisible &&
      (clockRt.state == IdleVisualState::Eyes) &&
      !EyeGame::isRunning() &&
      !eye.popInProgress &&
      !emotionState.excitedActive &&
      !emotionState.happyActive) {
    static uint32_t lastIdleLogMs = 0;
    if (IDLE_LOGS && millis() - lastIdleLogMs > 1000) {
      DisplayLog::printf("[IdleLook] Render tick: offX=%.2f offY=%.2f active=%d\n",
                         static_cast<double>(gMotion.offX),
                         static_cast<double>(gMotion.offY),
                         idleLook.active ? 1 : 0);
      lastIdleLogMs = millis();
    }
    uint8_t blinkMask = 0;
    if (eye.blinkInProgress) {
      if (blinkRt.left) blinkMask |= BLINK_LEFT_MASK;
      if (blinkRt.right) blinkMask |= BLINK_RIGHT_MASK;
    }
    EyeRenderer_drawFrame(eye.topOffset, eye.scale, blinkMask);
  }
  if (!layerVisible) {
    // Hide both eyes/clock when higher layers cover them
    Clock_setOpacity(LV_OPA_TRANSP);
    lv_obj_set_style_opa(lvCanvas, LV_OPA_TRANSP, 0);
  } else if (feedActive) {
    Clock_setOpacity(LV_OPA_TRANSP);
    lv_obj_set_style_opa(lvCanvas, LV_OPA_COVER, 0);
  } else if (!prevLayerVisible) {
    // Restore opacity based on current state when returning to layer
    if (clockRt.state == IdleVisualState::Clock) {
      Clock_setOpacity(LV_OPA_COVER);
      lv_obj_set_style_opa(lvCanvas, LV_OPA_TRANSP, 0);
    } else {
      Clock_setOpacity(LV_OPA_TRANSP);
      lv_obj_set_style_opa(lvCanvas, LV_OPA_COVER, 0);
    }
  }
  prevLayerVisible = layerVisible;

  
  // CRITICAL: clear gesture blocks on finger lift even if no new gesture arrives
  if (touchState.blockGesturesUntilLift || touchState.suppressMenuOpenUntilLift) {
    if (!TouchSystem::isTouchPressed()) {
      touchState.blockGesturesUntilLift = false;
      touchState.suppressMenuOpenUntilLift = false;
      DisplayLog::println("[Touch] Gesture block cleared after release");
    }
    if (TouchSystem::available()) {
      TouchSystem::get();  // drop stale while blocked
      DisplayLog::println("[Touch] Dropped gesture while blocked");
    }
  } else if (TouchSystem::available()) {
    DisplayLog::println(">>> TOUCH EVENT AVAILABLE <<<");
    TouchPoint touch = TouchSystem::get();
    DisplayLog::printf(">>> Touch details: x=%u, y=%u, gesture=%d <<<\n", touch.x, touch.y, touch.gesture);
    clockRt.lastTouchMs = nowMs;

    if (cleanAnim.active) {
      DisplayLog::println("[Clean] Touch ignored during clean animation");
      return;
    }
    if (sleepAnim.active) {
      if (touch.gesture == TOUCH_TAP) {
        Sleep_end(nowMs);
        touchState.blockGesturesUntilLift = true;
      }
      return;
    }
    if (MenuSystem::isFeeding()) {
      DisplayLog::println("[Feed] Touch ignored during feed animation");
      return;
    }

    // If clock screensaver is up, first tap only dismisses it (no menu action)
    bool clockWasVisible = (clockRt.state == IdleVisualState::Clock);
    if (clockWasVisible) {
      Clock_hide();
      // Consume this gesture; next tap can reach menu logic
      return;
    }

    // Treat TOUCH_NONE as release marker to clear any blocks
    if (touch.gesture == TOUCH_NONE) {
      if (TouchSystem::isTouchPressed()) {
        DisplayLog::println("[Touch] Release marker ignored while finger still down");
        return;
      }
      DisplayLog::println("[Touch] Release marker received, clearing blocks");
      touchState.blockGesturesUntilLift = false;
      touchState.suppressMenuOpenUntilLift = false;
      return;
    }

    if (EyeGame::isRunning()) {
      DisplayLog::println("[Processing] Game is ACTIVE (Layer 5)");
      switch (touch.gesture) {
        case TOUCH_TAP:
            EyeGame::handleTap(touch.x, touch.y);
            touchState.blockGesturesUntilLift = true;
            break;
        case TOUCH_LONG_PRESS:
          // Long press anywhere exits game back to games menu
          if (!Touch_isTapOnEyes(touch)) {
            DisplayLog::println("[Layer 5] LONG_PRESS off eyes -> exit to games menu");
            EyeGame::stop();
            MenuSystem::handleGameFinished();
            touchState.blockGesturesUntilLift = true;
            break;
          }
          DisplayLog::println("[Layer 5] LONG_PRESS on eyes ignored");
          break;
        default:
          break;
      }
    } else if (MenuSystem::isOptionsOpen()) {
      DisplayLog::println("[Processing] Options is OPEN (Layer 3)");
      switch (touch.gesture) {
        case TOUCH_TAP:
          DisplayLog::println("[Layer 3] Action: ACTIVATE OPTION");
          MenuSystem::activateCurrentOption();
          touchState.blockGesturesUntilLift = true;
          break;
        case TOUCH_LONG_PRESS:
          DisplayLog::println("[Layer 3] Action: BACK to stats (Layer 2)");
          MenuSystem::closeOptionsToStats();
          touchState.blockGesturesUntilLift = true;
          break;
        case TOUCH_SWIPE_UP:
        case TOUCH_SWIPE_LEFT:
          DisplayLog::println("[Layer 3] Action: OPTIONS PREV");
          MenuSystem::selectOptionsPrev();
          break;
        case TOUCH_SWIPE_DOWN:
        case TOUCH_SWIPE_RIGHT:
          DisplayLog::println("[Layer 3] Action: OPTIONS NEXT");
          MenuSystem::selectOptionsNext();
          break;
        default:
          break;
      }
    } else if (MenuSystem::isGamesOpen()) {
      DisplayLog::println("[Processing] Games menu is OPEN (Layer 4)");
      switch (touch.gesture) {
        case TOUCH_TAP:
          DisplayLog::println("[Layer 4] Action: START Tap the Greens");
          MenuSystem::startTapTheGreens();
          touchState.blockGesturesUntilLift = true;
          break;
        case TOUCH_LONG_PRESS:
          DisplayLog::println("[Layer 4] Action: BACK to stats (Layer 2)");
          MenuSystem::closeGamesToStats();
          touchState.blockGesturesUntilLift = true;
          break;
        default:
          break;
      }
    } else if (MenuSystem::isConnectOpen()) {
      DisplayLog::println("[Processing] Connect is OPEN (Layer 2)");
      switch (touch.gesture) {
        case TOUCH_TAP:
          if (MenuSystem::handleConnectTap(touch.x, touch.y)) {
            touchState.blockGesturesUntilLift = true;
          }
          break;
        case TOUCH_LONG_PRESS:
          DisplayLog::println("[Layer 2] Action: EXIT to menu (Layer 1)");
          MenuSystem::closeConnectToMenu();
          touchState.blockGesturesUntilLift = true;
          break;
        default:
          break;
      }
    } else if (MenuSystem::isMessageOpen()) {
      DisplayLog::println("[Processing] Message is OPEN (Layer 2)");
      switch (touch.gesture) {
        case TOUCH_LONG_PRESS:
          DisplayLog::println("[Layer 2] Action: EXIT to menu (Layer 1)");
          MenuSystem::closeMessageToMenu();
          touchState.blockGesturesUntilLift = true;
          break;
        default:
          break;
      }
    } else if (MenuSystem::isStatsOpen()) {
      DisplayLog::println("[Processing] Stats is OPEN (Layer 2)");
      switch (touch.gesture) {
        case TOUCH_SWIPE_UP:
        case TOUCH_SWIPE_LEFT:
          DisplayLog::println("[Layer 2] Action: STAT PREV");
          MenuSystem::statsPrev();
          break;
        case TOUCH_SWIPE_DOWN:
        case TOUCH_SWIPE_RIGHT:
          DisplayLog::println("[Layer 2] Action: STAT NEXT");
          MenuSystem::statsNext();
          break;
        case TOUCH_TAP:
          if (MenuSystem::getCurrentStatIndex() == 3 &&
              MenuSystem::isTapOnStatsTitle(touch.x, touch.y)) {
            DisplayLog::println("[Layer 2] Action: CLEAN animation");
            Clean_start(nowMs);
            MenuSystem::close();
            touchState.blockGesturesUntilLift = true;
            break;
          }
          if (MenuSystem::getCurrentStatIndex() == 1) {
            DisplayLog::println("[Layer 2] Action: OPEN games (Layer 4)");
            MenuSystem::openGamesMenu();
          } else {
            DisplayLog::println("[Layer 2] Action: OPEN options (Layer 3)");
            MenuSystem::openOptionsForCurrentStat();
          }
          touchState.blockGesturesUntilLift = true;
          break;
        case TOUCH_LONG_PRESS:
          DisplayLog::println("[Layer 2] Action: EXIT to menu (Layer 1)");
          MenuSystem::closeStatsToMenu();
          touchState.blockGesturesUntilLift = true;  // Block until finger lifts
          break;
        default:
          break;
      }
    } else if (MenuSystem::isBatteryOpen()) {
      DisplayLog::println("[Processing] Battery is OPEN (Layer 2)");
      switch (touch.gesture) {
        case TOUCH_TAP:
        case TOUCH_LONG_PRESS:
          DisplayLog::println("[Layer 2] Action: EXIT to menu (Layer 1)");
          MenuSystem::closeBatteryToMenu();
          touchState.blockGesturesUntilLift = true;
          break;
        default:
          break;
      }
    } else if (MenuSystem::isLevelOpen()) {
      DisplayLog::println("[Processing] Level is OPEN (Layer 2)");
      switch (touch.gesture) {
        case TOUCH_TAP:
        case TOUCH_LONG_PRESS:
          DisplayLog::println("[Layer 2] Action: EXIT to menu (Layer 1)");
          MenuSystem::closeLevelToMenu();
          touchState.blockGesturesUntilLift = true;
          break;
        default:
          break;
      }
    } else if (MenuSystem::isOpen()) {
      DisplayLog::println("[Processing] Menu is OPEN (Layer 1)");
      switch (touch.gesture) {
        case TOUCH_TAP:
          if (MenuSystem::isTapOnSelected(touch.x, touch.y)) {
            DisplayLog::println("[Layer 1] Action: ENTER (hit selected)");
            MenuSystem::activateSelected();
            if (MenuSystem::isStatsOpen() || MenuSystem::isConnectOpen() ||
                MenuSystem::isMessageOpen()) {
              touchState.blockGesturesUntilLift = true;
            }
          } else {
            DisplayLog::println("[Layer 1] TAP ignored (not on selected item)");
          }
          break;
          
        case TOUCH_SWIPE_UP:
        case TOUCH_SWIPE_LEFT:
          DisplayLog::println("[Layer 1] Action: SELECT PREV");
          MenuSystem::selectPrev();
          break;
          
        case TOUCH_SWIPE_DOWN:
        case TOUCH_SWIPE_RIGHT:
          DisplayLog::println("[Layer 1] Action: SELECT NEXT");
          MenuSystem::selectNext();
          break;
          
        case TOUCH_LONG_PRESS:
          DisplayLog::println("[Layer 1] Action: EXIT to Layer 0");
          MenuSystem::close();
          touchState.blockGesturesUntilLift = true;  // Block until finger lifts
          break;
          
        default:
          break;
      }
    } else {
      DisplayLog::println("[Processing] Menu is CLOSED (Layer 0)");
      switch (touch.gesture) {
        case TOUCH_TAP:
          DisplayLog::println("[Layer 0] Gesture: TAP");
          if (Touch_isTapOnEyes(touch)) {
            DisplayLog::println("[Layer 0] -> Eyes hit, triggering test emotion");
            Emotion_triggerTest(nowMs);
          } else {
            DisplayLog::println("[Layer 0] -> Eyes missed, opening menu!");
            MenuSystem::open();
            touchState.blockGesturesUntilLift = true;  // Block until finger lifts after opening
          }
          break;
        default:
          DisplayLog::printf("[Layer 0] Gesture %d ignored on closed menu\n", touch.gesture);
          break;
      }
    }
  }
  // If we suppressed menu reopening after a hold, clear once finger lifts
  if (touchState.suppressMenuOpenUntilLift && !TouchSystem::isTouchPressed()) {
    touchState.suppressMenuOpenUntilLift = false;
    DisplayLog::println("[Touch] Suppression cleared after release");
  }
  
  // Always update LVGL (for menu animations)
  lv_timer_handler();
  
  bool gameRunningNow = EyeGame::isRunning();
  updateGameEyes(gameRunningNow);
  IdleLook_update(nowMs);
  // Add state-driven blink update after IdleLook_update
  if (idleState.active) {
    if (idleState.type == IdleStateType::Blink) {
      if (!blinkRt.active) {
        Blink_start(nowMs, true, true);
      }
      if (Blink_update(nowMs)) {
        idleState.active = false;
        idleState.type = IdleStateType::None;
      }
    } else if (idleState.type == IdleStateType::Wink) {
      if (!blinkRt.active) {
        Wink_start(nowMs, random(0, 2) == 0);
      }
      if (Blink_update(nowMs)) {
        idleState.active = false;
        idleState.type = IdleStateType::None;
      }
    } else if (idleState.type == IdleStateType::JitterBoth ||
               idleState.type == IdleStateType::JitterLeft ||
               idleState.type == IdleStateType::JitterRight) {
      // NEGATIVE CUE: irritation / discomfort / unmet need
      if (!gMotion.jitterAmp) {   // fire once
        GlobalMotion_kickJitter(JITTER_AMP_PX, JITTER_DURATION_MS);
        // Soft noise burst for jitter states
        SoundSystem::eyeJitter(0.5f);
        if (IDLE_LOGS) DisplayLog::println("[IdleState] JITTER start");
      }
      if (nowMs - idleState.startMs >= idleState.durationMs) {
        idleState.active = false;
        idleState.type = IdleStateType::None;
      }
    } else if (idleState.type == IdleStateType::HappyBounce) {
      uint32_t tMs = nowMs - idleState.startMs;
      float t = static_cast<float>(tMs) / static_cast<float>(HAPPY_DURATION_MS);  // 0..1

      float phase = 2.0f * 3.1415926f * HAPPY_BOUNCE_FREQ_HZ * t;

      // Smooth continuous body motion
      float pos = sinf(phase);
      gMotion.offY = static_cast<float>(HAPPY_BOUNCE_AMPLITUDE) * pos;

      // Velocity-based squash & stretch (top edge reacts to motion)
      float vel = cosf(phase);
      eye.topOffset = static_cast<int16_t>(vel * 6.0f);

      // Happy bean scale
      eye.scale = HAPPY_SCALE;

      if (!happyPipPlayed && pos > 0.92f) {
        SoundSystem::happyPip(0.7f);
        happyPipPlayed = true;
      }
      if (pos < 0.2f) {
        happyPipPlayed = false;
      }

      if (tMs >= idleState.durationMs) {
        gMotion.offY = 0.0f;
        eye.topOffset = 0;
        eye.scale = 1.0f;
        idleState.active = false;
        idleState.type = IdleStateType::None;
      }
    } else if (idleState.type == IdleStateType::Excited1) {
      // Excited1 idle state logic
      uint32_t tMs = nowMs - idleState.startMs;
      if (tMs < 150) {
        // SNAP EXPAND
        float p = tMs / 150.0f;
        eye.scale = 1.0f + p * 0.2f;   // up to 1.2
      } else if (tMs < 1150) {
        // HOLD BIG
        eye.scale = 1.2f;
      } else if (tMs < 1300) {
        // EXCEPTION: jitter here represents excitement, NOT irritation
        // JITTER HIT (single trigger)
        eye.scale = 1.2f;
        if (gMotion.jitterAmp == 0) {
          GlobalMotion_kickJitter(10, 120);
        }
      } else if (tMs < 1650) {
        // POST-JITTER HOLD
        eye.scale = 1.2f;
      } else if (tMs < 2000) {
        // SNAP BACK
        float p = (tMs - 1650) / 350.0f;
        if (p > 1.0f) p = 1.0f;
        eye.scale = 1.2f - p * 0.2f;
      } else {
        eye.scale = 1.0f;
        idleState.active = false;
        idleState.type = IdleStateType::None;
      }
      // No offsets/jitter/topOffset are modified here except for GlobalMotion_kickJitter above.
    } else if (idleState.type == IdleStateType::Judging) {
      uint32_t tMs = nowMs - idleState.startMs;
      float t = static_cast<float>(tMs) / static_cast<float>(HAPPY_DURATION_MS);

      float phase = 2.0f * 3.1415926f * HAPPY_BOUNCE_FREQ_HZ * t;

      // Sideways body swing
      float pos = sinf(phase);
      gMotion.offX = static_cast<float>(HAPPY_BOUNCE_AMPLITUDE) * pos;

      // Opposing squash & stretch (top edge reacts to lateral motion)
      float vel = cosf(phase);
      // eye.topOffset = static_cast<int16_t>(vel * 4.0f); // REMOVED vertical squash

      // Horizontal squash & stretch based on lateral velocity
      float stretch = fabsf(vel);
      eye.scale = HAPPY_SCALE;
      g_visualObjects[(int)ObjId::LeftEye].scaleX  = 1.0f + stretch * 0.10f;
      g_visualObjects[(int)ObjId::RightEye].scaleX = 1.0f + stretch * 0.10f;
      g_visualObjects[(int)ObjId::LeftEye].scaleY  = 1.0f - stretch * 0.06f;
      g_visualObjects[(int)ObjId::RightEye].scaleY = 1.0f - stretch * 0.06f;

      if (tMs >= idleState.durationMs) {
        gMotion.offX = 0.0f;
        eye.topOffset = 0;
        eye.scale = 1.0f;
        g_visualObjects[(int)ObjId::LeftEye].scaleX  = 1.0f;
        g_visualObjects[(int)ObjId::RightEye].scaleX = 1.0f;
        g_visualObjects[(int)ObjId::LeftEye].scaleY  = 1.0f;
        g_visualObjects[(int)ObjId::RightEye].scaleY = 1.0f;
        idleState.active = false;
        idleState.type = IdleStateType::None;
      }
    } else if (idleState.type == IdleStateType::Giggle) {
      if (idleState.startMs == nowMs) {
        eye.topOffset = GIGGLE_OFFSET_PX;
        GlobalMotion_kickJitter(GIGGLE_JITTER_AMP, GIGGLE_DURATION_MS);
        if (IDLE_LOGS) DisplayLog::println("[IdleState] GIGGLE start");
      }
      // Force Y-only jitter
      gMotion.jitterX = 0;

      if (nowMs - idleState.startMs >= idleState.durationMs) {
        eye.topOffset = 0;
        idleState.active = false;
        idleState.type = IdleStateType::None;
      }
    }
  }
  updateIdleBlinkAndEmotion(higherLayerActive, gameRunningNow);
  bool popRender = layerVisible &&
                   !feedActive &&
                   !higherLayerActive &&
                   !gameRunningNow &&
                   (clockRt.state == IdleVisualState::Eyes);
  Pop_update(nowMs, popRender);
  Clock_updateIdle(nowMs);
}

void DisplaySystem_notifyUserInteraction(uint32_t nowMs) {
  clockRt.lastTouchMs = nowMs;
  if (clockRt.state == IdleVisualState::Clock) {
    Clock_hide();
  }
}

// =====================================================
// Game Eye Updates
// =====================================================
// Helper: refresh eyes during the game so colors update regularly
static void updateGameEyes(bool gameRunning) {
  static bool prevGameRunning = false;
  static uint16_t lastLeftColor = 0;
  static uint16_t lastRightColor = 0;
  static uint32_t nextRefreshMs = 0;

  if (!gameRunning) {
    prevGameRunning = false;
    nextRefreshMs = 0;
    return;
  }

  EyeGame::update();

  uint16_t left = EyeGame::getLeftColor565();
  uint16_t right = EyeGame::getRightColor565();
  uint32_t now = millis();
  bool colorChanged = !prevGameRunning || left != lastLeftColor || right != lastRightColor;
  bool refreshDue = (nextRefreshMs == 0) || (now >= nextRefreshMs);

  if (colorChanged || refreshDue) {
    EyeRenderer_drawFrame(eye.topOffset, eye.scale);
    lastLeftColor = left;
    lastRightColor = right;
    nextRefreshMs = now + static_cast<uint32_t>(random(1000, 2001));  // 1-2s refresh cadence
  }

  prevGameRunning = true;
}

// =====================================================
// Idle Blink & Emotion Update Helpers
// =====================================================
// Helper: handle idle blink/emotion animation (Layer 0 only, no game)
static void updateIdleBlinkAndEmotion(bool higherLayerActive, bool gameRunning) {
  // When clock screensaver is active, suppress eye/emotion updates
  if (clockRt.state == IdleVisualState::Clock) return;
  if (sleepAnim.active) return;
  if (MenuSystem::isFeeding()) return;
  // Update sub-state gating once per call
  SubStateSystem::update(subState);

  // Update eye animations when fully in Layer 0
  if (!higherLayerActive && !gameRunning) {
    uint32_t nowBlink = millis();

    if ((emotionState.currentEmotion == EYE_EMO_ANGRY1 ||
         emotionState.currentEmotion == EYE_EMO_ANGRY2 ||
         emotionState.currentEmotion == EYE_EMO_ANGRY3) &&
        emotionState.angryEndMs > 0 &&
        nowBlink >= emotionState.angryEndMs) {
      emotionState.currentEmotion = EYE_EMO_IDLE;
      emotionState.angryStartMs = 0;
      emotionState.angryEndMs = 0;
      Emotion_scheduleNextPick();
    }
    if (emotionState.currentEmotion == EYE_EMO_TIRED &&
        emotionState.tiredEndMs > 0 &&
        nowBlink >= emotionState.tiredEndMs) {
      emotionState.currentEmotion = EYE_EMO_IDLE;
      emotionState.tiredStartMs = 0;
      emotionState.tiredEndMs = 0;
      Emotion_scheduleNextPick();
    }
    if (emotionState.currentEmotion == EYE_EMO_WORRIED1 &&
        emotionState.worriedEndMs > 0 &&
        nowBlink >= emotionState.worriedEndMs) {
      emotionState.currentEmotion = EYE_EMO_IDLE;
      emotionState.worriedStartMs = 0;
      emotionState.worriedEndMs = 0;
      Emotion_scheduleNextPick();
    }
    if (emotionState.currentEmotion == EYE_EMO_SAD1 &&
        emotionState.sadEndMs > 0 &&
        nowBlink >= emotionState.sadEndMs) {
      emotionState.currentEmotion = EYE_EMO_IDLE;
      emotionState.sadStartMs = 0;
      emotionState.sadEndMs = 0;
      Emotion_scheduleNextPick();
    }
    if (emotionState.currentEmotion == EYE_EMO_SAD2 &&
        emotionState.sad2EndMs > 0 &&
        nowBlink >= emotionState.sad2EndMs) {
      emotionState.currentEmotion = EYE_EMO_IDLE;
      emotionState.sad2StartMs = 0;
      emotionState.sad2EndMs = 0;
      Emotion_scheduleNextPick();
    }
    if (emotionState.currentEmotion == EYE_EMO_HAPPY1 &&
        emotionState.happy1EndMs > 0 &&
        nowBlink >= emotionState.happy1EndMs) {
      emotionState.currentEmotion = EYE_EMO_IDLE;
      emotionState.happy1StartMs = 0;
      emotionState.happy1EndMs = 0;
      Emotion_scheduleNextPick();
    }
    if (emotionState.currentEmotion == EYE_EMO_HAPPY2 &&
        emotionState.happy2EndMs > 0 &&
        nowBlink >= emotionState.happy2EndMs) {
      emotionState.currentEmotion = EYE_EMO_IDLE;
      emotionState.happy2StartMs = 0;
      emotionState.happy2EndMs = 0;
      Emotion_scheduleNextPick();
    }
    if ((emotionState.currentEmotion == EYE_EMO_CURIOUS1 ||
         emotionState.currentEmotion == EYE_EMO_CURIOUS2) &&
        emotionState.curiousEndMs > 0 &&
        nowBlink >= emotionState.curiousEndMs) {
      emotionState.currentEmotion = EYE_EMO_IDLE;
      emotionState.curiousStartMs = 0;
      emotionState.curiousEndMs = 0;
      Emotion_scheduleNextPick();
    }

    if (Emotion_isActive()) {
      if (emotionState.excitedActive) {
      // (legacy excited animation removed)
      } else if (emotionState.happyActive) {
        EyeAnim_updateHappy(nowBlink);
      }
      return;
    }

    if (idleState.active || idleLook.active) {
      return;
    }

    if (nowBlink >= emotionState.nextEmotionPickMs) {
      EyeEmotion next = Emotion_pickAllowedWeighted(subState);
      DisplaySystem_setEmotion(next);
      Emotion_scheduleNextPick();
    }
  }
}

// =====================================================
// Emotion Public API
// =====================================================
// Emotion API: getter and setter
EyeEmotion DisplaySystem_getEmotion() {
  return emotionState.currentEmotion;
}

void DisplaySystem_setEmotion(EyeEmotion emo) {
  // Disable excited: normalize to IDLE
  if (emo == EYE_EMO_EXCITED) {
    emo = EYE_EMO_IDLE;
  }
  uint32_t now = millis();
  emotionState.excitedActive = false;
  emotionState.happyActive = false;
  if (emo == EYE_EMO_ANGRY1 || emo == EYE_EMO_ANGRY2 || emo == EYE_EMO_ANGRY3) {
    emotionState.angryStartMs = now;
    emotionState.angryEndMs = now + 2000;
  } else {
    emotionState.angryStartMs = 0;
    emotionState.angryEndMs = 0;
  }
  if (emo == EYE_EMO_TIRED) {
    emotionState.tiredStartMs = now;
    emotionState.tiredEndMs = now + 2000;
  } else {
    emotionState.tiredStartMs = 0;
    emotionState.tiredEndMs = 0;
  }
  if (emo == EYE_EMO_WORRIED1) {
    emotionState.worriedStartMs = now;
    emotionState.worriedEndMs = now + 2000;
  } else {
    emotionState.worriedStartMs = 0;
    emotionState.worriedEndMs = 0;
  }
  if (emo == EYE_EMO_SAD1) {
    emotionState.sadStartMs = now;
    emotionState.sadEndMs = now + 2000;
  } else {
    emotionState.sadStartMs = 0;
    emotionState.sadEndMs = 0;
  }
  if (emo == EYE_EMO_SAD2) {
    emotionState.sad2StartMs = now;
    emotionState.sad2EndMs = now + 2000;
  } else {
    emotionState.sad2StartMs = 0;
    emotionState.sad2EndMs = 0;
  }
  if (emo == EYE_EMO_HAPPY1) {
    emotionState.happy1StartMs = now;
    emotionState.happy1EndMs = now + 2000;
  } else {
    emotionState.happy1StartMs = 0;
    emotionState.happy1EndMs = 0;
  }
  if (emo == EYE_EMO_HAPPY2) {
    emotionState.happy2StartMs = now;
    emotionState.happy2EndMs = now + 2000;
  } else {
    emotionState.happy2StartMs = 0;
    emotionState.happy2EndMs = 0;
  }
  if (emo == EYE_EMO_CURIOUS1 || emo == EYE_EMO_CURIOUS2) {
    emotionState.curiousStartMs = now;
    emotionState.curiousEndMs = now + 2000;
  } else {
    emotionState.curiousStartMs = 0;
    emotionState.curiousEndMs = 0;
  }
  emotionState.currentEmotion = emo;
  // No special timers for disabled emotions; other emotions fall through without extra handling
}

void DisplaySystem_startExcitedNow() { DisplaySystem_setEmotion(EYE_EMO_EXCITED); }
void DisplaySystem_startSleep() { Sleep_start(millis()); }
bool DisplaySystem_isHatching() { return hatch.active; }

// =====================================================
// Visual Interpolation (current -> target)
// =====================================================
static void UpdateVisualInterpolation(uint32_t dtMs) {
  if (!g_visualObjects) return;

  // Interpolation speed (tuning knob)
  float k;
  switch (idleMoveSpeed) {
    case IdleMoveSpeed::Slow:   k = 0.06f; break;
    case IdleMoveSpeed::Fast:   k = 0.30f; break;
    default:                    k = 0.15f; break;
  }
  // Safety clamp to avoid unstable micro-jitter
  if (k > 0.25f) k = 0.25f;

  for (int i = 0; i < (int)ObjId::COUNT; ++i) {
    auto& o = g_visualObjects[i];

    o.offsetX += (int16_t)((o.targetOffsetX - o.offsetX) * k);
    o.offsetY += (int16_t)((o.targetOffsetY - o.offsetY) * k);
    o.scaleX  += (o.targetScaleX  - o.scaleX)  * k;
    o.scaleY  += (o.targetScaleY  - o.scaleY)  * k;
  }

  // Global motion interpolation (body motion only; NEVER damp jitter)
  if (gMotion.jitterAmp == 0) {
    float dx = gMotion.targetOffX - gMotion.offX;
    float dy = gMotion.targetOffY - gMotion.offY;

    if (fabsf(dx) < 0.5f) gMotion.offX = gMotion.targetOffX;
    else gMotion.offX += dx * k;

    if (fabsf(dy) < 0.5f) gMotion.offY = gMotion.targetOffY;
    else gMotion.offY += dy * k;
  }
}

// =====================================================
// Visual Neutral Return Helper (Target-based Model)
// =====================================================
static void ReturnVisualToNeutral() {
  // Reset object targets
  for (int i = 0; i < (int)ObjId::COUNT; ++i) {
    auto& o = g_visualObjects[i];
    o.targetOffsetX = 0;
    o.targetOffsetY = 0;
    o.targetScaleX  = 1.0f;
    o.targetScaleY  = 1.0f;
  }

  // Global motion targets
  gMotion.targetOffX = 0;
  gMotion.targetOffY = 0;
}
