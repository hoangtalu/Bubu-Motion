#include "menu_system.h"
#include "care_system.h"
#include "level_system.h"
#include "display_system.h"
#include "eye_game.h"
#include "message_system.h"
#include "wifi_service.h"
#include "ota/ota_manager.h"
#include "battery_system.h"
#include <lvgl.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include "logger.h"
DEFINE_MODULE_LOGGER(MenuLog)

namespace {

MenuState currentState = MENU_CLOSED;
MenuItem selectedItem = MENU_FEED;
bool gamesOpenedFromMenu = false;

// LVGL objects
lv_obj_t* menuPanel = nullptr;
lv_obj_t* menuList = nullptr;
lv_obj_t* menuItems[MENU_ITEM_COUNT] = {nullptr};
lv_obj_t* statsPanel = nullptr;
lv_obj_t* statsArc = nullptr;
lv_obj_t* statsTitle = nullptr;
lv_obj_t* optionsPanel = nullptr;
lv_obj_t* optionsTitle = nullptr;
lv_obj_t* optionsAction = nullptr;
lv_obj_t* gamesPanel = nullptr;
lv_obj_t* gamesTitle = nullptr;
lv_obj_t* gamesAction = nullptr;
lv_obj_t* gamesStatus = nullptr;
lv_obj_t* connectPanel = nullptr;
lv_obj_t* connectTitle = nullptr;
lv_obj_t* connectRow = nullptr;
lv_obj_t* connectLabel = nullptr;
lv_obj_t* connectSwitch = nullptr;
lv_obj_t* connectOtaBtn = nullptr;
lv_obj_t* batteryPanel = nullptr;
lv_obj_t* batteryTitle = nullptr;
lv_obj_t* batteryValue = nullptr;
lv_obj_t* levelPanel = nullptr;
lv_obj_t* levelArc = nullptr;
lv_obj_t* levelLabel = nullptr;

// Menu items (uses Vietnamese-capable font)
const char* menuItemNames = 
  "FEED\n"
  "PLAY\n"
  "CLEAN\n"
  "SLEEP\n"
  "CONNECT\n"
  "MESSAGE\n"
  "BATTERY\n"
  "STATS\n"
  "LEVEL";
const char* menuItemLabelTexts[MENU_ITEM_COUNT] = {"CHO ĂN", "GIẢI TRÍ", "TẮM RỬA", "NGỦ NGHỈ", "KẾT NỐI", "THÔNG ĐIỆP", "PIN", "TRẠNG THÁI", "CẤP ĐỘ"};

// Stats
constexpr size_t STAT_COUNT = 4;
const char* statNames[STAT_COUNT] = {"CÁI BỤNG", "CẢM XÚC", "NĂNG LƯỢNG", "SẠCH SẼ"};
uint32_t statColors[STAT_COUNT] = {0xFF7F50, 0x70C1FF, 0xFFD23F, 0x58F5C9};
size_t statIndex = 0;
const char* statOptionNames[STAT_COUNT] = {"BIT-Za", "TRÒ CHƠI", "NGỦ", "TẮM"};

// Colors
constexpr uint32_t COLOR_BACKGROUND = 0x050812;
constexpr uint32_t COLOR_MINT = 0x58F5C9;
constexpr uint32_t COLOR_PINK = 0xDB1758;
constexpr uint32_t COLOR_TEXT = 0xFFFFFF;
constexpr uint32_t COLOR_CONNECT_OK = 0x4CAF50;

static constexpr uint32_t OTA_BREATH_PERIOD_MS = 2000;
static bool otaActive = false;
static uint32_t otaStartMs = 0;
static uint32_t otaLastTickMs = 0;

char gameStatusMsg[64] = "Chạm để chơi";

static constexpr uint32_t FEED_ANIM_DURATION_MS = 5000;
static uint32_t feedAnimEndMs = 0;

enum OptionSelection {
  OPTION_MAIN
};

static OptionSelection optionsSelection = OPTION_MAIN;

void createCircularPanel() {
  if (menuPanel != nullptr) return;
  
  // Create circular panel (240x240 fills screen)
  menuPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(menuPanel, 240, 240);
  lv_obj_center(menuPanel);
  
  // Circular shape
  lv_obj_set_style_radius(menuPanel, LV_RADIUS_CIRCLE, 0);
  
  // Dark background
  lv_obj_set_style_bg_color(menuPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(menuPanel, LV_OPA_COVER, 0);
  
  // Neon border (gradient mint to pink)
  lv_obj_set_style_border_width(menuPanel, 12, 0);
  lv_obj_set_style_border_color(menuPanel, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_border_opa(menuPanel, LV_OPA_COVER, 0);
  
  // No padding
  lv_obj_set_style_pad_all(menuPanel, 0, 0);
  
  // Remove scrollbar
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_SCROLLABLE);
  
  // Start hidden
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
}

static uint8_t wrapIndex(int idx) {
  int v = idx % static_cast<int>(MENU_ITEM_COUNT);
  if (v < 0) v += MENU_ITEM_COUNT;
  return static_cast<uint8_t>(v);
}

static uint8_t stepTowardLinear(uint8_t current, int target) {
  if (target > static_cast<int>(current)) {
    return static_cast<uint8_t>(current + 1 >= MENU_ITEM_COUNT ? MENU_ITEM_COUNT - 1 : current + 1);
  } else if (target < static_cast<int>(current)) {
    return (current == 0) ? 0 : static_cast<uint8_t>(current - 1);
  }
  return current;
}

static uint8_t stepToward(uint8_t current, int target) {
  int n = static_cast<int>(MENU_ITEM_COUNT);
  int diff = target - static_cast<int>(current);
  if (diff > n / 2) diff -= n;
  if (diff < -n / 2) diff += n;
  if (diff > 1) diff = 1;
  if (diff < -1) diff = -1;
  return wrapIndex(static_cast<int>(current) + diff);
}

static void updateMenuItemStyles() {
  if (!menuList) return;
  // Center is 0 distance; compute styles by distance with wrap
  for (size_t i = 0; i < MENU_ITEM_COUNT; ++i) {
    int diff = static_cast<int>(i) - static_cast<int>(selectedItem);
    int adiff = abs(diff);
    int wrapDiff = static_cast<int>(MENU_ITEM_COUNT) - adiff;
    int dist = (wrapDiff < adiff) ? wrapDiff : adiff;
    lv_obj_t* item = menuItems[i];
    if (!item) continue;
    const lv_font_t* font = &lv_font_montserrat_vn_20;  // fallback smaller
    lv_opa_t opa = LV_OPA_50;
    if (dist == 0) { font = &lv_font_montserrat_vn_22; opa = LV_OPA_COVER; }
    else if (dist == 1) { font = &lv_font_montserrat_vn_20; opa = 200; }
    else if (dist == 2) { font = &lv_font_montserrat_vn_20; opa = LV_OPA_50; }
    else { font = &lv_font_montserrat_vn_20; opa = LV_OPA_40; }
    lv_obj_set_style_text_font(item, font, 0);
    lv_obj_set_style_text_opa(item, opa, 0);
    lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
  }
}

static void scrollMenuToIndex(uint8_t idx, lv_anim_enable_t anim) {
  if (!menuList || idx >= MENU_ITEM_COUNT) return;
  selectedItem = static_cast<MenuItem>(idx);
  updateMenuItemStyles();
  if (menuItems[idx]) {
    lv_obj_scroll_to_view(menuItems[idx], anim);
  }
}

static void menuListScrollCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_SCROLL_END) return;
  lv_obj_t* list = static_cast<lv_obj_t*>(lv_event_get_target(e));
  lv_area_t listCoords;
  lv_obj_get_coords(list, &listCoords);
  int16_t listMidY = (listCoords.y1 + listCoords.y2) / 2;
  int bestIdx = selectedItem;
  int bestDelta = 32000;
  for (size_t i = 0; i < MENU_ITEM_COUNT; ++i) {
    lv_obj_t* item = menuItems[i];
    if (!item) continue;
    lv_area_t c;
    lv_obj_get_coords(item, &c);
    int itemMid = (c.y1 + c.y2) / 2;
    int delta = abs(itemMid - listMidY);
    if (delta < bestDelta) {
      bestDelta = delta;
      bestIdx = static_cast<int>(i);
    }
  }
  uint8_t next = stepTowardLinear(static_cast<uint8_t>(selectedItem), bestIdx);
  if (next != static_cast<uint8_t>(selectedItem)) {
    scrollMenuToIndex(next, LV_ANIM_ON);
  }
}

