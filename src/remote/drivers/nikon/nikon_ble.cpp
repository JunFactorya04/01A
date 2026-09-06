/**
 * @file nikon_ble.cpp
 * @brief Nikon BLE camera remote driver (ML-L7 "Remote" protocol, client role)
 * @date 2026-09-06
 *
 * Flow (ported from gkoh/furble's NikonBase/NikonRemote, client-role NimBLE
 * code adapted to this project's classic BLEDevice/bluedroid client):
 *  pair()   : scan for camera advertising NIKON_SERVICE_UUID -> connect ->
 *             generate+save a random device/nonce identity -> run the
 *             4-message handshake to confirm the camera accepts it.
 *  connect(): reconnect with saved MAC -> re-run the SAME handshake using
 *             the saved identity (the camera expects this every session,
 *             it is not a one-time pairing step).
 *  trigger(): ensureConnected() -> write {MODE_SHUTTER,CMD_PRESS} then
 *             {MODE_SHUTTER,CMD_RELEASE} to the shutter characteristic.
 */

#include "nikon_ble.h"
#include "nikon_protocol.h"
#include <BLEDevice.h>
#include <Preferences.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstring>

#define NVS_NS       "remote"
#define KEY_MAC      "nikonMac"
#define KEY_ADDRTYPE "nikonAT"
#define KEY_ID       "nikonId"

typedef struct __attribute__((packed)) {
    uint8_t stage;
    uint8_t timestamp[8];
    uint8_t idOrSerial[8];
} nikon_msg_t;

// ---- module state ----
static BLEClient*               n_client    = nullptr;
static BLERemoteCharacteristic* n_pairChr   = nullptr;
static BLERemoteCharacteristic* n_ind1Chr   = nullptr;
static BLERemoteCharacteristic* n_shutter   = nullptr;
static volatile bool            n_connected = false;
static QueueHandle_t            n_queue     = nullptr;

static String  n_foundAddr;
static uint8_t n_foundType = BLE_ADDR_TYPE_RANDOM;
static volatile bool n_found = false;

// device+nonce identity, persisted across sessions (see nikon_protocol.h)
static uint8_t n_id[NIKON_ID_LEN] = {0};

namespace {

class NikonClientCB : public BLEClientCallbacks {
    void onConnect(BLEClient*) override { n_connected = true; }
    void onDisconnect(BLEClient*) override {
        n_connected = false;
        n_pairChr = nullptr;
        n_ind1Chr = nullptr;
        n_shutter = nullptr;
    }
};

class NikonScanCB : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        if (n_found) return;
        if (!dev.haveServiceUUID()) return;
        if (!dev.isAdvertisingService(BLEUUID(NIKON_SERVICE_UUID))) return;

        n_foundAddr = dev.getAddress().toString().c_str();
        n_foundType = dev.getAddressType();
        n_found = true;
        Serial.printf("[NikonBLE] camera found: %s (%s)\n",
                      dev.getName().c_str(), n_foundAddr.c_str());
        dev.getScan()->stop();
    }
};

NikonClientCB n_clientCB;
NikonScanCB   n_scanCB;

} // namespace

static void nikonBuildMsg(nikon_msg_t& msg, uint8_t stage, const uint8_t* timestamp,
                           const uint8_t* idOrSerial) {
    msg.stage = stage;
    memcpy(msg.timestamp, timestamp, 8);
    memcpy(msg.idOrSerial, idOrSerial, 8);
}

static void nikonPairIndicationCB(BLERemoteCharacteristic*, uint8_t* pData, size_t length, bool) {
    if (!n_queue) return;
    bool ok = false;
    if (length == sizeof(nikon_msg_t)) {
        nikon_msg_t in;
        memcpy(&in, pData, sizeof(in));
        if (in.stage == 0x02) {
            // Stage-2 echo must come back all-zero besides the stage byte.
            ok = (memcmp(in.timestamp, NIKON_ZERO8, 8) == 0) &&
                 (memcmp(in.idOrSerial, NIKON_ZERO8, 8) == 0);
        } else if (in.stage == 0x04) {
            ok = (memcmp(in.timestamp, NIKON_ZERO8, 8) == 0);
            if (ok) {
                char serial[9] = {0};
                memcpy(serial, in.idOrSerial, 8);
                Serial.printf("[NikonBLE] camera serial: %s\n", serial);
            }
        }
    }
    xQueueSend(n_queue, &ok, 0);
}

// Runs the 4-message handshake — every connection re-runs this, it is not a
// one-time pairing step. Requires n_pairChr/n_ind1Chr already resolved and
// n_id already populated (fresh at pair() time, reloaded from NVS otherwise).
bool NikonBLE::runHandshake() {
    if (!n_queue) n_queue = xQueueCreate(2, sizeof(bool));

    // preSubscribe: camera firmware expects this indication subscription
    // in place before it will respond to the pairing exchange.
    n_ind1Chr->registerForNotify(
        [](BLERemoteCharacteristic*, uint8_t*, size_t, bool) {}, false);

    n_pairChr->registerForNotify(nikonPairIndicationCB, false);

    nikon_msg_t out;
    bool success;

    // stage 1 -> expect stage-2 echo
    nikonBuildMsg(out, 0x01, NIKON_STAGE0_TIMESTAMP, n_id);
    if (!n_pairChr->canWrite()) return false;
    n_pairChr->writeValue((uint8_t*)&out, sizeof(out), true);
    if (xQueueReceive(n_queue, &success, pdMS_TO_TICKS(NIKON_HANDSHAKE_STAGE_TIMEOUT_MS)) == pdFALSE) {
        Serial.println("[NikonBLE] handshake stage 1 timeout");
        return false;
    }
    if (!success) {
        Serial.println("[NikonBLE] handshake stage 1 mismatch");
        return false;
    }

    // stage 3 -> expect stage-4 (serial) response
    nikonBuildMsg(out, 0x03, NIKON_ZERO8, NIKON_ZERO8);
    n_pairChr->writeValue((uint8_t*)&out, sizeof(out), true);
    if (xQueueReceive(n_queue, &success, pdMS_TO_TICKS(NIKON_HANDSHAKE_STAGE_TIMEOUT_MS)) == pdFALSE) {
        Serial.println("[NikonBLE] handshake stage 3 timeout");
        return false;
    }
    if (!success) {
        Serial.println("[NikonBLE] handshake stage 3 mismatch");
        return false;
    }

    Serial.println("[NikonBLE] handshake complete");
    return true;
}

