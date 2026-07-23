/**
 * @file display_mode.cpp
 * @brief DISPLAY mode core + power save engine (learned from M5Launcher powerSave.cpp)
 * @date 2026-07-23
 */

#include "display_mode.h"
#include "../factory_test/factory_test.h"
#include "../common/ui_theme.h"
#include <Preferences.h>

extern FactoryTest* _ft;

DisplayMode displayMode;

// Power save timeout choices (seconds), 0 = OFF
static const int DIMMER_STEPS[]   = {0, 5, 10, 15, 30, 60, 120, 300};
static const int DIMMER_STEPS_N   = sizeof(DIMMER_STEPS) / sizeof(DIMMER_STEPS[0]);

static int dimmerStepIndex(int sec) {
    for (int i = 0; i < DIMMER_STEPS_N; i++)
        if (DIMMER_STEPS[i] == sec) return i;
    return 4;   // default 30s
}

// ============ CONFIG ============
void DisplayMode::loadConfig() {
    Preferences prefs;
    prefs.begin("display");
    config.brightness = prefs.getInt("bright", 255);
    config.dimmerSec  = prefs.getInt("dimmer", 30);
    config.themeIndex = prefs.getInt("theme", 0);
    config.rotation   = prefs.getInt("rot", 3);
    prefs.end();

    if (config.brightness < 5)   config.brightness = 5;
    if (config.brightness > 255) config.brightness = 255;
    config.dimmerSec = DIMMER_STEPS[dimmerStepIndex(config.dimmerSec)];
    if (config.themeIndex < 0 || config.themeIndex >= UI_THEME_COUNT) config.themeIndex = 0;
    if (config.rotation != 1 && config.rotation != 3) config.rotation = 3;
}

void DisplayMode::saveConfig() {
    Preferences prefs;
    prefs.begin("display");
    prefs.putInt("bright", config.brightness);
    prefs.putInt("dimmer", config.dimmerSec);
    prefs.putInt("theme", config.themeIndex);
    prefs.putInt("rot", config.rotation);
    prefs.end();
}

void DisplayMode::applyBrightness() {
    if (_ft && _ft->_disp) _ft->_disp->setBrightness(config.brightness);
}

void DisplayMode::applyTheme() {
    uiThemeApply(config.themeIndex);
}

void DisplayMode::applyRotation() {
    // Only landscape orientations (1 and 3): canvas stays 240x135, safe.
    if (_ft && _ft->_disp) _ft->_disp->setRotation(config.rotation);
}

// ============ UI INTERACTION ============
void DisplayMode::handleEncoderRotate(int delta) {
    if (delta > 0) delta = 1;
    else if (delta < 0) delta = -1;

    if (editMode.state == DisplayEditMode::SELECTING) {
        int idx = (int)editMode.selectedIndex + delta;
        if (idx < 0) idx = 0;
        if (idx > 3) idx = 3;
        editMode.selectedIndex = (uint8_t)idx;

    } else {   // EDITING
        if (editMode.selectedIndex == 0) {
            // Brightness 5..255, step 10, live preview
            config.brightness += delta * 10;
            if (config.brightness < 5)   config.brightness = 5;
            if (config.brightness > 255) config.brightness = 255;
            applyBrightness();
        } else if (editMode.selectedIndex == 1) {
            // Power save timeout: step through preset list
            int i = dimmerStepIndex(config.dimmerSec) + delta;
            if (i < 0) i = 0;
            if (i >= DIMMER_STEPS_N) i = DIMMER_STEPS_N - 1;
            config.dimmerSec = DIMMER_STEPS[i];
        } else if (editMode.selectedIndex == 2) {
            // Theme: wrap through 8 presets, live preview
            int t = config.themeIndex + delta;
            if (t < 0) t = UI_THEME_COUNT - 1;
            if (t >= UI_THEME_COUNT) t = 0;
            config.themeIndex = t;
            applyTheme();
        } else {
            // Rotation: toggle 3 (normal) <-> 1 (flipped), live preview
            config.rotation = (config.rotation == 3) ? 1 : 3;
            applyRotation();
        }
    }
}

void DisplayMode::handleButtonPress() {
    editMode.state = (editMode.state == DisplayEditMode::SELECTING)
                         ? DisplayEditMode::EDITING
                         : DisplayEditMode::SELECTING;
}

// ============ POWER SAVE ENGINE (main menu only) ============
namespace DisplayPowerSave {

static unsigned long s_lastActivity = 0;
static bool s_dimmed    = false;
static bool s_screenOff = false;
static bool s_cpuLow    = false;

static void _cpuLow() {
    if (s_cpuLow) return;
    s_cpuLow = true;
    setCpuFrequencyMhz(80);
    // Idle-task watchdogs off while sleeping (M5Launcher pattern).
    disableCore0WDT();
#if SOC_CPU_CORES_NUM > 1
    disableCore1WDT();
#endif
}

static void _cpuNormal() {
    if (!s_cpuLow) return;
    s_cpuLow = false;
    setCpuFrequencyMhz(240);
    enableCore0WDT();
#if SOC_CPU_CORES_NUM > 1
    enableCore1WDT();
#endif
}

void tick() {
    if (displayMode.config.dimmerSec == 0) return;   // power save OFF
    if (s_lastActivity == 0) s_lastActivity = millis();

    unsigned long idleMs = millis() - s_lastActivity;
    unsigned long dimAt  = (unsigned long)displayMode.config.dimmerSec * 1000UL;

    // Stage 1: dim to ~5%
    if (!s_dimmed && idleMs >= dimAt) {
        s_dimmed = true;
        if (_ft && _ft->_disp) _ft->_disp->setBrightness(13);   // ~5% of 255
    }
    // Stage 2 (+5s): screen off + CPU 80MHz
    else if (s_dimmed && !s_screenOff && idleMs >= dimAt + 5000UL) {
        s_screenOff = true;
        if (_ft && _ft->_disp) _ft->_disp->setBrightness(0);
        _cpuLow();
    }
}

bool wake() {
    s_lastActivity = millis();
    if (s_dimmed || s_screenOff) {
        _cpuNormal();
        s_dimmed    = false;
        s_screenOff = false;
        displayMode.applyBrightness();
        return true;   // caller must swallow the waking key
    }
    return false;
}

void exitPowerSave() {
    _cpuNormal();
    s_dimmed    = false;
    s_screenOff = false;
    s_lastActivity = millis();
    displayMode.applyBrightness();
}

bool isSaving() { return s_dimmed || s_screenOff; }

}   // namespace DisplayPowerSave
