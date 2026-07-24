/**
 * @file trigger_mode_ui.cpp
 * @brief Trigger Mode UI - ON/OFF labels for G1, G2
 * @date 2026-07-24
 *
 * Follows GEOPIX UI STANDARD (see display_mode_ui.cpp):
 * themed header + rounded panel y=22 + inverse selection rows,
 * efontCN_16 items, bottom status panel y=113 h=20, no footer captions.
 */

#include "trigger_mode.h"
#include "../factory_test/factory_test.h"
#include "../common/ui_theme.h"   // themed palette
#include "../remote/remote_manager.h"
#include "../sleep_week/sleep_week_ui.h"
#include <smooth_ui_toolkit.h>

extern FactoryTest* _ft;

// ============ FORWARD DECLARATIONS ============
void renderTriggerHeader();
void renderG1G2Labels();
void renderToggleLabel(uint8_t index, const char* label, bool enabled, int y);
void renderBluetoothRow(int y);
void renderTriggerStatusPanel();
void renderTestButton();
void renderBluetoothScreen();
void renderBtRow(uint8_t index, const char* label, const char* value, int y);

// ============ UI CONSTANTS ============
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135

// Colors
#define COLOR_BG UI_BG          // themed
#define COLOR_TEXT UI_FG        // themed
#define COLOR_GREEN 0x07E0     // Green
#define COLOR_RED 0xF800       // Red
#define COLOR_BLUE 0x001F      // Blue
#define COLOR_BORDER UI_BORDER  // themed
#define COLOR_HIGHLIGHT UI_AL   // themed

// Layout (GEOPIX UI standard)
#define ITEM_HEIGHT 20
#define ITEM_Y_START 27
#define ITEM_INDENT 16

// ============ RENDER FUNCTIONS ============
void renderTriggerModeUI() {
    if (!_ft) return;
    
    // Clear screen
    _ft->_canvas->fillScreen(COLOR_BG);
    
    if (triggerMode.inBluetoothScreen()) {
        // Bluetooth camera remote sub-screen
        renderBluetoothScreen();
    } else {
        // Header
        renderTriggerHeader();
        
        // G1/G2 labels
        renderG1G2Labels();
        
        // Status panel
        renderTriggerStatusPanel();
        
        // Test button
        renderTestButton();
    }
    
    // Scheduler countdown popup
    if (schedulerPopupActive()) schedulerPopupDraw();
    
    // Push to display
    _ft->_canvas_update();
}

// ============ HEADER ============
void renderTriggerHeader() {
    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->drawString("TRIGGER MODE", SCREEN_WIDTH / 2, 2);
    
    // Back button
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("<", 5, 2);
}

// ============ G1/G2/BEEP/BT LABELS ============
void renderG1G2Labels() {
    // Settings panel (GEOPIX standard: y=22, gap below header)
    _ft->_canvas->drawRoundRect(8, 22, 224, 88, 5, COLOR_BORDER);

    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_left);

    renderToggleLabel(0, "Trigger", triggerMode.getTriggerEnabled(), ITEM_Y_START);
    renderToggleLabel(1, "Remote",  triggerMode.getRemoteEnabled(),  ITEM_Y_START + ITEM_HEIGHT);
    renderToggleLabel(2, "Beep",    triggerMode.getBeepEnabled(),    ITEM_Y_START + ITEM_HEIGHT * 2);
    renderBluetoothRow(ITEM_Y_START + ITEM_HEIGHT * 3);
}

void renderToggleLabel(uint8_t index, const char* label, bool enabled, int y) {
    bool isSelected = (triggerMode.editMode.selectedIndex == index);

    if (isSelected) {
        _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3, COLOR_BORDER);
    }

    // Label
    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->drawString(label, ITEM_INDENT, y);

    // ON/OFF indicator — green/red
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(enabled ? COLOR_GREEN : COLOR_RED);
    _ft->_canvas->drawString(enabled ? "ON" : "OFF", 210, y);

    // Arrow
    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString(">", 226, y);

    _ft->_canvas->setTextDatum(top_left);
}

// Bluetooth row (index 3) — 3rd shooting channel, ON/OFF like Trigger/Remote
void renderBluetoothRow(int y) {
    bool isSelected = (triggerMode.editMode.selectedIndex == 3);
    bool enabled = triggerMode.getBluetoothEnabled();

    if (isSelected) {
        _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3, COLOR_BORDER);
    }

    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->drawString("Bluetooth", ITEM_INDENT, y);

    // "ON SONY" / "OFF" — driver name shown when a BLE brand is selected
    CameraBrand brand = RemoteManager::getBrand();
    char valStr[16];
    if (enabled && brand != CameraBrand::None) {
        snprintf(valStr, sizeof(valStr), "ON %s", RemoteManager::brandName(brand));
    } else {
        snprintf(valStr, sizeof(valStr), enabled ? "ON" : "OFF");
    }
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(enabled ? COLOR_GREEN : COLOR_RED);
    _ft->_canvas->drawString(valStr, 210, y);

    // Arrow
    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString(">", 226, y);

    _ft->_canvas->setTextDatum(top_left);
}

