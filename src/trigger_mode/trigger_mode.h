/**
 * @file trigger_mode.h
 * @brief Trigger Mode - Dual output trigger system (G1, G2)
 * @date 2026-07-03
 */

#pragma once
#include "../common/hardware_config.h"

// ============ CONFIG STRUCTURE ============
struct TriggerConfig {
    bool triggerEnabled = true;   // G2 (Port B Yellow, GPIO 2) = Trigger (main)
    bool remoteEnabled  = false;  // G1 (Port B White,  GPIO 1) = Remote  (backup)
};

// ============ STATE STRUCTURE ============
struct TriggerState {
    bool isRunning = false;
    unsigned long lastTriggerTime = 0;
    uint16_t triggerCount = 0;
};

// ============ EDIT MODE ============
struct TriggerEditMode {
    enum EditState {
        IDLE = 0,
        SELECTING = 1
    } state = IDLE;
    
    uint8_t selectedIndex = 0;  // 0: Trigger (G2), 1: Remote (G1)
};

// ============ TRIGGER MODE CLASS ============
class TriggerMode {
public:
    TriggerConfig config;
    TriggerState state;
    TriggerEditMode editMode;
    
    TriggerMode();
    ~TriggerMode() = default;
    
    // ===== Initialization =====
    void init();
    void loadConfig();
    void saveConfig();
    
    // ===== Main Loop =====
    void update();
    
    // ===== Control =====
    void toggleTrigger();
    void toggleRemote();
    void enableAll();
    void disableAll();
    void testTrigger();
    
    // ===== Trigger Functions =====
    void triggerOut();      // G2 (Trigger)
    void triggerRemote();   // G1 (Remote)
    void triggerBoth();
    
    // ===== UI Interaction =====
    void handleEncoderRotate(int delta);
    void handleButtonPress();
    void handleButtonLongPress();
    
    // ===== Getters =====
    const char* getSelectedItemName();
    bool getTriggerEnabled() const;
    bool getRemoteEnabled() const;
    bool isRunning() const;
    uint16_t getTriggerCount() const;
    
private:
    void validateConfig();
};

// Global instance
extern TriggerMode triggerMode;
