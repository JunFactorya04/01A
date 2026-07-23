/**
 * @file canon_ble.cpp
 * @brief Canon BLE camera remote driver (BR-E1 emulation)
 * @date 2026-07-23
 *
 * Flow (from maxmacstn/ESP32-Canon-BLE-Remote analysis):
 *  pair()   : scan for Canon service UUID -> MITM-encrypted connect ->
 *             write [0x03 + name] to pairing char -> save MAC.
 *  trigger(): reconnect (NO_MITM) with saved MAC -> write 0x8C then 0x0C.
 */

#include "canon_ble.h"
#include "canon_protocol.h"
#include <BLEDevice.h>
#include <Preferences.h>

#define NVS_NS       "remote"
#define KEY_MAC      "canonMac"
#define KEY_ADDRTYPE "canonAT"
#define CANON_DEVICE_NAME "GEOPIX"

// ---- module state ----
static BLEClient*               c_client    = nullptr;
static BLERemoteCharacteristic* c_shutter   = nullptr;
static volatile bool            c_connected = false;

static String  c_foundAddr;
static uint8_t c_foundType = BLE_ADDR_TYPE_RANDOM;
static volatile bool c_found = false;
static volatile bool c_authDone = false;
static volatile bool c_authOk   = false;

namespace {

class CanonClientCB : public BLEClientCallbacks {
    void onConnect(BLEClient*) override { c_connected = true; }
    void onDisconnect(BLEClient*) override {
        c_connected = false;
        c_shutter = nullptr;
    }
};

class CanonSecurityCB : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return 123456; }
    void onPassKeyNotify(uint32_t) override {}
    bool onSecurityRequest() override { return true; }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
        c_authOk = cmpl.success;
        c_authDone = true;
        Serial.printf("[CanonBLE] auth %s\n", cmpl.success ? "ok" : "failed");
    }
    // Canon camera shows a confirm screen; reference waits then accepts
    bool onConfirmPIN(uint32_t) override {
        delay(3000);
        return true;
    }
};

class CanonScanCB : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        if (c_found) return;
        if (!dev.haveServiceUUID()) return;
        if (!dev.isAdvertisingService(BLEUUID(CANON_SERVICE_UUID))) return;

        c_foundAddr = dev.getAddress().toString().c_str();
        c_foundType = dev.getAddressType();
        c_found = true;
        Serial.printf("[CanonBLE] camera found: %s (%s)\n",
                      dev.getName().c_str(), c_foundAddr.c_str());
        dev.getScan()->stop();
    }
};

CanonClientCB   c_clientCB;
CanonSecurityCB c_securityCB;
CanonScanCB     c_scanCB;

} // namespace

static void canonSecurity(bool mitm) {
    BLEDevice::setEncryptionLevel(mitm ? ESP_BLE_SEC_ENCRYPT_MITM : ESP_BLE_SEC_ENCRYPT_NO_MITM);
    BLEDevice::setSecurityCallbacks(&c_securityCB);
    BLESecurity sec;
    sec.setAuthenticationMode(mitm ? ESP_LE_AUTH_REQ_SC_MITM_BOND : ESP_LE_AUTH_REQ_SC_BOND);
    sec.setCapability(ESP_IO_CAP_NONE);
    sec.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
}

