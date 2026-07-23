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
 * @brief Play the boot logo sequence on the given canvas (blocking, ~2.5s).
 * Sequence: logo fade in -> text fade in -> hold both -> fade out -> return.
 * @param canvas Full-screen LGFX_Sprite (240x135) already created.
 */
void bootLogoPlay(LGFX_Sprite* canvas);
