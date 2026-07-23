/**
 * @file remote_manager.cpp
 * @brief Camera remote manager implementation
 * @date 2026-07-23
 */

#include "remote_manager.h"
#include "drivers/sony/sony_ble.h"
#include "drivers/canon/canon_ble.h"
#include "drivers/nikon/nikon_ble.h"
#include <BLEDevice.h>
#include <Preferences.h>

#define NVS_NS    "remote"
#define KEY_BRAND "brand"

namespace {
    CameraBrand s_brand = CameraBrand::None;
    bool s_loaded = false;
    bool s_bleInit = false;

    SonyBLE  s_sony;
    CanonBLE s_canon;
    NikonBLE s_nikon;

    CameraDriver* driverFor(CameraBrand b) {
        switch (b) {
            case CameraBrand::Sony:  return &s_sony;
            case CameraBrand::Canon: return &s_canon;
            case CameraBrand::Nikon: return &s_nikon;
            default:                 return nullptr;
        }
    }
}

namespace RemoteManager {

void loadConfig() {
    Preferences p;
    p.begin(NVS_NS, true);
    uint8_t b = p.getUChar(KEY_BRAND, 0);
    p.end();
    if (b > 3) b = 0;
    s_brand = (CameraBrand)b;
    s_loaded = true;
}

void saveConfig() {
    Preferences p;
    p.begin(NVS_NS);
    p.putUChar(KEY_BRAND, (uint8_t)s_brand);
    p.end();
}

CameraBrand getBrand() {
    if (!s_loaded) loadConfig();
    return s_brand;
}

void setBrand(CameraBrand brand) {
    if (brand == s_brand) return;
    // disconnect old driver before switching
    CameraDriver* old = driverFor(s_brand);
    if (old) old->disconnect();
    s_brand = brand;
    saveConfig();
}

const char* brandName(CameraBrand brand) {
    switch (brand) {
        case CameraBrand::Sony:  return "SONY";
        case CameraBrand::Canon: return "CANON";
        case CameraBrand::Nikon: return "NIKON";
        default:                 return "GPIO";
    }
}

CameraDriver* driver() {
    return driverFor(getBrand());
}

bool bleReady() {
    if (s_bleInit) return true;
    BLEDevice::init("GEOPIX");
    s_bleInit = true;
    Serial.println("[Remote] BLE initialized");
    return true;
}

void shutdown() {
    CameraDriver* d = driverFor(s_brand);
    if (d) d->disconnect();
    // NOTE: BLEDevice::deinit() is left out on purpose — bluedroid
    // re-init after deinit is unreliable; disconnected + idle BLE
    // does not interfere with WiFi/OTA.
}

bool pairCamera(unsigned int scanSeconds) {
    CameraDriver* d = driver();
    if (!d) return false;
    if (!bleReady()) return false;
    return d->pair(scanSeconds);
}

bool triggerPhoto() {
    CameraDriver* d = driver();
    if (!d) return false;
    if (!d->hasPairedCamera()) return false;
    if (!bleReady()) return false;
    return d->trigger();
}

bool isConnected() {
    CameraDriver* d = driver();
    return d && s_bleInit && d->isConnected();
}

bool hasPairedCamera() {
    CameraDriver* d = driver();
    return d && d->hasPairedCamera();
}

void forgetCamera() {
    CameraDriver* d = driver();
    if (d) d->forgetCamera();
}

} // namespace RemoteManager
