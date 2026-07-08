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
    // Release the driver on re-entry, then plain re-init.
    // This is exactly the config that tested best in the field.
    if (_started) {
        Wire1.end();
        delay(2);
    }

    Wire1.begin(TFLUNA_SDA_PIN, TFLUNA_SCL_PIN);
    Wire1.setTimeOut(20);   // bound any blocking I2C op to 20ms

    _started        = true;
    _distance_m     = 0.0f;
    _strength       = 0;
    _valid          = false;
    _failCount      = 0;
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

// ============ SLEEP / WAKE (power saving) ============
// NOTE: kept in the API but NOT called on mode entry/exit. Field testing
// showed TF-Luna register writes (0x25) can stall the sensor for many
// seconds — the stable config never writes registers during normal use.
static bool writeEnableReg(uint8_t value) {
    Wire1.beginTransmission(TFLUNA_I2C_ADDR);
    Wire1.write((uint8_t)TFL_REG_ENABLE);
    Wire1.write(value);
    return Wire1.endTransmission() == 0;
}

void TFLuna::sleep() {
    if (!_started) return;
    writeEnableReg(0);      // stop measuring -> laser off, low power
    _valid = false;
}

void TFLuna::wake() {
    if (!_started) return;
    writeEnableReg(1);      // resume measuring
    _failCount = 0;
    _valid = false;         // next update() refreshes real data
}

// ============ BOOT WARM-UP ============
// Run once at power-on: init the bus early and exercise the sensor so its
// laser/I2C engine is fully settled BEFORE the user enters Auto Shoot.
// Exits early after a burst of good reads; hard-bounded by maxMs.
void TFLuna::warmUp(unsigned long maxMs) {
    begin();

    unsigned long start = millis();
    uint8_t goodReads = 0;

    while ((millis() - start) < maxMs && goodReads < 10) {
        uint16_t d, s;
        if (readRegisters(d, s)) goodReads++;
        delay(10);   // sensor frame pace (100Hz)
    }
}

// ============ SENSOR SOFT RESET ============
// Commands the TF-Luna itself to reboot (register 0x21 = 0x02).
// Used only when the sensor's I2C engine is hung and stops answering.
void TFLuna::softResetSensor() {
    Wire1.beginTransmission(TFLUNA_I2C_ADDR);
    Wire1.write((uint8_t)TFL_REG_SOFT_RESET);
    Wire1.write((uint8_t)0x02);
    Wire1.endTransmission();
    // Sensor reboots in ~500ms; reads fail meanwhile and the normal
    // 50ms backoff absorbs that without blocking the UI.
}

// ============ MAIN UPDATE ============
bool TFLuna::update() {
    // Never touch the bus before begin(): polling an un-initialized bus
    // racks up fails and triggers self-healing register writes (0x21)
    // that stall the sensor for seconds.
    if (!_started) return false;

    unsigned long now = millis();

    // Pace polling to the sensor frame rate (100Hz = 10ms). Hammering the
    // I2C slave faster than it updates its registers is a known cause of
    // TF-Luna bus lock-ups under sustained load.
    if (_failCount == 0 && (now - _lastAttemptTime) < 10) {
        return _valid;
    }

    // Backoff while failing: retry every 50ms so blocked I2C ops
    // (up to 2x20ms each) can't stall the UI/encoder.
    if (_failCount > 0 && (now - _lastAttemptTime) < 50) {
        return false;
    }
    _lastAttemptTime = now;

    uint16_t dist_cm = 0;
    uint16_t strength = 0;

    if (!readRegisters(dist_cm, strength)) {
        _valid = false;                     // I2C failed this cycle

        // Self-healing for a truly hung sensor ("overload"): restart the
        // I2C driver AND command the TF-Luna to reboot itself (reg 0x21).
        // At most once per 3s; sensor takes ~500ms to come back.
        if (++_failCount >= TFLUNA_MAX_FAILS &&
            (millis() - _lastRecoverTime) > 3000) {
            _lastRecoverTime = millis();
            begin();
            softResetSensor();
        }
        return false;
    }

    _failCount = 0;

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
