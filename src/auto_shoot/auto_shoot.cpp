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
    // Initialize TF-Luna sensor (I2C)
    initTfLuna();

    // Initialize dual trigger output pins: G2 = main, G1 = backup mirror
    pinMode(TRIGGER_G2_PIN, OUTPUT);
    pinMode(TRIGGER_G1_PIN, OUTPUT);
    digitalWrite(TRIGGER_G2_PIN, LOW);
    digitalWrite(TRIGGER_G1_PIN, LOW);

    // Load TRIGGER mode master-switch config (which outputs are authorized)
    triggerMode.loadConfig();

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

    // 1. Detect object presence (threshold + hysteresis)
    updateSensorData();

    // 2. Edge-triggered burst firing per config (burstShots + cooldownMs)
    checkAndTrigger();
}

// ============ SENSOR + OBJECT DETECTION ============
void AutoShoot::updateSensorData() {
    // Poll TF-Luna (I2C) once
    readTfLunaDistance();
    const TFLunaState& s = getTfLunaState();

    // Show the REAL sensor distance (what TF-Luna actually reports)
    state.currentDistance = s.rawDistance_m;
    state.currentStrength = s.strength;

    // A detection requires a REAL object return (strength OK, not a 0mm no-return).
    bool present = s.valid && s.objectPresent;

    // Zone with hysteresis to stop boundary flicker:
    //  - to ENTER the zone: distance must be solidly inside [min, max]
    //  - to STAY in the zone: distance may drift out by TFLUNA_HYSTERESIS
    const float H = TFLUNA_HYSTERESIS;
    float d = s.rawDistance_m;

    bool inZone;
    if (state.objectDetected) {
        inZone = present &&
                 (d >= (config.rangeMin - H) && d <= (config.rangeMax + H));
    } else {
        inZone = present &&
                 (d >= config.rangeMin && d <= config.rangeMax);
    }

    state.objectDetected = inZone;
}

// ============ TRIGGER LOGIC (edge-triggered burst) ============
void AutoShoot::checkAndTrigger() {
    unsigned long now = millis();

    // Rising edge: object just ENTERED the zone
    if (state.objectDetected && !state.wasInRange) {
        // Respect cooldown between bursts
        if (now - state.lastTrigger >= (unsigned long)config.cooldownMs) {
            triggerBurst(config.burstShots);   // fire N shots per config
            state.lastTrigger = now;
        }
    }

    // Track previous state for edge detection
    state.wasInRange = state.objectDetected;
}

// ============ CAMERA TRIGGER ============
void AutoShoot::triggerCamera() {
    // Acquire global trigger lock to prevent conflict with Timelapse
    if (!acquireTriggerLock()) return;

    // Fire G2 (Trigger) and/or G1 (Remote) based on TriggerMode master switch
    // If neither enabled, default to G2 (main trigger always works)
    bool fireG2 = triggerMode.config.triggerEnabled;
    bool fireG1 = triggerMode.config.remoteEnabled;
    if (!fireG2 && !fireG1) fireG2 = true;

    if (fireG2) digitalWrite(TRIGGER_G2_PIN, HIGH);
    if (fireG1) digitalWrite(TRIGGER_G1_PIN, HIGH);
    if (triggerMode.config.beepEnabled && g_speakerEnabled) tone(BUZZ_PIN, 2500, 30);   // shot feedback
    delay(30);
    if (fireG2) digitalWrite(TRIGGER_G2_PIN, LOW);
    if (fireG1) digitalWrite(TRIGGER_G1_PIN, LOW);

    releaseTriggerLock();
}

void AutoShoot::triggerBurst(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        triggerCamera();
        state.triggerCount++;
        
        // Inter-shot delay: 100ms between shots
        if (i < count - 1) {
            delay(100);
        }
    }
}

// ============ UI INTERACTION ============
void AutoShoot::handleEncoderRotate(int delta) {
    if (editMode.state == EditMode::SELECTING) {
        // Navigate between items (0-5: 0-3=settings, 4=START, 5=STOP)
        int newIndex = editMode.selectedIndex + (delta > 0 ? 1 : -1);
        if (newIndex >= 0 && newIndex <= 5) {
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
        if (editMode.selectedIndex == 4) {
            start();
        } else if (editMode.selectedIndex == 5) {
            stop();
        } else {
            // Enter edit mode for selected settings item
            editMode.state = EditMode::EDITING;
            editMode.enterTime = millis();
        }
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
    state.lastUpdate = 0;
    state.triggerCount = 0;
}

void AutoShoot::stop() {
    state.isRunning = false;
    state.wasInRange = false;
    state.objectDetected = false;

    // Drive both trigger outputs LOW when stopped
    digitalWrite(TRIGGER_G2_PIN, LOW);
    digitalWrite(TRIGGER_G1_PIN, LOW);
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
