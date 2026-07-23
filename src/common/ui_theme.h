/**
 * @file ui_theme.h
 * @brief Global UI theme API — 8 color schemes (FG/BG/AL RGB565)
 * @date 2026-07-23
 *
 * Learned from M5Launcher setUiColor(). Mode UIs map their palette
 * defines (COLOR_BG/COLOR_TEXT/COLOR_HIGHLIGHT/COLOR_BORDER) to these
 * globals, so render logic stays untouched.
 * Semantic colors (GREEN=running, RED=error, YELLOW=warning) are NOT
 * themed on purpose.
 */

#pragma once
#include <Arduino.h>

#define UI_THEME_COUNT 8

extern uint16_t UI_FG;       // main text        (was COLOR_TEXT)
extern uint16_t UI_BG;       // background       (was COLOR_BG)
extern uint16_t UI_AL;       // accent/highlight (was COLOR_HIGHLIGHT)
extern uint16_t UI_BORDER;   // muted/border     (was COLOR_BORDER)

void        uiThemeApply(int index);   // set globals from preset (no save)
const char* uiThemeName(int index);
