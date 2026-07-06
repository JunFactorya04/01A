/**
 * @file tf_luna.h
 * @brief TF-Luna LiDAR driver — I2C mode, pure distance read + normalization
 * @date 2026-07-06
 *
 * I2C: SDA=GPIO13, SCL=GPIO15, addr=0x10, bus=Wire1
 * (RTC BM8563 uses Wire/bus0 on GPIO11/12 — separate bus, no conflict)
 */

#pragma once
#include "../common/hardware_config.h"
#include <Wire.h>

// ============ TF-LUNA CONFIG ============
#define TFLUNA_MAX_DISTANCE 8.0f     // Meters (no-return / clamp value)

// ============ SENSOR STATE ============
struct TFLunaState {
    float distance_m = TFLUNA_MAX_DISTANCE;
    uint16_t strength = 0;
    bool valid = false;
    unsigned long lastUpdateTime = 0;
};

static TFLunaState tfLunaState;

// ============ INITIALIZATION ============
void initTfLuna() {
    // Dedicated I2C bus (Wire1) for TF-Luna
    Wire1.begin(TFLUNA_SDA_PIN, TFLUNA_SCL_PIN);
    tfLunaState.distance_m     = TFLUNA_MAX_DISTANCE;
    tfLunaState.valid          = false;
    tfLunaState.lastUpdateTime = millis();
}

// ============ RAW READ (I2C) ============
// Reads the 9-byte data frame, validates 0x59 0x59 header,
// converts + normalizes distance (0mm or >8000mm -> 8.0m).
bool readTfLunaRaw(float &distance_m, uint16_t &strength) {

    uint8_t buf[9];

    Wire1.beginTransmission(TFLUNA_I2C_ADDR);
    Wire1.write((uint8_t)0x00);
    if (Wire1.endTransmission() != 0) {
        return false;
    }

    Wire1.requestFrom((int)TFLUNA_I2C_ADDR, 9);
    int i = 0;
    while (Wire1.available() && i < 9) {
        buf[i++] = Wire1.read();
    }

    if (i != 9) return false;
    if (buf[0] != 0x59 || buf[1] != 0x59) return false;

    uint16_t dist_mm = buf[2] + (buf[3] << 8);
    strength = buf[4] + (buf[5] << 8);

    // ---- NORMALIZE ----
    if (dist_mm == 0) {
        distance_m = TFLUNA_MAX_DISTANCE;      // no return -> treat as far
    } else if (dist_mm > 8000) {
        distance_m = TFLUNA_MAX_DISTANCE;      // clamp to 8.0m
    } else {
        distance_m = dist_mm / 1000.0f;
    }

    return true;
}

// ============ MAIN UPDATE ============
void updateTfLunaRaw() {
    float    d = TFLUNA_MAX_DISTANCE;
    uint16_t s = 0;

    if (!readTfLunaRaw(d, s)) {
        tfLunaState.valid = false;             // read failed this cycle
        return;
    }

    tfLunaState.distance_m     = d;
    tfLunaState.strength       = s;
    tfLunaState.valid          = true;
    tfLunaState.lastUpdateTime = millis();
}

// ============ PUBLIC INTERFACE ============

/** @brief Poll sensor and return normalized distance (meters). */
float readTfLunaDistance() {
    updateTfLunaRaw();
    return tfLunaState.distance_m;
}

/** @brief Last distance without polling. */
float readTfLunaDistanceRaw() {
    return tfLunaState.distance_m;
}

/** @brief Signal strength (0-65535). */
uint16_t getTfLunaStrength() {
    return tfLunaState.strength;
}

/** @brief Motion detection kept for interface compatibility (unused in pure-threshold). */
bool isTfLunaMotionDetected() {
    return false;
}

/** @brief Full sensor state. */
const TFLunaState& getTfLunaState() {
    return tfLunaState;
}

/** @brief True if the last read succeeded within maxAgeMs. */
bool isTfLunaFresh(unsigned long maxAgeMs = 200) {
    return tfLunaState.valid && ((millis() - tfLunaState.lastUpdateTime) < maxAgeMs);
}
