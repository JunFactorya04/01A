/**
 * @file timelapse_ui.cpp
 * @brief GEOPIX Timelapse UI rendering with LovyanGFX
 *
 * Two screens, same pattern as Auto Shoot's Advance / Trigger Mode's
 * Bluetooth split:
 *   MAIN     — video calculator (default): Shoot Duration, Video Length,
 *              Video FPS, Advance (opens submenu below), START/PAUSE/RESUME.
 *              Bottom status box shows a live realtime countdown + progress
 *              bar (next shot, or BULB exposure remaining).
 *   ADVANCE  — direct Interval / Total Shots entry, plus Bulb Mode ON/OFF
 *              and Exposure seconds for milky way / astro long exposures.
 */

#include "timelapse.h"
#include "../factory_test/factory_test.h"
#include "../common/ui_theme.h"   // themed palette
#include "../sleep_week/sleep_week_ui.h"
#include <smooth_ui_toolkit.h>

extern FactoryTest* _ft;

// ============ FORWARD DECLARATIONS ============
void renderTimelapseHeader();
void renderTimelapseMainPanel();
void renderTimelapseItem(uint8_t index, const char* label, const char* valueStr);
void renderTimelapseAdvanceRow(int y);
void renderTimelapseStatusPanel();
void renderTimelapseControlButton();
void renderTimelapseAdvanceScreen();
void renderAdvanceToggleRow(uint8_t index, const char* label, const char* valueStr, uint16_t valColor, int y);

// ============ UI CONSTANTS ============
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135

// Colors
#define COLOR_BG UI_BG          // themed
#define COLOR_TEXT UI_FG        // themed
#define COLOR_GREEN 0x07E0     // Green
#define COLOR_RED 0xF800       // Red
#define COLOR_YELLOW 0xFFE0    // Yellow
#define COLOR_ORANGE 0xFD20    // Orange (bulb exposure)
#define COLOR_BLUE 0x001F      // Blue
#define COLOR_BORDER UI_BORDER  // themed
#define COLOR_HIGHLIGHT UI_AL   // themed

// Layout (GEOPIX UI standard)
#define ITEM_HEIGHT 20
#define ITEM_Y_START 27
#define ITEM_INDENT 16

// ============ UI STATE ============
static unsigned long timelapseBlinkTimer = 0;
static bool timelapseBlinkState = false;

// ============ HELPER FUNCTIONS ============
void updateTimelapseBlink() {
    if (millis() - timelapseBlinkTimer > 500) {
        timelapseBlinkState = !timelapseBlinkState;
        timelapseBlinkTimer = millis();
    }
}

// Format a millisecond duration (used by the Advance screen's Interval row)
const char* formatInterval(int ms) {
    static char buffer[16];
    if (ms >= 60000) {
        int minutes = ms / 60000;
        int seconds = (ms % 60000) / 1000;
        snprintf(buffer, sizeof(buffer), "%dm%ds", minutes, seconds);
    } else if (ms >= 1000) {
        snprintf(buffer, sizeof(buffer), "%ds", ms / 1000);
    } else {
        snprintf(buffer, sizeof(buffer), "%dms", ms);
    }
    return buffer;
}

// Format a whole number of seconds compactly (Shoot Duration, Video Length,
// countdown text). sec < 0 means infinite.
const char* formatSeconds(long sec) {
    static char buffer[20];
    if (sec < 0) {
        snprintf(buffer, sizeof(buffer), "INF");
    } else if (sec >= 3600) {
        long h = sec / 3600, m = (sec % 3600) / 60;
        snprintf(buffer, sizeof(buffer), "%ldh%02ldm", h, m);
    } else if (sec >= 60) {
        long m = sec / 60, s = sec % 60;
        snprintf(buffer, sizeof(buffer), "%ldm%02lds", m, s);
    } else {
        snprintf(buffer, sizeof(buffer), "%lds", sec);
    }
    return buffer;
}

// ============ RENDER FUNCTIONS ============
void renderTimelapseUI() {
    if (!_ft) return;

    updateTimelapseBlink();

    // Clear screen
    _ft->_canvas->fillScreen(COLOR_BG);

    if (timelapse.inAdvanceScreen()) {
        renderTimelapseAdvanceScreen();
    } else {
        renderTimelapseHeader();
        renderTimelapseMainPanel();
        renderTimelapseStatusPanel();
        renderTimelapseControlButton();
    }

    // Scheduler countdown popup
    if (schedulerPopupActive()) schedulerPopupDraw();

    // Push to display
    _ft->_canvas_update();
}

