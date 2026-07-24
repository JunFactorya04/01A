/**
 * @file timelapse_ui.cpp
 * @brief GEOPIX Timelapse UI rendering with LovyanGFX
 * @date 2026-07-24
 *
 * Follows GEOPIX UI STANDARD (see display_mode_ui.cpp):
 * themed header + rounded panel y=22 + inverse selection rows,
 * efontCN_16 items, bottom status panel y=113 h=20, no footer captions.
 */

#include "timelapse.h"
#include "../factory_test/factory_test.h"
#include "../common/ui_theme.h"   // themed palette
#include "../sleep_week/sleep_week_ui.h"
#include <smooth_ui_toolkit.h>

extern FactoryTest* _ft;

// ============ FORWARD DECLARATIONS ============
void renderTimelapseHeader();
void renderTimelapseSettingsPanel();
void renderTimelapseSettingsItem(uint8_t index, const char* label, float value, const char* unit);
void renderTimelapseStatusPanel();
void renderTimelapseControlButtons();

// ============ UI CONSTANTS ============
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135

// Colors
#define COLOR_BG UI_BG          // themed
#define COLOR_TEXT UI_FG        // themed
#define COLOR_GREEN 0x07E0     // Green
#define COLOR_RED 0xF800       // Red
#define COLOR_YELLOW 0xFFE0    // Yellow
#define COLOR_BLUE 0x001F      // Blue
#define COLOR_BORDER UI_BORDER  // themed
#define COLOR_HIGHLIGHT UI_AL   // themed

// Layout (GEOPIX UI standard)
#define ITEM_HEIGHT 20
#define ITEM_Y_START 27
#define ITEM_INDENT 16

// ============ UI STATE ============
static unsigned long timelapseBlinkTimer = 0;
static bool timelapseBlinkState = false;

// ============ HELPER FUNCTIONS ============
void updateTimelapseBlink() {
    if (millis() - timelapseBlinkTimer > 500) {
        timelapseBlinkState = !timelapseBlinkState;
        timelapseBlinkTimer = millis();
    }
}

// Format time in seconds/minutes
const char* formatInterval(int ms) {
    static char buffer[16];
    if (ms >= 60000) {
        int minutes = ms / 60000;
        int seconds = (ms % 60000) / 1000;
        snprintf(buffer, sizeof(buffer), "%dm%ds", minutes, seconds);
    } else if (ms >= 1000) {
        snprintf(buffer, sizeof(buffer), "%ds", ms / 1000);
    } else {
        snprintf(buffer, sizeof(buffer), "%dms", ms);
    }
    return buffer;
}

// ============ RENDER FUNCTIONS ============
void renderTimelapseUI() {
    if (!_ft) return;
    
    updateTimelapseBlink();
    
    // Clear screen
    _ft->_canvas->fillScreen(COLOR_BG);
    
    // Header
    renderTimelapseHeader();
    
    // Settings panel
    renderTimelapseSettingsPanel();
    
    // Status panel
    renderTimelapseStatusPanel();
    
    // Buttons
    renderTimelapseControlButtons();
    
    // Scheduler countdown popup
    if (schedulerPopupActive()) schedulerPopupDraw();
    
    // Push to display
    _ft->_canvas_update();
}

// ============ HEADER ============
void renderTimelapseHeader() {
    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->drawString("TIMELAPSE", SCREEN_WIDTH / 2, 2);
    
    // Back button
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("<", 5, 2);
}

// ============ SETTINGS PANEL ============
void renderTimelapseSettingsPanel() {
    // Settings panel (GEOPIX standard: y=22, gap below header)
    _ft->_canvas->drawRoundRect(8, 22, 224, 88, 5, COLOR_BORDER);

    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_left);

    // Item 0: Interval (editable)
    renderTimelapseSettingsItem(0, "Interval", timelapse.config.intervalMs, "");

    // Item 1: Total Shots (editable)
    renderTimelapseSettingsItem(1, "Total", (float)timelapse.config.totalShots, "");

    // Row 2: Duration (read-only, computed from interval x total)
    int y = ITEM_Y_START + (2 * ITEM_HEIGHT);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("Duration", ITEM_INDENT, y);

    char durBuf[24];
    long sec = timelapse.getEstimatedDurationSec();
    if (sec < 0) {
        snprintf(durBuf, sizeof(durBuf), "INF");
    } else {
        int mm = (int)(sec / 60);
        int ss = (int)(sec % 60);
        if (mm > 0) snprintf(durBuf, sizeof(durBuf), "%dm%02ds", mm, ss);
        else        snprintf(durBuf, sizeof(durBuf), "%ds", ss);
    }
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(COLOR_YELLOW);
    _ft->_canvas->drawString(durBuf, 210, y);
    _ft->_canvas->setTextDatum(top_left);

    // Row 3: Shots done / remaining (read-only, live)
    y = ITEM_Y_START + (3 * ITEM_HEIGHT);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("Shots", ITEM_INDENT, y);

    char shotBuf[24];
    int remaining = timelapse.getRemainingShots();
    if (remaining == -1) {
        snprintf(shotBuf, sizeof(shotBuf), "%d (INF)", timelapse.getShotCount());
    } else {
        snprintf(shotBuf, sizeof(shotBuf), "%d / %d",
                 timelapse.getShotCount(), timelapse.config.totalShots);
    }
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->drawString(shotBuf, 210, y);
    _ft->_canvas->setTextDatum(top_left);
}

