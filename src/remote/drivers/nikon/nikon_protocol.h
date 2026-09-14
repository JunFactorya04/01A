/**
 * @file nikon_protocol.h
 * @brief Nikon camera BLE remote protocol (ML-L7 "Remote" variant)
 * @date 2026-09-06
 *
 * Reference: github.com/gkoh/furble (NikonBase/NikonRemote classes, studied
 * not copied). Ported from NimBLE (client) semantics to this project's
 * classic Arduino BLEDevice (bluedroid) client.
 *
 * Role: the camera is the BLE SERVER (opposite of the previous, EXPERIMENTAL
 * driver this replaces, which had ESP32 advertising as an ML-L7 server and
 * waiting for the camera to connect in). ESP32 is the BLE CLIENT — it scans
 * for the camera advertising NIKON_SERVICE_UUID, connects, then must
 * complete a 4-message handshake on the pairing characteristic before the
 * camera will honour any shutter command. That handshake is not a one-time
 * pairing step: furble re-runs it on every single connection (including
 * reconnects), keyed off a persisted per-remote "identity" (device+nonce)
 * chosen at first pairing.
 */

#pragma once
#include <Arduino.h>

// ===== GATT =====
#define NIKON_SERVICE_UUID       "0000de00-3dd4-4255-8d62-6dc7b9bd5561"
// Pairing/handshake characteristic: write our stage message, indication
// carries the camera's stage response (both directions on this one UUID).
#define NIKON_CHAR_PAIR          "00002087-3dd4-4255-8d62-6dc7b9bd5561"
// Must be subscribed (indication) before the handshake will proceed —
// observed requirement, payload itself is not otherwise used.
#define NIKON_CHAR_IND1          "00002084-3dd4-4255-8d62-6dc7b9bd5561"
#define NIKON_CHAR_SHUTTER       "00002083-3dd4-4255-8d62-6dc7b9bd5561"

// ===== Handshake identity =====
// device[4] + nonce[4], chosen randomly at pair() time and persisted —
// every reconnect's handshake reuses the same identity.
#define NIKON_ID_LEN             8

// ===== Handshake message layout (packed, sent/received as raw bytes) =====
// byte 0      : stage (0x01/0x02/0x03/0x04/0x05)
// bytes 1-8   : 8-byte big-endian "timestamp" field
// bytes 9-16  : 8-byte id (device+nonce) or, on the stage-4 response, the
//               camera's serial number as ASCII
#define NIKON_MSG_LEN            17

// Fixed 8-byte big-endian value (=1) used as the stage-1 timestamp field by
// the Remote (ML-L7) protocol variant — furble hardcodes this rather than
// using a real clock value.
static const uint8_t NIKON_STAGE0_TIMESTAMP[8] = {0, 0, 0, 0, 0, 0, 0, 1};
static const uint8_t NIKON_ZERO8[8] = {0, 0, 0, 0, 0, 0, 0, 0};

#define NIKON_HANDSHAKE_STAGE_TIMEOUT_MS 10000

// ===== Shutter control (2 bytes, written to NIKON_CHAR_SHUTTER) =====
#define NIKON_MODE_SHUTTER       0x02
#define NIKON_CMD_PRESS          0x02
#define NIKON_CMD_RELEASE        0x00

// ===== Timing (ms) =====
#define NIKON_SHUTTER_HOLD_MS    150

// See SONY_RECONNECT_SETTLE_MS (sony_protocol.h) for why this exists. Only
// applied in shutterPress() after an actual reconnect, not every press —
// and a reconnect here also re-runs the full 4-message handshake, which
// already takes real time on its own, so this is layered on top of that.
#define NIKON_RECONNECT_SETTLE_MS 500
