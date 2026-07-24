/**
 * @file auto_shoot_ui.cpp
 * @brief Auto Shoot UI rendering with LovyanGFX (FIXED)
 */

#include "auto_shoot.h"
#include "../factory_test/factory_test.h"
#include "../common/ui_theme.h"   // themed palette
#include "../sleep_week/sleep_week_ui.h"
#include <smooth_ui_toolkit.h>
#include <math.h>

extern FactoryTest* _ft;

// ============ FORWARD DECLARATIONS (FIX COMPILER ERROR) ============
void renderAutoShootHeader();
void renderAutoShootSettingsPanel();
void renderAutoShootSettingsItem(uint8_t index, const char* label, float value, const char* unit);
void renderAutoShootStatusPanel();
void renderAutoShootControlButtons();

// ============ UI CONSTANTS ============
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135

#define COLOR_BG UI_BG
#define COLOR_TEXT UI_FG
#define COLOR_GREEN 0x07E0
#define COLOR_RED 0xF800
#define COLOR_YELLOW 0xFFE0
#define COLOR_BORDER UI_BORDER
#define COLOR_HIGHLIGHT UI_AL

#define ITEM_HEIGHT 20
#define ITEM_Y_START 27
#define ITEM_INDENT 10

// ============ UI STATE ============
static unsigned long blinkTimer = 0;
static bool blinkState = false;

// ============ HELPER ============
void updateBlink() {
    if (millis() - blinkTimer > 500) {
        blinkState = !blinkState;
        blinkTimer = millis();
    }
}

// ============ MAIN RENDER ============
void renderAutoShootUI() {
    if (!_ft || !_ft->_canvas) return;

    // FIX: safe float
    if (!isfinite(autoShoot.state.currentDistance)) {
        autoShoot.state.currentDistance = 0.0f;
    }

    updateBlink();

    // RESET CANVAS STATE (IMPORTANT)
    _ft->_canvas->setTextWrap(false);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->setTextDatum(top_left);

    _ft->_canvas->fillScreen(COLOR_BG);

    renderAutoShootHeader();
    renderAutoShootSettingsPanel();
    renderAutoShootStatusPanel();
    renderAutoShootControlButtons();

    if (schedulerPopupActive()) schedulerPopupDraw();

    _ft->_canvas_update();
}

// ============ HEADER ============
void renderAutoShootHeader() {
    if (!_ft || !_ft->_canvas) return;

    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->drawString("AUTO MODE", SCREEN_WIDTH / 2, 2);

    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("<", 5, 2);
}

// ============ SETTINGS PANEL ============
void renderAutoShootSettingsPanel() {
    if (!_ft || !_ft->_canvas) return;

    _ft->_canvas->drawRoundRect(8, 22, 224, 88, 5, COLOR_BORDER);

    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_left);

    renderAutoShootSettingsItem(0, "Range", autoShoot.config.rangeMin, " m");
    renderAutoShootSettingsItem(1, "Range Max", autoShoot.config.rangeMax, " m");
    renderAutoShootSettingsItem(2, "Burst", (float)autoShoot.config.burstShots, "");
    renderAutoShootSettingsItem(3, "Cool", (float)autoShoot.config.cooldownMs, " ms");
}

// ============ SETTINGS ITEM ============
void renderAutoShootSettingsItem(uint8_t index, const char* label, float value, const char* unit) {
    if (!_ft || !_ft->_canvas) return;

    if (!unit) unit = "";
    if (!isfinite(value)) value = 0;

    int y = ITEM_Y_START + (index * ITEM_HEIGHT);

    bool isSelected = (autoShoot.editMode.selectedIndex == index);
    bool isEditing = (autoShoot.editMode.state == EditMode::EDITING && isSelected);

    if (isSelected) {
        _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3,
            isEditing ? COLOR_HIGHLIGHT : COLOR_BORDER);
    }

    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);

    _ft->_canvas->drawString(label, ITEM_INDENT + 6, y + 1);

    char valueStr[32];

    if (index == 2 || index == 3) {
        snprintf(valueStr, sizeof(valueStr), "%d%s", (int)value, unit);
    } else {
        snprintf(valueStr, sizeof(valueStr), "%.1f%s", value, unit);
    }

    _ft->_canvas->setTextDatum(top_right);

    if (isEditing && blinkState) {
        _ft->_canvas->setTextColor(COLOR_BG);
    }

    _ft->_canvas->drawString(valueStr, 210, y + 1);

    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->drawString(">", 225, y + 1);

    _ft->_canvas->setTextDatum(top_left);
}

// ============ STATUS PANEL ============
void renderAutoShootStatusPanel() {
    if (!_ft || !_ft->_canvas) return;

    int y = 113;

    _ft->_canvas->drawRoundRect(8, y, 106, 20, 5, COLOR_BORDER);

    const char* status = autoShoot.getStatusString();
    if (!status) status = "IDLE";

    // One line: TF distance (left) + status (right)
    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_left);

    char distStr[32];
    snprintf(distStr, sizeof(distStr), "TF:%.1fm", autoShoot.state.currentDistance);
    _ft->_canvas->setTextColor(autoShoot.state.objectDetected ? COLOR_GREEN : COLOR_TEXT);
    _ft->_canvas->drawString(distStr, 14, y + 6);

    uint16_t color = COLOR_GREEN;
    if (strcmp(status, "IDLE") == 0) color = COLOR_TEXT;
    if (strcmp(status, "DETECTING") == 0) color = COLOR_YELLOW;
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(color);
    _ft->_canvas->drawString(status, 108, y + 6);

    _ft->_canvas->setTextDatum(top_left);
}

// ============ CONTROL BUTTONS ============
void renderAutoShootControlButtons() {
    if (!_ft || !_ft->_canvas) return;

    int btnY = 113;

    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_center);

    bool startSel = (autoShoot.editMode.selectedIndex == 4 && autoShoot.editMode.state == EditMode::SELECTING);
    bool stopSel  = (autoShoot.editMode.selectedIndex == 5 && autoShoot.editMode.state == EditMode::SELECTING);

    // START button
    _ft->_canvas->drawRoundRect(122, btnY, 52, 20, 5,
        (startSel || autoShoot.state.isRunning) ? COLOR_GREEN : COLOR_BORDER);
    if (startSel) {
        _ft->_canvas->fillRoundRect(123, btnY + 1, 50, 18, 4, COLOR_GREEN);
        _ft->_canvas->setTextColor(COLOR_BG);
    } else {
        _ft->_canvas->setTextColor(autoShoot.state.isRunning ? COLOR_BG : COLOR_GREEN);
    }
    _ft->_canvas->drawString("START", 148, btnY + 6);

    // STOP button
    _ft->_canvas->drawRoundRect(180, btnY, 52, 20, 5,
        (stopSel || autoShoot.state.isRunning) ? COLOR_RED : COLOR_BORDER);
    if (stopSel) {
        _ft->_canvas->fillRoundRect(181, btnY + 1, 50, 18, 4, COLOR_RED);
        _ft->_canvas->setTextColor(COLOR_BG);
    } else {
        _ft->_canvas->setTextColor(autoShoot.state.isRunning ? COLOR_RED : COLOR_TEXT);
    }
    _ft->_canvas->drawString("STOP", 206, btnY + 6);
}

// ============ INIT ============
void initAutoShootUI() {
    autoShoot.editMode.state = EditMode::SELECTING;
    autoShoot.editMode.selectedIndex = 0;
}