/**
 * @file setting_ui.cpp
 * @brief Setting Mode UI — DateTime editor + Speaker toggle
 * @date 2026-07-04
 */

#include "setting.h"
#include "../factory_test/factory_test.h"
#include "../common/ui_theme.h"   // themed palette
#include "../sleep_week/sleep_week_ui.h"
#include "../display_mode/display_mode.h"   // Brightness/Power Save/Theme/Rotation values + uiThemeName()
#include <smooth_ui_toolkit.h>
#include <time.h>

extern FactoryTest* _ft;

// ============ UI CONSTANTS ============
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 135

#define COLOR_BG        UI_BG
#define COLOR_TEXT      UI_FG
#define COLOR_GREEN     0x07E0
#define COLOR_RED       0xF800
#define COLOR_YELLOW    0xFFE0
#define COLOR_ORANGE    0xFD20
#define COLOR_BORDER    UI_BORDER
#define COLOR_HIGHLIGHT UI_AL

#define ITEM_HEIGHT   20
#define ITEM_Y_START  27
#define ITEM_INDENT   10

// ============ BLINK ============
static unsigned long settingBlinkTimer = 0;
static bool          settingBlinkState = false;

static void updateSettingBlink() {
    if (millis() - settingBlinkTimer > 500) {
        settingBlinkState  = !settingBlinkState;
        settingBlinkTimer  = millis();
    }
}

// ============ WEEKDAY NAMES ============
static const char* WDAY_SHORT[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

// ============ GEOPIX LOGO ============
// Play-button style (right-pointing triangle in dark green rounded box)
static void drawGeopixLogo(int x, int y) {
    // Dark green background
    _ft->_canvas->fillRoundRect(x, y, 28, 28, 6, 0x0300);
    // Bright lime-green right-pointing triangle
    _ft->_canvas->fillTriangle(x + 6, y + 5, x + 6, y + 23, x + 23, y + 14, 0x87E0);
}

// ============ INFO SCREEN ============
static void renderInfoScreen() {
    _ft->_canvas->fillScreen(COLOR_BG);

    // ── Logo ──
    drawGeopixLogo(12, 10);

    // ── Company name ──
    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(0x87E0);   // lime green
    _ft->_canvas->drawString("Geopix Digital", 47, 10);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("Solutions", 47, 28);

    // ── Divider ──
    _ft->_canvas->drawFastHLine(8, 48, 224, COLOR_BORDER);

    // ── Website & version ──
    _ft->_canvas->setFont(&fonts::efontCN_12);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("Website:", 12, 55);
    _ft->_canvas->setTextColor(0x87E0);
    _ft->_canvas->drawString("www.geopix.tw", 70, 55);

    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("Version:", 12, 70);
    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->drawString("v1.3", 70, 70);

    // ── Copyright ──
    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextColor(COLOR_BORDER);
    _ft->_canvas->drawString("(C) 2025 Geopix Digital Solutions", 12, 88);
    _ft->_canvas->drawString("All Rights Reserved.", 12, 100);

    // ── Return hint ──
    _ft->_canvas->setTextDatum(bottom_center);
    _ft->_canvas->setTextColor(COLOR_BORDER);
    _ft->_canvas->drawString("< Press to return >", 120, 133);
    _ft->_canvas->setTextDatum(top_left);
}

// ============ FORWARD DECLARATIONS ============
void renderSettingHeader();
void renderSettingMainItems();
void renderSettingDateTimeItems();
void renderSettingItem(uint8_t index, int visualRow, const char* label,
                       const char* valueStr, uint16_t valColor);

// ============ MAIN RENDER ============
void renderSettingUI() {
    if (!_ft || !_ft->_canvas) return;

    updateSettingBlink();
    _ft->_canvas->setTextWrap(false);
    _ft->_canvas->fillScreen(COLOR_BG);

    if (setting.editMode.state == SettingEditMode::SHOWING_INFO) {
        renderInfoScreen();
    } else {
        renderSettingHeader();
        if (setting.editMode.screen == SettingEditMode::DATETIME) {
            renderSettingDateTimeItems();
        } else {
            renderSettingMainItems();
        }
    }

    if (schedulerPopupActive()) schedulerPopupDraw();

    _ft->_canvas_update();
}

// ============ HEADER ============
void renderSettingHeader() {
    // Title
    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_center);
    if (setting.editMode.screen == SettingEditMode::DATETIME) {
        _ft->_canvas->setTextColor(COLOR_GREEN);
        _ft->_canvas->drawString("DATE & TIME", SCREEN_WIDTH / 2, 2);
    } else {
        _ft->_canvas->setTextColor(COLOR_ORANGE);
        _ft->_canvas->drawString("SETTING", SCREEN_WIDTH / 2, 2);
    }

    // Back arrow
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("<", 5, 2);

    // Hint: long-press to save
    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(COLOR_BORDER);
    _ft->_canvas->drawString("HOLD=SAVE", 235, 5);
}

