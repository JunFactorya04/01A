/**
 * @file battery_ui.h
 * @brief Shared battery indicator for mode headers
 *
 * Every mode draws its own header (there is no shared header renderer in this
 * codebase), so this is the one piece they all call, to avoid six copies of
 * the same layout drifting apart. Follows the GEOPIX UI STANDARD: header row
 * at y=2, theme colours from ui_theme.h, with the one exception of the fixed
 * semantic RED for the low-battery state.
 */
#pragma once
#include <LovyanGFX.hpp>

// Draws the battery percentage right-aligned at the header's right edge, RED
// below BATTERY_LOW_MV. Battery used to be visible only on the launcher, so a
// long timelapse gave no indication at all once you entered the mode.
void drawBatteryBadge(LGFX_Sprite* c);
