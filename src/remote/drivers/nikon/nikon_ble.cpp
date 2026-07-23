/**
 * @file nikon_ble.cpp
 * @brief Nikon BLE camera remote driver (ML-L7 emulation) — EXPERIMENTAL
 * @date 2026-07-23
 *
 * Role reversal vs Sony/Canon: ESP32 is a BLE SERVER advertising as
 * "ML-L7". The Nikon camera (client) connects to us and subscribes to
 * the button characteristic; trigger() sends notify events.
 */

#include "nikon_ble.h"
#include "nikon_protocol.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <Preferences.h>

#define NVS_NS      "remote"
#define KEY_PAIRED  "nikonPaired"

// ---- module state ----
static BLEServer*         n_server    = nullptr;
static BLEService*        n_service   = nullptr;
static BLECharacteristic* n_btnChar   = nullptr;
static volatile bool      n_connected = false;
static bool               n_started   = false;

namespace {

class NikonServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        n_connected = true;
        Serial.println("[NikonBLE] camera connected");
    }
    void onDisconnect(BLEServer* srv) override {
        n_connected = false;
        Serial.println("[NikonBLE] camera disconnected");
        // keep advertising so camera can reconnect
        srv->getAdvertising()->start();
    }
};

class NikonStatusCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* ch) override {
        // camera status frames — log only (experimental)
        std::string v = ch->getValue();
        Serial.printf("[NikonBLE] status frame %u bytes\n", (unsigned)v.length());
    }
};

NikonServerCB n_serverCB;
NikonStatusCB n_statusCB;

} // namespace

bool NikonBLE::startServer() {
    if (n_started) return true;

    n_server = BLEDevice::createServer();
    n_server->setCallbacks(&n_serverCB);

    n_service = n_server->createService(NIKON_SERVICE_UUID);

    n_btnChar = n_service->createCharacteristic(
        NIKON_CHAR_BUTTON,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    n_btnChar->addDescriptor(new BLE2902());

    BLECharacteristic* statusChar = n_service->createCharacteristic(
        NIKON_CHAR_STATUS,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    statusChar->setCallbacks(&n_statusCB);

    n_service->start();

    BLEAdvertising* adv = n_server->getAdvertising();
    adv->addServiceUUID(NIKON_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->start();

    n_started = true;
    Serial.println("[NikonBLE] advertising as ML-L7");
    return true;
}

bool NikonBLE::waitForCamera(unsigned int seconds) {
    unsigned long deadline = millis() + seconds * 1000UL;
    while (millis() < deadline) {
        if (n_connected) return true;
        delay(100);
    }
    return n_connected;
}

bool NikonBLE::pair(unsigned int scanSeconds) {
    if (!startServer()) return false;

    // Camera must be in "Connect to smart device / remote" pairing menu
    if (!waitForCamera(scanSeconds)) {
        Serial.println("[NikonBLE] no camera connected");
        return false;
    }

    Preferences p;
    p.begin(NVS_NS);
    p.putBool(KEY_PAIRED, true);
    p.end();

    Serial.println("[NikonBLE] paired (camera connected)");
    return true;
}

bool NikonBLE::connect() {
    if (!startServer()) return false;
    // Camera reconnects on its own; give it a short window
    return waitForCamera(5);
}

bool NikonBLE::isConnected() {
    return n_connected;
}

void NikonBLE::disconnect() {
    if (n_server && n_connected) {
        n_server->disconnect(n_server->getConnId());
    }
}

bool NikonBLE::trigger() {
    if (!n_connected && !connect()) return false;

    n_btnChar->setValue((uint8_t*)NIKON_SHUTTER_DOWN, 2);
    n_btnChar->notify();
    delay(NIKON_SHUTTER_HOLD_MS);
    n_btnChar->setValue((uint8_t*)NIKON_SHUTTER_UP, 2);
    n_btnChar->notify();
    return true;
}

bool NikonBLE::focus() {
    if (!n_connected && !connect()) return false;

    n_btnChar->setValue((uint8_t*)NIKON_FOCUS_DOWN, 2);
    n_btnChar->notify();
    delay(NIKON_FOCUS_SETTLE_MS);
    n_btnChar->setValue((uint8_t*)NIKON_FOCUS_UP, 2);
    n_btnChar->notify();
    return true;
}

bool NikonBLE::hasPairedCamera() {
    Preferences p;
    p.begin(NVS_NS, true);
    bool has = p.getBool(KEY_PAIRED, false);
    p.end();
    return has;
}

String NikonBLE::pairedAddress() {
    return hasPairedCamera() ? String("(bonded)") : String("");
}

void NikonBLE::forgetCamera() {
    disconnect();
    Preferences p;
    p.begin(NVS_NS);
    p.remove(KEY_PAIRED);
    p.end();
}
