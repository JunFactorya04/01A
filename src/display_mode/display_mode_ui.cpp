/**
 * @file display_mode_ui.cpp
 * @brief DISPLAY mode UI — brightness + power save editor
 * @date 2026-07-23
 */

#include "display_mode.h"
#include "display_mode_ui.h"
#include "../factory_test/factory_test.h"
#include "../common/ui_theme.h"   // themed palette

extern FactoryTest* _ft;

// ============ UI CONSTANTS (same palette as Setting mode) ============
#define COLOR_BG        UI_BG
#define COLOR_TEXT      UI_FG
#define COLOR_GREEN     0x07E0
#define COLOR_YELLOW    0xFFE0
#define COLOR_BORDER    UI_BORDER
#define COLOR_LIME      0x87E0

// ============ BLINK ============
static unsigned long s_blinkTimer = 0;
static bool          s_blinkState = false;

static void updateBlink() {
    if (millis() - s_blinkTimer > 500) {
        s_blinkState = !s_blinkState;
        s_blinkTimer = millis();
    }
}

void initDisplayUI() {
    s_blinkTimer = millis();
    s_blinkState = false;
}

// ============ RENDER ============
static void drawRow(LGFX_Sprite* c, int y, const char* label, const char* value,
                    bool isSel, bool editing, bool blinkHide, uint16_t valueColor) {
    c->setFont(&fonts::efontCN_14);
    c->setTextColor(isSel ? (editing ? COLOR_YELLOW : COLOR_LIME) : COLOR_TEXT);
    char lbuf[24];
    snprintf(lbuf, sizeof(lbuf), "%s%s", isSel ? "> " : "  ", label);
    c->drawString(lbuf, 12, y);

    if (!blinkHide) {
        c->setTextColor(isSel && editing ? COLOR_YELLOW : valueColor);
        c->setTextDatum(top_right);
        c->drawString(value, 228, y);
        c->setTextDatum(top_left);
    }
}

void renderDisplayUI() {
    if (!_ft || !_ft->_canvas) return;
    updateBlink();

    LGFX_Sprite* c = _ft->_canvas;
    c->fillScreen(COLOR_BG);

    // ── Header ──
    c->fillRect(0, 0, 240, 22, 0x07430F);
    c->setFont(&fonts::efontCN_16);
    c->setTextDatum(top_center);
    c->setTextColor(0x87C38F);
    c->drawString("DISPLAY", 120, 3);
    c->setTextDatum(top_left);

    bool editing = (displayMode.editMode.state == DisplayEditMode::EDITING);
    uint8_t sel  = displayMode.editMode.selectedIndex;
    char buf[24];

    // ── Item 0: Brightness ──
    {
        int pct = (displayMode.config.brightness * 100) / 255;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        drawRow(c, 28, "BRIGHTNESS", buf, sel == 0, editing,
                sel == 0 && editing && s_blinkState, COLOR_GREEN);

        // Progress bar
        int barW = 216, barX = 12, barY = 45;
        c->drawRect(barX, barY, barW, 6, COLOR_BORDER);
        int fill = (displayMode.config.brightness * (barW - 2)) / 255;
        if (fill > 0) c->fillRect(barX + 1, barY + 1, fill, 4, COLOR_LIME);
    }

    // ── Item 1: Power Save ──
    {
        if (displayMode.config.dimmerSec == 0)
            snprintf(buf, sizeof(buf), "OFF");
        else
            snprintf(buf, sizeof(buf), "%ds", displayMode.config.dimmerSec);
        drawRow(c, 56, "POWER SAVE", buf, sel == 1, editing,
                sel == 1 && editing && s_blinkState,
                displayMode.config.dimmerSec == 0 ? COLOR_BORDER : COLOR_GREEN);
    }

    // ── Item 2: Theme ──
    {
        snprintf(buf, sizeof(buf), "%s", uiThemeName(displayMode.config.themeIndex));
        drawRow(c, 76, "THEME", buf, sel == 2, editing,
                sel == 2 && editing && s_blinkState, UI_AL);
    }

    // ── Item 3: Rotation ──
    {
        snprintf(buf, sizeof(buf), "%s", displayMode.config.rotation == 3 ? "NORMAL" : "FLIP");
        drawRow(c, 96, "ROTATION", buf, sel == 3, editing,
                sel == 3 && editing && s_blinkState, COLOR_GREEN);
    }

    // ── Hint line ──
    c->setFont(&fonts::efontCN_10);
    c->setTextColor(COLOR_BORDER);
    if (sel == 1 && displayMode.config.dimmerSec > 0) {
        snprintf(buf, sizeof(buf), "Dim %ds, off +5s", displayMode.config.dimmerSec);
        c->drawString(buf, 12, 114);
    }

    // ── Footer hint ──
    c->setTextDatum(bottom_center);
    c->setTextColor(COLOR_BORDER);
    c->drawString(editing ? "Rotate: adjust | Press: done"
                          : "Press: edit | Hold: save & exit", 120, 133);
    c->setTextDatum(top_left);

    _ft->_canvas_update();
}