// ============ STATUS PANEL ============
void renderTriggerStatusPanel() {
    int y = 113;

    // Left: status panel
    _ft->_canvas->drawRoundRect(8, y, 128, 20, 5, COLOR_BORDER);

    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);

    // Trigger count
    _ft->_canvas->drawString("Trig:", 14, y + 5);
    _ft->_canvas->setTextColor(COLOR_GREEN);
    char countStr[16];
    snprintf(countStr, sizeof(countStr), "%d", triggerMode.getTriggerCount());
    _ft->_canvas->drawString(countStr, 44, y + 5);

    // Status
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(triggerMode.isRunning() ? COLOR_GREEN : COLOR_TEXT);
    _ft->_canvas->drawString(triggerMode.isRunning() ? "ACTIVE" : "IDLE", 130, y + 5);

    _ft->_canvas->setTextDatum(top_left);
}

// ============ TEST BUTTON ============
void renderTestButton() {
    int y = 113;
    bool isSelected = (triggerMode.editMode.selectedIndex == 4);

    // Right of status panel — same bottom row (GEOPIX standard)
    if (isSelected) {
        _ft->_canvas->fillRoundRect(142, y, 90, 20, 5, COLOR_HIGHLIGHT);
    } else {
        _ft->_canvas->drawRoundRect(142, y, 90, 20, 5, COLOR_GREEN);
    }

    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString("TEST TRIGGER", 187, y + 5);
    _ft->_canvas->setTextDatum(top_left);
}

// ============ BLUETOOTH SUB-SCREEN ============
void renderBtRow(uint8_t index, const char* label, const char* value, int y) {
    bool isSelected = (triggerMode.editMode.btIndex == index);

    if (isSelected) {
        _ft->_canvas->fillRoundRect(10, y - 1, 220, 15, 3, COLOR_BORDER);
    }

    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->drawString(label, ITEM_INDENT, y);

    if (value && value[0]) {
        _ft->_canvas->setTextDatum(top_right);
        _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_GREEN);
        _ft->_canvas->drawString(value, 210, y);
    }

    // Arrow
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString(">", 226, y);
    _ft->_canvas->setTextDatum(top_left);
}

void renderBluetoothScreen() {
    // Header (GEOPIX standard)
    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->drawString("BLUETOOTH", SCREEN_WIDTH / 2, 2);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("<", 5, 2);

    CameraBrand brand = RemoteManager::getBrand();

    // Settings panel (5 rows, 17px pitch)
    _ft->_canvas->drawRoundRect(8, 22, 224, 88, 5, COLOR_BORDER);
    _ft->_canvas->setFont(&fonts::efontCN_12);
    renderBtRow(0, "Enable", triggerMode.getBluetoothEnabled() ? "ON" : "OFF", 26);
    renderBtRow(1, "Driver", RemoteManager::brandName(brand), 43);
    renderBtRow(2, "PAIR CAMERA", "", 60);
    renderBtRow(3, "TEST SHOT", "", 77);
    renderBtRow(4, "FORGET", "", 94);

    // Status panel: connection state left, last action right
    int y = 113;
    _ft->_canvas->drawRoundRect(8, y, 224, 20, 5, COLOR_BORDER);
    _ft->_canvas->setFont(&fonts::efontCN_10);

    if (brand == CameraBrand::None) {
        _ft->_canvas->setTextColor(COLOR_TEXT);
        _ft->_canvas->drawString("Select driver: SONY/CANON/NIKON", 14, y + 5);
    } else {
        bool conn = RemoteManager::isConnected();
        bool paired = RemoteManager::hasPairedCamera();
        _ft->_canvas->setTextColor(conn ? COLOR_GREEN : (paired ? COLOR_BLUE : COLOR_RED));
        _ft->_canvas->drawString(conn ? "Connected" : (paired ? "Paired" : "Not paired"), 14, y + 5);

        if (triggerMode.btStatus[0]) {
            _ft->_canvas->setTextDatum(top_right);
            _ft->_canvas->setTextColor(COLOR_TEXT);
            _ft->_canvas->drawString(triggerMode.btStatus, 226, y + 5);
            _ft->_canvas->setTextDatum(top_left);
        }
    }
}

// Full-screen "pairing..." notice drawn before the blocking pair scan
void renderBtPairingScreen() {
    _ft->_canvas->fillScreen(COLOR_BG);
    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(middle_center);
    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->drawString("PAIRING...", SCREEN_WIDTH / 2, 50);
    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("Set camera to BLE pairing mode", SCREEN_WIDTH / 2, 80);
    _ft->_canvas->drawString("Scanning ~10s, please wait", SCREEN_WIDTH / 2, 95);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas_update();
}

// ============ INIT ============
void initTriggerModeUI() {
    triggerMode.editMode.state = TriggerEditMode::SELECTING;
    triggerMode.editMode.screen = TriggerEditMode::MAIN;
    triggerMode.editMode.selectedIndex = 0;
    triggerMode.editMode.btIndex = 0;
    triggerMode.btStatus[0] = '\0';
}
