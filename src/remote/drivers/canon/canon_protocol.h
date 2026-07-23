/**
 * @file canon_protocol.h
 * @brief Canon camera BLE remote protocol (BR-E1 compatible)
 * @date 2026-07-23
 *
 * Reference: github.com/maxmacstn/ESP32-Canon-BLE-Remote (studied, not copied).
 * Camera = BLE server. ESP32 = BLE client emulating a BR-E1 remote.
 * Tested camera families: EOS M/R/RP series with BR-E1 support.
 */

#pragma once
#include <Arduino.h>

// ===== GATT =====
#define CANON_SERVICE_UUID       "00050000-0000-1000-0000-d8492fffa821"
#define CANON_CHAR_PAIRING       "00050002-0000-1000-0000-d8492fffa821"
#define CANON_CHAR_SHUTTER       "00050003-0000-1000-0000-d8492fffa821"

// ===== Pairing =====
// Handshake: write [0x03, <device name bytes>] to pairing characteristic
#define CANON_PAIR_PREFIX        0x03

// ===== Shutter command bits (write 1 byte to shutter char) =====
#define CANON_BTN_RELEASE        0x80
#define CANON_BTN_FOCUS          0x40
#define CANON_BTN_TELE           0x20
#define CANON_BTN_WIDE           0x10
#define CANON_MODE_IMMEDIATE     0x0C
#define CANON_MODE_DELAY         0x04
#define CANON_MODE_MOVIE         0x08

// press shutter = MODE_IMMEDIATE | BTN_RELEASE = 0x8C, release = 0x0C
#define CANON_CMD_SHUTTER_DOWN   (CANON_MODE_IMMEDIATE | CANON_BTN_RELEASE)
#define CANON_CMD_FOCUS_DOWN     (CANON_MODE_IMMEDIATE | CANON_BTN_FOCUS)
#define CANON_CMD_NEUTRAL        (CANON_MODE_IMMEDIATE)

// ===== Timing (ms) =====
#define CANON_SHUTTER_HOLD_MS    200
#define CANON_FOCUS_HOLD_MS      200
