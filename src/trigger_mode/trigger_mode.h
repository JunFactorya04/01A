/**
 * @file trigger_mode.h
 * @brief Trigger Mode - Dual output trigger system (G1, G2)
 * @date 2026-07-03
 */

#pragma once
#include "../common/hardware_config.h"

// ============ CONFIG STRUCTURE ============
struct TriggerConfig {
    bool triggerEnabled = false;  // Trigger output (GPIO 4)
    bool wifiBtEnabled = false;   // WiFi/Bluetooth output (GPIO 5)
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
    
    uint8_t selectedIndex = 0;  // 0: Trigger, 1: WiFi/BT
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
    void toggleWifiBt();
    void enableAll();
    void disableAll();
    void testTrigger();
    
    // ===== Trigger Functions =====
    void triggerOut();
    void triggerWifiBt();
    void triggerBoth();
    
    // ===== UI Interaction =====
    void handleEncoderRotate(int delta);
    void handleButtonPress();
    void handleButtonLongPress();
    
    // ===== Getters =====
    const char* getSelectedItemName();
    bool getTriggerEnabled() const;
    bool getWifiBtEnabled() const;
    bool isRunning() const;
    uint16_t getTriggerCount() const;
    
private:
    void validateConfig();
};

// Global instance
extern TriggerMode triggerMode;
