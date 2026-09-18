/**
 * @file dev_mode_ui.cpp
 * @brief FOR DEVELOP rendering — follows the GEOPIX UI STANDARD
 *
 * Deliberately shows the same live sensor readout MULTI BOX's role screens do
 * (distance, baseline, threshold band, live marker): the whole point of the
 * bench harness is watching what MAIN's sensor sees while tuning the
 * threshold, which is exactly what cannot be judged from the numbers alone.
 */

#include "dev_mode_ui.h"
#include "dev_mode.h"
#include "../multi_box/multi_box.h"
#include "../multi_box/multi_box_controller.h"
#include "../trigger_mode/trigger_mode.h"
#include "../factory_test/factory_test.h"
#include "../common/ui_theme.h"
#include "../common/battery_ui.h"
#include "../auto_shoot/tf_luna.h"

extern FactoryTest* _ft;

#define COLOR_BG        UI_BG
#define COLOR_TEXT      UI_FG
#define COLOR_BORDER    UI_BORDER
#define COLOR_HIGHLIGHT UI_AL
#define COLOR_GREEN     0x07E0
#define COLOR_RED       0xF800
#define COLOR_YELLOW    0xFFE0
#define COLOR_ORANGE    0xFD20

#define ITEM_Y_START 27
#define ITEM_HEIGHT  20
#define VISIBLE_ROWS 4

void initDevModeUI() {
    devMode.editMode = DevModeEditMode();
}

// ---- shared bits ----
static void header(const char* title, const char* hint) {
    LGFX_Sprite* c = _ft->_canvas;
    c->setFont(&fonts::efontCN_16);
    c->setTextDatum(top_center);
    c->setTextColor(COLOR_HIGHLIGHT);
    c->drawString(title, 120, 2);

    c->setTextDatum(top_left);
    c->setTextColor(COLOR_TEXT);
    c->drawString("<", 5, 2);

    drawBatteryBadge(c);

    if (hint) {
        c->setFont(&fonts::efontCN_10);
        c->setTextDatum(top_right);
        c->setTextColor(COLOR_BORDER);
        c->drawString(hint, 235, 5);
    }
    c->setTextDatum(top_left);
}

static void infoRow(int y, const char* label, const char* value, uint16_t valColor) {
    LGFX_Sprite* c = _ft->_canvas;
    c->setFont(&fonts::efontCN_12);
    c->setTextDatum(top_left);
    c->setTextColor(COLOR_TEXT);
    c->drawString(label, 16, y);
    c->setTextDatum(top_right);
    c->setTextColor(valColor);
    c->drawString(value, 222, y);
    c->setTextDatum(top_left);
}

// Same bar MULTI BOX draws: fixed 0..TFLUNA_MAX_DISTANCE_M scale, baseline
// tick, threshold band, live marker.
static void sensorBar(int barX, int barY, int barW, int barH) {
    LGFX_Sprite* c = _ft->_canvas;
    const float span = TFLUNA_MAX_DISTANCE_M;
    auto distToX = [&](float d) -> int {
        if (d < 0.0f) d = 0.0f;
        if (d > span) d = span;
        return barX + (int)((d / span) * (float)(barW - 2)) + 1;
    };

    c->drawRect(barX, barY, barW, barH, COLOR_BORDER);

    if (multiBox.state.baselineCaptured) {
        float base = multiBox.state.baselineDistance;
        float thr  = multiBox.config.detectThresholdCm / 100.0f;
        int x0 = distToX(base - thr), x1 = distToX(base + thr);
        if (x1 > x0) c->fillRect(x0, barY + 1, x1 - x0, barH - 2, COLOR_BORDER);
        c->drawFastVLine(distToX(base), barY - 2, barH + 4, COLOR_HIGHLIGHT);
    }

    float live  = tfLuna.getDistance();
    bool  valid = tfLuna.hasObject();
    bool  firing = valid && multiBox.state.baselineCaptured &&
                   fabsf(live - multiBox.state.baselineDistance) * 100.0f >=
                       multiBox.config.detectThresholdCm;
    c->drawFastVLine(distToX(live), barY - 1, barH + 2,
                     !valid ? COLOR_RED : firing ? COLOR_GREEN : COLOR_TEXT);
}

