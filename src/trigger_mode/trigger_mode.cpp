/**
 * @file trigger_mode.cpp
 * @brief Trigger Mode implementation - Dual output trigger system
 * @date 2026-07-03
 */

#include "trigger_mode.h"
#include "../remote/remote_manager.h"
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
    RemoteManager::loadConfig();
    
    // Validate config
    validateConfig();
    
    // Initialize state
    state.isRunning = (config.triggerEnabled || config.remoteEnabled || config.bluetoothEnabled);
    state.lastTriggerTime = 0;
    state.triggerCount = 0;
}

// ============ CONFIG MANAGEMENT ============
void TriggerMode::loadConfig() {
    Preferences prefs;
    prefs.begin("triggerMode");
    
    config.triggerEnabled   = prefs.getBool("triggerEnabled", true);   // G2 default ON
    config.remoteEnabled    = prefs.getBool("remoteEnabled",  false);  // G1 default OFF
    config.bluetoothEnabled = prefs.getBool("btEnabled",      false);  // BLE default OFF
    config.beepEnabled      = prefs.getBool("beepEnabled",    true);   // beep default ON
    
    prefs.end();
}

void TriggerMode::saveConfig() {
    Preferences prefs;
    prefs.begin("triggerMode");
    
    prefs.putBool("triggerEnabled", config.triggerEnabled);
    prefs.putBool("remoteEnabled",  config.remoteEnabled);
    prefs.putBool("btEnabled",      config.bluetoothEnabled);
    prefs.putBool("beepEnabled",    config.beepEnabled);
    
    prefs.end();
}

void TriggerMode::validateConfig() {
    // No validation needed for boolean flags
}

// ============ MAIN UPDATE ============
void TriggerMode::update() {
    state.isRunning = (config.triggerEnabled || config.remoteEnabled || config.bluetoothEnabled);
}

// ============ CONTROL ============
void TriggerMode::toggleTrigger() {
    config.triggerEnabled = !config.triggerEnabled;
    state.isRunning = (config.triggerEnabled || config.remoteEnabled || config.bluetoothEnabled);
    saveConfig();
}

void TriggerMode::toggleRemote() {
    config.remoteEnabled = !config.remoteEnabled;
    state.isRunning = (config.triggerEnabled || config.remoteEnabled || config.bluetoothEnabled);
    saveConfig();
}

void TriggerMode::toggleBluetooth() {
    config.bluetoothEnabled = !config.bluetoothEnabled;
    saveConfig();
    // BLE channel OFF -> drop camera connection to free the radio
    if (!config.bluetoothEnabled) {
        RemoteManager::shutdown();
    }
}

