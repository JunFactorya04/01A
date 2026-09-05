/**
 * @file boot_logo.h
 * @brief GEOPIX boot logo animation (logo fade-in -> text fade-in -> hold -> fade-out)
 *
 * Self-contained module: only draws on the shared canvas, touches no other
 * subsystem. After it returns, control goes straight to the main UI.
 */
#pragma once
#include <LovyanGFX.h>

/**
 * @brief Play the boot logo sequence on the given canvas (blocking, ~2.5s
 * + extraHoldMs). Sequence: logo fade in -> hold -> fade out -> return.
 * @param canvas Full-screen LGFX_Sprite (240x135) already created.
 * @param extraHoldMs Extra time added to the hold phase (logo already fully
 * visible and static, so this is invisible as a "pause" — it just looks
 * like the logo stays a little longer). Use this to give the TF-Luna
 * sensor's own physical power-on settling more real wall-clock time on a
 * boot path that skips the ~2s manual power-button hold (VIN2 direct power
 * / RTC scheduled wake) — see the call site in main.cpp for why.
 */
void bootLogoPlay(LGFX_Sprite* canvas, unsigned long extraHoldMs = 0);
