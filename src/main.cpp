#include <Arduino.h>
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "display_system.h"
#include "care_system.h"
#include "eye_game.h"
#include "imu_monitor.h"
#include "wifi_service.h"
#include "logger.h"
#include "ota/ota_manager.h"
#include "sound/sound_system.h"
#include "battery_system.h"
#include "level_system.h"
#include <Wire.h>
#include "tca6408.h"
#include "board_pins.h"
DEFINE_MODULE_LOGGER(MainLog)

static void checkPsram() {
  if (psramFound()) {
    MainLog::println("[BOOT] PSRAM detected");
    MainLog::printf("[BOOT] PSRAM free: %u bytes\n",
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  } else {
    MainLog::println("[BOOT] PSRAM NOT FOUND");
  }
}

static void printMemoryReport(const char* label) {
  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
  size_t freeHeap = ESP.getFreeHeap();
  size_t largestFree = info.largest_free_block;
  size_t freePsram = ESP.getFreePsram();
  size_t totalPsram = ESP.getPsramSize();

  MainLog::println("------ Memory Report ------");
  if (label) MainLog::printf("Label: %s\n", label);
  MainLog::printf("Internal RAM free:    %u bytes\n", static_cast<unsigned>(freeHeap));
  MainLog::printf("Internal largest blk: %u bytes\n", static_cast<unsigned>(largestFree));
  MainLog::printf("PSRAM total:          %u bytes\n", static_cast<unsigned>(totalPsram));
  MainLog::printf("PSRAM free:           %u bytes\n", static_cast<unsigned>(freePsram));
  MainLog::println("---------------------------");
}

// Main app entrypoints
void setup() {
  Logger::begin(115200);
  delay(100);

  checkPsram();
  printMemoryReport("Boot start");

  wifiInit();
  SoundSystem::begin();
  
  // Init I2C and I/O expander for battery system
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  TCA6408::begin();
  DisplaySystem_begin();

  printMemoryReport("After display init");
  BatterySystem::begin();
  LevelSystem::begin();
  ImuMonitor::begin();
  wifiAutoConnectKnown();  // boot: scan + connect to known SSID if visible (no provisioning)
  CareSystem::begin();
  EyeGame::Config gameCfg;
  gameCfg.maxRounds = 40;
  gameCfg.rewardPerHit = CareSystem::kGameRewardPerHit;
  gameCfg.wrongTapMoodDelta = CareSystem::kGameWrongTapMood;
  gameCfg.wrongTapEnergyDelta = CareSystem::kGameWrongTapEnergy;
  EyeGame::configure(gameCfg);
  BubuOTA::begin();
  if (BubuOTA::wasRollback()) {
    Serial.println("[OTA] Rollback detected (previous update crashed).");
  }
}

static bool ota_check_done = false;

void loop() {
  // After wifi connection, check for OTA update once
  if (!ota_check_done && wifiGetState() == WifiState::CONNECTED) {
    ota_check_done = true;
    BubuOTA::runOnce();
    // If we're still here, it means no update was performed.
    // This could be because we're on the latest version, or the check/install failed.
    // In either case, shut down WiFi to save power.
    wifiStop();
  }

  CareSystem::setDecaySuspended(DisplaySystem_isHatching());
  CareSystem::update();
  EyeGame::update();
  ImuMonitor::update(millis());
  DisplaySystem_update();
  BatterySystem::update();
  wifiUpdate();
  delay(1);
}
