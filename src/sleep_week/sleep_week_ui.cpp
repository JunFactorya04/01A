/**
 * @file sleep_week_ui.cpp
 * @brief Sleep + Weekly Scheduler UI
 * @date 2026-07-24
 *
 * Follows GEOPIX UI STANDARD (see display_mode_ui.cpp):
 * themed header + rounded panel y=22 + inverse selection rows,
 * bottom status panel y=113 h=20, no footer captions.
 */

#include "sleep_week_scheduler.h"
#include "sleep_week_ui.h"
#include "../factory_test/factory_test.h"
#include "../common/ui_theme.h"   // themed palette
#include <smooth_ui_toolkit.h>
#include <time.h>

extern FactoryTest* _ft;

// ============ FORWARD DECLARATIONS ============
void renderSleepWeekHeader();
void renderDays();
void renderTimeSettings();
void renderModeSelection();
void renderStatus();

// ============ UI CONSTANTS ============
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135

// Colors
#define COLOR_BG UI_BG
#define COLOR_TEXT UI_FG
#define COLOR_GREEN 0x07E0
#define COLOR_RED 0xF800
#define COLOR_BLUE 0x001F
#define COLOR_BORDER UI_BORDER
#define COLOR_HIGHLIGHT UI_AL

// Layout (GEOPIX UI standard: panel y=22, items inside)
#define DAY_HEIGHT 18
#define DAY_Y_START 28

// Day names
const char* DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// ============ RENDER FUNCTIONS ============
void renderSleepWeekUI() {
    if (!_ft) return;
    
    // Clear screen
    _ft->_canvas->fillScreen(COLOR_BG);
    
    // Header
    renderSleepWeekHeader();
    
    // Days
    renderDays();
    
    // Time settings
    renderTimeSettings();
    
    // Mode selection
    renderModeSelection();
    
    // Status
    renderStatus();
    
    // Scheduler countdown popup (if within 5s of an event)
    if (schedulerPopupActive()) schedulerPopupDraw();
    
    // Push to display
    _ft->_canvas_update();
}

// ============ HEADER ============
void renderSleepWeekHeader() {
    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->drawString("SLEEP & WAKE", SCREEN_WIDTH / 2, 2);
    
    // Back button
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("<", 5, 2);

    // Real-time clock (HH:MM) + date (DD/MM) top-right
    time_t now = time(nullptr);
    struct tm* lt = (now > 0) ? localtime(&now) : nullptr;
    if (lt) {
        char hm[8];
        char dt[8];
        snprintf(hm, sizeof(hm), "%02d:%02d", lt->tm_hour, lt->tm_min);
        snprintf(dt, sizeof(dt), "%02d/%02d", lt->tm_mday, lt->tm_mon + 1);
        _ft->_canvas->setFont(&fonts::efontCN_10);
        _ft->_canvas->setTextDatum(top_right);
        _ft->_canvas->setTextColor(COLOR_GREEN);
        _ft->_canvas->drawString(hm, 236, 1);
        _ft->_canvas->setTextColor(COLOR_TEXT);
        _ft->_canvas->drawString(dt, 236, 12);
        _ft->_canvas->setTextDatum(top_left);
    }
}

// ============ DAYS ============
void renderDays() {
    // Settings panel (GEOPIX standard: y=22, gap below header)
    _ft->_canvas->drawRoundRect(8, 22, 224, 88, 5, COLOR_BORDER);

    _ft->_canvas->setFont(&fonts::efontCN_12);
    _ft->_canvas->setTextDatum(top_center);

    for (int i = 0; i < 7; i++) {
        int x = 12 + (i * 31);
        int y = DAY_Y_START;

        bool isSelected = (sleepWeekScheduler.editMode.selectedIndex == i);
        bool isEnabled = sleepWeekScheduler.getDayEnabled(i);

        // Background highlight (inverse selection, standard)
        if (isSelected) {
            _ft->_canvas->fillRoundRect(x, y - 2, 29, 26, 3, COLOR_HIGHLIGHT);
        }

        // Day name (centered in cell)
        _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);
        _ft->_canvas->drawString(DAY_NAMES[i], x + 14, y);

        // ON/OFF indicator
        _ft->_canvas->setFont(&fonts::efontCN_10);
        _ft->_canvas->setTextColor(isSelected ? COLOR_BG
                                              : (isEnabled ? COLOR_GREEN : COLOR_RED));
        _ft->_canvas->drawString(isEnabled ? "ON" : "OFF", x + 14, y + 13);
        _ft->_canvas->setFont(&fonts::efontCN_12);
    }
    _ft->_canvas->setTextDatum(top_left);
}

