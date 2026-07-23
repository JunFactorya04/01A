/**
 * @file nikon_protocol.h
 * @brief Nikon camera BLE remote protocol (ML-L7 style) — EXPERIMENTAL
 * @date 2026-07-23
 *
 * !!! EXPERIMENTAL — no official reference available !!!
 * Designed from public reverse-engineering notes of the Nikon ML-L7 remote.
 * Unlike Sony/Canon, the Nikon camera is the BLE CLIENT: the ML-L7 remote
 * is a BLE peripheral. ESP32 therefore ADVERTISES as an ML-L7 and waits
 * for the camera to connect and subscribe.
 *
 * Adjust constants below after field testing with a real camera
 * (Z50/Z fc/P950/P1000/B600 family supports ML-L7).
 */

#pragma once
#include <Arduino.h>

// ===== Identity =====
// Camera pairs by device name — must advertise as the real remote
#define NIKON_DEVICE_NAME        "ML-L7"

// ===== GATT (ML-L7 remote control service) =====
#define NIKON_SERVICE_UUID       "0000de00-0000-1000-8000-00805f9b34fb"
// Button events: remote -> camera (notify)
#define NIKON_CHAR_BUTTON        "0000de01-0000-1000-8000-00805f9b34fb"
// Camera status: camera -> remote (write)
#define NIKON_CHAR_STATUS        "0000de02-0000-1000-8000-00805f9b34fb"

// ===== Button event payloads (2 bytes, notify on DE01) =====
// byte0 = event group, byte1 = key code  — EXPERIMENTAL VALUES
static const uint8_t NIKON_SHUTTER_DOWN[] = {0x01, 0x01};
static const uint8_t NIKON_SHUTTER_UP[]   = {0x01, 0x00};
static const uint8_t NIKON_FOCUS_DOWN[]   = {0x02, 0x01};  // half-press
static const uint8_t NIKON_FOCUS_UP[]     = {0x02, 0x00};

// ===== Timing (ms) =====
#define NIKON_SHUTTER_HOLD_MS    150
#define NIKON_FOCUS_SETTLE_MS    100
