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
    state.isRunning = (config.triggerEnabled || config.remoteEnabled);
    state.lastTriggerTime = 0;
    state.triggerCount = 0;
}

// ============ CONFIG MANAGEMENT ============
void TriggerMode::loadConfig() {
    Preferences prefs;
    prefs.begin("triggerMode");
    
    config.triggerEnabled = prefs.getBool("triggerEnabled", true);   // G2 default ON
    config.remoteEnabled  = prefs.getBool("remoteEnabled",  false);  // G1 default OFF
    
    prefs.end();
}

void TriggerMode::saveConfig() {
    Preferences prefs;
    prefs.begin("triggerMode");
    
    prefs.putBool("triggerEnabled", config.triggerEnabled);
    prefs.putBool("remoteEnabled",  config.remoteEnabled);
    
    prefs.end();
}

void TriggerMode::validateConfig() {
    // No validation needed for boolean flags
}

// ============ MAIN UPDATE ============
void TriggerMode::update() {
    state.isRunning = (config.triggerEnabled || config.remoteEnabled);
}

// ============ CONTROL ============
void TriggerMode::toggleTrigger() {
    config.triggerEnabled = !config.triggerEnabled;
    state.isRunning = (config.triggerEnabled || config.remoteEnabled);
    saveConfig();
}

void TriggerMode::toggleRemote() {
    config.remoteEnabled = !config.remoteEnabled;
    state.isRunning = (config.triggerEnabled || config.remoteEnabled);
    saveConfig();
}

void TriggerMode::enableAll() {
    config.triggerEnabled = true;
    config.remoteEnabled  = true;
    state.isRunning = true;
    saveConfig();
}

void TriggerMode::disableAll() {
    config.triggerEnabled = false;
    config.remoteEnabled  = false;
    state.isRunning = false;
    saveConfig();
}

void TriggerMode::testTrigger() {
    // Fire both simultaneously (same as AUTO SHOOT / TIMELAPSE behavior)
    triggerBoth();
}

// ============ TRIGGER FUNCTIONS ============
// triggerOut()  — fires G2 (Port B Yellow, GPIO 2) = Trigger (main)
void TriggerMode::triggerOut() {
    if (!config.triggerEnabled) return;
    
    if (!acquireTriggerLock()) return;
    
    digitalWrite(TRIGGER_G2_PIN, HIGH);
    delay(30);
    digitalWrite(TRIGGER_G2_PIN, LOW);
    
    state.lastTriggerTime = millis();
    state.triggerCount++;
    
    releaseTriggerLock();
}

// triggerRemote() — fires G1 (Port B White, GPIO 1) = Remote (backup)
void TriggerMode::triggerRemote() {
    if (!config.remoteEnabled) return;
    
    if (!acquireTriggerLock()) return;
    
    digitalWrite(TRIGGER_G1_PIN, HIGH);
    delay(30);
    digitalWrite(TRIGGER_G1_PIN, LOW);
    
    state.lastTriggerTime = millis();
    state.triggerCount++;
    
    releaseTriggerLock();
}

void TriggerMode::triggerBoth() {
    if (!config.triggerEnabled && !config.remoteEnabled) return;
    
    if (!acquireTriggerLock()) return;
    
    if (config.triggerEnabled) digitalWrite(TRIGGER_G2_PIN, HIGH);  // G2
    if (config.remoteEnabled)  digitalWrite(TRIGGER_G1_PIN, HIGH);  // G1
    
    delay(30);
    
    if (config.triggerEnabled) digitalWrite(TRIGGER_G2_PIN, LOW);
    if (config.remoteEnabled)  digitalWrite(TRIGGER_G1_PIN, LOW);
    
    state.lastTriggerTime = millis();
    state.triggerCount++;
    
    releaseTriggerLock();
}

// ============ UI INTERACTION ============
void TriggerMode::handleEncoderRotate(int delta) {
    if (editMode.state == TriggerEditMode::SELECTING) {
        // Navigate: 0=Trigger(G2), 1=Remote(G1), 2=TEST button
        int newIndex = editMode.selectedIndex + (delta > 0 ? 1 : -1);
        if (newIndex >= 0 && newIndex <= 2) {
            editMode.selectedIndex = newIndex;
        }
    }
}

void TriggerMode::handleButtonPress() {
    if (editMode.state == TriggerEditMode::SELECTING) {
        if (editMode.selectedIndex == 0) {
            toggleTrigger();   // G2 — saves config
        } else if (editMode.selectedIndex == 1) {
            toggleRemote();    // G1 — saves config
        } else if (editMode.selectedIndex == 2) {
            testTrigger();     // fire enabled outputs once
        }
    }
}

void TriggerMode::handleButtonLongPress() {
    // Long press: exit only. Config already persisted on each toggle.
    // DO NOT disableAll() — that would wipe the master-switch settings.
    editMode.state = TriggerEditMode::IDLE;
    editMode.selectedIndex = 0;
}

// ============ GETTERS ============
const char* TriggerMode::getSelectedItemName() {
    switch (editMode.selectedIndex) {
        case 0: return "Trigger";  // G2
        case 1: return "Remote";   // G1
        default: return "Unknown";
    }
}

bool TriggerMode::getTriggerEnabled() const {
    return config.triggerEnabled;
}

bool TriggerMode::getRemoteEnabled() const {
    return config.remoteEnabled;
}

bool TriggerMode::isRunning() const {
    return state.isRunning;
}

uint16_t TriggerMode::getTriggerCount() const {
    return state.triggerCount;
}