void renderTimelapseSettingsItem(uint8_t index, const char* label, float value, const char* unit) {
    int y = ITEM_Y_START + (index * ITEM_HEIGHT);
    bool isSelected = (timelapse.editMode.selectedIndex == index);
    bool isEditing = (timelapse.editMode.state == TimelapseEditMode::EDITING && isSelected);

    // Background highlight (editing=highlight, selected=border)
    if (isSelected) {
        _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3,
                                   isEditing ? COLOR_HIGHLIGHT : COLOR_BORDER);
    }

    // Label
    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->drawString(label, ITEM_INDENT, y);

    // Value string
    char valueStr[32];
    if (index == 0) {                       // Interval
        snprintf(valueStr, sizeof(valueStr), "%s", formatInterval((int)value));
    } else {                                // Total Shots
        if ((int)value == 0) snprintf(valueStr, sizeof(valueStr), "INF");
        else                 snprintf(valueStr, sizeof(valueStr), "%d", (int)value);
    }

    // Value color (green; inverted when selected; blink when editing)
    _ft->_canvas->setTextDatum(top_right);
    if (isEditing && timelapseBlinkState) {
        _ft->_canvas->setTextColor(COLOR_BG);
    } else {
        _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_GREEN);
    }
    _ft->_canvas->drawString(valueStr, 210, y);

    // Arrow indicator
    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString(">", 226, y);

    _ft->_canvas->setTextDatum(top_left);
}

// ============ STATUS PANEL ============
void renderTimelapseStatusPanel() {
    int y = 113;

    // Left: status panel (GEOPIX standard bottom row)
    _ft->_canvas->drawRoundRect(8, y, 128, 20, 5, COLOR_BORDER);

    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("St:", 14, y + 5);

    const char* status = timelapse.getStatusString();
    uint16_t statusColor = COLOR_GREEN;
    if (strcmp(status, "IDLE") == 0) {
        statusColor = COLOR_TEXT;
    } else if (strcmp(status, "RUNNING") == 0) {
        statusColor = COLOR_GREEN;
    } else if (strcmp(status, "PAUSED") == 0) {
        statusColor = COLOR_YELLOW;
    } else if (strcmp(status, "DONE") == 0) {
        statusColor = COLOR_GREEN;
    }
    _ft->_canvas->setTextColor(statusColor);
    _ft->_canvas->drawString(status, 34, y + 5);

    // Remaining shots (right side of status panel)
    _ft->_canvas->setTextDatum(top_right);
    int remaining = timelapse.getRemainingShots();
    if (remaining == -1) {
        _ft->_canvas->setTextColor(COLOR_TEXT);
        _ft->_canvas->drawString("Infinite", 130, y + 5);
    } else if (remaining > 0) {
        char remainStr[16];
        snprintf(remainStr, sizeof(remainStr), "%d left", remaining);
        _ft->_canvas->setTextColor(COLOR_TEXT);
        _ft->_canvas->drawString(remainStr, 130, y + 5);
    } else {
        _ft->_canvas->setTextColor(COLOR_RED);
        _ft->_canvas->drawString("Done", 130, y + 5);
    }

    _ft->_canvas->setTextDatum(top_left);
}

// ============ CONTROL BUTTON (START / PAUSE / RESUME) ============
void renderTimelapseControlButtons() {
    int btnY = 113;
    bool isSel = (timelapse.editMode.selectedIndex == 2);

    const char* label = timelapse.getControlLabel();          // START / PAUSE / RESUME
    uint16_t accent = timelapse.isRunning() ? COLOR_RED : COLOR_GREEN;

    // Right of status panel — same bottom row (GEOPIX standard)
    if (isSel) {
        _ft->_canvas->fillRoundRect(142, btnY, 90, 20, 5, COLOR_HIGHLIGHT);
    } else {
        _ft->_canvas->drawRoundRect(142, btnY, 90, 20, 5, accent);
    }

    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(isSel ? COLOR_BG : accent);
    _ft->_canvas->drawString(label, 187, btnY + 5);
    _ft->_canvas->setTextDatum(top_left);
}

// ============ INTERACTION HANDLER ============
void handleTimelapseInput() {
    // This will be called from main loop
    // Encoder rotation and button press handled via callbacks
}

// ============ INIT ============
void initTimelapseUI() {
    timelapse.editMode.state = TimelapseEditMode::SELECTING;
    timelapse.editMode.selectedIndex = 0;
}
