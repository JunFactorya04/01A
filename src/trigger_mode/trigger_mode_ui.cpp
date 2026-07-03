/**
 * @file trigger_mode_ui.cpp
 * @brief Trigger Mode UI - ON/OFF labels for G1, G2
 * @date 2026-07-03
 */

#include "trigger_mode.h"
#include "../factory_test/factory_test.h"
#include <smooth_ui_toolkit.h>

extern FactoryTest* _ft;

// ============ FORWARD DECLARATIONS ============
void renderTriggerHeader();
void renderG1G2Labels();
void renderToggleLabel(uint8_t index, const char* label, bool enabled, int y);
void renderTriggerStatusPanel();
void renderTestButton();

// ============ UI CONSTANTS ============
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135

// Colors
#define COLOR_BG 0x0000        // Black
#define COLOR_TEXT 0xFFFF      // White
#define COLOR_GREEN 0x07E0     // Green
#define COLOR_RED 0xF800       // Red
#define COLOR_BLUE 0x001F      // Blue
#define COLOR_BORDER 0x4208    // Dark gray
#define COLOR_HIGHLIGHT 0x27E0 // Bright green

// Layout
#define ITEM_HEIGHT 30
#define ITEM_Y_START 22
#define ITEM_INDENT 15

// ============ RENDER FUNCTIONS ============
void renderTriggerModeUI() {
    if (!_ft) return;
    
    // Clear screen
    _ft->_canvas->fillScreen(COLOR_BG);
    
    // Header
    renderTriggerHeader();
    
    // G1/G2 labels
    renderG1G2Labels();
    
    // Status panel
    renderTriggerStatusPanel();
    
    // Test button
    renderTestButton();
    
    // Push to display
    _ft->_canvas_update();
}

// ============ HEADER ============
void renderTriggerHeader() {
    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(COLOR_BLUE);
    _ft->_canvas->drawString("TRIGGER MODE", SCREEN_WIDTH / 2, 2);
    
    // Back button
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("<", 5, 2);
}

// ============ G1/G2 LABELS ============
void renderG1G2Labels() {
    _ft->_canvas->setFont(&fonts::efontCN_12);
    _ft->_canvas->setTextDatum(top_left);
    
    // Trigger Label (G2 - main)
    renderToggleLabel(0, "Trigger", triggerMode.getTriggerEnabled(), ITEM_Y_START);
    
    // Remote Label (G1 - backup)
    renderToggleLabel(1, "Remote", triggerMode.getRemoteEnabled(), ITEM_Y_START + ITEM_HEIGHT + 8);
}

void renderToggleLabel(uint8_t index, const char* label, bool enabled, int y) {
    bool isSelected = (triggerMode.editMode.selectedIndex == index);
    
    // Background highlight
    if (isSelected) {
        _ft->_canvas->fillRoundRect(10, y - 3, 220, 26, 5, COLOR_HIGHLIGHT);
    }
    
    // Text color
    uint16_t textColor = isSelected ? COLOR_BG : COLOR_TEXT;
    _ft->_canvas->setTextColor(textColor);
    
    // Label
    _ft->_canvas->drawString(label, ITEM_INDENT, y + 6);
    
    // ON/OFF indicator
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(enabled ? COLOR_GREEN : COLOR_RED);
    _ft->_canvas->drawString(enabled ? "ON" : "OFF", 220, y + 6);
    
    // Arrow indicator
    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_BLUE);
    _ft->_canvas->drawString(">", 235, y + 6);
    
    _ft->_canvas->setTextDatum(top_left);
}

// ============ STATUS PANEL ============
void renderTriggerStatusPanel() {
    int y = 88;
    
    // Panel border
    _ft->_canvas->drawRoundRect(8, y, 224, 20, 5, COLOR_BORDER);
    
    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    
    // Trigger count
    _ft->_canvas->drawString("Triggers:", 12, y + 5);
    _ft->_canvas->setTextColor(COLOR_GREEN);
    char countStr[16];
    snprintf(countStr, sizeof(countStr), "%d", triggerMode.getTriggerCount());
    _ft->_canvas->drawString(countStr, 70, y + 5);
    
    // Status
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(triggerMode.isRunning() ? COLOR_GREEN : COLOR_TEXT);
    _ft->_canvas->drawString(triggerMode.isRunning() ? "ACTIVE" : "IDLE", 225, y + 5);
    
    _ft->_canvas->setTextDatum(top_left);
}

// ============ TEST BUTTON ============
void renderTestButton() {
    int y = 115;
    
    // Button
    _ft->_canvas->drawRoundRect(60, y, 120, 14, 3, COLOR_BORDER);
    
    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("TEST TRIGGER", 120, y + 2);
}

// ============ INIT ============
void initTriggerModeUI() {
    triggerMode.editMode.state = TriggerEditMode::SELECTING;
    triggerMode.editMode.selectedIndex = 0;
}
