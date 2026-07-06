/**
 * @file timelapse.h
 * @brief GEOPIX Timelapse System - Time-based camera trigger
 * @date 2026-07-02
 */

#pragma once
#include "../common/hardware_config.h"

// ============ CONFIG STRUCTURE ============
struct TimelapseConfig {
    int intervalMs = 5000;      // time between shots (milliseconds)
    int totalShots = 0;         // 0 = infinite, >0 = max shots
    bool enable = false;        // master switch
};

// ============ STATE STRUCTURE ============
struct TimelapseState {
    unsigned long lastShotTime = 0;
    int shotCount = 0;
    bool isRunning = false;
    bool isPaused = false;   // paused mid-sequence (progress preserved)
};

// ============ EDIT MODE ============
struct TimelapseEditMode {
    enum EditState {
        IDLE = 0,
        SELECTING = 1,
        EDITING = 2
    } state = IDLE;
    
    uint8_t selectedIndex = 0;  // 0: interval, 1: totalShots, 2: START/PAUSE control
    unsigned long enterTime = 0;
};

// ============ TIMELAPSE CLASS ============
class Timelapse {
public:
    TimelapseConfig config;
    TimelapseState state;
    TimelapseEditMode editMode;
    
    Timelapse();
    ~Timelapse() = default;
    
    // ===== Initialization =====
    void init();
    void loadConfig();
    void saveConfig();
    
    // ===== Main Loop =====
    void update();
    
    // ===== Control =====
    void start();          // fresh start (resets shot count)
    void stop();           // stop + reset run state
    void pause();          // pause, keep progress
    void resume();         // resume from pause
    void toggleRunPause(); // START -> PAUSE -> RESUME toggle
    
    // ===== Camera Trigger =====
    void triggerCamera();
    
    // ===== UI Interaction =====
    void handleEncoderRotate(int delta);
    void handleButtonPress();
    void handleButtonLongPress();
    
    // ===== Getters =====
    const char* getSelectedItemName();
    int getSelectedValue();
    const char* getStatusString();
    const char* getControlLabel() const;   // START / PAUSE / RESUME
    long getEstimatedDurationSec() const;  // total time to finish; -1 = infinite
    int getShotCount() const;
    int getRemainingShots() const;
    unsigned long getTimeUntilNextShot() const;
    bool isRunning() const;
    bool isPaused() const;
    bool isEnabled() const;
    
private:
    void validateConfig();
};

// Global instance
extern Timelapse timelapse;
