/**
 * @file fuji_protocol.h
 * @brief Fujifilm camera BLE remote protocol (Basic/unsecured variant)
 * @date 2026-09-06
 *
 * Reference: github.com/gkoh/furble (Fujifilm/FujifilmBasic classes, studied not copied).
 * Camera = BLE server. ESP32 = BLE client emulating the Fujifilm Camera Remote app.
 * "Basic" = unsecured pairing via a 4-byte token broadcast in the camera's
 * advertisement manufacturer data. Works on older Fujifilm X/GFX firmware;
 * newer firmware (circa mid-2025+) requires the separate "Secure" protocol
 * (not implemented — far more complex, many undocumented characteristics).
 */

#pragma once
#include <Arduino.h>

// ===== Advertisement (manufacturer data) =====
// 2-byte company ID + 1-byte type + 4-byte pairing token, little-endian.
#define FUJI_COMPANY_ID          0x04d8
#define FUJI_ADV_TYPE_TOKEN      0x02
#define FUJI_TOKEN_LEN           4

// Secondary service UUIDs advertised alongside manufacturer data — used to
// recognize a Fujifilm "Basic" camera (vs. a Secure one, which won't
// advertise these).
#define FUJI_ADV_SVC_CR          "af854c2e-b214-458e-97e2-912c4ecf2cb8"
#define FUJI_ADV_SVC_XAPP        "117c4142-edd4-4c77-8696-dd18eebb770a"

// ===== GATT =====
#define FUJI_SVC_PAIR            "91f1de68-dff6-466e-8b65-ff13b0f16fb8"
#define FUJI_CHAR_PAIR           "aba356eb-9633-4e60-b73f-f52516dbd671"
#define FUJI_CHAR_IDENTIFY       "85b9163e-62d1-49ff-a6f5-054b4630d4a1"

#define FUJI_SVC_SHUTTER         "6514eb81-4e8f-458d-aa2a-e691336cdfac"
#define FUJI_CHAR_SHUTTER        "7fcf49c6-4ff0-4777-a03d-1a79166af7a8"

// ===== Shutter command (2 command bytes + 2 param bytes, written as two
// separate 2-byte writes to the shutter characteristic) =====
#define FUJI_CMD_SHUTTER         0x01, 0x00   // "command" write (always this)
#define FUJI_PARAM_RELEASE       0x00, 0x00
#define FUJI_PARAM_PRESS         0x02, 0x00
#define FUJI_PARAM_FOCUS         0x03, 0x00

// ===== Timing (ms) =====
#define FUJI_SHUTTER_HOLD_MS     200
#define FUJI_FOCUS_HOLD_MS       200

// See SONY_RECONNECT_SETTLE_MS (sony_protocol.h) for why this exists. Only
// applied in shutterPress() after an actual reconnect, not every press.
#define FUJI_RECONNECT_SETTLE_MS 500
