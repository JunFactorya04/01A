/**
 * @file ui_theme.cpp
 * @brief Global UI theme — 8 presets (RGB565)
 * @date 2026-07-23
 */

#include "ui_theme.h"

// Defaults = original Geopix palette (theme 0)
uint16_t UI_FG     = 0xFFFF;
uint16_t UI_BG     = 0x0000;
uint16_t UI_AL     = 0x27E0;
uint16_t UI_BORDER = 0x4208;

struct ThemePreset {
    const char* name;
    uint16_t fg, bg, al, border;
};

static const ThemePreset THEMES[UI_THEME_COUNT] = {
    {"GEOPIX", 0xFFFF, 0x0000, 0x27E0, 0x4208},   // original palette
    {"GREEN",  0x07E0, 0x0000, 0xF800, 0x0280},
    {"RED",    0xF800, 0x0000, 0xE3E0, 0x5000},
    {"BLUE",   0x94BF, 0x0000, 0xD81F, 0x2110},
    {"YELLOW", 0xFFE0, 0x0000, 0xFB80, 0x5280},
    {"PURPLE", 0xE01F, 0x0000, 0xF800, 0x5010},
    {"WHITE",  0xFFFF, 0x0000, 0x6B6D, 0x4208},
    {"INVERT", 0x0000, 0xFFFF, 0x6B6D, 0xB596},
};

void uiThemeApply(int index) {
    if (index < 0 || index >= UI_THEME_COUNT) index = 0;
    UI_FG     = THEMES[index].fg;
    UI_BG     = THEMES[index].bg;
    UI_AL     = THEMES[index].al;
    UI_BORDER = THEMES[index].border;
}

const char* uiThemeName(int index) {
    if (index < 0 || index >= UI_THEME_COUNT) index = 0;
    return THEMES[index].name;
}
