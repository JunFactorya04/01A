/**
 * @file timelapse_ui.cpp
 * @brief GEOPIX Timelapse UI rendering with LovyanGFX
 * @date 2026-07-02
 */

#include "timelapse.h"
#include "../factory_test/factory_test.h"
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
#define COLOR_BG 0x0000        // Black
#define COLOR_TEXT 0xFFFF      // White
#define COLOR_GREEN 0x07E0     // Green
#define COLOR_RED 0xF800       // Red
#define COLOR_YELLOW 0xFFE0    // Yellow
#define COLOR_BLUE 0x001F      // Blue
#define COLOR_BORDER 0x4208    // Dark gray
#define COLOR_HIGHLIGHT 0x27E0 // Bright green

// Layout
#define ITEM_HEIGHT 22
#define ITEM_Y_START 18
#define ITEM_INDENT 10

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
        snprintf(buffer, sizeof(buffer), "%dm %ds", minutes, seconds);
    } else {
        snprintf(buffer, sizeof(buffer), "%ds", ms / 1000);
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
    
    // Push to display
    _ft->_canvas_update();
}

// ============ HEADER ============
void renderTimelapseHeader() {
    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(COLOR_BLUE);
    _ft->_canvas->drawString("TIMELAPSE", SCREEN_WIDTH / 2, 2);
    
    // Back button
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("<", 5, 2);
}

// ============ SETTINGS PANEL ============
void renderTimelapseSettingsPanel() {
    // Panel border
    _ft->_canvas->drawRoundRect(8, 16, 224, 68, 5, COLOR_BORDER);
    
    _ft->_canvas->setFont(&fonts::efontCN_12);
    _ft->_canvas->setTextDatum(top_left);
    
    // Item 0: Interval
    renderTimelapseSettingsItem(0, "Interval", timelapse.config.intervalMs, "");
    
    // Item 1: Total Shots
    renderTimelapseSettingsItem(1, "Total", (float)timelapse.config.totalShots, "");
    
    // Item 2: Enable
    renderTimelapseSettingsItem(2, "Enable", timelapse.config.enable ? 1.0f : 0.0f, "");
}

void renderTimelapseSettingsItem(uint8_t index, const char* label, float value, const char* unit) {
    int y = ITEM_Y_START + (index * ITEM_HEIGHT);
    bool isSelected = (timelapse.editMode.selectedIndex == index);
    bool isEditing = (timelapse.editMode.state == TimelapseEditMode::EDITING && isSelected);
    
    // Background highlight
    if (isSelected) {
        _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3, 
                                   isEditing ? COLOR_HIGHLIGHT : COLOR_BORDER);
    }
    
    // Text color
    uint16_t textColor = isSelected ? COLOR_BG : COLOR_TEXT;
    _ft->_canvas->setTextColor(textColor);
    
    // Label
    _ft->_canvas->drawString(label, ITEM_INDENT + 6, y + 2);
    
    // Value
    _ft->_canvas->setTextDatum(top_right);
    char valueStr[32];
    
    if (index == 0) {  // Interval
        snprintf(valueStr, sizeof(valueStr), "%s", formatInterval((int)value));
    } else if (index == 1) {  // Total Shots
        if ((int)value == 0) {
            snprintf(valueStr, sizeof(valueStr), "INF");
        } else {
            snprintf(valueStr, sizeof(valueStr), "%d", (int)value);
        }
    } else if (index == 2) {  // Enable
        snprintf(valueStr, sizeof(valueStr), "%s", (int)value ? "ON" : "OFF");
    }
    
    // Blinking effect when editing
    if (isEditing && timelapseBlinkState && index != 2) {
        _ft->_canvas->setTextColor(COLOR_BG);
    }
    
    _ft->_canvas->drawString(valueStr, 210, y + 2);
    
    // Arrow indicator
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(COLOR_BLUE);
    _ft->_canvas->drawString(">", 225, y + 2);
    
    _ft->_canvas->setTextDatum(top_left);
}

// ============ STATUS PANEL ============
void renderTimelapseStatusPanel() {
    int y = 88;
    
    // Panel border
    _ft->_canvas->drawRoundRect(8, y, 224, 24, 5, COLOR_BORDER);
    
    _ft->_canvas->setFont(&fonts::efontCN_12);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    
    // Left side: Shot count
    _ft->_canvas->drawString("Shots:", 12, y + 5);
    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->setFont(&fonts::efontCN_16);
    char countStr[16];
    snprintf(countStr, sizeof(countStr), "%d", timelapse.getShotCount());
    _ft->_canvas->drawString(countStr, 50, y + 3);
    
    // Right side: Status
    _ft->_canvas->setFont(&fonts::efontCN_12);
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("St:", 155, y + 5);
    
    // Status indicator
    const char* status = timelapse.getStatusString();
    uint16_t statusColor = COLOR_GREEN;
    if (strcmp(status, "IDLE") == 0) {
        statusColor = COLOR_TEXT;
    } else if (strcmp(status, "RUNNING") == 0) {
        statusColor = COLOR_YELLOW;
    }
    
    _ft->_canvas->setTextColor(statusColor);
    _ft->_canvas->drawString(status, 225, y + 5);
    
    // Remaining shots (bottom)
    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    int remaining = timelapse.getRemainingShots();
    if (remaining == -1) {
        _ft->_canvas->drawString("Infinite", 120, y + 13);
    } else if (remaining > 0) {
        char remainStr[32];
        snprintf(remainStr, sizeof(remainStr), "%d left", remaining);
        _ft->_canvas->drawString(remainStr, 120, y + 13);
    } else {
        _ft->_canvas->setTextColor(COLOR_RED);
        _ft->_canvas->drawString("Done", 120, y + 13);
    }
}

// ============ CONTROL BUTTONS ============
void renderTimelapseControlButtons() {
    // START button (left)
    int btnY = 115;
    int btnHeight = 14;
    
    _ft->_canvas->drawRoundRect(12, btnY, 100, btnHeight, 3, 
                               timelapse.isRunning() ? COLOR_GREEN : COLOR_BORDER);
    
    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(timelapse.isRunning() ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString("START", 62, btnY + 2);
    
    // STOP button (right)
    _ft->_canvas->drawRoundRect(128, btnY, 100, btnHeight, 3, 
                               timelapse.isRunning() ? COLOR_RED : COLOR_BORDER);
    
    _ft->_canvas->setTextColor(timelapse.isRunning() ? COLOR_RED : COLOR_TEXT);
    _ft->_canvas->drawString("STOP", 178, btnY + 2);
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
