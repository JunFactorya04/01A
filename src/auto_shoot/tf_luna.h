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

// ============ RAW READ (I2C register mode) ============
// TF-Luna I2C returns raw registers (NO 0x59 header):
//   0x00 DIST_LOW  0x01 DIST_HIGH  0x02 AMP_LOW  0x03 AMP_HIGH
// Reads 6 bytes from 0x00, converts + normalizes (0mm or >8000mm -> 8.0m).
bool readTfLunaRaw(float &distance_m, uint16_t &strength) {

    uint8_t buf[6];

    Wire1.beginTransmission(TFLUNA_I2C_ADDR);
    Wire1.write((uint8_t)0x00);              // start at DIST_LOW register
    if (Wire1.endTransmission() != 0) {
        return false;                        // no ACK from sensor
    }

    Wire1.requestFrom((int)TFLUNA_I2C_ADDR, 6);
    int i = 0;
    while (Wire1.available() && i < 6) {
        buf[i++] = Wire1.read();
    }
    if (i != 6) return false;

    uint16_t dist_mm = buf[0] | (buf[1] << 8);   // distance (mm)
    strength         = buf[2] | (buf[3] << 8);   // signal amplitude

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