// ---- MAIN ----
// Cycle bar, modelled on Timelapse's status panel: a phase word, a countdown,
// and a bar that fills as that phase elapses. Multi Box has one more phase
// than Timelapse (the End Delay between the finish-line detection and the
// shutter actually closing), so it gets its own colour rather than being
// folded into the exposure.
static void renderCycleBar(int y) {
    LGFX_Sprite* c = _ft->_canvas;

    const char*  phase = "STOPPED";
    uint16_t     col   = COLOR_BORDER;
    float        frac  = 0.0f;
    char         cd[16] = "--";

    if (multiBox.state.session == MultiBoxState::EXPOSING) {
        unsigned long elapsed = millis() - multiBox.state.sessionStartMs;

        bool waitingOut = multiBox.state.endRequested;
        if (waitingOut) {
            // Detected, holding for End Delay before the shutter closes.
            long left = (long)(multiBox.state.endDueAtMs - millis());
            if (left < 0) left = 0;
            phase = "DETECTED";
            col   = COLOR_YELLOW;
            snprintf(cd, sizeof(cd), "%.1fs", left / 1000.0f);
            uint32_t total = multiBox.config.endDelayMs;
            frac = total ? 1.0f - ((float)left / (float)total) : 1.0f;
        } else {
            unsigned long minMs = (unsigned long)multiBox.config.minBulbSec * 1000UL;
            if (elapsed < minMs) {
                // Inside the minimum: the sensor is ignored entirely, so say
                // so and count toward the moment it goes live. Showing this
                // as plain "BULB OPEN" made a deliberately ignored sweep look
                // like a missed one.
                phase = "MIN - SENSOR OFF";
                col   = COLOR_BORDER;
                snprintf(cd, sizeof(cd), "%.1fs", (minMs - elapsed) / 1000.0f);
                frac  = (float)elapsed / (float)minMs;
            } else {
                // Sensor live, counting toward the max-bulb cap.
                unsigned long total = (unsigned long)multiBox.config.maxBulbSec * 1000UL;
                phase = "BULB - WATCHING";
                col   = COLOR_ORANGE;
                snprintf(cd, sizeof(cd), "%lus", elapsed / 1000UL);
                frac  = total ? (float)elapsed / (float)total : 0.0f;
            }
        }
    } else if (multiBox.state.session == MultiBoxState::REARM) {
        unsigned long elapsed = millis() - multiBox.state.sessionStartMs;
        unsigned long total   = multiBox.config.rearmMs;
        phase = "REST";
        col   = COLOR_BORDER;
        long left = (long)total - (long)elapsed;
        if (left < 0) left = 0;
        snprintf(cd, sizeof(cd), "%.1fs", left / 1000.0f);
        frac = total ? (float)elapsed / (float)total : 1.0f;
    } else if (devMode.state.running) {
        unsigned long left  = devMode.timeUntilNextCycle();
        unsigned long total = devMode.config.intervalMs;
        phase = "WAIT INTERVAL";
        col   = COLOR_GREEN;
        snprintf(cd, sizeof(cd), "%.1fs", left / 1000.0f);
        frac = total ? 1.0f - ((float)left / (float)total) : 0.0f;
    }

    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    c->setFont(&fonts::efontCN_10);
    c->setTextDatum(top_left);
    c->setTextColor(col);
    c->drawString(phase, 16, y);
    c->setTextDatum(top_right);
    c->setTextColor(COLOR_TEXT);
    c->drawString(cd, 222, y);
    c->setTextDatum(top_left);

    const int barX = 16, barY = y + 12, barW = 206, barH = 6;
    c->drawRect(barX, barY, barW, barH, COLOR_BORDER);
    int fillW = (int)(frac * (barW - 2));
    if (fillW > 0) c->fillRect(barX + 1, barY + 1, fillW, barH - 2, col);
}

