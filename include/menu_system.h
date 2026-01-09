#pragma once
#include <Arduino.h>

enum MenuState {
  MENU_CLOSED,
  MENU_OPEN,
  MENU_FEEDING,
  MENU_BATTERY_OPEN,
  MENU_CONNECT_OPEN,
  MENU_MESSAGE_OPEN,
  MENU_STATS_OPEN,
  MENU_OPTIONS_OPEN,
  MENU_GAMES_OPEN,
  MENU_GAME_ACTIVE,
  MENU_LEVEL_OPEN
};

enum MenuItem {
  MENU_FEED,
  MENU_PLAY,
  MENU_CLEAN,
  MENU_SLEEP,
  MENU_CONNECT,
  MENU_MESSAGE,
  MENU_BATTERY,
  MENU_STATS,
  MENU_LEVEL,
  MENU_ITEM_COUNT  // Total number of items
};

namespace MenuSystem {
  void begin();
  void open();
  void close();
  void showStats();
  bool isOpen();
  bool isFeeding();
  bool isBatteryOpen();
  bool isConnectOpen();
  bool isMessageOpen();
  bool isStatsOpen();
  bool isOptionsOpen();
  bool isGamesOpen();
  bool isGameActive();
  bool isLevelOpen();
  void otaSetActive(bool active);
  void otaPulse(uint32_t nowMs);
  
  void selectNext();
  void selectPrev();
  void statsNext();
  void statsPrev();
  size_t getCurrentStatIndex();
  MenuItem getSelected();
  void activateSelected();
  bool handleConnectTap(uint16_t x, uint16_t y);
  void closeConnectToMenu();
  void closeMessageToMenu();
  void closeStatsToMenu();
  void closeBatteryToMenu();
  void closeLevelToMenu();
  void openOptionsForCurrentStat();
  void closeOptionsToStats();
  void activateCurrentOption();
  void selectOptionsPrev();
  void selectOptionsNext();
  void openGamesMenu();
  void closeGamesToStats();
  void startTapTheGreens();
  void handleGameFinished();

  void render();  // Call this from display update

  // Hit-test helpers
  bool isTapOnSelected(uint16_t x, uint16_t y);
  bool isTapOnStatsTitle(uint16_t x, uint16_t y);
}
