/**
 * @file auto_shoot_ui.cpp
 * @brief Auto Shoot UI rendering with LovyanGFX
 *
 * Two screens, same pattern as TriggerMode's MAIN/BLUETOOTH split:
 *   MAIN     — Burst, Cooldown, Advance (opens the submenu below), START/STOP.
 *              Pure mode by default: no range restriction, freed-up space
 *              used for a bigger live status readout.
 *   ADVANCE  — Range Filter ON/OFF, Range Min, Range Max. Turning the
 *              filter ON restores the exact original [Min, Max] band-pass
 *              behavior and its zone-bar visualization.
 */

#include "auto_shoot.h"
#include "tf_luna.h"   // TFLUNA_MAX_DISTANCE_M — scale for the live range-zone bar
#include "../factory_test/factory_test.h"
#include "../common/ui_theme.h"   // themed palette
#include "../sleep_week/sleep_week_ui.h"
#include <smooth_ui_toolkit.h>
#include <math.h>

extern FactoryTest* _ft;

// ============ FORWARD DECLARATIONS ============
void renderAutoShootHeader();
void renderAutoShootSettingsPanel();
void renderAutoShootSettingsItem(uint8_t index, const char* label, int value, const char* unit);
void renderAutoShootAdvanceRow(int y);
void renderAutoShootLiveInfo();
void renderAutoShootStatusPanel();
void renderAutoShootControlButtons();
void renderAutoShootAdvanceScreen();
void renderAdvanceItem(uint8_t index, const char* label, const char* valueStr);
static void renderZoneBar(int barX, int barY, int barW, int barH);

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

    if (autoShoot.inAdvanceScreen()) {
        renderAutoShootAdvanceScreen();
    } else {
        renderAutoShootHeader();
        renderAutoShootSettingsPanel();
        renderAutoShootLiveInfo();
        renderAutoShootStatusPanel();
        renderAutoShootControlButtons();
    }

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

    // Confirmed TF-Luna sample rate, top-right. tfLuna.getFrameRateHz() is
    // just a cached value from the one readback done in begin() -- reading
    // it here for display costs nothing extra on the I2C bus, same as any
    // other text drawn on this already-throttled (~30fps) screen.
    // 0 = the write/readback in configureFrameRate() didn't check out, so
    // the driver quietly fell back to the safe 100Hz pacing.
    uint16_t fps = tfLuna.getFrameRateHz();
    char fpsStr[16];
    if (fps > 0) snprintf(fpsStr, sizeof(fpsStr), "%uHz", (unsigned)fps);
    else         snprintf(fpsStr, sizeof(fpsStr), "100Hz*");

    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(fps > 0 ? COLOR_GREEN : COLOR_YELLOW);
    _ft->_canvas->drawString(fpsStr, 235, 3);
    _ft->_canvas->setTextDatum(top_left);
}

// ============ SETTINGS PANEL (MAIN: Burst / Cooldown / Advance) ============
void renderAutoShootSettingsPanel() {
    if (!_ft || !_ft->_canvas) return;

    // Only 3 rows now (was 4) -- Range Min/Max moved into the Advance
    // submenu, so the panel is shorter, freeing space below for
    // renderAutoShootLiveInfo().
    _ft->_canvas->drawRoundRect(8, 22, 224, 70, 5, COLOR_BORDER);

    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_left);

    renderAutoShootSettingsItem(0, "Burst", autoShoot.config.burstShots, "");
    renderAutoShootSettingsItem(1, "Cooldown", autoShoot.config.cooldownMs, " ms");
    renderAutoShootAdvanceRow(ITEM_Y_START + ITEM_HEIGHT * 2);
}

