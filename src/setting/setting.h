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
    enum Screen { MAIN = 0, DATETIME = 1 } screen = MAIN;
    uint8_t selectedIndex = 0;
    // MAIN (7 rows, list scrolls — see setting_ui.cpp's windowing, same
    // pattern as the OTA update WiFi/release lists):
    //   0=Date&Time  1=Speaker  2=Brightness  3=Power Save  4=Theme
    //   5=Rotation   6=Info
    // Indices 2-5 are Brightness/Power Save/Theme/Rotation, moved here from
    // the old standalone DISPLAY mode (now MULTI BOX, see
    // factory_test_multi_box.cpp) as flat rows, not a submenu. Their VALUE
    // ADJUSTMENT (encoder-drag behavior + live apply) is delegated to the
    // existing, unchanged DisplayMode class (display_mode.h/.cpp) — this
    // struct's own `state`/`selectedIndex` still own all navigation, only
    // the per-field stepping logic is reused so it doesn't get duplicated
    // (and can't drift from DisplayMode's own copy of that logic).
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