// ============ TIME SETTINGS ============
void renderTimeSettings() {
    int y = 61;

    bool sleepSel  = (sleepWeekScheduler.editMode.selectedIndex == 7);
    bool wakeSel   = (sleepWeekScheduler.editMode.selectedIndex == 8);
    bool sleepEdit = (sleepSel && sleepWeekScheduler.editMode.state == SleepWeekEditMode::SELECTING_TIME);
    bool wakeEdit  = (wakeSel  && sleepWeekScheduler.editMode.state == SleepWeekEditMode::SELECTING_TIME);

    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_left);

    // Sleep time highlight background
    if (sleepSel) {
        _ft->_canvas->fillRoundRect(10, y - 1, 108, 18, 3, sleepEdit ? COLOR_HIGHLIGHT : COLOR_BORDER);
    }
    // Wake time highlight background
    if (wakeSel) {
        _ft->_canvas->fillRoundRect(122, y - 1, 108, 18, 3, wakeEdit ? COLOR_HIGHLIGHT : COLOR_BORDER);
    }

    // Sleep label & value
    _ft->_canvas->setTextColor(sleepSel ? COLOR_BG : COLOR_TEXT);
    _ft->_canvas->drawString("Sleep", 16, y);
    char sleepStr[16];
    snprintf(sleepStr, sizeof(sleepStr), "%02d:%02d",
             sleepWeekScheduler.config.timeConfig.sleepHour,
             sleepWeekScheduler.config.timeConfig.sleepMinute);
    _ft->_canvas->setTextColor(sleepSel ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString(sleepStr, 68, y);

    // Wake label & value
    _ft->_canvas->setTextColor(wakeSel ? COLOR_BG : COLOR_TEXT);
    _ft->_canvas->drawString("Wake", 128, y);
    char wakeStr[16];
    snprintf(wakeStr, sizeof(wakeStr), "%02d:%02d",
             sleepWeekScheduler.config.timeConfig.wakeHour,
             sleepWeekScheduler.config.timeConfig.wakeMinute);
    _ft->_canvas->setTextColor(wakeSel ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString(wakeStr, 178, y);
}

// ============ MODE SELECTION ============
void renderModeSelection() {
    int y = 86;
    bool modeSel  = (sleepWeekScheduler.editMode.selectedIndex == 9);
    bool modeEdit = (sleepWeekScheduler.editMode.state == SleepWeekEditMode::SELECTING_MODE);

    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_left);

    if (modeSel || modeEdit) {
        _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3, modeEdit ? COLOR_HIGHLIGHT : COLOR_BORDER);
    }

    _ft->_canvas->setTextColor((modeSel || modeEdit) ? COLOR_BG : COLOR_TEXT);
    _ft->_canvas->drawString("Mode", 16, y);

    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor((modeSel || modeEdit) ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString(sleepWeekScheduler.getModeString(), 210, y);

    // Arrow (standard)
    _ft->_canvas->setTextColor((modeSel || modeEdit) ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString(">", 226, y);
    _ft->_canvas->setTextDatum(top_left);
}

// ============ STATUS ============
void renderStatus() {
    int y = 113;
    bool enableSel = (sleepWeekScheduler.editMode.selectedIndex == 10);

    // Bottom status panel (GEOPIX standard y=113 h=20)
    if (enableSel) {
        _ft->_canvas->fillRoundRect(8, y, 224, 20, 5, COLOR_BORDER);
    } else {
        _ft->_canvas->drawRoundRect(8, y, 224, 20, 5, COLOR_BORDER);
    }

    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_left);

    // Scheduler on/off label (inverse when selected for toggling)
    _ft->_canvas->setTextColor(enableSel ? COLOR_BG : COLOR_TEXT);
    _ft->_canvas->drawString("Scheduler:", 14, y + 5);
    _ft->_canvas->setTextColor(sleepWeekScheduler.isSchedulerEnabled() ? COLOR_GREEN : COLOR_RED);
    _ft->_canvas->drawString(sleepWeekScheduler.isSchedulerEnabled() ? "ON" : "OFF", 78, y + 5);

    // Sleep status
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(sleepWeekScheduler.isSleeping() ? COLOR_RED
                               : (enableSel ? COLOR_BG : COLOR_TEXT));
    _ft->_canvas->drawString(sleepWeekScheduler.isSleeping() ? "SLEEP" : "AWAKE", 226, y + 5);

    _ft->_canvas->setTextDatum(top_left);
}

// ============ INIT ============
void initSleepWeekUI() {
    sleepWeekScheduler.editMode.state = SleepWeekEditMode::SELECTING_DAY;
    sleepWeekScheduler.editMode.selectedIndex = 0;
}

// ============ GLOBAL SCHEDULER COUNTDOWN OVERLAY ============
bool schedulerPopupActive() {
    int s; const char* l;
    return sleepWeekScheduler.getCountdown(s, l);
}

void schedulerPopupDraw() {
    if (!_ft || !_ft->_canvas) return;
    int secs; const char* label;
    if (!sleepWeekScheduler.getCountdown(secs, label)) return;

    // Centered red popup box
    const int w = 170, h = 60;
    const int x = (SCREEN_WIDTH - w) / 2;
    const int y = (SCREEN_HEIGHT - h) / 2;

    _ft->_canvas->fillRoundRect(x, y, w, h, 8, COLOR_RED);
    _ft->_canvas->drawRoundRect(x, y, w, h, 8, COLOR_TEXT);

    char buf[32];
    snprintf(buf, sizeof(buf), "%s in %ds", label, secs);

    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->setFont(&fonts::efontCN_24);
    _ft->_canvas->drawString(buf, SCREEN_WIDTH / 2, y + 10);

    _ft->_canvas->setFont(&fonts::efontCN_12);
    _ft->_canvas->drawString("Scheduler event", SCREEN_WIDTH / 2, y + 40);
    _ft->_canvas->setTextDatum(top_left);
}
