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
    uint8_t selectedIndex = 0;
    // 0=Year  1=Month  2=Day  3=Hour  4=Minute  5=Speaker  6=Info
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
