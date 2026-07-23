/**
 * @file trigger_mode.h
 * @brief Trigger Mode - Dual output trigger system (G1, G2)
 * @date 2026-07-03
 */

#pragma once
#include "../common/hardware_config.h"

// ============ CONFIG STRUCTURE ============
struct TriggerConfig {
    bool triggerEnabled   = true;   // G2 (Port B Yellow, GPIO 2) = Trigger (main)
    bool remoteEnabled    = false;  // G1 (Port B White,  GPIO 1) = Remote  (backup)
    bool bluetoothEnabled = false;  // BLE camera remote (SONY/CANON/NIKON driver)
    bool beepEnabled      = true;   // beep on each trigger-out (AUTO SHOOT / TIMELAPSE)
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
    
    enum Screen {
        MAIN = 0,       // main list
        BLUETOOTH = 1   // Bluetooth camera remote sub-screen
    } screen = MAIN;
    
    uint8_t selectedIndex = 0;  // MAIN: 0=Trigger(G2), 1=Remote(G1), 2=Beep, 3=Bluetooth, 4=TEST
    uint8_t btIndex = 0;        // BLUETOOTH: 0=Enable, 1=Driver, 2=PAIR, 3=TEST SHOT, 4=FORGET
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
    void toggleBluetooth();
    void toggleBeep();
    void enableAll();
    void disableAll();
    void testTrigger();
    
    // ===== Trigger Functions =====
    void triggerOut();      // G2 (Trigger)
    void triggerRemote();   // G1 (Remote)
    void triggerBoth();
    // Fire BLE camera shot if Bluetooth channel is ON and a driver selected.
    // Called by AUTO SHOOT / TIMELAPSE after the G1/G2 pulse (same command).
    void fireBluetoothIfEnabled();
    
    // ===== UI Interaction =====
    void handleEncoderRotate(int delta);
    void handleButtonPress();
    void handleButtonLongPress();
    
    // ===== Bluetooth sub-screen (camera remote driver) =====
    bool inBluetoothScreen() const;
    void closeBluetoothScreen();
    bool btPairRequested = false;   // set by UI press, executed in mode loop
    void doBtPair();                // blocking pair (loop draws PAIRING screen first)
    char btStatus[28] = "";         // status line shown in BT screen
    
    // ===== Getters =====
    const char* getSelectedItemName();
    bool getTriggerEnabled() const;
    bool getRemoteEnabled() const;
    bool getBluetoothEnabled() const;
    bool getBeepEnabled() const;
    bool isRunning() const;
    uint16_t getTriggerCount() const;
    
private:
    void validateConfig();
};

// Global instance
extern TriggerMode triggerMode;
