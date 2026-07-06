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
    
    // Initialize state — always start idle (never auto-run on entering mode)
    config.enable      = false;
    state.isRunning    = false;
    state.isPaused     = false;
    state.lastShotTime = millis();
    state.shotCount    = 0;
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
    if (!state.isRunning) return;
    
    unsigned long now = millis();
    
    // Interval check
    if (now - state.lastShotTime >= (unsigned long)config.intervalMs) {
        
        // Sequence complete?
        if (config.totalShots != 0 && state.shotCount >= config.totalShots) {
            state.isRunning = false;
            state.isPaused  = false;   // finished (DONE) — keep shotCount for display
            config.enable   = false;
            return;
        }
        
        // Trigger camera with lock
        triggerCamera();
        
        state.lastShotTime = now;
        state.shotCount++;
    }
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
    config.enable      = true;
    state.isRunning    = true;
    state.isPaused     = false;
    state.lastShotTime = millis();
    state.shotCount    = 0;
}

void Timelapse::stop() {
    config.enable   = false;
    state.isRunning = false;
    state.isPaused  = false;
}

void Timelapse::pause() {
    // Pause but preserve progress (shotCount, so shooting can resume)
    config.enable   = false;
    state.isRunning = false;
    state.isPaused  = true;
}

void Timelapse::resume() {
    config.enable      = true;
    state.isRunning    = true;
    state.isPaused     = false;
    state.lastShotTime = millis();   // wait a full interval before next shot
}

void Timelapse::toggleRunPause() {
    if (state.isRunning) {
        pause();                     // running -> paused
    } else if (state.isPaused) {
        resume();                    // paused  -> running
    } else {
        start();                     // idle/done -> fresh start
    }
}

// ============ UI INTERACTION ============
void Timelapse::handleEncoderRotate(int delta) {
    if (editMode.state == TimelapseEditMode::SELECTING) {
        // Navigate: 0=Interval, 1=Total, 2=START/PAUSE control
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
        }
        
        validateConfig();
    }
}

void Timelapse::handleButtonPress() {
    if (editMode.state == TimelapseEditMode::SELECTING) {
        if (editMode.selectedIndex == 2) {
            // START / PAUSE / RESUME toggle
            toggleRunPause();
        } else {
            // Enter edit mode for Interval / Total
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
    // Long press: stop and exit back to menu
    stop();
    saveConfig();
    editMode.state = TimelapseEditMode::IDLE;
    editMode.selectedIndex = 0;
}

// ============ GETTERS ============
const char* Timelapse::getSelectedItemName() {
    switch (editMode.selectedIndex) {
        case 0: return "Interval";
        case 1: return "Total Shots";
        case 2: return "Control";
        default: return "Unknown";
    }
}

int Timelapse::getSelectedValue() {
    switch (editMode.selectedIndex) {
        case 0: return config.intervalMs;
        case 1: return config.totalShots;
        default: return 0;
    }
}

const char* Timelapse::getStatusString() {
    if (state.isRunning) return "RUNNING";
    if (state.isPaused)  return "PAUSED";
    if (config.totalShots != 0 && state.shotCount >= config.totalShots && state.shotCount > 0)
        return "DONE";
    return "IDLE";
}

const char* Timelapse::getControlLabel() const {
    if (state.isRunning) return "PAUSE";
    if (state.isPaused)  return "RESUME";
    return "START";
}

// Total time to finish the whole sequence (seconds). -1 = infinite (totalShots=0).
long Timelapse::getEstimatedDurationSec() const {
    if (config.totalShots <= 0) return -1;
    // total = intervals between shots * interval
    long sec = (long)((long long)config.totalShots * config.intervalMs / 1000);
    return sec;
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

bool Timelapse::isPaused() const {
    return state.isPaused;
}

bool Timelapse::isEnabled() const {
    return config.enable;
}