// ============ SETTINGS ITEM (integer values only: Burst, Cooldown) ============
void renderAutoShootSettingsItem(uint8_t index, const char* label, int value, const char* unit) {
    if (!_ft || !_ft->_canvas) return;
    if (!unit) unit = "";

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
    snprintf(valueStr, sizeof(valueStr), "%d%s", value, unit);

    _ft->_canvas->setTextDatum(top_right);
    if (isEditing && blinkState) {
        _ft->_canvas->setTextColor(COLOR_BG);
    }
    _ft->_canvas->drawString(valueStr, 210, y + 1);

    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->drawString(">", 225, y + 1);

    _ft->_canvas->setTextDatum(top_left);
}

// ============ ADVANCE ROW (on MAIN screen — opens the submenu) ============
void renderAutoShootAdvanceRow(int y) {
    if (!_ft || !_ft->_canvas) return;

    bool isSelected = (autoShoot.editMode.selectedIndex == 2 &&
                        autoShoot.editMode.state == EditMode::SELECTING);

    if (isSelected) {
        _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3, COLOR_BORDER);
    }

    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);
    _ft->_canvas->drawString("Advance", ITEM_INDENT + 6, y + 1);

    bool filtered = autoShoot.config.filterEnabled;
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : (filtered ? COLOR_GREEN : COLOR_TEXT));
    _ft->_canvas->drawString(filtered ? "Filter ON" : "Filter OFF", 210, y + 1);

    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString(">", 225, y + 1);

    _ft->_canvas->setTextDatum(top_left);
}

// ============ LIVE INFO (freed-up space: mode + zone bar) ============
void renderAutoShootLiveInfo() {
    if (!_ft || !_ft->_canvas) return;

    int y = 94;

    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("Mode:", 12, y);

    bool filtered = autoShoot.config.filterEnabled;
    _ft->_canvas->setTextColor(filtered ? COLOR_GREEN : COLOR_YELLOW);
    _ft->_canvas->drawString(filtered ? "FILTERED (zone)" : "PURE (full range)", 46, y);

    // Same bar the Advance screen uses (see renderZoneBar) -- in pure mode
    // it just shows the live marker with no highlighted band, since no
    // zone applies; in filtered mode it shows the configured [Min, Max]
    // band even without opening Advance.
    renderZoneBar(10, y + 12, 220, 6);
}