// ============ MAIN ITEMS PANEL (7 rows, scrolling window) ============
// 7 items don't fit on a 240x135 screen at once, so this windows to 4
// visible rows at a time and scrolls to keep the selection visible — same
// "firstVisible" pattern already used by the OTA update WiFi/release lists
// (factory_test_ota_update.cpp), not a new UI idea.
#define SETTING_MAIN_ROW_COUNT   7
#define SETTING_MAIN_VISIBLE_ROWS 4

void renderSettingMainItems() {
    _ft->_canvas->drawRoundRect(8, 22, 224, 88, 5, COLOR_BORDER);
    _ft->_canvas->setFont(&fonts::efontCN_16);

    int sel = setting.editMode.selectedIndex;
    int firstVisible = 0;
    if (sel >= SETTING_MAIN_VISIBLE_ROWS) firstVisible = sel - SETTING_MAIN_VISIBLE_ROWS + 1;
    if (firstVisible > SETTING_MAIN_ROW_COUNT - SETTING_MAIN_VISIBLE_ROWS)
        firstVisible = SETTING_MAIN_ROW_COUNT - SETTING_MAIN_VISIBLE_ROWS;
    if (firstVisible < 0) firstVisible = 0;

    char valBuf[24];

    // ── Date & Time ── (index 0)
    if (0 >= firstVisible && 0 < firstVisible + SETTING_MAIN_VISIBLE_ROWS) {
        snprintf(valBuf, sizeof(valBuf), ">");
        renderSettingItem(0, 0 - firstVisible, "Date & Time", valBuf, COLOR_GREEN);
    }

    // ── Speaker ── (index 1)
    if (1 >= firstVisible && 1 < firstVisible + SETTING_MAIN_VISIBLE_ROWS) {
        uint16_t spkColor = setting.config.speakerEnabled ? COLOR_GREEN : COLOR_RED;
        snprintf(valBuf, sizeof(valBuf), "%s", setting.config.speakerEnabled ? "ON" : "OFF");
        renderSettingItem(1, 1 - firstVisible, "Speaker", valBuf, spkColor);
    }

    // ── Brightness ── (index 2) — same value/format as the old DISPLAY mode
    if (2 >= firstVisible && 2 < firstVisible + SETTING_MAIN_VISIBLE_ROWS) {
        int pct = (displayMode.config.brightness * 100) / 255;
        snprintf(valBuf, sizeof(valBuf), "%d%%", pct);
        renderSettingItem(2, 2 - firstVisible, "Brightness", valBuf, COLOR_GREEN);
    }

    // ── Power Save ── (index 3)
    if (3 >= firstVisible && 3 < firstVisible + SETTING_MAIN_VISIBLE_ROWS) {
        if (displayMode.config.dimmerSec == 0) snprintf(valBuf, sizeof(valBuf), "OFF");
        else snprintf(valBuf, sizeof(valBuf), "%ds", displayMode.config.dimmerSec);
        renderSettingItem(3, 3 - firstVisible, "Power Save", valBuf, COLOR_GREEN);
    }

    // ── Theme ── (index 4)
    if (4 >= firstVisible && 4 < firstVisible + SETTING_MAIN_VISIBLE_ROWS) {
        snprintf(valBuf, sizeof(valBuf), "%s", uiThemeName(displayMode.config.themeIndex));
        renderSettingItem(4, 4 - firstVisible, "Theme", valBuf, COLOR_GREEN);
    }

    // ── Rotation ── (index 5)
    if (5 >= firstVisible && 5 < firstVisible + SETTING_MAIN_VISIBLE_ROWS) {
        snprintf(valBuf, sizeof(valBuf), "%s", displayMode.config.rotation == 3 ? "NORMAL" : "FLIP");
        renderSettingItem(5, 5 - firstVisible, "Rotation", valBuf, COLOR_GREEN);
    }

    // ── Info ── (index 6)
    if (6 >= firstVisible && 6 < firstVisible + SETTING_MAIN_VISIBLE_ROWS) {
        renderSettingItem(6, 6 - firstVisible, "Info", ">", COLOR_BORDER);
    }

    // Scroll hints — small arrows at the panel's top/bottom edge when more
    // rows exist off-screen in that direction.
    if (firstVisible > 0) {
        _ft->_canvas->setFont(&fonts::efontCN_10);
        _ft->_canvas->setTextDatum(top_center);
        _ft->_canvas->setTextColor(COLOR_BORDER);
        _ft->_canvas->drawString("^", 220, 23);
    }
    if (firstVisible + SETTING_MAIN_VISIBLE_ROWS < SETTING_MAIN_ROW_COUNT) {
        _ft->_canvas->setFont(&fonts::efontCN_10);
        _ft->_canvas->setTextDatum(top_center);
        _ft->_canvas->setTextColor(COLOR_BORDER);
        _ft->_canvas->drawString("v", 220, 100);
    }
    _ft->_canvas->setTextDatum(top_left);
}

