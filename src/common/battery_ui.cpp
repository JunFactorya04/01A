#include "battery_ui.h"
#include "battery.h"
#include "ui_theme.h"

// Semantic colours, deliberately not themed -- same convention as the mode UIs
// (see ui_theme.h: GREEN/RED/YELLOW stay fixed across all 8 themes).
#define BATTERY_RED 0xF800

// Geometry. Sits on the LEFT of the header, just right of the "<" back arrow
// at x=5. The right-hand side is spoken for in several modes -- Auto Mode puts
// the TF-Luna frame rate there, Sleep & Wake the date -- so the left is the
// only edge free across all six.
static const int16_t BAT_X = 22;   // left edge of the shell
static const int16_t BAT_Y = 3;
static const int16_t BAT_W = 38;   // fits "100%" at efontCN_16
static const int16_t BAT_H = 14;
static const int16_t NUB_W = 3;    // the little positive terminal

// A bolt drawn inside the shell while charging. Two triangles meeting at the
// waist, which is enough to read as a lightning bolt at this size.
static void drawChargingBolt(LGFX_Sprite* c, int16_t cx, int16_t cy) {
    c->fillTriangle(cx + 2, cy - 5, cx - 3, cy + 1, cx,     cy + 1, BATTERY_RED);
    c->fillTriangle(cx - 2, cy + 5, cx + 3, cy - 1, cx,     cy - 1, BATTERY_RED);
}

void drawBatteryBadge(LGFX_Sprite* c) {
    if (!c) return;

    uint8_t  pct     = batteryPercent();
    bool     low     = batteryLow();
    uint16_t shell   = low ? BATTERY_RED : UI_FG;

    // Shell + terminal nub
    c->drawRoundRect(BAT_X, BAT_Y, BAT_W, BAT_H, 2, shell);
    c->fillRect(BAT_X + BAT_W, BAT_Y + 4, NUB_W, BAT_H - 8, shell);

    // Percentage inside the shell. Kept as text rather than a fill bar: at
    // 38x14 a proportional fill would sit behind the digits and make them
    // unreadable against either colour.
    c->setFont(&fonts::efontCN_16);
    c->setTextDatum(middle_center);
    c->setTextColor(shell);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", pct);
    c->drawString(buf, BAT_X + BAT_W / 2, BAT_Y + BAT_H / 2);

    // Charging bolt, over the left of the shell. batteryCharging() is hard
    // false unless a charger STAT pin is wired and declared -- see battery.h.
    if (batteryCharging()) {
        drawChargingBolt(c, BAT_X + 7, BAT_Y + BAT_H / 2);
    }
}
