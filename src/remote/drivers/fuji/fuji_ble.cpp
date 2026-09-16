/**
 * @file fuji_ble.cpp
 * @brief Fujifilm BLE camera remote driver (Basic/unsecured pairing)
 * @date 2026-09-06
 *
 * Flow (from gkoh/furble FujifilmBasic analysis):
 *  pair()   : scan for Fujifilm manufacturer data (company 0x04d8, type
 *             TOKEN) carrying a 4-byte pairing token -> connect -> write
 *             token to pairing char -> write our name to identify char ->
 *             save MAC + token.
 *  trigger(): reconnect with saved MAC/token -> re-send token + identify
 *             (camera expects this every connection, not just first pair)
 *             -> write 2-byte command then 2-byte param to shutter char.
 */

#include "fuji_ble.h"
#include "fuji_protocol.h"
#include <BLEDevice.h>
#include <Preferences.h>

#define NVS_NS       "remote"
#define KEY_MAC      "fujiMac"
#define KEY_ADDRTYPE "fujiAT"
#define KEY_TOKEN    "fujiTok"
#define FUJI_DEVICE_NAME "GEOPIX"

// ---- module state ----
static BLEClient*               f_client    = nullptr;
static BLERemoteCharacteristic* f_shutter   = nullptr;
static volatile bool            f_connected = false;

static String  f_foundAddr;
static uint8_t f_foundType = BLE_ADDR_TYPE_RANDOM;
static uint8_t f_foundToken[FUJI_TOKEN_LEN] = {0};
static volatile bool f_found = false;

namespace {

class FujiClientCB : public BLEClientCallbacks {
    void onConnect(BLEClient*) override { f_connected = true; }
    void onDisconnect(BLEClient*) override {
        f_connected = false;
        f_shutter = nullptr;
    }
};

// Scan callback: match Fujifilm "Basic" camera by manufacturer data
// (company ID + TOKEN type + 4-byte token) and one of its two known
// secondary service UUIDs.
class FujiScanCB : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        if (f_found) return;

        std::string md = dev.getManufacturerData();
        // 2 byte company id + 1 byte type + 4 byte token = 7 bytes
        if (md.length() != 7) return;
        uint16_t companyId = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
        if (companyId != FUJI_COMPANY_ID) return;
        if ((uint8_t)md[2] != FUJI_ADV_TYPE_TOKEN) return;

        if (!dev.isAdvertisingService(BLEUUID(FUJI_ADV_SVC_CR)) &&
            !dev.isAdvertisingService(BLEUUID(FUJI_ADV_SVC_XAPP))) return;

        for (int i = 0; i < FUJI_TOKEN_LEN; i++) f_foundToken[i] = (uint8_t)md[3 + i];
        f_foundAddr = dev.getAddress().toString().c_str();
        f_foundType = dev.getAddressType();
        f_found = true;
        Serial.printf("[FujiBLE] camera found: %s (%s)\n",
                      dev.getName().c_str(), f_foundAddr.c_str());
        dev.getScan()->stop();
    }
};

FujiClientCB f_clientCB;
FujiScanCB   f_scanCB;

} // namespace

bool FujiBLE::connectTo(const String& addr, bool doHandshake) {
    if (!f_client) {
        f_client = BLEDevice::createClient();
        f_client->setClientCallbacks(&f_clientCB);
    }

    BLEAddress bleAddr(addr.c_str());
    if (!f_client->connect(bleAddr, (esp_ble_addr_type_t)f_foundType)) {
        Serial.println("[FujiBLE] connect failed");
        return false;
    }

    // Camera re-checks the pairing token + identity on every connection,
    // not just the first pairing — unlike Canon this is not a one-time
    // handshake, so it always runs (doHandshake kept for interface symmetry
    // with CanonBLE; Fuji always re-identifies).
    (void)doHandshake;

    BLERemoteService* pairSvc = f_client->getService(FUJI_SVC_PAIR);
    if (!pairSvc) {
        Serial.println("[FujiBLE] pairing service not found");
        f_client->disconnect();
        return false;
    }

    BLERemoteCharacteristic* pairChar = pairSvc->getCharacteristic(FUJI_CHAR_PAIR);
    if (!pairChar) {
        Serial.println("[FujiBLE] pairing char not found");
        f_client->disconnect();
        return false;
    }
    pairChar->writeValue(f_foundToken, FUJI_TOKEN_LEN, true);

    BLERemoteCharacteristic* idChar = pairSvc->getCharacteristic(FUJI_CHAR_IDENTIFY);
    if (!idChar) {
        Serial.println("[FujiBLE] identify char not found");
        f_client->disconnect();
        return false;
    }
    idChar->writeValue((uint8_t*)FUJI_DEVICE_NAME, strlen(FUJI_DEVICE_NAME), true);

    BLERemoteService* shutterSvc = f_client->getService(FUJI_SVC_SHUTTER);
    if (!shutterSvc) {
        Serial.println("[FujiBLE] shutter service not found");
        f_client->disconnect();
        return false;
    }
    f_shutter = shutterSvc->getCharacteristic(FUJI_CHAR_SHUTTER);
    if (!f_shutter) {
        Serial.println("[FujiBLE] shutter char not found");
        f_client->disconnect();
        return false;
    }

    Serial.println("[FujiBLE] connected");
    return true;
}

