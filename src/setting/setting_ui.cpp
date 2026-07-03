/**
 * @file setting_ui.cpp
 * @brief Setting Mode UI — DateTime editor + Speaker toggle
 * @date 2026-07-04
 */

#include "setting.h"
#include "../factory_test/factory_test.h"
#include <smooth_ui_toolkit.h>
#include <time.h>

extern FactoryTest* _ft;

// ============ UI CONSTANTS ============
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 135

#define COLOR_BG        0x0000
#define COLOR_TEXT      0xFFFF
#define COLOR_GREEN     0x07E0
#define COLOR_RED       0xF800
#define COLOR_YELLOW    0xFFE0
#define COLOR_ORANGE    0xFD20
#define COLOR_BORDER    0x4208
#define COLOR_HIGHLIGHT 0x27E0

#define ITEM_HEIGHT   18
#define ITEM_Y_START  22
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

// ============ FORWARD DECLARATIONS ============
void renderSettingHeader();
void renderSettingItems();
void renderSettingItem(uint8_t index, const char* label, const char* valueStr,
                       uint16_t valColor);

// ============ MAIN RENDER ============
void renderSettingUI() {
    if (!_ft || !_ft->_canvas) return;

    updateSettingBlink();

    _ft->_canvas->setTextWrap(false);
    _ft->_canvas->fillScreen(COLOR_BG);

    renderSettingHeader();
    renderSettingItems();

    _ft->_canvas_update();
}

// ============ HEADER ============
void renderSettingHeader() {
    // Title
    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(COLOR_ORANGE);
    _ft->_canvas->drawString("SETTING", SCREEN_WIDTH / 2, 2);

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

// ============ ITEMS PANEL ============
void renderSettingItems() {
    _ft->_canvas->drawRoundRect(8, 18, 224, 116, 5, COLOR_BORDER);
    _ft->_canvas->setFont(&fonts::efontCN_12);

    char valBuf[24];

    // ── Year ──
    snprintf(valBuf, sizeof(valBuf), "%04d", setting.dateTime.year);
    renderSettingItem(0, "Year", valBuf, COLOR_GREEN);

    // ── Month ──
    snprintf(valBuf, sizeof(valBuf), "%02d", setting.dateTime.month);
    renderSettingItem(1, "Month", valBuf, COLOR_GREEN);

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
    renderSettingItem(2, "Day", valBuf, COLOR_GREEN);

    // ── Hour ──
    snprintf(valBuf, sizeof(valBuf), "%02d", setting.dateTime.hour);
    renderSettingItem(3, "Hour", valBuf, COLOR_GREEN);

    // ── Minute ──
    snprintf(valBuf, sizeof(valBuf), "%02d", setting.dateTime.minute);
    renderSettingItem(4, "Minute", valBuf, COLOR_GREEN);

    // ── Speaker ──
    uint16_t spkColor = setting.config.speakerEnabled ? COLOR_GREEN : COLOR_RED;
    snprintf(valBuf, sizeof(valBuf), "%s",
             setting.config.speakerEnabled ? "ON" : "OFF");
    renderSettingItem(5, "Speaker", valBuf, spkColor);
}

void renderSettingItem(uint8_t index, const char* label, const char* valueStr,
                       uint16_t valColor) {
    int y = ITEM_Y_START + (index * ITEM_HEIGHT);
    bool isSel  = (setting.editMode.selectedIndex == index);
    bool isEdit = (setting.editMode.state == SettingEditMode::EDITING && isSel);

    // Background highlight
    if (isSel) {
        _ft->_canvas->fillRoundRect(10, y - 1, 220, 14, 3,
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
    _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_BORDER);
    _ft->_canvas->drawString(">", 225, y + 1);

    _ft->_canvas->setTextDatum(top_left);
}

// ============ INIT ============
void initSettingUI() {
    setting.editMode.state = SettingEditMode::SELECTING;
    setting.editMode.selectedIndex = 0;
}