void createMenuRoller() {
  if (menuList != nullptr) return;
  
  menuList = lv_obj_create(menuPanel);
  lv_obj_set_size(menuList, 200, 160);
  lv_obj_center(menuList);
  lv_obj_set_scroll_dir(menuList, LV_DIR_VER);
  lv_obj_set_scroll_snap_y(menuList, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(menuList, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(menuList, LV_OBJ_FLAG_SCROLL_MOMENTUM);  // limit fling to reduce multi-item jumps
  lv_obj_set_style_pad_all(menuList, 0, 0);
  lv_obj_set_style_pad_row(menuList, 6, 0);
  lv_obj_set_style_bg_opa(menuList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(menuList, 0, 0);
  lv_obj_set_flex_flow(menuList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(menuList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_add_event_cb(menuList, menuListScrollCb, LV_EVENT_SCROLL_END, nullptr);

  for (size_t i = 0; i < MENU_ITEM_COUNT; ++i) {
    lv_obj_t* label = lv_label_create(menuList);
    menuItems[i] = label;
    lv_label_set_text(label, menuItemLabelTexts[i]);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_pad_all(label, 8, 0);
    lv_obj_set_style_min_height(label, 28, 0);
  }
  
  scrollMenuToIndex(0, LV_ANIM_OFF);
}

void updateBorderGradient() {
  // TODO: Implement gradient border animation
  // For now using solid mint color
  // Future: animate between mint and pink based on position
}

static bool isPointInside(lv_obj_t* obj, uint16_t x, uint16_t y) {
  if (!obj) return false;
  lv_area_t area;
  lv_obj_get_coords(obj, &area);
  return (x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2);
}

static bool connectSwitchIsOn() {
  return connectSwitch && lv_obj_has_state(connectSwitch, LV_STATE_CHECKED);
}

static void setConnectSwitchState(bool on) {
  if (!connectSwitch) return;
  if (on) {
    lv_obj_add_state(connectSwitch, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(connectSwitch, LV_STATE_CHECKED);
  }
}

static void syncConnectSwitchState() {
  WifiState s = wifiGetState();
  bool on = (s == WifiState::PROVISIONING ||
             s == WifiState::CONNECTING ||
             s == WifiState::CONNECTED);
  setConnectSwitchState(on);

  if (otaActive) {
    uint32_t nowMs = millis();
    float phase = 0.0f;
    if (OTA_BREATH_PERIOD_MS > 0) {
      phase = static_cast<float>((nowMs - otaStartMs) % OTA_BREATH_PERIOD_MS) /
              static_cast<float>(OTA_BREATH_PERIOD_MS);
    }
    float sWave = sinf(phase * 2.0f * 3.14159265f);
    float blend = 0.5f + 0.5f * sWave;
    uint8_t baseR = static_cast<uint8_t>((COLOR_CONNECT_OK >> 16) & 0xFF);
    uint8_t baseG = static_cast<uint8_t>((COLOR_CONNECT_OK >> 8) & 0xFF);
    uint8_t baseB = static_cast<uint8_t>(COLOR_CONNECT_OK & 0xFF);
    uint8_t r = static_cast<uint8_t>(baseR + (255 - baseR) * blend);
    uint8_t g = static_cast<uint8_t>(baseG + (255 - baseG) * blend);
    uint8_t b = static_cast<uint8_t>(baseB + (255 - baseB) * blend);
    if (connectPanel) {
      lv_obj_set_style_bg_color(connectPanel, lv_color_hex(COLOR_BACKGROUND), 0);
      lv_obj_set_style_border_color(connectPanel, lv_color_make(r, g, b), 0);
    }
    return;
  }

  // Update connect panel border to indicate state
  uint32_t color = 0xF44336; // red default
  switch (s) {
    case WifiState::PROVISIONING: color = 0xFFC107; break; // yellow
    case WifiState::CONNECTING:   color = 0x2196F3; break; // blue
    case WifiState::CONNECTED:    color = COLOR_CONNECT_OK; break; // green
    case WifiState::FAILED:
    case WifiState::OFF:
    default: color = 0xF44336; break; // red
  }
  if (connectPanel) {
    lv_obj_set_style_bg_color(connectPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_border_color(connectPanel, lv_color_hex(color), 0);
  }
}

void createLevelPanel() {
  if (levelPanel != nullptr) return;

  levelPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(levelPanel, 240, 240);
  lv_obj_center(levelPanel);
  lv_obj_set_style_radius(levelPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(levelPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(levelPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(levelPanel, 12, 0);
  lv_obj_set_style_border_color(levelPanel, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_border_opa(levelPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(levelPanel, 0, 0);
  lv_obj_clear_flag(levelPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(levelPanel, LV_OBJ_FLAG_HIDDEN);

  levelArc = lv_arc_create(levelPanel);
  lv_obj_set_size(levelArc, 220, 220);
  lv_obj_center(levelArc);
  lv_arc_set_rotation(levelArc, 135);
  lv_arc_set_bg_angles(levelArc, 0, 270);
  lv_arc_set_mode(levelArc, LV_ARC_MODE_NORMAL);
  lv_obj_remove_style(levelArc, nullptr, LV_PART_KNOB);
  lv_obj_set_style_arc_width(levelArc, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_width(levelArc, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(levelArc, lv_color_hex(0x202020), LV_PART_MAIN);

  levelLabel = lv_label_create(levelPanel);
  lv_obj_set_style_text_color(levelLabel, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(levelLabel, &lv_font_montserrat_48, 0);
  lv_label_set_text(levelLabel, "1");
  lv_obj_center(levelLabel);
}

void updateLevelUI() {
  if (!levelPanel) return;

  int level = LevelSystem::getLevel();
  int current_xp = LevelSystem::getXP();
  int next_level_xp = LevelSystem::getXPForNextLevel();

  lv_arc_set_range(levelArc, 0, next_level_xp);
  lv_arc_set_value(levelArc, current_xp);

  lv_label_set_text_fmt(levelLabel, "%d", level);

  // Cycle through colors based on level
  const uint32_t level_colors[] = {0xFF7F50, 0x70C1FF, 0xFFD23F, 0x58F5C9, 0xDB1758, 0x9A3BFF};
  const int num_colors = sizeof(level_colors) / sizeof(level_colors[0]);
  uint32_t color = level_colors[(level - 1) % num_colors];
  
  lv_obj_set_style_arc_color(levelArc, lv_color_hex(color), LV_PART_INDICATOR);
  lv_obj_set_style_border_color(levelPanel, lv_color_hex(color), 0);
}

bool isLevelOpen() {
  return currentState == MENU_LEVEL_OPEN;
}

void showLevel() {
  currentState = MENU_LEVEL_OPEN;
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(levelPanel, LV_OBJ_FLAG_HIDDEN);
  updateLevelUI();
  MenuLog::println("[MenuSystem] Level screen opened (Layer 2)");
}

void closeLevelToMenu() {
  if (currentState != MENU_LEVEL_OPEN) return;
  lv_obj_add_flag(levelPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_OPEN;
  selectedItem = MENU_LEVEL;
  scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
  MenuLog::println("[MenuSystem] Level screen closed -> back to menu");
}

void createStatsPanel() {
  if (statsPanel != nullptr) return;

  statsPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(statsPanel, 240, 240);
  lv_obj_center(statsPanel);
  lv_obj_set_style_radius(statsPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(statsPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(statsPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(statsPanel, 12, 0);
  lv_obj_set_style_border_color(statsPanel, lv_color_hex(COLOR_PINK), 0);
  lv_obj_set_style_border_opa(statsPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(statsPanel, 0, 0);
  lv_obj_clear_flag(statsPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);

  statsTitle = lv_label_create(statsPanel);
  lv_obj_set_style_text_color(statsTitle, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(statsTitle, &lv_font_montserrat_vn_22, 0);
  lv_label_set_text(statsTitle, "Hunger");
  lv_obj_align(statsTitle, LV_ALIGN_TOP_MID, 0, 20);

  statsArc = lv_arc_create(statsPanel);
  lv_obj_set_size(statsArc, 180, 180);
  lv_obj_center(statsArc);
  lv_arc_set_rotation(statsArc, 135);
  lv_arc_set_bg_angles(statsArc, 0, 270);
  lv_arc_set_mode(statsArc, LV_ARC_MODE_NORMAL);
  lv_arc_set_range(statsArc, 0, 100);
  lv_obj_remove_style(statsArc, nullptr, LV_PART_KNOB);  // hide knob
  lv_obj_set_style_arc_width(statsArc, 16, LV_PART_MAIN);
  lv_obj_set_style_arc_width(statsArc, 16, LV_PART_INDICATOR);

  // Center stat name inside arc
  lv_obj_align(statsTitle, LV_ALIGN_CENTER, 0, 0);
}

void createOptionsPanel() {
  if (optionsPanel != nullptr) return;

  optionsPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(optionsPanel, 240, 240);
  lv_obj_center(optionsPanel);
  lv_obj_set_style_radius(optionsPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(optionsPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(optionsPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(optionsPanel, 12, 0);
  lv_obj_set_style_border_color(optionsPanel, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_border_opa(optionsPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(optionsPanel, 0, 0);
  lv_obj_clear_flag(optionsPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(optionsPanel, LV_OBJ_FLAG_HIDDEN);

  optionsTitle = lv_label_create(optionsPanel);
  lv_obj_set_style_text_color(optionsTitle, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(optionsTitle, &lv_font_montserrat_vn_22, 0);
  lv_label_set_text(optionsTitle, "Option");
  lv_obj_align(optionsTitle, LV_ALIGN_TOP_MID, 0, 30);

  optionsAction = lv_label_create(optionsPanel);
  lv_obj_set_style_text_color(optionsAction, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_text_font(optionsAction, &lv_font_montserrat_vn_22, 1);
  lv_label_set_text(optionsAction, "Action");
  lv_obj_align(optionsAction, LV_ALIGN_CENTER, 0, -5);
}

void createGamesPanel() {
  if (gamesPanel != nullptr) return;

  gamesPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(gamesPanel, 240, 240);
  lv_obj_center(gamesPanel);
  lv_obj_set_style_radius(gamesPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(gamesPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(gamesPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(gamesPanel, 12, 0);
  lv_obj_set_style_border_color(gamesPanel, lv_color_hex(COLOR_PINK), 0);
  lv_obj_set_style_border_opa(gamesPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(gamesPanel, 0, 0);
  lv_obj_clear_flag(gamesPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(gamesPanel, LV_OBJ_FLAG_HIDDEN);

  gamesTitle = lv_label_create(gamesPanel);
  lv_obj_set_style_text_color(gamesTitle, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(gamesTitle, &lv_font_montserrat_vn_22, 0);
  lv_label_set_text(gamesTitle, "Trò chơi");
  lv_obj_align(gamesTitle, LV_ALIGN_TOP_MID, 0, 24);

  gamesAction = lv_label_create(gamesPanel);
  lv_obj_set_style_text_color(gamesAction, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_text_font(gamesAction, &lv_font_montserrat_vn_22, 0);
  lv_label_set_text(gamesAction, "Chạm màu xanh");
  lv_obj_align(gamesAction, LV_ALIGN_CENTER, 0, -10);

  gamesStatus = lv_label_create(gamesPanel);
  lv_obj_set_style_text_color(gamesStatus, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(gamesStatus, &lv_font_montserrat_vn_20, 0);
  lv_label_set_text(gamesStatus, gameStatusMsg);
  lv_obj_align(gamesStatus, LV_ALIGN_CENTER, 0, 30);
}

void createBatteryPanel() {
  if (batteryPanel != nullptr) return;

  batteryPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(batteryPanel, 240, 240);
  lv_obj_center(batteryPanel);
  lv_obj_set_style_radius(batteryPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(batteryPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(batteryPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(batteryPanel, 12, 0);
  lv_obj_set_style_border_color(batteryPanel, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_border_opa(batteryPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(batteryPanel, 0, 0);
  lv_obj_clear_flag(batteryPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(batteryPanel, LV_OBJ_FLAG_HIDDEN);

  batteryTitle = lv_label_create(batteryPanel);
  lv_obj_set_style_text_color(batteryTitle, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(batteryTitle, &lv_font_montserrat_vn_22, 0);
  lv_label_set_text(batteryTitle, "Battery");
  lv_obj_align(batteryTitle, LV_ALIGN_TOP_MID, 0, 24);

  batteryValue = lv_label_create(batteryPanel);
  lv_obj_set_style_text_color(batteryValue, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(batteryValue, &lv_font_montserrat_vn_28, 0);
  lv_label_set_text(batteryValue, "--%");
  lv_obj_align(batteryValue, LV_ALIGN_CENTER, 0, 0);
}

void createConnectPanel() {
  if (connectPanel != nullptr) return;

  connectPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(connectPanel, 240, 240);
  lv_obj_center(connectPanel);
  lv_obj_set_style_radius(connectPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(connectPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(connectPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(connectPanel, 12, 0);
  lv_obj_set_style_border_color(connectPanel, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_border_opa(connectPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(connectPanel, 0, 0);
  lv_obj_clear_flag(connectPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);

  connectTitle = lv_label_create(connectPanel);
  lv_obj_set_style_text_color(connectTitle, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(connectTitle, &lv_font_montserrat_vn_22, 0);
  lv_label_set_text(connectTitle, "Connect");
  lv_obj_align(connectTitle, LV_ALIGN_TOP_MID, 0, 24);

  connectRow = lv_obj_create(connectPanel);
  lv_obj_set_size(connectRow, 180, 46);
  lv_obj_align(connectRow, LV_ALIGN_CENTER, 0, 10);
  lv_obj_set_style_bg_opa(connectRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(connectRow, 0, 0);
  lv_obj_set_style_pad_all(connectRow, 0, 0);
  lv_obj_set_style_pad_column(connectRow, 16, 0);
  lv_obj_clear_flag(connectRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(connectRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(connectRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  connectLabel = lv_label_create(connectRow);
  lv_obj_set_style_text_color(connectLabel, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(connectLabel, &lv_font_montserrat_vn_20, 0);
  lv_label_set_text(connectLabel, "Wifi");

  connectSwitch = lv_switch_create(connectRow);
  syncConnectSwitchState();

  connectOtaBtn = lv_btn_create(connectPanel);
  lv_obj_set_size(connectOtaBtn, 150, 48);
  lv_obj_align(connectOtaBtn, LV_ALIGN_CENTER, 0, 70);
  // LVGL “gummy” look: pill radius, soft gradient, shadow, outline
  lv_obj_set_style_radius(connectOtaBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(connectOtaBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(connectOtaBtn, lv_color_hex(0xFF6FA5), 0);
  lv_obj_set_style_bg_grad_dir(connectOtaBtn, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_grad_color(connectOtaBtn, lv_color_hex(0xFF3E7C), 0);
  lv_obj_set_style_shadow_width(connectOtaBtn, 16, 0);
  lv_obj_set_style_shadow_opa(connectOtaBtn, LV_OPA_50, 0);
  lv_obj_set_style_shadow_color(connectOtaBtn, lv_color_hex(0xC60F55), 0);
  lv_obj_set_style_outline_width(connectOtaBtn, 2, 0);
  lv_obj_set_style_outline_opa(connectOtaBtn, LV_OPA_40, 0);
  lv_obj_set_style_outline_color(connectOtaBtn, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(connectOtaBtn, 0, 0);
  lv_obj_set_style_pad_all(connectOtaBtn, 12, 0);
  // Pressed state: slightly darker, softer shadow
  lv_obj_set_style_bg_color(connectOtaBtn, lv_color_hex(0xFF4F8D), LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(connectOtaBtn, lv_color_hex(0xE73275), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_opa(connectOtaBtn, LV_OPA_30, LV_STATE_PRESSED);
  // Gummy transition styles
  static lv_style_prop_t gum_props[] = {LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT, LV_STYLE_TEXT_LETTER_SPACE, 0};
  static lv_style_transition_dsc_t gum_tr_def;
  static lv_style_transition_dsc_t gum_tr_pr;
  static lv_style_t gum_style_def;
  static lv_style_t gum_style_pr;
  static bool gum_init = false;
  if (!gum_init) {
    lv_style_transition_dsc_init(&gum_tr_def, gum_props, lv_anim_path_overshoot, 250, 100, nullptr);
    lv_style_transition_dsc_init(&gum_tr_pr, gum_props, lv_anim_path_ease_in_out, 250, 0, nullptr);
    lv_style_init(&gum_style_def);
    lv_style_set_transition(&gum_style_def, &gum_tr_def);
    lv_style_init(&gum_style_pr);
    lv_style_set_transform_width(&gum_style_pr, 10);
    lv_style_set_transform_height(&gum_style_pr, -10);
    lv_style_set_text_letter_space(&gum_style_pr, 10);
    lv_style_set_transition(&gum_style_pr, &gum_tr_pr);
    gum_init = true;
  }
  // Apply gummy styles to main part with proper state masks
  lv_obj_add_style(connectOtaBtn, &gum_style_pr, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_style(connectOtaBtn, &gum_style_def, LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_obj_t* otaLabel = lv_label_create(connectOtaBtn);
  lv_label_set_text(otaLabel, "CẬP NHẬT");
  lv_obj_set_style_text_color(otaLabel, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_center(otaLabel);
}

void updateStatsUI() {
  if (!statsPanel) return;
  lv_label_set_text(statsTitle, statNames[statIndex]);

  int value = 0;
  switch (statIndex) {
    case 0: value = CareSystem::getHunger(); break;
    case 1: value = CareSystem::getMood(); break;
    case 2: value = CareSystem::getEnergy(); break;
    case 3: value = CareSystem::getCleanliness(); break;
  }

  if (value < 0) value = 0;
  if (value > 100) value = 100;

  lv_arc_set_value(statsArc, value);
  lv_obj_set_style_arc_color(statsArc, lv_color_hex(statColors[statIndex]), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(statsArc, lv_color_hex(0x202020), LV_PART_MAIN);
}

void updateOptionsUI() {
  if (!optionsPanel) return;
  lv_label_set_text(optionsTitle, statNames[statIndex]);
  lv_label_set_text(optionsAction, statOptionNames[statIndex]);

  // Highlight selection
  if (optionsSelection == OPTION_MAIN) {
    lv_obj_set_style_text_color(optionsAction, lv_color_hex(COLOR_MINT), 0);
  } else {
    lv_obj_set_style_text_color(optionsAction, lv_color_hex(COLOR_TEXT), 0);
  }
}

void updateGamesUI() {
  if (!gamesPanel) return;
  lv_label_set_text(gamesStatus, gameStatusMsg);
}

static void startFeedAnim() {
  feedAnimEndMs = millis() + FEED_ANIM_DURATION_MS;
  currentState = MENU_FEEDING;
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  MenuLog::println("[MenuSystem] Feed animation started");
}

}  // anonymous namespace

namespace MenuSystem {

void begin() {
  MenuLog::println("[MenuSystem] Initializing LVGL menu...");
  createCircularPanel();
  createMenuRoller();
  createStatsPanel();
  createOptionsPanel();
  createGamesPanel();
  createConnectPanel();
  MessageSystem::begin();
  createBatteryPanel();
  createLevelPanel();
  syncConnectSwitchState();
  MenuLog::println("[MenuSystem] Ready!");
}

void open() {
  if (currentState == MENU_OPEN) return;
  
  currentState = MENU_OPEN;
  selectedItem = MENU_FEED;
  MessageSystem::close();
  
  // Show panel with fade-in animation
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  
  // Hide other panels if they were visible
  lv_obj_add_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(levelPanel, LV_OBJ_FLAG_HIDDEN);
  
  // Reset list to first item
  scrollMenuToIndex(0, LV_ANIM_OFF);
  
  MenuLog::println("[MenuSystem] Menu opened (Layer 1)");
}

void close() {
  if (currentState == MENU_CLOSED) return;
  
  currentState = MENU_CLOSED;
  feedAnimEndMs = 0;
  
  // Hide all panels
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(optionsPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(gamesPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(batteryPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(levelPanel, LV_OBJ_FLAG_HIDDEN);
  MessageSystem::close();
  
  MenuLog::println("[MenuSystem] Menu closed (back to Layer 0)");
}

bool isOpen() {
  return currentState == MENU_OPEN;
}

bool isFeeding() {
  return currentState == MENU_FEEDING;
}

bool isBatteryOpen() {
  return currentState == MENU_BATTERY_OPEN;
}

bool isLevelOpen() {
  return currentState == MENU_LEVEL_OPEN;
}

bool isConnectOpen() {
  return currentState == MENU_CONNECT_OPEN;
}

bool isMessageOpen() {
  return currentState == MENU_MESSAGE_OPEN;
}

static void showConnect() {
  currentState = MENU_CONNECT_OPEN;
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);
  syncConnectSwitchState();
  MenuLog::println("[MenuSystem] Connect opened (Layer 2)");
}

static void showMessage() {
  currentState = MENU_MESSAGE_OPEN;
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  MessageSystem::open(MenuSystem::closeMessageToMenu);
  MenuLog::println("[MenuSystem] Message opened (Layer 2)");
}

void closeConnectToMenu() {
  if (currentState != MENU_CONNECT_OPEN) return;
  lv_obj_add_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_OPEN;
  selectedItem = MENU_CONNECT;
  scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
  MenuLog::println("[MenuSystem] Connect closed -> back to menu");
}

void closeMessageToMenu() {
  if (currentState != MENU_MESSAGE_OPEN) return;
  MessageSystem::close();
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_OPEN;
  selectedItem = MENU_MESSAGE;
  scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
  MenuLog::println("[MenuSystem] Message closed -> back to menu");
}

bool handleConnectTap(uint16_t x, uint16_t y) {
  if (currentState != MENU_CONNECT_OPEN) return false;
  if (isPointInside(connectOtaBtn, x, y)) {
    if (wifiGetState() == WifiState::CONNECTED) {
      MenuLog::println("[MenuSystem] OTA triggered from Connect");
      BubuOTA::runManual();
    } else {
      MenuLog::println("[MenuSystem] OTA blocked: Wi-Fi not connected");
    }
    return true;
  }
  if (isPointInside(connectSwitch, x, y)) {
    bool enable = !connectSwitchIsOn();
    setConnectSwitchState(enable);
    if (enable) {
      wifiStart(false);
    } else {
      wifiStop();
    }
    return true;
  }
  return false;
}

void selectNext() {
  if (currentState != MENU_OPEN) return;
  
  int current = static_cast<int>(selectedItem);
  if (current < MENU_ITEM_COUNT - 1) {
    uint8_t next = static_cast<uint8_t>(current + 1);
    scrollMenuToIndex(next, LV_ANIM_ON);
    MenuLog::printf("[MenuSystem] Selected: %d\n", selectedItem);
  }
}

void selectPrev() {
  if (currentState != MENU_OPEN) return;
  
  int current = static_cast<int>(selectedItem);
  if (current > 0) {
    uint8_t prev = static_cast<uint8_t>(current - 1);
    scrollMenuToIndex(prev, LV_ANIM_ON);
    MenuLog::printf("[MenuSystem] Selected: %d\n", selectedItem);
  }
}

MenuItem getSelected() {
  return selectedItem;
}

size_t getCurrentStatIndex() {
  return statIndex;
}

bool isStatsOpen() {
  return currentState == MENU_STATS_OPEN;
}

void showStats() {
  currentState = MENU_STATS_OPEN;
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  updateStatsUI();
  MenuLog::println("[MenuSystem] Stats opened (Layer 2)");
}

void closeStatsToMenu() {
  if (currentState != MENU_STATS_OPEN) return;
  lv_obj_add_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_OPEN;
  selectedItem = MENU_STATS;
  MenuLog::println("[MenuSystem] Stats closed -> back to menu");
}

void closeBatteryToMenu() {
  if (currentState != MENU_BATTERY_OPEN) return;
  lv_obj_add_flag(batteryPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_OPEN;
  selectedItem = MENU_BATTERY;
  scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
  MenuLog::println("[MenuSystem] Battery closed -> back to menu");
}

void closeLevelToMenu() {
  if (currentState != MENU_LEVEL_OPEN) return;
  lv_obj_add_flag(levelPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_OPEN;
  selectedItem = MENU_LEVEL;
  scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
  MenuLog::println("[MenuSystem] Level screen closed -> back to menu");
}

void statsNext() {
  if (currentState != MENU_STATS_OPEN) return;
  statIndex = (statIndex + 1) % STAT_COUNT;
  updateStatsUI();
}

void statsPrev() {
  if (currentState != MENU_STATS_OPEN) return;
  statIndex = (statIndex + STAT_COUNT - 1) % STAT_COUNT;
  updateStatsUI();
}

bool isOptionsOpen() {
  return currentState == MENU_OPTIONS_OPEN;
}

bool isGamesOpen() {
  return currentState == MENU_GAMES_OPEN;
}

bool isGameActive() {
  return currentState == MENU_GAME_ACTIVE;
}

void openOptionsForCurrentStat() {
  if (currentState != MENU_STATS_OPEN) return;
  currentState = MENU_OPTIONS_OPEN;
  optionsSelection = OPTION_MAIN;
  lv_obj_add_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(optionsPanel, LV_OBJ_FLAG_HIDDEN);
  updateOptionsUI();
  MenuLog::printf("[MenuSystem] Options opened for %s (Layer 3)\n", statNames[statIndex]);
}

void closeOptionsToStats() {
  if (currentState != MENU_OPTIONS_OPEN) return;
  lv_obj_add_flag(optionsPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_STATS_OPEN;
  updateStatsUI();
  MenuLog::println("[MenuSystem] Options closed -> back to stats");
}

void openGamesMenu() {
  bool fromStats = (currentState == MENU_STATS_OPEN);
  bool fromMenu = (currentState == MENU_OPEN);
  if (!fromStats && !fromMenu) return;
  if (fromStats && statIndex != 1) return;  // Stats path only when Mood

  gamesOpenedFromMenu = fromMenu;
  currentState = MENU_GAMES_OPEN;

  if (fromStats) {
    lv_obj_add_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  } else if (fromMenu) {
    lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_clear_flag(gamesPanel, LV_OBJ_FLAG_HIDDEN);
  updateGamesUI();
  MenuLog::println(fromMenu ? "[MenuSystem] Games menu opened from Play (Layer 4)" : "[MenuSystem] Games menu opened (Layer 4)");
}

void closeGamesToStats() {
  if (currentState != MENU_GAMES_OPEN) return;
  lv_obj_add_flag(gamesPanel, LV_OBJ_FLAG_HIDDEN);
  if (gamesOpenedFromMenu) {
    lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
    currentState = MENU_OPEN;
    selectedItem = MENU_PLAY;
    scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
    MenuLog::println("[MenuSystem] Games closed -> back to menu");
  } else {
    lv_obj_clear_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
    currentState = MENU_STATS_OPEN;
    updateStatsUI();
    MenuLog::println("[MenuSystem] Games closed -> back to stats");
  }
}

void startTapTheGreens() {
  if (currentState != MENU_GAMES_OPEN) return;
  lv_obj_add_flag(gamesPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_GAME_ACTIVE;
  strncpy(gameStatusMsg, "Playing...", sizeof(gameStatusMsg) - 1);
  EyeGame::start(CareSystem::STAT_MOOD);
  MenuLog::println("[MenuSystem] Starting Tap the Greens (Layer 5)");
}

void handleGameFinished() {
  if (currentState != MENU_GAME_ACTIVE && currentState != MENU_GAMES_OPEN) return;
  EyeGame::GameResult res = EyeGame::getLastResult();
  uint8_t score = EyeGame::getScore();
  int reward = static_cast<int>(score) * static_cast<int>(EyeGame::getRewardPerHit());

  switch (res) {
    case EyeGame::GAME_FINISH_NORMAL:
      snprintf(gameStatusMsg, sizeof(gameStatusMsg), "Giỏi quá! %u (+%d Tâm trạng)", score, reward);
      break;
    case EyeGame::GAME_FINISH_WRONG_TAP:
      snprintf(gameStatusMsg, sizeof(gameStatusMsg), "SAI RỒI! %+d Tâm trạng, -5 Năng lượng", reward - 10);
      break;
    case EyeGame::GAME_NONE:
    default:
      snprintf(gameStatusMsg, sizeof(gameStatusMsg), "Stopped");
      break;
  }

  currentState = MENU_GAMES_OPEN;
  lv_obj_clear_flag(gamesPanel, LV_OBJ_FLAG_HIDDEN);
  updateGamesUI();
  MenuLog::println("[MenuSystem] Game finished -> back to games menu");
}

void otaSetActive(bool active) {
  otaActive = active;
  if (active) {
    otaStartMs = millis();
    otaLastTickMs = otaStartMs;
    syncConnectSwitchState();
  } else {
    otaStartMs = 0;
    otaLastTickMs = 0;
    syncConnectSwitchState();
  }
}

void otaPulse(uint32_t nowMs) {
  if (!otaActive) return;
  if (currentState != MENU_CONNECT_OPEN) return;
  if (!connectPanel) return;

  if (otaLastTickMs == 0 || nowMs < otaLastTickMs) {
    otaLastTickMs = nowMs;
  } else {
    uint32_t dt = nowMs - otaLastTickMs;
    if (dt > 0) {
      lv_tick_inc(dt);
      otaLastTickMs = nowMs;
    }
  }

  float phase = 0.0f;
  if (OTA_BREATH_PERIOD_MS > 0) {
    phase = static_cast<float>((nowMs - otaStartMs) % OTA_BREATH_PERIOD_MS) /
            static_cast<float>(OTA_BREATH_PERIOD_MS);
  }
  float sWave = sinf(phase * 2.0f * 3.14159265f);
  float blend = 0.5f + 0.5f * sWave;
  uint8_t baseR = static_cast<uint8_t>((COLOR_CONNECT_OK >> 16) & 0xFF);
  uint8_t baseG = static_cast<uint8_t>((COLOR_CONNECT_OK >> 8) & 0xFF);
  uint8_t baseB = static_cast<uint8_t>(COLOR_CONNECT_OK & 0xFF);
  uint8_t r = static_cast<uint8_t>(baseR + (255 - baseR) * blend);
  uint8_t g = static_cast<uint8_t>(baseG + (255 - baseG) * blend);
  uint8_t b = static_cast<uint8_t>(baseB + (255 - baseB) * blend);
  lv_obj_set_style_bg_color(connectPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_border_color(connectPanel, lv_color_make(r, g, b), 0);
  lv_timer_handler();
}

void activateCurrentOption() {
  if (currentState != MENU_OPTIONS_OPEN) return;
  if (optionsSelection == OPTION_MAIN) {
    MenuLog::printf("[MenuSystem] Activate option: %s (%s)\n", statNames[statIndex], statOptionNames[statIndex]);
    // Simple stat boosts per option
    switch (statIndex) {
      case 0: CareSystem::addHunger(CareSystem::kSandwichBoost); break;        // Sandwich
      case 1: CareSystem::addMood(CareSystem::kGamesBoost); break;             // Games
      case 2: CareSystem::addEnergy(CareSystem::kSleepBoost); break;           // Sleep
      case 3: CareSystem::addCleanliness(CareSystem::kBathBoost); break;       // Bath
    }
    updateStatsUI();
    closeOptionsToStats();
  } else {
    updateOptionsUI();
  }
}

void selectOptionsPrev() {
  if (currentState != MENU_OPTIONS_OPEN) return;
  optionsSelection = OPTION_MAIN;
  updateOptionsUI();
}

void selectOptionsNext() {
  if (currentState != MENU_OPTIONS_OPEN) return;
  optionsSelection = OPTION_MAIN;
  updateOptionsUI();
}

void activateSelected() {
  if (currentState != MENU_OPEN) return;
  
  MenuLog::printf("[MenuSystem] Activated: %s\n", menuItemLabelTexts[selectedItem]);
  
  switch (selectedItem) {
    case MENU_FEED:
      startFeedAnim();
      break;
    case MENU_PLAY:
      openGamesMenu();
      startTapTheGreens();
      break;
    case MENU_CLEAN:
      // TODO: Clean animation/action
      break;
    case MENU_SLEEP:
      DisplaySystem_startSleep();
      close();
      break;
    case MENU_CONNECT:
      showConnect();
      break;
    case MENU_MESSAGE:
      showMessage();
      break;
    case MENU_BATTERY:
      currentState = MENU_BATTERY_OPEN;
      lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(batteryPanel, LV_OBJ_FLAG_HIDDEN);
      break;
    case MENU_STATS:
      showStats();
      break;
    case MENU_LEVEL:
      showLevel();
      break;
  }
}

void render() {
  if (currentState == MENU_STATS_OPEN) {
    updateStatsUI();
  } else if (currentState == MENU_LEVEL_OPEN) {
    updateLevelUI();
  } else if (currentState == MENU_OPTIONS_OPEN) {
    updateOptionsUI();
  } else if (currentState == MENU_GAMES_OPEN) {
    updateGamesUI();
  } else if (currentState == MENU_FEEDING) {
    if (feedAnimEndMs != 0 && millis() >= feedAnimEndMs) {
      feedAnimEndMs = 0;
      CareSystem::addHunger(CareSystem::kSandwichBoost); // Apply feed after anim
      lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
      currentState = MENU_OPEN;
      scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
      MenuLog::println("[MenuSystem] Feed animation ended -> back to menu");
    }
  } else if (currentState == MENU_CONNECT_OPEN) {
    syncConnectSwitchState();
  } else if (currentState == MENU_BATTERY_OPEN) {
    BatteryStatus bs = BatterySystem::getStatus();
    char buf[16];
    if (bs.percent <= 100) {
      snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(bs.percent));
    } else {
      snprintf(buf, sizeof(buf), "--%%");
    }
    lv_label_set_text(batteryValue, buf);
  }
}

bool isTapOnSelected(uint16_t x, uint16_t y) {
  if (!MenuSystem::isOpen()) return false;
  lv_obj_t* sel = menuItems[selectedItem];
  return isPointInside(sel, x, y);
}

bool isTapOnStatsTitle(uint16_t x, uint16_t y) {
  if (currentState != MENU_STATS_OPEN) return false;
  return isPointInside(statsTitle, x, y);
}

}  // namespace MenuSystem