// ============ STATUS PANEL ============
void renderAutoShootStatusPanel() {
    if (!_ft || !_ft->_canvas) return;

    int y = 113;

    _ft->_canvas->drawRoundRect(8, y, 106, 20, 5, COLOR_BORDER);

    const char* status = autoShoot.getStatusString();
    if (!status) status = "IDLE";

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

    bool startSel = (autoShoot.editMode.selectedIndex == 3 && autoShoot.editMode.state == EditMode::SELECTING);
    bool stopSel  = (autoShoot.editMode.selectedIndex == 4 && autoShoot.editMode.state == EditMode::SELECTING);

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

// ============ LIVE ZONE BAR (shared by MAIN's live info + ADVANCE) ============
// Fixed 0..TFLUNA_MAX_DISTANCE_M scale — the sensor's real max range, NOT
// just the configured Range Max — so the [Range Min, Range Max] band is
// shown in its true physical position. Highlighted zone only drawn when
// the filter is enabled (in pure mode there IS no zone restriction).
static void renderZoneBar(int barX, int barY, int barW, int barH) {
    if (!_ft || !_ft->_canvas) return;
    const float span = TFLUNA_MAX_DISTANCE_M;

    auto distToX = [&](float d) -> int {
        if (d < 0.0f) d = 0.0f;
        if (d > span) d = span;
        return barX + (int)((d / span) * (float)(barW - 2)) + 1;
    };

    _ft->_canvas->drawRect(barX, barY, barW, barH, COLOR_BORDER);

    if (autoShoot.config.filterEnabled) {
        int zoneX0 = distToX(autoShoot.config.rangeMin);
        int zoneX1 = distToX(autoShoot.config.rangeMax);
        if (zoneX1 > zoneX0) {
            _ft->_canvas->fillRect(zoneX0, barY + 1, zoneX1 - zoneX0, barH - 2, COLOR_HIGHLIGHT);
        }
    }

    // Live reading marker — green when it counts as a detection, red when
    // it doesn't (outside the zone, or no return at all).
    int markX = distToX(autoShoot.state.currentDistance);
    uint16_t markColor = autoShoot.state.objectDetected ? COLOR_GREEN : COLOR_RED;
    _ft->_canvas->drawFastVLine(markX, barY - 1, barH + 2, markColor);
}

// ============ ADVANCE SCREEN (Range Filter submenu) ============
void renderAutoShootAdvanceScreen() {
    if (!_ft || !_ft->_canvas) return;

    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->drawString("ADVANCE", SCREEN_WIDTH / 2, 2);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("<", 5, 2);

    _ft->_canvas->drawRoundRect(8, 22, 224, 70, 5, COLOR_BORDER);
    _ft->_canvas->setFont(&fonts::efontCN_16);

    bool filtered = autoShoot.config.filterEnabled;

    // Row 0: Range Filter ON/OFF
    {
        int y = ITEM_Y_START;
        bool isSel = (autoShoot.editMode.advanceIndex == 0);
        if (isSel) _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3, COLOR_BORDER);
        _ft->_canvas->setTextDatum(top_left);
        _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);
        _ft->_canvas->drawString("Range Filter", ITEM_INDENT + 6, y + 1);
        _ft->_canvas->setTextDatum(top_right);
        _ft->_canvas->setTextColor(isSel ? COLOR_BG : (filtered ? COLOR_GREEN : COLOR_RED));
        _ft->_canvas->drawString(filtered ? "ON" : "OFF", 210, y + 1);
        _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_GREEN);
        _ft->_canvas->drawString(">", 225, y + 1);
        _ft->_canvas->setTextDatum(top_left);
    }

    // Row 1/2: Range Min / Range Max — same fields, same editing logic as
    // before the Advance submenu existed, just relocated here.
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f m", autoShoot.config.rangeMin);
    renderAdvanceItem(1, "Range Min", buf);
    snprintf(buf, sizeof(buf), "%.1f m", autoShoot.config.rangeMax);
    renderAdvanceItem(2, "Range Max", buf);

    // Live zone bar right under the panel — see it update as you drag
    // Range Min/Max, exactly like the MAIN screen's copy.
    renderZoneBar(10, 96, 220, 8);

    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_BORDER);
    _ft->_canvas->drawString(filtered ? "Filter ON: only [Min,Max] counts"
                                       : "Filter OFF: Min/Max have no effect",
                              12, 108);
}

void renderAdvanceItem(uint8_t index, const char* label, const char* valueStr) {
    if (!_ft || !_ft->_canvas) return;

    int y = ITEM_Y_START + (index * ITEM_HEIGHT);
    bool isSel  = (autoShoot.editMode.advanceIndex == index);
    bool isEdit = (autoShoot.editMode.state == EditMode::EDITING && isSel);

    if (isSel) {
        _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3,
            isEdit ? COLOR_HIGHLIGHT : COLOR_BORDER);
    }

    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);
    _ft->_canvas->drawString(label, ITEM_INDENT + 6, y + 1);

    _ft->_canvas->setTextDatum(top_right);
    if (isEdit && blinkState) {
        _ft->_canvas->setTextColor(COLOR_BG);
    } else {
        _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_GREEN);
    }
    _ft->_canvas->drawString(valueStr, 210, y + 1);

    _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString(">", 225, y + 1);

    _ft->_canvas->setTextDatum(top_left);
}

// ============ INIT ============
void initAutoShootUI() {
    autoShoot.editMode.state = EditMode::SELECTING;
    autoShoot.editMode.screen = EditMode::MAIN;
    autoShoot.editMode.selectedIndex = 0;
    autoShoot.editMode.advanceIndex = 0;
}
