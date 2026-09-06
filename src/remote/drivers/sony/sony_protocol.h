/**
 * @file sony_protocol.h
 * @brief Sony camera BLE remote protocol (freemote / RMT-P1BT compatible)
 * @date 2026-07-23
 *
 * Source: user-provided reference (sony_driver — freemote protocol).
 * Camera = BLE server. ESP32 = BLE client (acts as remote).
 * Tested camera families: A7 IV, A6700, ZV series (RMT-P1BT protocol).
 */

#pragma once
#include <Arduino.h>

// ===== GATT =====
// Sony Remote Control service
#define SONY_SERVICE_UUID       "8000FF00-FF00-FFFF-FFFF-FFFFFFFFFFFF"
#define SONY_CHAR_COMMAND       ((uint16_t)0xFF01)  // write commands
#define SONY_CHAR_NOTIFY        ((uint16_t)0xFF02)  // camera status notify

// ===== Advertisement =====
// Sony manufacturer company ID (little-endian in manufacturer data)
#define SONY_COMPANY_ID_LO      0x2D
#define SONY_COMPANY_ID_HI      0x01
// Camera-tag byte in manufacturer payload; the byte after it carries flags
#define SONY_ADV_CAMERA_TAG     0x22
#define SONY_ADV_FLAG_PAIRING   0x40   // camera is in pairing mode
#define SONY_ADV_FLAG_REMOTE    0x02   // BLE remote control enabled

// ===== Commands (2 bytes, write to 0xFF01) =====
static const uint8_t SONY_FOCUS_DOWN[]   = {0x01, 0x07};  // shutter half down
static const uint8_t SONY_FOCUS_UP[]     = {0x01, 0x06};  // shutter half up
static const uint8_t SONY_SHUTTER_DOWN[] = {0x01, 0x09};  // shutter full down
static const uint8_t SONY_SHUTTER_UP[]   = {0x01, 0x08};  // shutter full up
static const uint8_t SONY_RECORD_DOWN[]  = {0x01, 0x0F};  // movie rec down
static const uint8_t SONY_RECORD_UP[]    = {0x01, 0x0E};  // movie rec up

// ===== Timing (ms) =====
// Half-press is still sent (protocol requires it before full-press) --
// this is a reverse-engineered community protocol with no official Sony
// documentation, so that step is NOT skipped even though camera is always
// used in Manual Focus: the camera's firmware likely mirrors a real
// 2-stage mechanical button and may simply ignore SHUTTER_DOWN if it never
// saw FOCUS_DOWN first. What DID get cut is the wait TIME at each stage --
// confirmed MF-only means these gaps are pure firmware-processing margin,
// not AF settle time. SONY_SHUTTER_HOLD_MS is the one with the most
// real risk if cut too far (needs to be long enough for the camera to
// register a full press-and-release, not just an AF-related wait) --
// verify on real hardware that shots aren't being dropped after lowering it.
#define SONY_FOCUS_SETTLE_MS    5
#define SONY_SHUTTER_HOLD_MS    50
#define SONY_RELEASE_GAP_MS     10