static void renderMain() {
    LGFX_Sprite* c = _ft->_canvas;
    header("FOR DEVELOP", "1 BOX BENCH");
    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);

    char buf[28];

    float live = tfLuna.getDistance();
    bool  valid = tfLuna.hasObject();
    if (valid) snprintf(buf, sizeof(buf), "%.2fm", live);
    else       snprintf(buf, sizeof(buf), "NO SIGNAL");
    infoRow(27, "DIST", buf, valid ? COLOR_TEXT : COLOR_RED);

    if (multiBox.state.baselineCaptured) {
        snprintf(buf, sizeof(buf), "%.2fm  %ucm",
                 multiBox.state.baselineDistance, multiBox.config.detectThresholdCm);
        infoRow(44, "BASE", buf, COLOR_TEXT);
    } else {
        infoRow(44, "BASE", "CALIBRATING", COLOR_YELLOW);
    }

    sensorBar(16, 62, 206, 8);

    // The OUT channel row lived here; it is a setting, visible on ADVANCE, and
    // the cycle bar earns the space better -- knowing where you are in the
    // cycle is what you watch on a bench, shot after shot.
    renderCycleBar(78);

    snprintf(buf, sizeof(buf), "C%lu  N%lu",
             (unsigned long)devMode.state.cycles,
             (unsigned long)multiBox.state.shotCount);
    c->setFont(&fonts::efontCN_10);
    c->setTextDatum(top_right);
    c->setTextColor(COLOR_BORDER);
    c->drawString(buf, 222, 98);
    c->setTextDatum(top_left);

    bool onRun = devMode.editMode.mainIndex == 0;
    const char* runLabel = devMode.state.running ? "STOP" : "START";

    c->fillRoundRect(8, 113, 110, 20, 4, onRun ? COLOR_HIGHLIGHT : COLOR_BG);
    c->drawRoundRect(8, 113, 110, 20, 4, COLOR_BORDER);
    c->setFont(&fonts::efontCN_12);
    c->setTextDatum(middle_center);
    c->setTextColor(onRun ? COLOR_BG : (devMode.state.running ? COLOR_RED : COLOR_GREEN));
    c->drawString(runLabel, 63, 123);

    c->fillRoundRect(122, 113, 110, 20, 4, !onRun ? COLOR_HIGHLIGHT : COLOR_BG);
    c->drawRoundRect(122, 113, 110, 20, 4, COLOR_BORDER);
    c->setTextColor(!onRun ? COLOR_BG : COLOR_TEXT);
    c->drawString("ADVANCE >", 177, 123);
    c->setTextDatum(top_left);
}

// ---- SETTINGS PAGES ----
// Split into three: the cycle itself on ADVANCE, sensor tuning and output
// channels on their own sub-pages. They are separate jobs done at separate
// times, and one nine-row list made both harder to find.
static void advLabelValue(DevRow row, char* label, char* value, size_t n) {
    switch (row) {
        case DevRow::SHOOT_MODE:
            snprintf(label, n, "Shoot");
            snprintf(value, n, "%s", devMode.config.singleShot ? "SINGLE" : "AUTO"); break;
        case DevRow::INTERVAL:
            snprintf(label, n, "Interval");
            if (devMode.config.intervalMs >= 1000)
                snprintf(value, n, "%.1fs", devMode.config.intervalMs / 1000.0f);
            else
                snprintf(value, n, "%lums", (unsigned long)devMode.config.intervalMs);
            break;
        case DevRow::MIN_BULB:
            snprintf(label, n, "Min Bulb");
            snprintf(value, n, "%us", multiBox.config.minBulbSec); break;
        case DevRow::MAX_BULB:
            snprintf(label, n, "Max Bulb");
            snprintf(value, n, "%us", multiBox.config.maxBulbSec); break;
        case DevRow::REST:
            snprintf(label, n, "Rest");
            if (multiBox.config.rearmMs >= 1000)
                snprintf(value, n, "%.1fs", multiBox.config.rearmMs / 1000.0f);
            else
                snprintf(value, n, "%ums", multiBox.config.rearmMs);
            break;
        case DevRow::END_DELAY:
            snprintf(label, n, "End Delay");
            if (multiBox.config.endDelayMs >= 1000)
                snprintf(value, n, "%.1fs", multiBox.config.endDelayMs / 1000.0f);
            else
                snprintf(value, n, "%ums", multiBox.config.endDelayMs);
            break;
        case DevRow::SENSOR_PAGE:
            snprintf(label, n, "Sensor"); snprintf(value, n, ">"); break;
        case DevRow::TRIGOUT_PAGE:
            snprintf(label, n, "Trigger Out"); snprintf(value, n, ">"); break;
        default:
            snprintf(label, n, "< BACK"); snprintf(value, n, " "); break;
    }
}

static void sensorLabelValue(DevSensorRow row, char* label, char* value, size_t n) {
    switch (row) {
        case DevSensorRow::DETECT:
            snprintf(label, n, "Detect");
            snprintf(value, n, "%ucm", multiBox.config.detectThresholdCm); break;
        case DevSensorRow::RANGE_ON:
            snprintf(label, n, "Range Filter");
            snprintf(value, n, "%s", multiBox.config.rangeFilterEnabled ? "ON" : "OFF"); break;
        case DevSensorRow::RANGE_MIN:
            snprintf(label, n, "Range Min");
            snprintf(value, n, "%.2fm", multiBox.config.rangeMinCm / 100.0f); break;
        case DevSensorRow::RANGE_MAX:
            snprintf(label, n, "Range Max");
            snprintf(value, n, "%.2fm", multiBox.config.rangeMaxCm / 100.0f); break;
        default:
            snprintf(label, n, "< BACK"); snprintf(value, n, " "); break;
    }
}

