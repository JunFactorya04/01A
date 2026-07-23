/**
 * @file sony_ble.cpp
 * @brief Sony BLE camera remote driver (freemote protocol)
 * @date 2026-07-23
 *
 * Flow:
 *  pair()   : scan for Sony camera advertising (manufacturer ID 0x012D,
 *             camera tag 0x22). Connect with encryption -> bond ->
 *             save MAC to NVS "remote".
 *  trigger(): auto-reconnect with saved MAC, write focus/shutter sequence.
 */

#include "sony_ble.h"
#include "sony_protocol.h"
#include <BLEDevice.h>
#include <Preferences.h>

#define NVS_NS       "remote"
#define KEY_MAC      "sonyMac"
#define KEY_ADDRTYPE "sonyAT"

// ---- module state ----
static BLEClient*               s_client   = nullptr;
static BLERemoteCharacteristic* s_cmdChar  = nullptr;
static volatile bool            s_connected = false;

// scan result
static String  s_foundAddr;
static uint8_t s_foundType = BLE_ADDR_TYPE_RANDOM;
static volatile bool s_found = false;

// ---- callbacks ----
namespace {

class SonyClientCB : public BLEClientCallbacks {
    void onConnect(BLEClient*) override { s_connected = true; }
    void onDisconnect(BLEClient*) override {
        s_connected = false;
        s_cmdChar = nullptr;
    }
};

class SonySecurityCB : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return 123456; }
    void onPassKeyNotify(uint32_t) override {}
    bool onSecurityRequest() override { return true; }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
        Serial.printf("[SonyBLE] pairing %s\n", cmpl.success ? "success" : "failed");
    }
    bool onConfirmPIN(uint32_t) override { return true; }
};

// Scan callback: match Sony camera in pairing/remote mode
class SonyScanCB : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        if (s_found) return;
        std::string md = dev.getManufacturerData();
        if (md.length() < 3) return;
        // Sony company ID 0x012D little-endian
        if ((uint8_t)md[0] != SONY_COMPANY_ID_LO || (uint8_t)md[1] != SONY_COMPANY_ID_HI) return;

        // find camera tag byte, check remote/pairing flags on next byte
        bool ok = false;
        for (size_t i = 1; i < md.length(); i++) {
            if ((uint8_t)md[i - 1] == SONY_ADV_CAMERA_TAG) {
                uint8_t f = (uint8_t)md[i];
                if ((f & SONY_ADV_FLAG_PAIRING) || (f & SONY_ADV_FLAG_REMOTE)) ok = true;
                break;
            }
        }
        if (!ok) return;

        s_foundAddr = dev.getAddress().toString().c_str();
        s_foundType = dev.getAddressType();
        s_found = true;
        Serial.printf("[SonyBLE] camera found: %s (%s)\n",
                      dev.getName().c_str(), s_foundAddr.c_str());
        dev.getScan()->stop();
    }
};

SonyClientCB   s_clientCB;
SonySecurityCB s_securityCB;
SonyScanCB     s_scanCB;

} // namespace

// ---- helpers ----
static void applySecurity() {
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    BLEDevice::setSecurityCallbacks(&s_securityCB);
    BLESecurity sec;
    sec.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    sec.setCapability(ESP_IO_CAP_NONE);
    sec.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
}

bool SonyBLE::connectTo(const String& addr, uint8_t addrType) {
    if (s_client && s_connected) return true;

    applySecurity();

    if (!s_client) {
        s_client = BLEDevice::createClient();
        s_client->setClientCallbacks(&s_clientCB);
    }

    BLEAddress bleAddr(addr.c_str());
    if (!s_client->connect(bleAddr, (esp_ble_addr_type_t)addrType)) {
        Serial.println("[SonyBLE] connect failed");
        return false;
    }

    BLERemoteService* svc = s_client->getService(SONY_SERVICE_UUID);
    if (!svc) {
        Serial.println("[SonyBLE] service not found");
        s_client->disconnect();
        return false;
    }

    s_cmdChar = svc->getCharacteristic(BLEUUID(SONY_CHAR_COMMAND));
    if (!s_cmdChar) {
        Serial.println("[SonyBLE] command char not found");
        s_client->disconnect();
        return false;
    }

    Serial.println("[SonyBLE] connected");
    return true;
}

// ---- API ----
bool SonyBLE::pair(unsigned int scanSeconds) {
    s_found = false;
    applySecurity();

    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&s_scanCB);
    scan->setActiveScan(true);
    scan->start(scanSeconds, false);   // blocking scan
    scan->clearResults();
    scan->setAdvertisedDeviceCallbacks(nullptr);

    if (!s_found) {
        Serial.println("[SonyBLE] no camera found");
        return false;
    }

    if (!connectTo(s_foundAddr, s_foundType)) return false;

    // Trigger bonding by reading/writing on encrypted char happens lazily;
    // save identity now that link is up
    Preferences p;
    p.begin(NVS_NS);
    p.putString(KEY_MAC, s_foundAddr);
    p.putUChar(KEY_ADDRTYPE, s_foundType);
    p.end();

    Serial.println("[SonyBLE] paired & saved");
    return true;
}

bool SonyBLE::connect() {
    Preferences p;
    p.begin(NVS_NS, true);
    String mac = p.getString(KEY_MAC, "");
    uint8_t at = p.getUChar(KEY_ADDRTYPE, BLE_ADDR_TYPE_RANDOM);
    p.end();

    if (mac.isEmpty()) return false;
    return connectTo(mac, at);
}

bool SonyBLE::isConnected() {
    return s_connected && s_cmdChar != nullptr;
}

void SonyBLE::disconnect() {
    if (s_client && s_connected) s_client->disconnect();
    s_cmdChar = nullptr;
}

bool SonyBLE::ensureConnected() {
    if (isConnected()) return true;
    return connect();
}

bool SonyBLE::trigger() {
    if (!ensureConnected()) return false;

    // half-press -> full press -> release (freemote sequence)
    s_cmdChar->writeValue((uint8_t*)SONY_FOCUS_DOWN, 2, true);
    delay(SONY_FOCUS_SETTLE_MS);
    s_cmdChar->writeValue((uint8_t*)SONY_SHUTTER_DOWN, 2, true);
    delay(SONY_SHUTTER_HOLD_MS);
    s_cmdChar->writeValue((uint8_t*)SONY_SHUTTER_UP, 2, true);
    delay(SONY_RELEASE_GAP_MS);
    s_cmdChar->writeValue((uint8_t*)SONY_FOCUS_UP, 2, true);
    return true;
}

bool SonyBLE::focus() {
    if (!ensureConnected()) return false;
    s_cmdChar->writeValue((uint8_t*)SONY_FOCUS_DOWN, 2, true);
    delay(SONY_FOCUS_SETTLE_MS);
    s_cmdChar->writeValue((uint8_t*)SONY_FOCUS_UP, 2, true);
    return true;
}

bool SonyBLE::hasPairedCamera() {
    Preferences p;
    p.begin(NVS_NS, true);
    bool has = p.getString(KEY_MAC, "").length() > 0;
    p.end();
    return has;
}

String SonyBLE::pairedAddress() {
    Preferences p;
    p.begin(NVS_NS, true);
    String mac = p.getString(KEY_MAC, "");
    p.end();
    return mac;
}

void SonyBLE::forgetCamera() {
    disconnect();
    Preferences p;
    p.begin(NVS_NS);
    p.remove(KEY_MAC);
    p.remove(KEY_ADDRTYPE);
    p.end();
}