bool CanonBLE::connectTo(const String& addr, bool doHandshake) {
    if (!c_client) {
        c_client = BLEDevice::createClient();
        c_client->setClientCallbacks(&c_clientCB);
    }

    BLEAddress bleAddr(addr.c_str());
    if (!c_client->connect(bleAddr, (esp_ble_addr_type_t)c_foundType)) {
        Serial.println("[CanonBLE] connect failed");
        return false;
    }

    BLERemoteService* svc = c_client->getService(CANON_SERVICE_UUID);
    if (!svc) {
        Serial.println("[CanonBLE] service not found");
        c_client->disconnect();
        return false;
    }

    if (doHandshake) {
        BLERemoteCharacteristic* pairing = svc->getCharacteristic(CANON_CHAR_PAIRING);
        if (!pairing) {
            Serial.println("[CanonBLE] pairing char not found");
            c_client->disconnect();
            return false;
        }
        // handshake: 0x03 + device name
        const char* devName = CANON_DEVICE_NAME;
        size_t nameLen = strlen(devName);
        uint8_t buf[1 + 16];
        buf[0] = CANON_PAIR_PREFIX;
        if (nameLen > 16) nameLen = 16;
        memcpy(buf + 1, devName, nameLen);
        pairing->writeValue(buf, 1 + nameLen, true);
        delay(200);
    }

    c_shutter = svc->getCharacteristic(CANON_CHAR_SHUTTER);
    if (!c_shutter) {
        Serial.println("[CanonBLE] shutter char not found");
        c_client->disconnect();
        return false;
    }

    Serial.println("[CanonBLE] connected");
    return true;
}

bool CanonBLE::pair(unsigned int scanSeconds) {
    c_found = false;
    c_authDone = false;
    canonSecurity(true);   // MITM for initial pairing

    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&c_scanCB);
    scan->setActiveScan(true);
    scan->start(scanSeconds, false);
    scan->clearResults();
    scan->setAdvertisedDeviceCallbacks(nullptr);

    if (!c_found) {
        Serial.println("[CanonBLE] no camera found");
        return false;
    }

    if (!connectTo(c_foundAddr, true)) return false;

    // reference drops to NO_MITM for subsequent connections
    c_client->disconnect();
    delay(500);
    canonSecurity(false);
    if (!connectTo(c_foundAddr, false)) return false;

    Preferences p;
    p.begin(NVS_NS);
    p.putString(KEY_MAC, c_foundAddr);
    p.putUChar(KEY_ADDRTYPE, c_foundType);
    p.end();

    Serial.println("[CanonBLE] paired & saved");
    return true;
}

bool CanonBLE::connect() {
    Preferences p;
    p.begin(NVS_NS, true);
    String mac = p.getString(KEY_MAC, "");
    c_foundType = p.getUChar(KEY_ADDRTYPE, BLE_ADDR_TYPE_RANDOM);
    p.end();

    if (mac.isEmpty()) return false;
    canonSecurity(false);
    return connectTo(mac, false);
}

bool CanonBLE::isConnected() {
    return c_connected && c_shutter != nullptr;
}

void CanonBLE::disconnect() {
    if (c_client && c_connected) c_client->disconnect();
    c_shutter = nullptr;
}

bool CanonBLE::ensureConnected() {
    if (isConnected()) return true;
    return connect();
}

bool CanonBLE::trigger() {
    if (!ensureConnected()) return false;
    uint8_t down = CANON_CMD_SHUTTER_DOWN;
    uint8_t up   = CANON_CMD_NEUTRAL;
    c_shutter->writeValue(&down, 1, true);
    delay(CANON_SHUTTER_HOLD_MS);
    c_shutter->writeValue(&up, 1, true);
    return true;
}

bool CanonBLE::focus() {
    if (!ensureConnected()) return false;
    uint8_t down = CANON_CMD_FOCUS_DOWN;
    uint8_t up   = CANON_CMD_NEUTRAL;
    c_shutter->writeValue(&down, 1, true);
    delay(CANON_FOCUS_HOLD_MS);
    c_shutter->writeValue(&up, 1, true);
    return true;
}

bool CanonBLE::hasPairedCamera() {
    Preferences p;
    p.begin(NVS_NS, true);
    bool has = p.getString(KEY_MAC, "").length() > 0;
    p.end();
    return has;
}

String CanonBLE::pairedAddress() {
    Preferences p;
    p.begin(NVS_NS, true);
    String mac = p.getString(KEY_MAC, "");
    p.end();
    return mac;
}

void CanonBLE::forgetCamera() {
    disconnect();
    Preferences p;
    p.begin(NVS_NS);
    p.remove(KEY_MAC);
    p.remove(KEY_ADDRTYPE);
    p.end();
}