void TriggerMode::toggleBeep() {
    config.beepEnabled = !config.beepEnabled;
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
// Short beep on fire ??respects global speaker on/off (Setting mode).
static inline void triggerBeep() {
    if (g_speakerEnabled) {
        tone(BUZZ_PIN, 2500, 40);
    }
}

// triggerOut()  ??fires G2 (Port B Yellow, GPIO 2) = Trigger (main)
void TriggerMode::triggerOut() {
    if (!config.triggerEnabled) return;
    
    if (!acquireTriggerLock()) return;
    
    digitalWrite(TRIGGER_G2_PIN, HIGH);
    triggerBeep();
    delay(30);
    digitalWrite(TRIGGER_G2_PIN, LOW);
    
    state.lastTriggerTime = millis();
    state.triggerCount++;
    
    releaseTriggerLock();
}

// triggerRemote() ??fires G1 (Port B White, GPIO 1) = Remote (backup)
void TriggerMode::triggerRemote() {
    if (!config.remoteEnabled) return;
    
    if (!acquireTriggerLock()) return;
    
    digitalWrite(TRIGGER_G1_PIN, HIGH);
    triggerBeep();
    delay(30);
    digitalWrite(TRIGGER_G1_PIN, LOW);
    
    state.lastTriggerTime = millis();
    state.triggerCount++;
    
    releaseTriggerLock();
}

void TriggerMode::triggerBoth() {
    if (!config.triggerEnabled && !config.remoteEnabled && !config.bluetoothEnabled) return;
    
    if (config.triggerEnabled || config.remoteEnabled) {
        if (!acquireTriggerLock()) return;
        
        if (config.triggerEnabled) digitalWrite(TRIGGER_G2_PIN, HIGH);  // G2
        if (config.remoteEnabled)  digitalWrite(TRIGGER_G1_PIN, HIGH);  // G1
        triggerBeep();
        
        delay(30);
        
        if (config.triggerEnabled) digitalWrite(TRIGGER_G2_PIN, LOW);
        if (config.remoteEnabled)  digitalWrite(TRIGGER_G1_PIN, LOW);
        
        releaseTriggerLock();
    } else {
        triggerBeep();
    }
    
    // BLE channel ??same command, fires after the GPIO pulse
    fireBluetoothIfEnabled();
    
    state.lastTriggerTime = millis();
    state.triggerCount++;
}

// fireBluetoothIfEnabled() ??3rd shooting channel (BLE camera remote).
// Same ON/OFF mechanism as Trigger (G2) / Remote (G1): ON = shoot, OFF = skip.
// Runs after the G1/G2 pulse so GPIO timing is never affected.
void TriggerMode::fireBluetoothIfEnabled() {
    if (!config.bluetoothEnabled) return;
    if (RemoteManager::getBrand() == CameraBrand::None) return;
    RemoteManager::triggerPhoto();
}

// ============ UI INTERACTION ============
void TriggerMode::handleEncoderRotate(int delta) {
    if (editMode.state != TriggerEditMode::SELECTING) return;

    if (editMode.screen == TriggerEditMode::BLUETOOTH) {
        // Navigate: 0=Enable, 1=Driver, 2=PAIR, 3=TEST SHOT, 4=FORGET
        int newIndex = editMode.btIndex + (delta > 0 ? 1 : -1);
        if (newIndex >= 0 && newIndex <= 4) {
            editMode.btIndex = newIndex;
        }
        return;
    }

    // MAIN: 0=Trigger(G2), 1=Remote(G1), 2=Beep, 3=Bluetooth, 4=TEST
    int newIndex = editMode.selectedIndex + (delta > 0 ? 1 : -1);
    if (newIndex >= 0 && newIndex <= 4) {
        editMode.selectedIndex = newIndex;
    }
}

void TriggerMode::handleButtonPress() {
    if (editMode.state != TriggerEditMode::SELECTING) return;

    if (editMode.screen == TriggerEditMode::BLUETOOTH) {
        switch (editMode.btIndex) {
            case 0:    // Bluetooth channel ON/OFF (like Trigger/Remote)
                toggleBluetooth();
                btStatus[0] = '\0';
                break;
            case 1: {  // cycle driver: GPIO -> SONY -> CANON -> NIKON
                uint8_t b = ((uint8_t)RemoteManager::getBrand() + 1) % 4;
                RemoteManager::setBrand((CameraBrand)b);
                btStatus[0] = '\0';
                break;
            }
            case 2:    // PAIR ??defer to mode loop (blocking scan)
                if (RemoteManager::getBrand() != CameraBrand::None) {
                    btPairRequested = true;
                }
                break;
            case 3:    // TEST SHOT via BLE driver
                if (RemoteManager::getBrand() != CameraBrand::None) {
                    bool ok = RemoteManager::triggerPhoto();
                    snprintf(btStatus, sizeof(btStatus), ok ? "Shot OK" : "Shot FAILED");
                }
                break;
            case 4:    // FORGET paired camera
                RemoteManager::forgetCamera();
                snprintf(btStatus, sizeof(btStatus), "Camera forgotten");
                break;
        }
        return;
    }

    if (editMode.selectedIndex == 0) {
        toggleTrigger();   // G2 ??saves config
    } else if (editMode.selectedIndex == 1) {
        toggleRemote();    // G1 ??saves config
    } else if (editMode.selectedIndex == 2) {
        toggleBeep();      // trigger-out beep on/off ??saves config
    } else if (editMode.selectedIndex == 3) {
        // open Bluetooth sub-screen
        editMode.screen = TriggerEditMode::BLUETOOTH;
        editMode.btIndex = 0;
        btStatus[0] = '\0';
    } else if (editMode.selectedIndex == 4) {
        testTrigger();     // fire enabled outputs once
    }
}

void TriggerMode::handleButtonLongPress() {
    // In Bluetooth sub-screen: long press = back to main list (no exit)
    if (editMode.screen == TriggerEditMode::BLUETOOTH) {
        closeBluetoothScreen();
        return;
    }
    // Long press: exit only. Config already persisted on each toggle.
    // DO NOT disableAll() ??that would wipe the master-switch settings.
    editMode.state = TriggerEditMode::IDLE;
    editMode.selectedIndex = 0;
}

// ============ BLUETOOTH SUB-SCREEN ============
bool TriggerMode::inBluetoothScreen() const {
    return editMode.screen == TriggerEditMode::BLUETOOTH;
}

void TriggerMode::closeBluetoothScreen() {
    editMode.screen = TriggerEditMode::MAIN;
    editMode.selectedIndex = 3;   // back on Bluetooth row
    btPairRequested = false;
}

void TriggerMode::doBtPair() {
    btPairRequested = false;
    bool ok = RemoteManager::pairCamera(10);
    snprintf(btStatus, sizeof(btStatus), ok ? "Paired!" : "Pair FAILED");
}

// ============ GETTERS ============
const char* TriggerMode::getSelectedItemName() {
    switch (editMode.selectedIndex) {
        case 0: return "Trigger";    // G2
        case 1: return "Remote";     // G1
        case 2: return "Beep";
        case 3: return "Bluetooth";
        default: return "Unknown";
    }
}

bool TriggerMode::getTriggerEnabled() const {
    return config.triggerEnabled;
}

bool TriggerMode::getRemoteEnabled() const {
    return config.remoteEnabled;
}

bool TriggerMode::getBluetoothEnabled() const {
    return config.bluetoothEnabled;
}

bool TriggerMode::getBeepEnabled() const {
    return config.beepEnabled;
}

bool TriggerMode::isRunning() const {
    return state.isRunning;
}

uint16_t TriggerMode::getTriggerCount() const {
    return state.triggerCount;
}
