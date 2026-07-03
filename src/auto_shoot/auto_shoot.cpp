/**
 * @file auto_shoot.cpp
 * @brief Auto Shoot system implementation with optimized TF-Luna
 * @date 2026-07-01
 */

#include "auto_shoot.h"
#include "tf_luna.h"
#include "../common/hardware_config.h"
#include "../trigger_mode/trigger_mode.h"
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
    pinMode(CAMERA_TRIGGER_PIN, OUTPUT);
    digitalWrite(CAMERA_TRIGGER_PIN, LOW);
    
    // Load saved configuration
    loadConfig();
    
    // Validate config
    validateConfig();
    
    // Initialize state
    state.isRunning = false;
    state.wasInRange = false;
    state.objectDetected = false;
    state.lastTrigger = 0;
    state.lastUpdate = 0;
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
    
    // Update at ~10Hz (100ms interval)
    if (now - state.lastUpdate < 100) return;
    state.lastUpdate = now;
    
    // 1. Read and process sensor
    updateSensorData();
    
    // 2. Check and trigger
    checkAndTrigger();
}

// ============ SENSOR ============
void AutoShoot::updateSensorData() {
    // Use filtered distance (motion-aware)
    state.currentDistance = readTfLunaDistance();
    state.currentStrength = getTfLunaStrength();
    
    // Get motion detection
    bool hasMotion = isTfLunaMotionDetected();
    
    // Check if in range AND sensor is fresh
    bool inRange = (state.currentDistance >= config.rangeMin &&
                   state.currentDistance <= config.rangeMax);
    
    // Object detected only if in range with motion
    state.objectDetected = inRange && hasMotion;
}

// ============ TRIGGER LOGIC ============
void AutoShoot::checkAndTrigger() {
    unsigned long now = millis();
    
    // Edge detection: object ENTERED zone (transition from out to in)
    if (state.objectDetected && !state.wasInRange) {
        
        // Check cooldown before triggering
        if (now - state.lastTrigger > config.cooldownMs) {
            
            // Trigger burst shots
            triggerBurst(config.burstShots);
            
            state.lastTrigger = now;
        }
    }
    
    // Update previous state for next cycle
    state.wasInRange = state.objectDetected;
}

// ============ CAMERA TRIGGER ============
void AutoShoot::triggerCamera() {
    // Use Trigger Mode to handle Trigger and WiFi/BT outputs
    // Trigger both if enabled
    triggerMode.triggerBoth();
}

void AutoShoot::triggerBurst(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        triggerCamera();
        
        // Inter-shot delay: 100ms between shots
        if (i < count - 1) {
            delay(100);
        }
    }
}

// ============ UI INTERACTION ============
void AutoShoot::handleEncoderRotate(int delta) {
    if (editMode.state == EditMode::SELECTING) {
        // Navigate between items (0-3)
        int newIndex = editMode.selectedIndex + (delta > 0 ? 1 : -1);
        if (newIndex >= 0 && newIndex <= 3) {
            editMode.selectedIndex = newIndex;
        }
    }
    else if (editMode.state == EditMode::EDITING) {
        // Edit selected value - normalize delta
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
        // Enter edit mode for selected item
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
    // Long press: exit back to menu
    editMode.state = EditMode::IDLE;
    editMode.selectedIndex = 0;
    state.isRunning = false;
}

// ============ CONTROL ============
void AutoShoot::start() {
    state.isRunning = true;
    state.wasInRange = false;
    state.lastTrigger = 0;
}

void AutoShoot::stop() {
    state.isRunning = false;
    state.wasInRange = false;
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
        case 2: return (float)config.burstShots;
        case 3: return (float)config.cooldownMs;
        default: return 0.0f;
    }
}

const char* AutoShoot::getStatusString() {
    if (!state.isRunning) return "IDLE";
    if (state.objectDetected) return "DETECTING";
    return "ACTIVE";
}

uint16_t AutoShoot::getTriggeredCount() const {
    return state.triggerCount;
}

unsigned long AutoShoot::getTimeSinceLastTrigger() const {
    return millis() - state.lastTrigger;
}

bool AutoShoot::isRunning() const {
    return state.isRunning;
}

bool AutoShoot::isObjectDetected() const {
    return state.objectDetected;
}