bool FujiBLE::pair(unsigned int scanSeconds) {
    f_found = false;

    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&f_scanCB);
    scan->setActiveScan(true);
    scan->start(scanSeconds, false);
    scan->clearResults();
    scan->setAdvertisedDeviceCallbacks(nullptr);

    if (!f_found) {
        Serial.println("[FujiBLE] no camera found");
        return false;
    }

    if (!connectTo(f_foundAddr, true)) return false;

    Preferences p;
    p.begin(NVS_NS);
    p.putString(KEY_MAC, f_foundAddr);
    p.putUChar(KEY_ADDRTYPE, f_foundType);
    p.putBytes(KEY_TOKEN, f_foundToken, FUJI_TOKEN_LEN);
    p.end();

    Serial.println("[FujiBLE] paired & saved");
    return true;
}

bool FujiBLE::connect() {
    Preferences p;
    p.begin(NVS_NS, true);
    String mac = p.getString(KEY_MAC, "");
    f_foundType = p.getUChar(KEY_ADDRTYPE, BLE_ADDR_TYPE_RANDOM);
    p.getBytes(KEY_TOKEN, f_foundToken, FUJI_TOKEN_LEN);
    p.end();

    if (mac.isEmpty()) return false;
    return connectTo(mac, true);
}

bool FujiBLE::isConnected() {
    return f_connected && f_shutter != nullptr;
}

void FujiBLE::disconnect() {
    if (f_client && f_connected) f_client->disconnect();
    f_shutter = nullptr;
}

bool FujiBLE::ensureConnected() {
    if (isConnected()) return true;
    return connect();
}

static void fujiShutterCmd(uint8_t p0, uint8_t p1) {
    uint8_t cmd[2] = {FUJI_CMD_SHUTTER};
    uint8_t param[2] = {p0, p1};
    f_shutter->writeValue(cmd, 2, true);
    f_shutter->writeValue(param, 2, true);
}

bool FujiBLE::trigger() {
    bool wasConnected = isConnected();
    if (!ensureConnected()) return false;
    if (!wasConnected) delay(FUJI_RECONNECT_SETTLE_MS);
    fujiShutterCmd(FUJI_PARAM_PRESS);
    delay(FUJI_SHUTTER_HOLD_MS);
    fujiShutterCmd(FUJI_PARAM_RELEASE);
    return true;
}

bool FujiBLE::focus() {
    if (!ensureConnected()) return false;
    fujiShutterCmd(FUJI_PARAM_FOCUS);
    delay(FUJI_FOCUS_HOLD_MS);
    fujiShutterCmd(FUJI_PARAM_RELEASE);
    return true;
}

// Bulb hold: same PRESS param trigger() uses, just without the fixed hold
// delay -- the camera (in its own Bulb mode) keeps the shutter open until
// the matching RELEASE param arrives.
bool FujiBLE::shutterPress() {
    bool wasConnected = isConnected();
    if (!ensureConnected()) return false;
    if (!wasConnected) delay(FUJI_RECONNECT_SETTLE_MS);
    fujiShutterCmd(FUJI_PARAM_PRESS);
    return true;
}

bool FujiBLE::shutterRelease() {
    // Reconnect if needed (see SonyBLE::shutterRelease() for why) -- the
    // camera's shutter is still open regardless of our BLE link state.
    if (!ensureConnected()) return false;
    // Send twice (see SonyBLE::shutterRelease() for why) -- writeValue()
    // can't report success, and a single write after a long idle hold
    // intermittently failed to close the shutter on real hardware.
    fujiShutterCmd(FUJI_PARAM_RELEASE);
    delay(50);
    fujiShutterCmd(FUJI_PARAM_RELEASE);
    return true;
}

bool FujiBLE::hasPairedCamera() {
    Preferences p;
    p.begin(NVS_NS, true);
    bool has = p.getString(KEY_MAC, "").length() > 0;
    p.end();
    return has;
}

String FujiBLE::pairedAddress() {
    Preferences p;
    p.begin(NVS_NS, true);
    String mac = p.getString(KEY_MAC, "");
    p.end();
    return mac;
}

void FujiBLE::forgetCamera() {
    disconnect();
    Preferences p;
    p.begin(NVS_NS);
    p.remove(KEY_MAC);
    p.remove(KEY_ADDRTYPE);
    p.remove(KEY_TOKEN);
    p.end();
}
