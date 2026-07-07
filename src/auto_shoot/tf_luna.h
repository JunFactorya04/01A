/**
 * @file tf_luna.h
 * @brief TF-Luna LiDAR I2C sensor API (Benewake TF-Luna)
 * @date 2026-07-07
 *
 * I2C ONLY — DinMeter Port A: SDA=GPIO13, SCL=GPIO15, addr=0x10, bus=Wire1
 * (RTC BM8563 uses Wire/bus0 on GPIO11/12 — separate bus, no conflict)
 *
 * Register map (per Benewake datasheet / budryerson TFLuna-I2C):
 *   0x00 DIST_LO   0x01 DIST_HI   (unit: cm)
 *   0x02 FLUX_LO   0x03 FLUX_HI   (signal strength)
 *   0x04 TEMP_LO   0x05 TEMP_HI   (unit: 0.01 C)
 *
 * Flow: TF-Luna I2C -> TFLuna API -> AutoShoot range detection
 *       -> existing Trigger Mode -> G1/G2 output
 */

#pragma once
#include <Arduino.h>

// ============ TF-LUNA CONFIG ============
#define TFLUNA_MAX_DISTANCE_M 8.0f   // sensor max range (meters), clamp above this

// TF-Luna I2C registers
#define TFL_REG_DIST_LO 0x00
#define TFL_REG_FLUX_LO 0x02

// ============ TF-LUNA API ============
class TFLuna {
public:
    /** @brief Init I2C bus (Wire1) and reset state. Call once at startup. */
    void begin();

    /**
     * @brief Poll the sensor once over I2C and refresh internal state.
     * Call every loop cycle (low latency, continuous reading).
     * @return true if the I2C read succeeded this cycle.
     */
    bool update();

    /** @brief Last measured distance in meters (0.0 = no object/no return, clamped to 8.0). */
    float getDistance() const { return _distance_m; }

    /** @brief Last signal strength (flux, 0-65535). */
    uint16_t getStrength() const { return _strength; }

    /** @brief True if the last I2C read succeeded. */
    bool isValid() const { return _valid; }

    /** @brief True if the sensor sees a real return (valid read and distance > 0). */
    bool hasObject() const { return _valid && _distance_m > 0.0f; }

    /**
     * @brief Range detection: object present and minRange <= distance <= maxRange.
     * Range comes dynamically from caller config — never hardcoded here.
     */
    bool inRange(float minRange, float maxRange) const {
        return hasObject() && _distance_m >= minRange && _distance_m <= maxRange;
    }

    /** @brief millis() timestamp of the last successful read. */
    unsigned long getLastUpdateTime() const { return _lastUpdateTime; }

private:
    float _distance_m = 0.0f;
    uint16_t _strength = 0;
    bool _valid = false;
    unsigned long _lastUpdateTime = 0;

    // Raw I2C register read (6 bytes: dist, flux, temp)
    bool readRegisters(uint16_t &dist_cm, uint16_t &strength);
};

// Global instance
extern TFLuna tfLuna;

// ============ LEGACY COMPATIBILITY WRAPPERS ============
// Old free-function interface mapped onto the TFLuna API.
inline void initTfLuna()                    { tfLuna.begin(); }
inline float readTfLunaDistance()           { tfLuna.update(); return tfLuna.getDistance(); }
inline float readTfLunaDistanceRaw()        { return tfLuna.getDistance(); }
inline float getTfLunaRawDistance()         { return tfLuna.getDistance(); }
inline uint16_t getTfLunaStrength()         { return tfLuna.getStrength(); }
inline bool isTfLunaObjectPresent()         { return tfLuna.hasObject(); }
inline bool isTfLunaMotionDetected()        { return tfLuna.hasObject(); }
inline bool isTfLunaFresh(unsigned long maxAgeMs = 200) {
    return tfLuna.isValid() && ((millis() - tfLuna.getLastUpdateTime()) < maxAgeMs);
}