// ============ DATETIME ITEMS PANEL ============
void renderSettingDateTimeItems() {
    _ft->_canvas->drawRoundRect(8, 22, 224, 108, 5, COLOR_BORDER);
    _ft->_canvas->setFont(&fonts::efontCN_16);

    char valBuf[24];

    // ── Year ──
    snprintf(valBuf, sizeof(valBuf), "%04d", setting.dateTime.year);
    renderSettingItem(0, 0, "Year", valBuf, COLOR_GREEN);

    // ── Month ──
    snprintf(valBuf, sizeof(valBuf), "%02d", setting.dateTime.month);
    renderSettingItem(1, 1, "Month", valBuf, COLOR_GREEN);

    // ── Day (compute weekday on-the-fly for display) ──
    {
        struct tm ti = {};
        ti.tm_year = setting.dateTime.year - 1900;
        ti.tm_mon  = setting.dateTime.month - 1;
        ti.tm_mday = setting.dateTime.day;
        ti.tm_isdst = -1;
        mktime(&ti);
        snprintf(valBuf, sizeof(valBuf), "%02d (%s)",
                 setting.dateTime.day,
                 ti.tm_wday >= 0 && ti.tm_wday <= 6 ? WDAY_SHORT[ti.tm_wday] : "?");
    }
    renderSettingItem(2, 2, "Day", valBuf, COLOR_GREEN);

    // ── Hour ──
    snprintf(valBuf, sizeof(valBuf), "%02d", setting.dateTime.hour);
    renderSettingItem(3, 3, "Hour", valBuf, COLOR_GREEN);

    // ── Minute ──
    snprintf(valBuf, sizeof(valBuf), "%02d", setting.dateTime.minute);
    renderSettingItem(4, 4, "Minute", valBuf, COLOR_GREEN);
}

// `index` drives selection/highlight logic; `visualRow` is the on-screen
// slot it's drawn in (0-based from the top of the panel) — they differ only
// on the scrolling MAIN screen, where a row can be selected while sitting
// in visual slot 0-3 regardless of its absolute index.
void renderSettingItem(uint8_t index, int visualRow, const char* label,
                       const char* valueStr, uint16_t valColor) {
    int y = ITEM_Y_START + (visualRow * ITEM_HEIGHT);
    bool isSel  = (setting.editMode.selectedIndex == index);
    bool isEdit = (setting.editMode.state == SettingEditMode::EDITING && isSel);

    // Background highlight (Auto Shoot style)
    if (isSel) {
        _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3,
            isEdit ? COLOR_HIGHLIGHT : COLOR_BORDER);
    }

    // Label
    uint16_t labelColor = isSel ? COLOR_BG : COLOR_TEXT;
    _ft->_canvas->setTextColor(labelColor);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->drawString(label, ITEM_INDENT + 6, y + 1);

    // Value
    _ft->_canvas->setTextDatum(top_right);
    if (isEdit && settingBlinkState) {
        _ft->_canvas->setTextColor(COLOR_BG);         // blink off
    } else if (isEdit) {
        _ft->_canvas->setTextColor(COLOR_YELLOW);     // blink on (yellow = editing)
    } else {
        _ft->_canvas->setTextColor(isSel ? COLOR_BG : valColor);
    }
    _ft->_canvas->drawString(valueStr, 210, y + 1);

    // Arrow indicator
    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->drawString(">", 225, y + 1);

    _ft->_canvas->setTextDatum(top_left);
}

// ============ INIT ============
void initSettingUI() {
    setting.editMode.state = SettingEditMode::SELECTING;
    setting.editMode.selectedIndex = 0;
}
