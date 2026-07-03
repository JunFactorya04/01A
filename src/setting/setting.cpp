/**
 * @file setting.cpp
 * @brief System Settings implementation
 * @date 2026-07-04
 */

#include "setting.h"
#include "../common/hardware_config.h"
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>

// Global instance
Setting setting;

// Static members
SettingRTCReadFn  Setting::s_readFn  = nullptr;
SettingRTCWriteFn Setting::s_writeFn = nullptr;

// ============ HELPERS ============
static const uint8_t DAYS_TABLE[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};

uint8_t Setting::daysInMonth(uint16_t year, uint8_t month) {
    if (month < 1 || month > 12) return 30;
    if (month == 2) {
        bool leap = (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
        return leap ? 29 : 28;
    }
    return DAYS_TABLE[month];
}

// ============ CALLBACKS ============
void Setting::setRTCCallbacks(SettingRTCReadFn readFn, SettingRTCWriteFn writeFn) {
    s_readFn  = readFn;
    s_writeFn = writeFn;
}

// ============ INIT / CONFIG ============
void Setting::init() {
    loadConfig();
    g_speakerEnabled = config.speakerEnabled;
}

void Setting::loadConfig() {
    Preferences prefs;
    prefs.begin("settings");
    config.speakerEnabled = prefs.getBool("speaker", true);
    prefs.end();
}

void Setting::saveConfig() {
    Preferences prefs;
    prefs.begin("settings");
    prefs.putBool("speaker", config.speakerEnabled);
    prefs.end();
}

// ============ RTC I/O ============
void Setting::readFromRTC() {
    if (s_readFn) s_readFn(dateTime);
}

void Setting::applyToRTC() {
    // Compute weekDay and normalise via mktime
    struct tm ti = {};
    ti.tm_year  = dateTime.year - 1900;
    ti.tm_mon   = dateTime.month - 1;
    ti.tm_mday  = dateTime.day;
    ti.tm_hour  = dateTime.hour;
    ti.tm_min   = dateTime.minute;
    ti.tm_sec   = 0;
    ti.tm_isdst = -1;
    time_t t = mktime(&ti);

    dateTime.weekDay = (uint8_t)ti.tm_wday;
    dateTime.second  = 0;

    // Write to physical RTC chip
    if (s_writeFn) s_writeFn(dateTime);

    // Also update ESP32 system time so SLEEP&WEEK scheduler works immediately
    if (t > 0) {
        struct timeval tv = { t, 0 };
        settimeofday(&tv, nullptr);
    }
}

// ============ CLAMP ============
void Setting::clampDateTime() {
    if (dateTime.year   < 2000) dateTime.year  = 2000;
    if (dateTime.year   > 2099) dateTime.year  = 2099;
    if (dateTime.month  < 1)    dateTime.month = 1;
    if (dateTime.month  > 12)   dateTime.month = 12;
    uint8_t maxDay = daysInMonth(dateTime.year, dateTime.month);
    if (dateTime.day    < 1)      dateTime.day   = 1;
    if (dateTime.day    > maxDay) dateTime.day   = maxDay;
    if (dateTime.hour   > 23)   dateTime.hour  = 0;
    if (dateTime.minute > 59)   dateTime.minute = 0;
}

// ============ ENCODER ============
void Setting::handleEncoderRotate(int delta) {
    if (delta > 0) delta = 1;
    else if (delta < 0) delta = -1;

    if (editMode.state == SettingEditMode::SELECTING) {
        int newIdx = (int)editMode.selectedIndex + delta;
        if (newIdx >= 0 && newIdx <= 5) editMode.selectedIndex = (uint8_t)newIdx;

    } else if (editMode.state == SettingEditMode::EDITING) {
        switch (editMode.selectedIndex) {
            case 0: {   // Year: 2000-2099, wraps
                int y = (int)dateTime.year + delta;
                if (y < 2000) y = 2099;
                if (y > 2099) y = 2000;
                dateTime.year = (uint16_t)y;
                break;
            }
            case 1: {   // Month: 1-12, wraps
                int m = (int)dateTime.month + delta;
                if (m < 1)  m = 12;
                if (m > 12) m = 1;
                dateTime.month = (uint8_t)m;
                // clamp day in case new month is shorter
                uint8_t maxD = daysInMonth(dateTime.year, dateTime.month);
                if (dateTime.day > maxD) dateTime.day = maxD;
                break;
            }
            case 2: {   // Day: 1..daysInMonth, wraps
                uint8_t maxD = daysInMonth(dateTime.year, dateTime.month);
                int d = (int)dateTime.day + delta;
                if (d < 1)    d = maxD;
                if (d > maxD) d = 1;
                dateTime.day = (uint8_t)d;
                break;
            }
            case 3: {   // Hour: 0-23, wraps
                int h = (int)dateTime.hour + delta;
                if (h < 0)  h = 23;
                if (h > 23) h = 0;
                dateTime.hour = (uint8_t)h;
                break;
            }
            case 4: {   // Minute: 0-59, wraps
                int mn = (int)dateTime.minute + delta;
                if (mn < 0)  mn = 59;
                if (mn > 59) mn = 0;
                dateTime.minute = (uint8_t)mn;
                break;
            }
            case 5: {   // Speaker: encoder toggles too
                config.speakerEnabled = !config.speakerEnabled;
                g_speakerEnabled = config.speakerEnabled;
                saveConfig();
                break;
            }
        }
    }
}

// ============ BUTTON ============
void Setting::handleButtonPress() {
    if (editMode.state == SettingEditMode::SELECTING) {
        if (editMode.selectedIndex == 5) {
            // Speaker: instant toggle, no EDITING mode
            config.speakerEnabled = !config.speakerEnabled;
            g_speakerEnabled = config.speakerEnabled;
            saveConfig();
        } else {
            editMode.state = SettingEditMode::EDITING;
        }
    } else if (editMode.state == SettingEditMode::EDITING) {
        clampDateTime();
        editMode.state = SettingEditMode::SELECTING;
    }
}

void Setting::handleButtonLongPress() {
    // Long press = SAVE: write to RTC + update system time, then exit
    clampDateTime();
    applyToRTC();
    editMode.state = SettingEditMode::IDLE;
}
