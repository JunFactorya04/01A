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
#include <math.h>

// Global instance
AutoShoot autoShoot;

// ============ CONSTRUCTOR ============
AutoShoot::AutoShoot() {
    // Default config set in header
}

// ============ INITIALIZATION ============
void AutoShoot::init() {
    // Initialize TF-Luna sensor API (I2C, Wire1)
    tfLuna.begin();

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

    config.filterEnabled = prefs.getBool("filterEn", false);
    config.rangeMin = prefs.getFloat("minR", 0.1f);
    config.rangeMax = prefs.getFloat("maxR", 8.0f);
    config.burstShots = prefs.getInt("burst", 1);
    config.cooldownMs = prefs.getInt("cooldown", 0);
    config.retriggerDeltaCm = prefs.getInt("retrigCm", 15);

    prefs.end();

    validateConfig();
}

void AutoShoot::saveConfig() {
    Preferences prefs;
    prefs.begin("autoShoot");

    prefs.putBool("filterEn", config.filterEnabled);
    prefs.putFloat("minR", config.rangeMin);
    prefs.putFloat("maxR", config.rangeMax);
    prefs.putInt("burst", config.burstShots);
    prefs.putInt("cooldown", config.cooldownMs);
    prefs.putInt("retrigCm", config.retriggerDeltaCm);

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

    // Clamp Cooldown (0 = no delay between bursts)
    if (config.cooldownMs < 0) config.cooldownMs = 0;
    if (config.cooldownMs > 5000) config.cooldownMs = 5000;

    // Clamp Retrigger distance (below 2cm is basically pure sensor noise,
    // not real movement)
    if (config.retriggerDeltaCm < 2) config.retriggerDeltaCm = 2;
    if (config.retriggerDeltaCm > 100) config.retriggerDeltaCm = 100;
}

// ============ MAIN UPDATE ============
void AutoShoot::update() {
    // 1. Always poll the sensor so the UI shows real-time distance,
    //    even before START (useful for aiming / setting range)
    updateSensorData();

    // 2. Firing logic only when running
    if (!state.isRunning) return;

    // Edge-triggered burst firing per config (burstShots + cooldownMs)
    checkAndTrigger();
}

// ============ SENSOR + OBJECT DETECTION ============
void AutoShoot::updateSensorData() {
    // Poll TF-Luna once via the sensor API
    tfLuna.update();

    // Real sensor values for display
    state.currentDistance = tfLuna.getDistance();
    state.currentStrength = tfLuna.getStrength();

    // Pure mode (default, filterEnabled=false): any valid return counts —
    // full sensor power, no distance restriction, maximum realtime
    // responsiveness. Filtered mode (Advance > Range Filter ON): same
    // band-pass [rangeMin, rangeMax] logic as before this switch existed,
    // completely unchanged.
    state.objectDetected = config.filterEnabled
        ? tfLuna.inRange(config.rangeMin, config.rangeMax)
        : tfLuna.hasObject();
}

