/**
 * @file display_mode_ui.cpp
 * @brief DISPLAY mode UI — brightness + power save editor
 * @date 2026-07-24
 *
 * ★ GEOPIX UI STANDARD (reference layout for all modes) ★
 *
 * Screen 240x135, all colors mapped to theme (ui_theme.h):
 *   COLOR_BG=UI_BG, COLOR_TEXT=UI_FG, COLOR_BORDER=UI_BORDER,
 *   COLOR_HIGHLIGHT=UI_AL. Semantic GREEN/RED/YELLOW stay fixed.
 *
 * Layout spec:
 *   Header : efontCN_16, title top_center GREEN at (120,2), "<" TEXT at (5,2)
 *   Panel  : drawRoundRect(8, 22, 224, H, 5, BORDER) — y=22 leaves a gap
 *            under the header so title never touches the frame
 *   Items  : efontCN_16, row height 20px starting y=27;
 *            selected row = fillRoundRect(10, y-1, 220, 18, 3) with
 *            BORDER (selecting) / HIGHLIGHT (editing), text inverted to BG;
 *            label left x=16, value right-aligned x=210, ">" GREEN x=226;
 *            editing value blinks (500ms)
 *   Status : optional 1-line bottom panel y=113 h=20, efontCN_10
 *   Footer : NONE — no hint captions, saves space
 */

#include "display_mode.h"
#include "display_mode_ui.h"
#include "../factory_test/factory_test.h"
#include "../common/ui_theme.h"   // themed palette

extern FactoryTest* _ft;

// ============ UI CONSTANTS (same palette as Auto Shoot mode) ============
#define COLOR_BG        UI_BG
#define COLOR_TEXT      UI_FG
#define COLOR_GREEN     0x07E0
#define COLOR_YELLOW    0xFFE0
#define COLOR_BORDER    UI_BORDER
#define COLOR_HIGHLIGHT UI_AL

#define ITEM_HEIGHT     20
#define ITEM_Y_START    27
#define ITEM_INDENT     10

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

// ============ RENDER (Auto Shoot style: panel + inverse selection) ============
static void drawItem(LGFX_Sprite* c, uint8_t index, const char* label,
                     const char* value, bool isSel, bool isEditing) {
    int y = ITEM_Y_START + (index * ITEM_HEIGHT);

    if (isSel) {
        c->fillRoundRect(10, y - 1, 220, 18, 3,
            isEditing ? COLOR_HIGHLIGHT : COLOR_BORDER);
    }

    c->setTextDatum(top_left);
    c->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);
    c->drawString(label, ITEM_INDENT + 6, y);

    c->setTextDatum(top_right);
    if (isEditing && s_blinkState) c->setTextColor(COLOR_BG);
    c->drawString(value, 210, y);

    c->setTextColor(COLOR_GREEN);
    c->drawString(">", 226, y);

    c->setTextDatum(top_left);
}

void renderDisplayUI() {
    if (!_ft || !_ft->_canvas) return;
    updateBlink();

    LGFX_Sprite* c = _ft->_canvas;
    c->setTextWrap(false);
    c->setTextColor(COLOR_TEXT);
    c->setTextDatum(top_left);
    c->fillScreen(COLOR_BG);

    // ── Header (Auto Shoot style) ──
    c->setFont(&fonts::efontCN_16);
    c->setTextDatum(top_center);
    c->setTextColor(COLOR_GREEN);
    c->drawString("DISPLAY MODE", 120, 2);
    c->setTextDatum(top_left);
    c->setTextColor(COLOR_TEXT);
    c->drawString("<", 5, 2);

    bool editing = (displayMode.editMode.state == DisplayEditMode::EDITING);
    uint8_t sel  = displayMode.editMode.selectedIndex;
    char buf[24];

    // ── Settings panel (gap below header, big font) ──
    c->drawRoundRect(8, 22, 224, 88, 5, COLOR_BORDER);
    c->setFont(&fonts::efontCN_16);

    // Item 0: Brightness
    int pct = (displayMode.config.brightness * 100) / 255;
    snprintf(buf, sizeof(buf), "%d%%", pct);
    drawItem(c, 0, "Brightness", buf, sel == 0, sel == 0 && editing);

    // Item 1: Power Save
    if (displayMode.config.dimmerSec == 0) snprintf(buf, sizeof(buf), "OFF");
    else snprintf(buf, sizeof(buf), "%ds", displayMode.config.dimmerSec);
    drawItem(c, 1, "Power Save", buf, sel == 1, sel == 1 && editing);

    // Item 2: Theme
    snprintf(buf, sizeof(buf), "%s", uiThemeName(displayMode.config.themeIndex));
    drawItem(c, 2, "Theme", buf, sel == 2, sel == 2 && editing);

    // Item 3: Rotation
    snprintf(buf, sizeof(buf), "%s", displayMode.config.rotation == 3 ? "NORMAL" : "FLIP");
    drawItem(c, 3, "Rotation", buf, sel == 3, sel == 3 && editing);

    // ── Status panel: brightness bar + hint ──
    int py = 113;
    c->drawRoundRect(8, py, 224, 20, 5, COLOR_BORDER);

    int barW = 130, barX = 16, barY = py + 7;
    c->drawRect(barX, barY, barW, 6, COLOR_BORDER);
    int fill = (displayMode.config.brightness * (barW - 2)) / 255;
    if (fill > 0) c->fillRect(barX + 1, barY + 1, fill, 4, COLOR_HIGHLIGHT);

    c->setFont(&fonts::efontCN_10);
    c->setTextDatum(top_right);
    if (sel == 1 && displayMode.config.dimmerSec > 0) {
        snprintf(buf, sizeof(buf), "Dim %ds, off +5s", displayMode.config.dimmerSec);
        c->setTextColor(COLOR_YELLOW);
        c->drawString(buf, 226, py + 5);
    } else {
        c->setTextColor(COLOR_TEXT);
        c->drawString("---", 226, py + 5);
    }
    c->setTextDatum(top_left);

    _ft->_canvas_update();
}