// ============ HEADER ============
void renderTimelapseHeader() {
    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->drawString("TIMELAPSE", SCREEN_WIDTH / 2, 2);

    // Back button
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("<", 5, 2);
}

// ============ MAIN PANEL (video calculator) ============
void renderTimelapseMainPanel() {
    _ft->_canvas->drawRoundRect(8, 22, 224, 88, 5, COLOR_BORDER);
    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_left);

    // Row 0: Shoot Duration — how long the whole sequence will run for
    renderTimelapseItem(0, "Duration", formatSeconds(timelapse.getEstimatedDurationSec()));

    // Row 1: Video Length — how long the resulting video will play for
    {
        float vlen = timelapse.getVideoLengthSec();
        char buf[20];
        if (vlen < 0.0f) snprintf(buf, sizeof(buf), "INF");
        else             snprintf(buf, sizeof(buf), "%s", formatSeconds((long)(vlen + 0.5f)));
        renderTimelapseItem(1, "Video", buf);
    }

    // Row 2: Video FPS
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%dfps", timelapse.getVideoFps());
        renderTimelapseItem(2, "Video FPS", buf);
    }

    // Row 3: Advance — opens Interval/Total Shots/Bulb submenu, shows live
    // shot progress so you don't have to open it just to check progress.
    renderTimelapseAdvanceRow(ITEM_Y_START + ITEM_HEIGHT * 3);
}

// Generic MAIN-screen editable row (Shoot Duration / Video Length / Video FPS)
void renderTimelapseItem(uint8_t index, const char* label, const char* valueStr) {
    int y = ITEM_Y_START + (index * ITEM_HEIGHT);
    bool isSelected = (timelapse.editMode.selectedIndex == index);
    bool isEditing = (timelapse.editMode.state == TimelapseEditMode::EDITING && isSelected);

    if (isSelected) {
        _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3,
                                   isEditing ? COLOR_HIGHLIGHT : COLOR_BORDER);
    }

    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->drawString(label, ITEM_INDENT, y);

    _ft->_canvas->setTextDatum(top_right);
    if (isEditing && timelapseBlinkState) {
        _ft->_canvas->setTextColor(COLOR_BG);
    } else {
        _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_GREEN);
    }
    _ft->_canvas->drawString(valueStr, 210, y);

    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString(">", 226, y);

    _ft->_canvas->setTextDatum(top_left);
}

// "Advance" row — opens the submenu; value shows live shot progress.
void renderTimelapseAdvanceRow(int y) {
    bool isSelected = (timelapse.editMode.selectedIndex == 3);

    if (isSelected) {
        _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3, COLOR_BORDER);
    }

    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->drawString("Advance", ITEM_INDENT, y);

    char buf[24];
    int remaining = timelapse.getRemainingShots();
    if (remaining == -1) snprintf(buf, sizeof(buf), "%d (INF)", timelapse.getShotCount());
    else                 snprintf(buf, sizeof(buf), "%d/%d", timelapse.getShotCount(), timelapse.config.totalShots);

    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString(buf, 210, y);

    _ft->_canvas->setTextColor(isSelected ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString(">", 226, y);

    _ft->_canvas->setTextDatum(top_left);
}

