/**
 * @file setting.h
 * @brief System Settings — DateTime (RTC) + Speaker ON/OFF
 * @date 2026-07-04
 */

#pragma once
#include <Arduino.h>

// ============ DATETIME STATE ============
struct SettingDateTime {
    uint16_t year    = 2026;
    uint8_t  month   = 1;
    uint8_t  day     = 1;
    uint8_t  weekDay = 0;   // 0=Sun…6=Sat, recomputed by mktime on save
    uint8_t  hour    = 0;
    uint8_t  minute  = 0;
    uint8_t  second  = 0;
};

// ============ CONFIG (persisted) ============
struct SettingConfig {
    bool speakerEnabled = true;
};

// ============ EDIT MODE ============
struct SettingEditMode {
    enum EditState { IDLE = 0, SELECTING = 1, EDITING = 2, SHOWING_INFO = 3 } state = IDLE;
    // DISPLAY_SETTINGS = brightness/power-save/theme/rotation, moved here
    // from the old standalone DISPLAY mode (now MULTI BOX, see
    // factory_test_multi_box.cpp). Named DISPLAY_SETTINGS rather than
    // DISPLAY because Arduino.h #define's DISPLAY as a numeric macro,
    // which silently breaks enum parsing if reused here. Fully delegated
    // to the existing DisplayMode class/UI (display_mode.h,
    // display_mode_ui.cpp) — unchanged logic, just a new entry point; while
    // this screen is active, DisplayMode's OWN editMode (SELECTING/EDITING)
    // governs sub-navigation, not this struct's `state`.
    enum Screen { MAIN = 0, DATETIME = 1, DISPLAY_SETTINGS = 2 } screen = MAIN;
    uint8_t selectedIndex = 0;
    // MAIN: 0=Date&Time  1=Speaker  2=Display  3=Info
    // DATETIME: 0=Year  1=Month  2=Day  3=Hour  4=Minute
};

// ============ RTC CALLBACKS ============
// Set before calling readFromRTC() / applyToRTC()
typedef void (*SettingRTCReadFn)(SettingDateTime& dt);
typedef void (*SettingRTCWriteFn)(const SettingDateTime& dt);

// ============ SETTING CLASS ============
class Setting {
public:
    SettingConfig   config;
    SettingDateTime dateTime;
    SettingEditMode editMode;

    Setting() = default;

    void init();
    void loadConfig();
    void saveConfig();

    // RTC I/O
    static void setRTCCallbacks(SettingRTCReadFn readFn, SettingRTCWriteFn writeFn);
    void readFromRTC();
    void applyToRTC();    // write to RTC chip + update ESP32 system time

    // UI interaction
    void handleEncoderRotate(int delta);
    void handleButtonPress();
    void handleButtonLongPress();

    static uint8_t daysInMonth(uint16_t year, uint8_t month);

private:
    static SettingRTCReadFn  s_readFn;
    static SettingRTCWriteFn s_writeFn;
    void clampDateTime();
};

extern Setting setting;
