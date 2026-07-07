/**
 * @file auto_shoot.h
 * @brief Auto Shoot system - TF-Luna LiDAR + Camera trigger
 * @date 2026-07-01
 */

#pragma once
#include <Arduino.h>

// ============ CONFIG STRUCTURE ============
struct AutoShootConfig {
    float rangeMin = 0.1f;      // meters
    float rangeMax = 8.0f;      // meters
    uint8_t burstShots = 1;     // 1-10 shots
    uint16_t cooldownMs = 500;  // milliseconds
};

// ============ STATE STRUCTURE ============
struct AutoShootState {
    bool isRunning = false;
    bool wasInRange = false;
    bool objectDetected = false;
    unsigned long lastTrigger = 0;
    unsigned long lastUpdate = 0;
    
    float currentDistance = 0.0f;
    uint16_t currentStrength = 0;
    uint16_t triggerCount = 0;  // Total triggers since start
};

// ============ EDIT MODE ============
struct EditMode {
    enum EditState {
        IDLE = 0,
        SELECTING = 1,
        EDITING = 2
    } state = IDLE;
    
    uint8_t selectedIndex = 0;  // 0: rangeMin, 1: rangeMax, 2: burst, 3: cooldown
    unsigned long enterTime = 0;
};

// ============ AUTO SHOOT CLASS ============
class AutoShoot {
public:
    AutoShootConfig config;
    AutoShootState state;
    EditMode editMode;
    
    AutoShoot();
    ~AutoShoot() = default;
    
    // ===== Initialization =====
    void init();
    void loadConfig();
    void saveConfig();
    
    // ===== Main Loop =====
    void update();
    
    // ===== Control =====
    void start();
    void stop();
    
    // ===== Camera Trigger =====
    void triggerCamera();
    void triggerBurst(uint8_t count);
    
    // ===== Sensor Processing =====
    void updateSensorData();
    
    // ===== Auto-Shoot Logic =====
    void checkAndTrigger();
    
    // ===== UI Interaction =====
    void handleEncoderRotate(int delta);
    void handleButtonPress();
    void handleButtonLongPress();
    
    // ===== Getters =====
    const char* getSelectedItemName();
    float getSelectedValue();
    const char* getStatusString();
    uint16_t getTriggeredCount() const;
    unsigned long getTimeSinceLastTrigger() const;
    bool isRunning() const;
    bool isObjectDetected() const;
    
private:
    void validateConfig();
};

// Global instance
extern AutoShoot autoShoot;
