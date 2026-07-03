/**
 * @file trigger_mode.cpp
 * @brief Trigger Mode implementation - Dual output trigger system
 * @date 2026-07-03
 */

#include "trigger_mode.h"
#include <Preferences.h>

// Global instance
TriggerMode triggerMode;

// ============ CONSTRUCTOR ============
TriggerMode::TriggerMode() {
    // Default config set in header
}

// ============ INITIALIZATION ============
void TriggerMode::init() {
    // Initialize GPIO G1, G2 for trigger outputs
    pinMode(TRIGGER_G1_PIN, OUTPUT);
    digitalWrite(TRIGGER_G1_PIN, LOW);
    
    pinMode(TRIGGER_G2_PIN, OUTPUT);
    digitalWrite(TRIGGER_G2_PIN, LOW);
    
    // Load saved configuration
    loadConfig();
    
    // Validate config
    validateConfig();
    
    // Initialize state
    state.isRunning = (config.triggerEnabled || config.wifiBtEnabled);
    state.lastTriggerTime = 0;
    state.triggerCount = 0;
}

// ============ CONFIG MANAGEMENT ============
void TriggerMode::loadConfig() {
    Preferences prefs;
    prefs.begin("triggerMode");
    
    config.triggerEnabled = prefs.getBool("triggerEnabled", false);
    config.wifiBtEnabled = prefs.getBool("wifiBtEnabled", false);
    
    prefs.end();
}

void TriggerMode::saveConfig() {
    Preferences prefs;
    prefs.begin("triggerMode");
    
    prefs.putBool("triggerEnabled", config.triggerEnabled);
    prefs.putBool("wifiBtEnabled", config.wifiBtEnabled);
    
    prefs.end();
}

void TriggerMode::validateConfig() {
    // No validation needed for boolean flags
}

// ============ MAIN UPDATE ============
void TriggerMode::update() {
    // Update running state based on enabled outputs
    state.isRunning = (config.triggerEnabled || config.wifiBtEnabled);
    
    // Trigger logic will be handled by Auto Shoot or Timelapse
    // This mode only controls which outputs are active
}

// ============ CONTROL ============
void TriggerMode::toggleTrigger() {
    config.triggerEnabled = !config.triggerEnabled;
    state.isRunning = (config.triggerEnabled || config.wifiBtEnabled);
    saveConfig();
}

void TriggerMode::toggleWifiBt() {
    config.wifiBtEnabled = !config.wifiBtEnabled;
    state.isRunning = (config.triggerEnabled || config.wifiBtEnabled);
    saveConfig();
}

void TriggerMode::enableAll() {
    config.triggerEnabled = true;
    config.wifiBtEnabled = true;
    state.isRunning = true;
    saveConfig();
}

void TriggerMode::disableAll() {
    config.triggerEnabled = false;
    config.wifiBtEnabled = false;
    state.isRunning = false;
    saveConfig();
}

void TriggerMode::testTrigger() {
    // Manual test trigger
    triggerOut();
    if (config.wifiBtEnabled) {
        triggerWifiBt();
    }
}

// ============ TRIGGER FUNCTIONS ============
void TriggerMode::triggerOut() {
    if (!config.triggerEnabled) return;
    
    // Acquire global trigger lock
    if (!acquireTriggerLock()) return;
    
    digitalWrite(TRIGGER_G1_PIN, HIGH);
    delay(30);
    digitalWrite(TRIGGER_G1_PIN, LOW);
    
    state.lastTriggerTime = millis();
    state.triggerCount++;
    
    releaseTriggerLock();
}

void TriggerMode::triggerWifiBt() {
    if (!config.wifiBtEnabled) return;
    
    // Acquire global trigger lock
    if (!acquireTriggerLock()) return;
    
    digitalWrite(TRIGGER_G2_PIN, HIGH);
    delay(30);
    digitalWrite(TRIGGER_G2_PIN, LOW);
    
    state.lastTriggerTime = millis();
    state.triggerCount++;
    
    releaseTriggerLock();
}

void TriggerMode::triggerBoth() {
    if (!config.triggerEnabled && !config.wifiBtEnabled) return;
    
    // Acquire global trigger lock
    if (!acquireTriggerLock()) return;
    
    if (config.triggerEnabled) {
        digitalWrite(TRIGGER_G1_PIN, HIGH);
    }
    if (config.wifiBtEnabled) {
        digitalWrite(TRIGGER_G2_PIN, HIGH);
    }
    
    delay(30);
    
    if (config.triggerEnabled) {
        digitalWrite(TRIGGER_G1_PIN, LOW);
    }
    if (config.wifiBtEnabled) {
        digitalWrite(TRIGGER_G2_PIN, LOW);
    }
    
    state.lastTriggerTime = millis();
    state.triggerCount++;
    
    releaseTriggerLock();
}

// ============ UI INTERACTION ============
void TriggerMode::handleEncoderRotate(int delta) {
    if (editMode.state == TriggerEditMode::SELECTING) {
        // Navigate between G1 and G2
        int newIndex = editMode.selectedIndex + (delta > 0 ? 1 : -1);
        if (newIndex >= 0 && newIndex <= 1) {
            editMode.selectedIndex = newIndex;
        }
    }
}

void TriggerMode::handleButtonPress() {
    if (editMode.state == TriggerEditMode::SELECTING) {
        // Toggle selected output
        if (editMode.selectedIndex == 0) {
            toggleTrigger();
        } else {
            toggleWifiBt();
        }
    }
}

void TriggerMode::handleButtonLongPress() {
    // Long press: disable all and exit
    disableAll();
    editMode.state = TriggerEditMode::IDLE;
    editMode.selectedIndex = 0;
}

// ============ GETTERS ============
const char* TriggerMode::getSelectedItemName() {
    switch (editMode.selectedIndex) {
        case 0: return "Trigger";
        case 1: return "WiFi/BT";
        default: return "Unknown";
    }
}

bool TriggerMode::getTriggerEnabled() const {
    return config.triggerEnabled;
}

bool TriggerMode::getWifiBtEnabled() const {
    return config.wifiBtEnabled;
}

bool TriggerMode::isRunning() const {
    return state.isRunning;
}

uint16_t TriggerMode::getTriggerCount() const {
    return state.triggerCount;
}
