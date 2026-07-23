/**
 * @file remote_manager.h
 * @brief Camera remote manager — brand selection + BLE lifecycle
 * @date 2026-07-23
 *
 * Single entry point for the rest of the firmware:
 *   RemoteManager::triggerPhoto()  — used by triggerRemote() (G1 path)
 * Brand NONE = plain GPIO pulse on G1 (handled by caller, not here).
 *
 * NVS namespace "remote": brand + per-driver pairing identity.
 */

#pragma once
#include "camera_driver.h"

namespace RemoteManager {

    // ----- config -----
    void loadConfig();                 // read brand from NVS
    void saveConfig();                 // persist brand
    CameraBrand getBrand();
    void setBrand(CameraBrand brand);  // switches active driver (disconnects old)
    const char* brandName(CameraBrand brand);

    // ----- driver access -----
    // nullptr when brand == None
    CameraDriver* driver();

    // ----- BLE lifecycle -----
    // Init BLE stack on first use. Safe to call repeatedly.
    bool bleReady();
    // Disconnect + note: BLE stack stays initialized (deinit is unstable
    // on Arduino-ESP32 bluedroid). OTA/WiFi still works alongside.
    void shutdown();

    // ----- high-level ops (route to active driver) -----
    bool pairCamera(unsigned int scanSeconds = 10);
    bool triggerPhoto();               // returns false if not connected/paired
    bool isConnected();
    bool hasPairedCamera();
    void forgetCamera();
}
