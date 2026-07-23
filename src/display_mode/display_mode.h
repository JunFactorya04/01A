/**
 * @file display_mode.h
 * @brief DISPLAY mode — brightness + 2-stage power save (learned from M5Launcher powerSave.cpp)
 * @date 2026-07-23
 *
 * Power save is ONLY active at the Main Menu (hooked in view.cpp).
 * Feature modes (Auto Shoot, Timelapse, ...) are never affected.
 */

#pragma once
#include <Arduino.h>

// ============ CONFIG (persisted in NVS "display") ============
struct DisplayModeConfig {
    int brightness = 255;   // 5..255, applied globally
    int dimmerSec  = 30;    // power save timeout in seconds, 0 = OFF
    int themeIndex = 0;     // 0..UI_THEME_COUNT-1, global UI colors
    int rotation   = 3;     // panel rotation: 3 = normal, 1 = flipped 180°
};

// ============ EDIT MODE ============
struct DisplayEditMode {
    enum EditState { SELECTING = 0, EDITING = 1 } state = SELECTING;
    uint8_t selectedIndex = 0;   // 0=Brightness 1=Power Save 2=Theme 3=Rotation
};

// ============ DISPLAY MODE CLASS ============
class DisplayMode {
public:
    DisplayModeConfig config;
    DisplayEditMode   editMode;

    void loadConfig();
    void saveConfig();
    void applyBrightness();               // push config.brightness to panel
    void applyTheme();                    // push config.themeIndex to UI globals
    void applyRotation();                 // push config.rotation to panel

    // UI interaction (same pattern as Setting mode)
    void handleEncoderRotate(int delta);
    void handleButtonPress();
};

extern DisplayMode displayMode;

// ============ POWER SAVE ENGINE (main menu only) ============
// 2 stages: after dimmerSec -> dim to 5%, +5s more -> screen off + CPU 80MHz.
namespace DisplayPowerSave {
    void tick();          // call every frame at main menu
    bool wake();          // any input: restore brightness/CPU, reset timer.
                          // returns true if it actually woke (caller must swallow the key)
    void exitPowerSave(); // force full-awake state (call before entering any app)
    bool isSaving();      // dimmed or screen off
}