// ============ STATUS PANEL — realtime countdown + progress bar ============
void renderTimelapseStatusPanel() {
    int y = 113;
    const int boxW = 128;

    _ft->_canvas->drawRoundRect(8, y, boxW, 20, 5, COLOR_BORDER);

    const char* status = timelapse.getStatusString();

    uint16_t statusColor = COLOR_TEXT;
    if (strcmp(status, "RUNNING") == 0) statusColor = COLOR_GREEN;
    else if (strcmp(status, "PAUSED") == 0) statusColor = COLOR_YELLOW;
    else if (strcmp(status, "BULB") == 0) statusColor = COLOR_ORANGE;
    else if (strcmp(status, "DONE") == 0) statusColor = COLOR_GREEN;

    // Line 1: status word (left) + realtime countdown (right)
    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(statusColor);
    _ft->_canvas->drawString(status, 14, y + 2);

    char cdBuf[20];
    if (timelapse.state.isExposing) {
        unsigned long ms = timelapse.getBulbTimeRemaining();
        snprintf(cdBuf, sizeof(cdBuf), "%lus", (unsigned long)((ms + 999) / 1000));
    } else if (timelapse.state.isRunning) {
        unsigned long ms = timelapse.getTimeUntilNextShot();
        snprintf(cdBuf, sizeof(cdBuf), "next %lus", (unsigned long)((ms + 999) / 1000));
    } else {
        snprintf(cdBuf, sizeof(cdBuf), "--");
    }
    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString(cdBuf, boxW, y + 2);
    _ft->_canvas->setTextDatum(top_left);

    // Line 2: progress bar — fills as the wait/exposure elapses. Orange
    // while actively holding a bulb exposure, green while waiting out a
    // normal interval, empty otherwise (paused/idle/done).
    const int barX = 14, barY = y + 13, barW = boxW - 20, barH = 5;
    _ft->_canvas->drawRect(barX, barY, barW, barH, COLOR_BORDER);

    float frac = 0.0f;
    uint16_t barColor = COLOR_BORDER;
    if (timelapse.state.isExposing) {
        unsigned long totalMs = (unsigned long)timelapse.config.bulbExposureSec * 1000UL;
        unsigned long remain  = timelapse.getBulbTimeRemaining();
        if (totalMs > 0) frac = 1.0f - ((float)remain / (float)totalMs);
        barColor = COLOR_ORANGE;
    } else if (timelapse.state.isRunning) {
        unsigned long totalMs = (unsigned long)timelapse.config.intervalMs;
        unsigned long remain  = timelapse.getTimeUntilNextShot();
        if (totalMs > 0) frac = 1.0f - ((float)remain / (float)totalMs);
        barColor = COLOR_HIGHLIGHT;
    }
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    int fillW = (int)(frac * (barW - 2));
    if (fillW > 0) _ft->_canvas->fillRect(barX + 1, barY + 1, fillW, barH - 2, barColor);
}

// ============ CONTROL BUTTON (START / PAUSE / RESUME) ============
void renderTimelapseControlButton() {
    int btnY = 113;
    bool isSel = (timelapse.editMode.selectedIndex == 4);

    const char* label = timelapse.getControlLabel();          // START / PAUSE / RESUME
    uint16_t accent = timelapse.isRunning() ? COLOR_RED : COLOR_GREEN;

    if (isSel) {
        _ft->_canvas->fillRoundRect(142, btnY, 90, 20, 5, COLOR_HIGHLIGHT);
    } else {
        _ft->_canvas->drawRoundRect(142, btnY, 90, 20, 5, accent);
    }

    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(isSel ? COLOR_BG : accent);
    _ft->_canvas->drawString(label, 187, btnY + 5);
    _ft->_canvas->setTextDatum(top_left);
}

