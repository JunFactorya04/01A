/**
 * @file auto_shoot.h
 * @brief Auto Shoot system - TF-Luna LiDAR + Camera trigger
 * @date 2026-07-01
 */

#pragma once
#include <Arduino.h>

// ============ CONFIG STRUCTURE ============
struct AutoShootConfig {
    // Range Filter master switch (Advance submenu). OFF by default = "pure"
    // mode: any valid sensor return counts as detected, full realtime power,
    // no distance restriction. ON = band-pass [rangeMin, rangeMax], same
    // logic as before this switch existed.
    bool filterEnabled = false;
    float rangeMin = 0.1f;      // meters — only applied when filterEnabled
    float rangeMax = 8.0f;      // meters — only applied when filterEnabled
    uint8_t burstShots = 1;     // 1-10 shots
    uint16_t cooldownMs = 0;    // milliseconds — default 0: fire as fast as the cooldown gate allows
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

    // Distance reading at the last shot. Used only by checkAndTrigger() to
    // detect a meaningful in-zone MOVE, so a long object that never fully
    // exits the range keeps getting photographed instead of firing once.
    float lastTriggerDistance = 0.0f;
};

// ============ EDIT MODE ============
struct EditMode {
    enum EditState {
        IDLE = 0,
        SELECTING = 1,
        EDITING = 2
    } state = IDLE;

    // MAIN = the primary Auto Mode screen (Burst / Cooldown / Advance /
    // START / STOP). ADVANCE = the Range Filter submenu (Filter ON/OFF,
    // Range Min, Range Max) — same pattern as TriggerMode's Bluetooth
    // sub-screen: one shared `state` (SELECTING/EDITING), separate index
    // per screen so navigating one doesn't disturb the other.
    enum Screen {
        MAIN = 0,
        ADVANCE = 1
    } screen = MAIN;

    uint8_t selectedIndex = 0;  // MAIN: 0=Burst 1=Cooldown 2=Advance 3=START 4=STOP
    uint8_t advanceIndex = 0;   // ADVANCE: 0=Filter ON/OFF 1=Range Min 2=Range Max
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

    // ===== Advance (Range Filter) submenu =====
    bool inAdvanceScreen() const { return editMode.screen == EditMode::ADVANCE; }
    void closeAdvanceScreen();   // long-press inside Advance -> back to MAIN, not exit

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
