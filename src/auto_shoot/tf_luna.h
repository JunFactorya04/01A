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
#define TFLUNA_MAX_DISTANCE 8.0f     // Meters (sensor max)
#define TFLUNA_FAR_SENTINEL 99.0f    // "no object" distance (out of any zone)
#define TFLUNA_MIN_STRENGTH 100      // reject weak/noisy returns below this amplitude
#define TFLUNA_HYSTERESIS   0.10f    // meters of boundary hysteresis (anti-flicker)

// ============ SENSOR STATE ============
struct TFLunaState {
    float distance_m = TFLUNA_FAR_SENTINEL;   // gated distance for zone logic
    float rawDistance_m = 0.0f;               // actual sensor reading (for display)
    uint16_t strength = 0;
    bool valid = false;          // I2C frame read OK this cycle
    bool objectPresent = false;  // real return: strength OK and dist_mm > 0
    unsigned long lastUpdateTime = 0;
};

static TFLunaState tfLunaState;

// ============ INITIALIZATION ============
void initTfLuna() {
    // Dedicated I2C bus (Wire1) for TF-Luna
    Wire1.begin(TFLUNA_SDA_PIN, TFLUNA_SCL_PIN);
    tfLunaState.distance_m     = TFLUNA_FAR_SENTINEL;
    tfLunaState.valid          = false;
    tfLunaState.objectPresent  = false;
    tfLunaState.lastUpdateTime = millis();
}

// ============ RAW READ (I2C register mode) ============
// TF-Luna I2C returns raw registers (NO 0x59 header):
//   0x00 DIST_LOW  0x01 DIST_HIGH  0x02 AMP_LOW  0x03 AMP_HIGH
// A 0mm reading = NO RETURN (nothing detected). A weak amplitude = noise.
// Both are reported as "no object" (far sentinel) so they never trigger.
bool readTfLunaRaw(float &distance_m, uint16_t &strength, bool &objectPresent) {

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

    if (dist_mm > 8000) dist_mm = 8000;          // clamp absurd values

    // Actual sensor distance for display (0.0m when no return)
    tfLunaState.rawDistance_m = dist_mm / 1000.0f;

    // ---- NO OBJECT / NOISE FILTER (for detection only) ----
    // 0mm  = no return (nothing in front)  -> not an object
    // weak = amplitude below threshold     -> noise, not an object
    if (dist_mm == 0 || strength < TFLUNA_MIN_STRENGTH) {
        distance_m    = TFLUNA_FAR_SENTINEL; // out of any [min,max] zone
        objectPresent = false;
        return true;
    }

    distance_m    = dist_mm / 1000.0f;
    objectPresent = true;
    return true;
}

// ============ MAIN UPDATE ============
void updateTfLunaRaw() {
    float    d = TFLUNA_FAR_SENTINEL;
    uint16_t s = 0;
    bool     present = false;

    if (!readTfLunaRaw(d, s, present)) {
        tfLunaState.valid = false;             // read failed this cycle
        tfLunaState.objectPresent = false;
        return;
    }

    tfLunaState.distance_m     = d;
    tfLunaState.strength       = s;
    tfLunaState.objectPresent  = present;
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

/** @brief Actual sensor distance in meters (real value for display, 0 = no return). */
float getTfLunaRawDistance() {
    return tfLunaState.rawDistance_m;
}

/** @brief Signal strength (0-65535). */
uint16_t getTfLunaStrength() {
    return tfLunaState.strength;
}

/** @brief Motion detection kept for interface compatibility (unused in pure-threshold). */
bool isTfLunaMotionDetected() {
    return false;
}

/** @brief True if a real object return is present (strength OK and dist > 0). */
bool isTfLunaObjectPresent() {
    return tfLunaState.objectPresent;
}

/** @brief Full sensor state. */
const TFLunaState& getTfLunaState() {
    return tfLunaState;
}

/** @brief True if the last read succeeded within maxAgeMs. */
bool isTfLunaFresh(unsigned long maxAgeMs = 200) {
    return tfLunaState.valid && ((millis() - tfLunaState.lastUpdateTime) < maxAgeMs);
}
