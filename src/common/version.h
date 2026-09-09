/**
 * @file version.h
 * @brief Single source of truth for the displayed firmware version.
 * @date 2026-09-09
 *
 * GEOPIX_FW_VERSION is normally injected at build time by tools/version.py
 * (a PlatformIO pre-build script) from `git describe --tags`, so a real
 * release build always shows its actual tag with no manual editing —
 * that's what let "v1.3" ship inside a v1.4 release before this existed.
 * The #ifndef fallback below only applies if that script didn't run (e.g.
 * building outside PlatformIO, or no .git present).
 */

#pragma once

#ifndef GEOPIX_FW_VERSION
#define GEOPIX_FW_VERSION "dev"
#endif
