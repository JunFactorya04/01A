/**
 * @file tf_luna.cpp
 * @brief TF-Luna LiDAR I2C sensor API implementation
 * @date 2026-07-07
 */

#include "tf_luna.h"
#include "../common/hardware_config.h"
#include <Wire.h>

// Global instance
TFLuna tfLuna;

// ============ INITIALIZATION ============
void TFLuna::begin() {
    // Dedicated I2C bus (Wire1) for TF-Luna — DinMeter Port A
    Wire1.begin(TFLUNA_SDA_PIN, TFLUNA_SCL_PIN);

    _distance_m     = 0.0f;
    _strength       = 0;
    _valid          = false;
    _lastUpdateTime = millis();
}

// ============ RAW I2C READ ============
// Reads 6 registers starting at DIST_LO:
//   dist (cm, little-endian), flux, temp — temp is read but unused.
bool TFLuna::readRegisters(uint16_t &dist_cm, uint16_t &strength) {
    Wire1.beginTransmission(TFLUNA_I2C_ADDR);
    Wire1.write((uint8_t)TFL_REG_DIST_LO);
    if (Wire1.endTransmission() != 0) {
        return false;                       // no ACK from sensor
    }

    uint8_t buf[6];
    Wire1.requestFrom((int)TFLUNA_I2C_ADDR, 6);
    int i = 0;
    while (Wire1.available() && i < 6) {
        buf[i++] = Wire1.read();
    }
    if (i != 6) return false;               // short read

    dist_cm  = buf[0] | (buf[1] << 8);      // distance (cm)
    strength = buf[2] | (buf[3] << 8);      // signal amplitude (flux)
    return true;
}

// ============ MAIN UPDATE ============
bool TFLuna::update() {
    uint16_t dist_cm = 0;
    uint16_t strength = 0;

    if (!readRegisters(dist_cm, strength)) {
        _valid = false;                     // I2C failed this cycle
        return false;
    }

    // Convert cm -> meters, clamp to sensor max range.
    // 0 = no return (no object). No aggressive strength filtering.
    float d = dist_cm / 100.0f;
    if (d > TFLUNA_MAX_DISTANCE_M) d = TFLUNA_MAX_DISTANCE_M;

    _distance_m     = d;
    _strength       = strength;
    _valid          = true;
    _lastUpdateTime = millis();
    return true;
}