// ============ ADVANCE SCREEN ============
void renderTimelapseAdvanceScreen() {
    _ft->_canvas->setFont(&fonts::efontCN_16);
    _ft->_canvas->setTextDatum(top_center);
    _ft->_canvas->setTextColor(COLOR_GREEN);
    _ft->_canvas->drawString("ADVANCE", SCREEN_WIDTH / 2, 2);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_TEXT);
    _ft->_canvas->drawString("<", 5, 2);

    _ft->_canvas->drawRoundRect(8, 22, 224, 88, 5, COLOR_BORDER);
    _ft->_canvas->setFont(&fonts::efontCN_16);

    bool bulbOn = timelapse.config.bulbEnabled;

    // Row 0: Interval
    {
        bool isSel  = (timelapse.editMode.advanceIndex == 0);
        bool isEdit = (timelapse.editMode.state == TimelapseEditMode::EDITING && isSel);
        int y = ITEM_Y_START;
        if (isSel) _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3, isEdit ? COLOR_HIGHLIGHT : COLOR_BORDER);
        _ft->_canvas->setTextDatum(top_left);
        _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);
        _ft->_canvas->drawString("Interval", ITEM_INDENT, y);
        _ft->_canvas->setTextDatum(top_right);
        if (isEdit && timelapseBlinkState) _ft->_canvas->setTextColor(COLOR_BG);
        else _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_GREEN);
        _ft->_canvas->drawString(formatInterval(timelapse.config.intervalMs), 210, y);
        _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_GREEN);
        _ft->_canvas->drawString(">", 226, y);
        _ft->_canvas->setTextDatum(top_left);
    }

    // Row 1: Total Shots
    {
        bool isSel  = (timelapse.editMode.advanceIndex == 1);
        bool isEdit = (timelapse.editMode.state == TimelapseEditMode::EDITING && isSel);
        int y = ITEM_Y_START + ITEM_HEIGHT;
        if (isSel) _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3, isEdit ? COLOR_HIGHLIGHT : COLOR_BORDER);
        _ft->_canvas->setTextDatum(top_left);
        _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);
        _ft->_canvas->drawString("Total Shots", ITEM_INDENT, y);
        char buf[16];
        if (timelapse.config.totalShots == 0) snprintf(buf, sizeof(buf), "INF");
        else snprintf(buf, sizeof(buf), "%d", timelapse.config.totalShots);
        _ft->_canvas->setTextDatum(top_right);
        if (isEdit && timelapseBlinkState) _ft->_canvas->setTextColor(COLOR_BG);
        else _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_GREEN);
        _ft->_canvas->drawString(buf, 210, y);
        _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_GREEN);
        _ft->_canvas->drawString(">", 226, y);
        _ft->_canvas->setTextDatum(top_left);
    }

    // Row 2: Bulb Mode ON/OFF
    renderAdvanceToggleRow(2, "Bulb Mode", bulbOn ? "ON" : "OFF", bulbOn ? COLOR_GREEN : COLOR_RED,
                           ITEM_Y_START + ITEM_HEIGHT * 2);

    // Row 3: Exposure (only meaningful when Bulb is ON — dimmed label
    // otherwise, still editable so it can be set up in advance)
    {
        bool isSel  = (timelapse.editMode.advanceIndex == 3);
        bool isEdit = (timelapse.editMode.state == TimelapseEditMode::EDITING && isSel);
        int y = ITEM_Y_START + ITEM_HEIGHT * 3;
        if (isSel) _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3, isEdit ? COLOR_HIGHLIGHT : COLOR_BORDER);
        _ft->_canvas->setTextDatum(top_left);
        _ft->_canvas->setTextColor(isSel ? COLOR_BG : (bulbOn ? COLOR_TEXT : COLOR_BORDER));
        _ft->_canvas->drawString("Exposure", ITEM_INDENT, y);
        char buf[16];
        snprintf(buf, sizeof(buf), "%ds", timelapse.config.bulbExposureSec);
        _ft->_canvas->setTextDatum(top_right);
        if (isEdit && timelapseBlinkState) _ft->_canvas->setTextColor(COLOR_BG);
        else _ft->_canvas->setTextColor(isSel ? COLOR_BG : (bulbOn ? COLOR_ORANGE : COLOR_BORDER));
        _ft->_canvas->drawString(buf, 210, y);
        _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_GREEN);
        _ft->_canvas->drawString(">", 226, y);
        _ft->_canvas->setTextDatum(top_left);
    }

    // Footer hint
    _ft->_canvas->setFont(&fonts::efontCN_10);
    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(COLOR_BORDER);
    if (bulbOn) {
        // Interval is the REST time AFTER each exposure completes (not
        // overlapping with it) -- spell that out here since it's easy to
        // assume Interval still means "time between shot starts".
        char buf[48];
        snprintf(buf, sizeof(buf), "%ds expose + %s rest/shot",
                 timelapse.config.bulbExposureSec, formatInterval(timelapse.config.intervalMs));
        _ft->_canvas->drawString(buf, 12, 113);
    } else {
        _ft->_canvas->drawString("Bulb OFF: quick trigger pulse", 12, 113);
    }
}

void renderAdvanceToggleRow(uint8_t index, const char* label, const char* valueStr,
                            uint16_t valColor, int y) {
    bool isSel = (timelapse.editMode.advanceIndex == index);

    if (isSel) {
        _ft->_canvas->fillRoundRect(10, y - 1, 220, 18, 3, COLOR_BORDER);
    }

    _ft->_canvas->setTextDatum(top_left);
    _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);
    _ft->_canvas->drawString(label, ITEM_INDENT, y);

    _ft->_canvas->setTextDatum(top_right);
    _ft->_canvas->setTextColor(isSel ? COLOR_BG : valColor);
    _ft->_canvas->drawString(valueStr, 210, y);

    _ft->_canvas->setTextColor(isSel ? COLOR_BG : COLOR_GREEN);
    _ft->_canvas->drawString(">", 226, y);

    _ft->_canvas->setTextDatum(top_left);
}

// ============ INTERACTION HANDLER ============
void handleTimelapseInput() {
    // Called from FactoryTest::handleTimelapseInput() — encoder/button
    // handled there via Timelapse's own methods.
}

// ============ INIT ============
void initTimelapseUI() {
    timelapse.editMode.state = TimelapseEditMode::SELECTING;
    timelapse.editMode.screen = TimelapseEditMode::MAIN;
    timelapse.editMode.selectedIndex = 0;
    timelapse.editMode.advanceIndex = 0;
}
