/**
 * @file timelapse.cpp
 * @brief GEOPIX Timelapse System implementation
 * @date 2026-07-02
 */

#include "timelapse.h"
#include "../trigger_mode/trigger_mode.h"
#include <Preferences.h>

// Global instance
Timelapse timelapse;

// ============ CONSTRUCTOR ============
Timelapse::Timelapse() {
    // Default config set in header
}

// ============ INITIALIZATION ============
void Timelapse::init() {
    // Initialize G2 output pin (Port B Yellow, GPIO 2) for camera trigger
    pinMode(TRIGGER_G2_PIN, OUTPUT);
    digitalWrite(TRIGGER_G2_PIN, LOW);
    
    // Also init triggerMode so both G1/G2 GPIO are configured and config loaded
    triggerMode.init();
    
    // Load saved configuration
    loadConfig();
    
    // Validate config
    validateConfig();
    
    // Initialize state
    state.isRunning = false;
    state.lastShotTime = millis();  // prevent immediate trigger if enable=true on load
    state.shotCount = 0;
}

// ============ CONFIG MANAGEMENT ============
void Timelapse::loadConfig() {
    Preferences prefs;
    prefs.begin("timelapse");
    
    config.intervalMs = prefs.getInt("interval", 5000);
    config.totalShots = prefs.getInt("totalShots", 0);
    config.enable = prefs.getBool("enable", false);
    
    prefs.end();
    
    validateConfig();
}

void Timelapse::saveConfig() {
    Preferences prefs;
    prefs.begin("timelapse");
    
    prefs.putInt("interval", config.intervalMs);
    prefs.putInt("totalShots", config.totalShots);
    prefs.putBool("enable", config.enable);
    
    prefs.end();
}

void Timelapse::validateConfig() {
    // Clamp Interval (100ms to 1 hour)
    if (config.intervalMs < 100) config.intervalMs = 100;
    if (config.intervalMs > 3600000) config.intervalMs = 3600000;
    
    // Clamp Total Shots (0 to 10000)
    if (config.totalShots < 0) config.totalShots = 0;
    if (config.totalShots > 10000) config.totalShots = 10000;
}

// ============ MAIN UPDATE ============
void Timelapse::update() {
    if (!config.enable) return;
    
    unsigned long now = millis();
    
    // Interval check
    if (now - state.lastShotTime >= config.intervalMs) {
        
        // Check max shots
        if (config.totalShots != 0 && state.shotCount >= config.totalShots) {
            config.enable = false;
            state.isRunning = false;
            saveConfig();
            return;
        }
        
        // Trigger camera with lock
        triggerCamera();
        
        state.lastShotTime = now;
        state.shotCount++;
    }
    
    state.isRunning = config.enable;
}

// ============ CAMERA TRIGGER ============
void Timelapse::triggerCamera() {
    // Acquire global trigger lock to prevent conflict with Auto Shoot
    if (!acquireTriggerLock()) return;

    // Fire G2 (Trigger) and/or G1 (Remote) based on TriggerMode config
    // If neither enabled, default to G2 (main trigger always works)
    bool fireG2 = triggerMode.config.triggerEnabled;
    bool fireG1 = triggerMode.config.remoteEnabled;
    if (!fireG2 && !fireG1) fireG2 = true;

    if (fireG2) digitalWrite(TRIGGER_G2_PIN, HIGH);
    if (fireG1) digitalWrite(TRIGGER_G1_PIN, HIGH);
    delay(30);
    if (fireG2) digitalWrite(TRIGGER_G2_PIN, LOW);
    if (fireG1) digitalWrite(TRIGGER_G1_PIN, LOW);

    releaseTriggerLock();
}

// ============ CONTROL ============
void Timelapse::start() {
    config.enable = true;
    state.isRunning = true;
    state.lastShotTime = millis();
    state.shotCount = 0;
    saveConfig();
}

void Timelapse::stop() {
    config.enable = false;
    state.isRunning = false;
    saveConfig();
}

void Timelapse::toggleEnable() {
    config.enable = !config.enable;
    if (config.enable) {
        state.lastShotTime = millis();
        state.shotCount = 0;
    }
    state.isRunning = config.enable;
    saveConfig();
}

// ============ UI INTERACTION ============
void Timelapse::handleEncoderRotate(int delta) {
    if (editMode.state == TimelapseEditMode::SELECTING) {
        // Navigate between items (0-2)
        int newIndex = editMode.selectedIndex + (delta > 0 ? 1 : -1);
        if (newIndex >= 0 && newIndex <= 2) {
            editMode.selectedIndex = newIndex;
        }
    }
    else if (editMode.state == TimelapseEditMode::EDITING) {
        // Edit selected value - normalize delta
        if (delta > 0) delta = 1;
        else if (delta < 0) delta = -1;
        
        switch (editMode.selectedIndex) {
            case 0:  // Interval
                config.intervalMs += (delta * 100);
                break;
            case 1:  // Total Shots
                config.totalShots += delta;
                break;
            case 2:  // Enable toggle (handled by button press)
                break;
        }
        
        validateConfig();
    }
}

void Timelapse::handleButtonPress() {
    if (editMode.state == TimelapseEditMode::SELECTING) {
        // Enter edit mode for selected item
        if (editMode.selectedIndex == 2) {
            // Enable toggle - immediate action
            toggleEnable();
        } else {
            // Enter edit mode
            editMode.state = TimelapseEditMode::EDITING;
            editMode.enterTime = millis();
        }
    }
    else if (editMode.state == TimelapseEditMode::EDITING) {
        // Save and exit edit mode
        saveConfig();
        editMode.state = TimelapseEditMode::SELECTING;
    }
}

void Timelapse::handleButtonLongPress() {
    // Long press: exit back to menu
    editMode.state = TimelapseEditMode::IDLE;
    editMode.selectedIndex = 0;
    stop();
}

// ============ GETTERS ============
const char* Timelapse::getSelectedItemName() {
    switch (editMode.selectedIndex) {
        case 0: return "Interval";
        case 1: return "Total Shots";
        case 2: return "Enable";
        default: return "Unknown";
    }
}

int Timelapse::getSelectedValue() {
    switch (editMode.selectedIndex) {
        case 0: return config.intervalMs;
        case 1: return config.totalShots;
        case 2: return config.enable ? 1 : 0;
        default: return 0;
    }
}

const char* Timelapse::getStatusString() {
    if (!config.enable) return "IDLE";
    if (state.isRunning) return "RUNNING";
    return "STOPPED";
}

int Timelapse::getShotCount() const {
    return state.shotCount;
}

int Timelapse::getRemainingShots() const {
    if (config.totalShots == 0) return -1;  // Infinite
    return config.totalShots - state.shotCount;
}

unsigned long Timelapse::getTimeUntilNextShot() const {
    if (!config.enable) return 0;
    unsigned long elapsed = millis() - state.lastShotTime;
    if (elapsed >= config.intervalMs) return 0;
    return config.intervalMs - elapsed;
}

bool Timelapse::isRunning() const {
    return state.isRunning;
}

bool Timelapse::isEnabled() const {
    return config.enable;
}