bool NikonBLE::connectTo(const String& addr) {
    if (!n_client) {
        n_client = BLEDevice::createClient();
        n_client->setClientCallbacks(&n_clientCB);
    }

    BLEAddress bleAddr(addr.c_str());
    if (!n_client->connect(bleAddr, (esp_ble_addr_type_t)n_foundType)) {
        Serial.println("[NikonBLE] connect failed");
        return false;
    }

    BLERemoteService* svc = n_client->getService(NIKON_SERVICE_UUID);
    if (!svc) {
        Serial.println("[NikonBLE] service not found");
        n_client->disconnect();
        return false;
    }

    n_pairChr = svc->getCharacteristic(NIKON_CHAR_PAIR);
    n_ind1Chr = svc->getCharacteristic(NIKON_CHAR_IND1);
    if (!n_pairChr || !n_ind1Chr) {
        Serial.println("[NikonBLE] handshake characteristics not found");
        n_client->disconnect();
        return false;
    }

    if (!runHandshake()) {
        Serial.println("[NikonBLE] handshake failed");
        n_client->disconnect();
        return false;
    }

    n_shutter = svc->getCharacteristic(NIKON_CHAR_SHUTTER);
    if (!n_shutter) {
        Serial.println("[NikonBLE] shutter char not found");
        n_client->disconnect();
        return false;
    }

    Serial.println("[NikonBLE] connected");
    return true;
}

bool NikonBLE::pair(unsigned int scanSeconds) {
    n_found = false;

    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&n_scanCB);
    scan->setActiveScan(true);
    scan->start(scanSeconds, false);
    scan->clearResults();
    scan->setAdvertisedDeviceCallbacks(nullptr);

    if (!n_found) {
        Serial.println("[NikonBLE] no camera found");
        return false;
    }

    // Fresh identity for this remote — persisted so every future reconnect's
    // handshake re-authenticates with the same device/nonce pair.
    for (int i = 0; i < NIKON_ID_LEN; i++) n_id[i] = (uint8_t)esp_random();
    n_id[0] = 0x01; // observed: remote-mode device id always starts with 0x01

    if (!connectTo(n_foundAddr)) return false;

    Preferences p;
    p.begin(NVS_NS);
    p.putString(KEY_MAC, n_foundAddr);
    p.putUChar(KEY_ADDRTYPE, n_foundType);
    p.putBytes(KEY_ID, n_id, NIKON_ID_LEN);
    p.end();

    Serial.println("[NikonBLE] paired & saved");
    return true;
}

bool NikonBLE::connect() {
    Preferences p;
    p.begin(NVS_NS, true);
    String mac = p.getString(KEY_MAC, "");
    n_foundType = p.getUChar(KEY_ADDRTYPE, BLE_ADDR_TYPE_RANDOM);
    p.getBytes(KEY_ID, n_id, NIKON_ID_LEN);
    p.end();

    if (mac.isEmpty()) return false;
    return connectTo(mac);
}

bool NikonBLE::isConnected() {
    return n_connected && n_shutter != nullptr;
}

void NikonBLE::disconnect() {
    if (n_client && n_connected) n_client->disconnect();
    n_pairChr = nullptr;
    n_ind1Chr = nullptr;
    n_shutter = nullptr;
}

bool NikonBLE::ensureConnected() {
    if (isConnected()) return true;
    return connect();
}

bool NikonBLE::trigger() {
    if (!ensureConnected()) return false;
    uint8_t down[2] = {NIKON_MODE_SHUTTER, NIKON_CMD_PRESS};
    uint8_t up[2]   = {NIKON_MODE_SHUTTER, NIKON_CMD_RELEASE};
    n_shutter->writeValue(down, 2, true);
    delay(NIKON_SHUTTER_HOLD_MS);
    n_shutter->writeValue(up, 2, true);
    return true;
}

bool NikonBLE::focus() {
    // The Remote (ML-L7) protocol has no separate half-press command —
    // matches furble's NikonRemote::focusPress()/focusRelease() no-ops.
    return ensureConnected();
}

bool NikonBLE::hasPairedCamera() {
    Preferences p;
    p.begin(NVS_NS, true);
    bool has = p.getString(KEY_MAC, "").length() > 0;
    p.end();
    return has;
}

String NikonBLE::pairedAddress() {
    Preferences p;
    p.begin(NVS_NS, true);
    String mac = p.getString(KEY_MAC, "");
    p.end();
    return mac;
}

void NikonBLE::forgetCamera() {
    disconnect();
    Preferences p;
    p.begin(NVS_NS);
    p.remove(KEY_MAC);
    p.remove(KEY_ADDRTYPE);
    p.remove(KEY_ID);
    p.end();
}