static void trigLabelValue(DevTrigRow row, char* label, char* value, size_t n) {
    switch (row) {
        case DevTrigRow::CH_TRIGGER:
            snprintf(label, n, "G2 (Trigger)");
            snprintf(value, n, "%s", triggerMode.config.triggerEnabled ? "ON" : "OFF"); break;
        case DevTrigRow::CH_REMOTE:
            snprintf(label, n, "G1 (Remote)");
            snprintf(value, n, "%s", triggerMode.config.remoteEnabled ? "ON" : "OFF"); break;
        case DevTrigRow::CH_BLE:
            snprintf(label, n, "Bluetooth");
            snprintf(value, n, "%s", triggerMode.config.bluetoothEnabled ? "ON" : "OFF"); break;
        default:
            snprintf(label, n, "< BACK"); snprintf(value, n, " "); break;
    }
}

static void renderRowList(const char* title, const char* hint,
                          uint8_t count, uint8_t sel, bool editing,
                          void (*fill)(uint8_t, char*, char*, size_t)) {
    LGFX_Sprite* c = _ft->_canvas;
    header(title, hint);
    c->drawRoundRect(8, 22, 224, 88, 5, COLOR_BORDER);

    uint8_t firstVisible = 0;
    if (sel >= VISIBLE_ROWS) firstVisible = sel - VISIBLE_ROWS + 1;

    for (uint8_t i = firstVisible; i < count && i < firstVisible + VISIBLE_ROWS; i++) {
        bool isSel = (i == sel);
        bool isEd  = isSel && editing;
        int y = ITEM_Y_START + (i - firstVisible) * ITEM_HEIGHT;
        if (isSel) c->fillRoundRect(10, y - 1, 220, 18, 3, isEd ? COLOR_HIGHLIGHT : COLOR_BORDER);

        char label[24], value[16];
        fill(i, label, value, sizeof(label));

        c->setFont(&fonts::efontCN_16);
        c->setTextDatum(top_left);
        c->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);
        c->drawString(label, 16, y + 1);
        c->setTextDatum(top_right);
        c->drawString(value, 216, y + 1);
        c->setTextDatum(top_left);
    }

    if (firstVisible > 0) {
        c->setTextDatum(top_right); c->setTextColor(COLOR_BORDER);
        c->drawString("^", 226, 24); c->setTextDatum(top_left);
    }
    if (firstVisible + VISIBLE_ROWS < count) {
        c->setTextDatum(top_right); c->setTextColor(COLOR_BORDER);
        c->drawString("v", 226, 94); c->setTextDatum(top_left);
    }
}

static void fillAdv(uint8_t i, char* l, char* v, size_t n)    { advLabelValue(devMode.advRow(i), l, v, n); }
static void fillSensor(uint8_t i, char* l, char* v, size_t n) { sensorLabelValue(devMode.sensorRow(i), l, v, n); }
static void fillTrig(uint8_t i, char* l, char* v, size_t n)   { trigLabelValue(devMode.trigRow(i), l, v, n); }

void renderDevModeUI() {
    LGFX_Sprite* c = _ft->_canvas;
    c->fillScreen(COLOR_BG);

    bool editing = devMode.editMode.state == DevModeEditMode::EDITING;
    switch (devMode.editMode.screen) {
        case DevModeEditMode::ADVANCE:
            renderRowList("ADVANCE", "HOLD=BACK", DEV_ADV_ROW_COUNT,
                          devMode.editMode.advIndex, editing, fillAdv);
            break;
        case DevModeEditMode::SENSOR: {
            renderRowList("SENSOR", "HOLD=BACK", DEV_SENSOR_ROW_COUNT,
                          devMode.editMode.sensorIndex, editing, fillSensor);
            // Confirmed TF-Luna frame rate: 0 means the 200Hz upgrade did not
            // take and it fell back to the safe 100Hz pacing.
            char hz[16];
            uint16_t fps = tfLuna.getFrameRateHz();
            if (fps) snprintf(hz, sizeof(hz), "%uHz", fps);
            else     snprintf(hz, sizeof(hz), "100Hz?");
            c->setFont(&fonts::efontCN_10);
            c->setTextDatum(top_left);
            c->setTextColor(fps >= 200 ? COLOR_GREEN : COLOR_YELLOW);
            c->drawString(hz, 16, 112);
            break;
        }
        case DevModeEditMode::TRIGOUT:
            renderRowList("TRIGGER OUT", "HOLD=BACK", DEV_TRIG_ROW_COUNT,
                          devMode.editMode.trigIndex, editing, fillTrig);
            break;
        default:
            renderMain();
            break;
    }

    _ft->_canvas_update();
}
