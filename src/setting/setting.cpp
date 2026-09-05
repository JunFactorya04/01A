/**
 * @file setting.cpp
 * @brief System Settings implementation
 * @date 2026-07-04
 */

#include "setting.h"
#include "../common/hardware_config.h"
#include "../display_mode/display_mode.h"   // DISPLAY sub-screen delegates to this, unchanged
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
        if (editMode.screen == SettingEditMode::DATETIME) {
            // DATETIME: 0-4 (Year, Month, Day, Hour, Minute)
            int newIdx = (int)editMode.selectedIndex + delta;
            if (newIdx >= 0 && newIdx <= 4) editMode.selectedIndex = (uint8_t)newIdx;
        } else {
            // MAIN: 0-6 (Date&Time, Speaker, Brightness, Power Save, Theme,
            // Rotation, Info) — list scrolls in setting_ui.cpp
            int newIdx = (int)editMode.selectedIndex + delta;
            if (newIdx >= 0 && newIdx <= 6) editMode.selectedIndex = (uint8_t)newIdx;
        }

    } else if (editMode.state == SettingEditMode::EDITING) {
        if (editMode.screen == SettingEditMode::MAIN &&
            editMode.selectedIndex >= 2 && editMode.selectedIndex <= 5) {
            // Brightness/Power Save/Theme/Rotation: reuse DisplayMode's own
            // per-field stepping (brightness +-10, power-save preset table,
            // theme wrap, rotation toggle) instead of duplicating it here.
            // DisplayMode's index is 0-3 (subtract Setting's own +2 offset);
            // forcing its state to EDITING makes it run that stepping
            // switch rather than its own (unused, from here) row navigation.
            displayMode.editMode.selectedIndex = editMode.selectedIndex - 2;
            displayMode.editMode.state = DisplayEditMode::EDITING;
            displayMode.handleEncoderRotate(delta);
            return;
        }

        // DATETIME screen fields
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
        }
    }
}

// ============ BUTTON ============
void Setting::handleButtonPress() {
    if (editMode.state == SettingEditMode::SELECTING) {
        if (editMode.screen == SettingEditMode::MAIN) {
            // MAIN screen
            if (editMode.selectedIndex == 0) {
                // Date & Time: enter DATETIME sub-screen
                editMode.screen = SettingEditMode::DATETIME;
                editMode.selectedIndex = 0;
            } else if (editMode.selectedIndex == 1) {
                // Speaker: instant toggle, no EDITING mode
                config.speakerEnabled = !config.speakerEnabled;
                g_speakerEnabled = config.speakerEnabled;
                saveConfig();
            } else if (editMode.selectedIndex >= 2 && editMode.selectedIndex <= 5) {
                // Brightness / Power Save / Theme / Rotation: enter EDITING.
                // Sync DisplayMode's index so the very first encoder tick
                // (before any further sync in handleEncoderRotate) already
                // points at the right field.
                editMode.state = SettingEditMode::EDITING;
                displayMode.editMode.selectedIndex = editMode.selectedIndex - 2;
                displayMode.editMode.state = DisplayEditMode::EDITING;
            } else if (editMode.selectedIndex == 6) {
                // Info screen
                editMode.state = SettingEditMode::SHOWING_INFO;
            }
        } else {
            // DATETIME screen: enter EDITING mode
            editMode.state = SettingEditMode::EDITING;
        }
    } else if (editMode.state == SettingEditMode::EDITING) {
        if (editMode.screen == SettingEditMode::MAIN &&
            editMode.selectedIndex >= 2 && editMode.selectedIndex <= 5) {
            // Brightness/Power Save/Theme/Rotation: just stop editing this
            // field (live-apply already happened per tick in DisplayMode's
            // own handler). Persistence is deferred to mode exit, matching
            // DisplayMode's original "thoát ra thì tự lưu" rule exactly.
            editMode.state = SettingEditMode::SELECTING;
            return;
        }
        // DATETIME editing: return to SELECTING
        clampDateTime();
        editMode.state = SettingEditMode::SELECTING;
    } else if (editMode.state == SettingEditMode::SHOWING_INFO) {
        // Any press returns to MAIN selecting
        editMode.state = SettingEditMode::SELECTING;
        editMode.screen = SettingEditMode::MAIN;
        editMode.selectedIndex = 6;  // back on Info row
    }
}

void Setting::handleButtonLongPress() {
    // Persist any Brightness/Power Save/Theme/Rotation change regardless of
    // which screen we're leaving from: FactoryTest::handleSettingButtonLongPress()
    // always exits Setting mode on any long press (pre-existing behavior,
    // unchanged here), so this is effectively always "on exit" — the same
    // "thoát ra thì tự lưu" rule DisplayMode always applied as its own mode.
    displayMode.saveConfig();
    displayMode.applyBrightness();
    displayMode.applyTheme();
    displayMode.applyRotation();

    if (editMode.screen == SettingEditMode::DATETIME) {
        // DATETIME screen: long press returns to MAIN
        clampDateTime();
        applyToRTC();
        editMode.screen = SettingEditMode::MAIN;
        editMode.state = SettingEditMode::SELECTING;
        editMode.selectedIndex = 0;  // back on Date&Time row
    } else {
        // MAIN screen: long press = SAVE + exit
        clampDateTime();
        applyToRTC();
        editMode.state = SettingEditMode::IDLE;
    }
}
