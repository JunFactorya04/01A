/**
 * @file tf_luna.h
 * @brief TF-Luna LiDAR sensor driver with motion detection & filtering
 * @date 2026-07-01
 */

#pragma once
#include "../common/hardware_config.h"

// ============ TF-LUNA CONFIG ============
#define TFLUNA_MIN_STRENGTH 100      // Minimum signal strength (0-65535)
#define TFLUNA_MIN_DISTANCE 0.1f     // Meters
#define TFLUNA_MAX_DISTANCE 8.0f     // Meters
#define TFLUNA_FILTER_ALPHA 0.6f     // Low-pass filter (0.0-1.0)
#define TFLUNA_MOTION_THRESHOLD 0.05f // 5cm for motion detection
#define TFLUNA_OUTLIER_THRESHOLD 0.5f // 50cm max jump (outlier detection)

// ============ SENSOR STATE ============
struct TFLunaState {
    float distance_m = 0.0f;
    float distance_filtered = 0.0f;
    uint16_t strength = 0;
    bool hasMotion = false;
    float velocity = 0.0f;  // Change per update
    unsigned long lastUpdateTime = 0;
};

static TFLunaState tfLunaState;

// ============ INITIALIZATION ============
void initTfLuna() {
    TFSerial.begin(115200, SERIAL_8N1, TFLUNA_RX_PIN, -1);
    delay(100);
    tfLunaState.lastUpdateTime = millis();
}

// ============ RAW READ ============
bool readTfLunaRaw(float &distance_m, uint16_t &strength) {

    // Drain stale frames — keep only the latest one in buffer
    while (TFSerial.available() > 9) {
        TFSerial.read();
    }

    if (TFSerial.available() >= 9) {
        
        uint8_t buf[9];
        TFSerial.readBytes(buf, 9);
        
        // HEADER CHECK (0x59 0x59)
        if (buf[0] == 0x59 && buf[1] == 0x59) {
            
            uint16_t dist_mm = buf[2] + (buf[3] << 8);
            strength = buf[4] + (buf[5] << 8);
            
            distance_m = dist_mm / 1000.0f;
            
            return true;
        }
    }
    
    return false;
}

// ============ FILTERING ============
void applyLowPassFilter(float rawDistance) {
    // Low-pass filter to smooth sensor noise
    // TFLUNA_FILTER_ALPHA closer to 1.0 = more responsive
    // TFLUNA_FILTER_ALPHA closer to 0.0 = more smooth
    tfLunaState.distance_filtered = 
        (rawDistance * TFLUNA_FILTER_ALPHA) + 
        (tfLunaState.distance_filtered * (1.0f - TFLUNA_FILTER_ALPHA));
}

// ============ VALIDATION ============
bool validateDistance(float distance_m, uint16_t strength) {
    
    // 1. Check signal strength
    if (strength < TFLUNA_MIN_STRENGTH) {
        return false;  // Signal too weak
    }
    
    // 2. Check distance range
    if (distance_m < TFLUNA_MIN_DISTANCE || distance_m > TFLUNA_MAX_DISTANCE) {
        return false;  // Out of valid range
    }
    
    // 3. Outlier detection — skip if not yet initialized (distance_m == 0)
    //    At startup distance_m=0, so any real reading would be rejected forever.
    if (tfLunaState.distance_m > 0.0f) {
        float delta = abs(distance_m - tfLunaState.distance_m);
        if (delta > TFLUNA_OUTLIER_THRESHOLD) {
            return false;  // Possible outlier/spike
        }
    }
    
    return true;
}

// ============ MOTION DETECTION ============
void detectMotion(float currentDistance) {
    
    // Calculate velocity (change in distance)
    tfLunaState.velocity = abs(currentDistance - tfLunaState.distance_m);
    
    // Detect motion if velocity exceeds threshold
    tfLunaState.hasMotion = (tfLunaState.velocity > TFLUNA_MOTION_THRESHOLD);
}

// ============ MAIN UPDATE ============
void updateTfLunaRaw() {
    
    float rawDistance = 0.0f;
    uint16_t strength = 0;
    
    // Try to read from sensor
    if (!readTfLunaRaw(rawDistance, strength)) {
        return;  // No valid frame
    }
    
    // Validate reading
    if (!validateDistance(rawDistance, strength)) {
        return;  // Invalid reading
    }
    
    // Update raw distance
    tfLunaState.distance_m = rawDistance;
    tfLunaState.strength = strength;
    
    // Apply filtering
    applyLowPassFilter(rawDistance);
    
    // Detect motion
    detectMotion(rawDistance);
    
    // Update timestamp
    tfLunaState.lastUpdateTime = millis();
}

// ============ PUBLIC INTERFACE ============

/**
 * @brief Get filtered distance (preferred for auto-shoot)
 * @return Smoothed distance in meters
 */
float readTfLunaDistance() {
    updateTfLunaRaw();
    return tfLunaState.distance_filtered;
}

/**
 * @brief Get raw distance without filter
 * @return Raw distance in meters
 */
float readTfLunaDistanceRaw() {
    updateTfLunaRaw();
    return tfLunaState.distance_m;
}

/**
 * @brief Check if object is moving
 * @return true if motion detected
 */
bool isTfLunaMotionDetected() {
    return tfLunaState.hasMotion;
}

/**
 * @brief Get signal strength
 * @return Signal strength (0-65535)
 */
uint16_t getTfLunaStrength() {
    return tfLunaState.strength;
}

/**
 * @brief Get velocity (change rate)
 * @return Velocity in meters per update
 */
float getTfLunaVelocity() {
    return tfLunaState.velocity;
}

/**
 * @brief Get sensor state
 * @return Full sensor state struct
 */
const TFLunaState& getTfLunaState() {
    return tfLunaState;
}

/**
 * @brief Check if sensor is fresh (updated recently)
 * @param maxAgeMs Maximum age in milliseconds
 * @return true if updated within maxAgeMs
 */
bool isTfLunaFresh(unsigned long maxAgeMs = 200) {
    return (millis() - tfLunaState.lastUpdateTime) < maxAgeMs;
}