// ============ TRIGGER LOGIC (entry edge + in-zone movement, cooldown-paced) ============
// FIXED: the old version only fired on the RISING EDGE (object just entered
// the zone). If the object never fully left the zone again — e.g. a long
// object where the measured distance stays somewhere under rangeMax the
// entire time it passes by — wasInRange stayed true forever and NOTHING
// fired after that first shot. Now: still fires immediately on entry, but
// ALSO keeps firing (cooldown-paced) whenever the distance changes
// meaningfully while still inside the zone, so a long/slow object keeps
// getting photographed instead of just once.
void AutoShoot::checkAndTrigger() {
    unsigned long now = millis();

    if (state.objectDetected) {
        bool cooldownOk = (now - state.lastTrigger) >= (unsigned long)config.cooldownMs;

        bool justEntered = !state.wasInRange;                                  // rising edge
        float moved = fabsf(state.currentDistance - state.lastTriggerDistance);
        float retriggerDeltaM = (float)config.retriggerDeltaCm / 100.0f;
        bool movedInZone = !justEntered && (moved >= retriggerDeltaM);

        if (cooldownOk && (justEntered || movedInZone)) {
            triggerBurst(config.burstShots);   // fire N shots per config
            state.lastTrigger = now;
            state.lastTriggerDistance = state.currentDistance;
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
    // If NO channel enabled (incl. Bluetooth), default to G2 (main always works)
    bool fireG2 = triggerMode.config.triggerEnabled;
    bool fireG1 = triggerMode.config.remoteEnabled;
    if (!fireG2 && !fireG1 && !triggerMode.config.bluetoothEnabled) fireG2 = true;

    if (fireG2) digitalWrite(TRIGGER_G2_PIN, HIGH);
    if (fireG1) digitalWrite(TRIGGER_G1_PIN, HIGH);
    if (triggerMode.config.beepEnabled && g_speakerEnabled) tone(BUZZ_PIN, 2500, 30);   // shot feedback
    delay(6);
    if (fireG2) digitalWrite(TRIGGER_G2_PIN, LOW);
    if (fireG1) digitalWrite(TRIGGER_G1_PIN, LOW);

    releaseTriggerLock();

    // 3rd channel: BLE camera remote — same command, after GPIO pulse
    triggerMode.fireBluetoothIfEnabled();
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
        if (editMode.screen == EditMode::ADVANCE) {
            // ADVANCE: 0=Filter ON/OFF, 1=Range Min, 2=Range Max, 3=Retrigger
            int newIndex = editMode.advanceIndex + (delta > 0 ? 1 : -1);
            if (newIndex >= 0 && newIndex <= 3) {
                editMode.advanceIndex = newIndex;
            }
            return;
        }

        // MAIN: 0=Burst, 1=Cooldown, 2=Advance, 3=START, 4=STOP
        int newIndex = editMode.selectedIndex + (delta > 0 ? 1 : -1);
        if (newIndex >= 0 && newIndex <= 4) {
            editMode.selectedIndex = newIndex;
        }
    }
    else if (editMode.state == EditMode::EDITING) {
        // Edit selected value - normalize delta
        if (delta > 0) delta = 1;
        else if (delta < 0) delta = -1;

        if (editMode.screen == EditMode::ADVANCE) {
            // Range Min (1) / Range Max (2) — identical logic to the
            // original Range Min/Max editing (validateConfig() below still
            // clamps/auto-swaps exactly as before), just relocated from
            // the main screen into this submenu. Retrigger (3) is plain
            // +-1cm/click, deliberately NOT speed-scaled — see the comment
            // in CLAUDE.md about why Auto Shoot's encoder steps stay at
            // their original, hardware-confirmed-stable form.
            if (editMode.advanceIndex == 1) {          // Range Min
                config.rangeMin += (delta * 0.1f);
            } else if (editMode.advanceIndex == 2) {    // Range Max
                config.rangeMax += (delta * 0.1f);
            } else if (editMode.advanceIndex == 3) {    // Retrigger distance (cm)
                config.retriggerDeltaCm += delta;
            }
            validateConfig();
            return;
        }

        switch (editMode.selectedIndex) {
            case 0:  // Burst Shots
                config.burstShots += delta;
                break;
            case 1:  // Cooldown — 10ms step (was 50ms) for finer control
                     // now that the default is 0
                config.cooldownMs += (delta * 10);
                break;
        }

        validateConfig();
    }
}

void AutoShoot::handleButtonPress() {
    if (editMode.state == EditMode::SELECTING) {
        if (editMode.screen == EditMode::ADVANCE) {
            if (editMode.advanceIndex == 0) {
                // Range Filter ON/OFF — instant toggle, no EDITING needed
                config.filterEnabled = !config.filterEnabled;
                saveConfig();
            } else {
                // Range Min (1) / Range Max (2) / Retrigger (3) — enter EDITING
                editMode.state = EditMode::EDITING;
                editMode.enterTime = millis();
            }
            return;
        }

        // MAIN
        if (editMode.selectedIndex == 2) {
            // Open the Advance (Range Filter) submenu
            editMode.screen = EditMode::ADVANCE;
            editMode.advanceIndex = 0;
        } else if (editMode.selectedIndex == 3) {
            start();
        } else if (editMode.selectedIndex == 4) {
            stop();
        } else {
            // 0=Burst, 1=Cooldown -> enter edit mode
            editMode.state = EditMode::EDITING;
            editMode.enterTime = millis();
        }
    }
    else if (editMode.state == EditMode::EDITING) {
        // Save and exit edit mode (MAIN's Burst/Cooldown and ADVANCE's
        // Range Min/Max both funnel through here identically)
        saveConfig();
        editMode.state = EditMode::SELECTING;
    }
}

void AutoShoot::handleButtonLongPress() {
    // Inside the Advance submenu: long press = back to MAIN screen only,
    // same convention as TriggerMode's Bluetooth sub-screen. Do NOT stop a
    // run in progress just for navigating back.
    if (editMode.screen == EditMode::ADVANCE) {
        closeAdvanceScreen();
        return;
    }

    // MAIN screen: long press = exit back to the launcher menu
    editMode.state = EditMode::IDLE;
    editMode.selectedIndex = 0;
    state.isRunning = false;
}

void AutoShoot::closeAdvanceScreen() {
    editMode.screen = EditMode::MAIN;
    editMode.selectedIndex = 2;   // back on the "Advance" row
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
    if (editMode.screen == EditMode::ADVANCE) {
        switch (editMode.advanceIndex) {
            case 0: return "Range Filter";
            case 1: return "Range Min";
            case 2: return "Range Max";
            case 3: return "Retrigger";
            default: return "Unknown";
        }
    }
    switch (editMode.selectedIndex) {
        case 0: return "Burst Shots";
        case 1: return "Cooldown";
        case 2: return "Advance";
        default: return "Unknown";
    }
}

float AutoShoot::getSelectedValue() {
    if (editMode.screen == EditMode::ADVANCE) {
        switch (editMode.advanceIndex) {
            case 1: return config.rangeMin;
            case 2: return config.rangeMax;
            case 3: return (float)config.retriggerDeltaCm;
            default: return 0.0f;
        }
    }
    switch (editMode.selectedIndex) {
        case 0: return (float)config.burstShots;
        case 1: return (float)config.cooldownMs;
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
