/**
 * @file auto_shoot.cpp
 * @brief Auto Shoot system implementation
 * @date 2026-07-01
 */

#include "auto_shoot.h"
#include "tf_luna.h"
#include <Preferences.h>

// Global instance
AutoShoot autoShoot;

// ============ CONSTRUCTOR ============
AutoShoot::AutoShoot() {
    // Default config set in header
}

// ============ INITIALIZATION ============
void AutoShoot::init() {
    // Initialize TF-Luna sensor
    initTfLuna();
    
    // Initialize GPIO2 for camera trigger
    pinMode(2, OUTPUT);
    digitalWrite(2, LOW);
    
    // Load saved configuration
    loadConfig();
    
    // Validate config
    validateConfig();
}

// ============ CONFIG MANAGEMENT ============
void AutoShoot::loadConfig() {
    Preferences prefs;
    prefs.begin("autoShoot");
    
    config.rangeMin = prefs.getFloat("minR", 0.5f);
    config.rangeMax = prefs.getFloat("maxR", 4.0f);
    config.burstShots = prefs.getInt("burst", 1);
    config.cooldownMs = prefs.getInt("cooldown", 500);
    
    prefs.end();
    
    validateConfig();
}

void AutoShoot::saveConfig() {
    Preferences prefs;
    prefs.begin("autoShoot");
    
    prefs.putFloat("minR", config.rangeMin);
    prefs.putFloat("maxR", config.rangeMax);
    prefs.putInt("burst", config.burstShots);
    prefs.putInt("cooldown", config.cooldownMs);
    
    prefs.end();
}

void AutoShoot::validateConfig() {
    // Clamp Range Min
    if (config.rangeMin < 0.1f) config.rangeMin = 0.1f;
    if (config.rangeMin > 8.0f) config.rangeMin = 8.0f;
    
    // Clamp Range Max
    if (config.rangeMax < 0.1f) config.rangeMax = 0.1f;
    if (config.rangeMax > 8.0f) config.rangeMax = 8.0f;
    
    // AUTO SWAP if min > max
    if (config.rangeMin > config.rangeMax) {
        float temp = config.rangeMin;
        config.rangeMin = config.rangeMax;
        config.rangeMax = temp;
    }
    
    // Clamp Burst Shots
    if (config.burstShots < 1) config.burstShots = 1;
    if (config.burstShots > 10) config.burstShots = 10;
    
    // Clamp Cooldown
    if (config.cooldownMs < 50) config.cooldownMs = 50;
    if (config.cooldownMs > 5000) config.cooldownMs = 5000;
}

// ============ MAIN UPDATE ============
void AutoShoot::update() {
    if (!state.isRunning) return;
    
    unsigned long now = millis();
    
    // Update at ~10Hz
    if (now - state.lastUpdate < 100) return;
    state.lastUpdate = now;
    
    // 1. Read sensor
    updateSensorData();
    
    // 2. Check and trigger
    checkAndTrigger();
}

// ============ SENSOR ============
void AutoShoot::updateSensorData() {
    state.currentDistance = readTfLunaDistance();
    
    // Check if in range
    state.objectDetected = (state.currentDistance >= config.rangeMin &&
                           state.currentDistance <= config.rangeMax);
}

// ============ TRIGGER LOGIC ============
void AutoShoot::checkAndTrigger() {
    unsigned long now = millis();
    
    // Edge detection: object entered zone
    if (state.objectDetected && !state.wasInRange) {
        
        // Check cooldown
        if (now - state.lastTrigger > config.cooldownMs) {
            
            // Trigger burst
            triggerBurst(config.burstShots);
            
            state.lastTrigger = now;
        }
    }
    
    // Update previous state
    state.wasInRange = state.objectDetected;
}

// ============ CAMERA TRIGGER ============
void AutoShoot::triggerCamera() {
    // 30ms pulse
    digitalWrite(2, HIGH);
    delayMicroseconds(30000);  // 30ms
    digitalWrite(2, LOW);
}

void AutoShoot::triggerBurst(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        triggerCamera();
        
        // Inter-shot delay: 100ms
        if (i < count - 1) {
            delay(100);
        }
    }
}

// ============ UI INTERACTION ============
void AutoShoot::handleEncoderRotate(int delta) {
    if (editMode.state == EditMode::SELECTING) {
        // Navigate between items
        int newIndex = editMode.selectedIndex + (delta > 0 ? 1 : -1);
        if (newIndex >= 0 && newIndex <= 3) {
            editMode.selectedIndex = newIndex;
        }
    }
    else if (editMode.state == EditMode::EDITING) {
        // Edit selected value
        if (delta > 0) delta = 1;
        else if (delta < 0) delta = -1;
        
        switch (editMode.selectedIndex) {
            case 0:  // Range Min
                config.rangeMin += (delta * 0.1f);
                break;
            case 1:  // Range Max
                config.rangeMax += (delta * 0.1f);
                break;
            case 2:  // Burst Shots
                config.burstShots += delta;
                break;
            case 3:  // Cooldown
                config.cooldownMs += (delta * 50);
                break;
        }
        
        validateConfig();
    }
}

void AutoShoot::handleButtonPress() {
    if (editMode.state == EditMode::SELECTING) {
        // Enter edit mode
        editMode.state = EditMode::EDITING;
        editMode.enterTime = millis();
    }
    else if (editMode.state == EditMode::EDITING) {
        // Save and exit edit mode
        saveConfig();
        editMode.state = EditMode::SELECTING;
    }
}

void AutoShoot::handleButtonLongPress() {
    // Exit to menu
    editMode.state = EditMode::IDLE;
    editMode.selectedIndex = 0;
}

// ============ GETTERS ============
const char* AutoShoot::getSelectedItemName() {
    switch (editMode.selectedIndex) {
        case 0: return "Range Min";
        case 1: return "Range Max";
        case 2: return "Burst Shots";
        case 3: return "Cooldown";
        default: return "Unknown";
    }
}

float AutoShoot::getSelectedValue() {
    switch (editMode.selectedIndex) {
        case 0: return config.rangeMin;
        case 1: return config.rangeMax;
        case 2: return config.burstShots;
        case 3: return config.cooldownMs;
        default: return 0.0f;
    }
}

const char* AutoShoot::getStatusString() {
    if (!state.isRunning) return "IDLE";
    if (state.objectDetected) return "DETECTING";
    return "ACTIVE";
}
