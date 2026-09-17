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
static volatile bool s_authDone = false;
static volatile bool s_authOk   = false;

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
        s_authOk = cmpl.success;
        s_authDone = true;
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
    s_authDone = false;

    if (!s_client) {
        s_client = BLEDevice::createClient();
        s_client->setClientCallbacks(&s_clientCB);
    }

    BLEAddress bleAddr(addr.c_str());
    if (!s_client->connect(bleAddr, (esp_ble_addr_type_t)addrType)) {
        Serial.println("[SonyBLE] connect failed");
        return false;
    }

    // Wait for the encryption/bonding handshake to actually finish before
    // touching any characteristic. Discovering services while security is
    // still pending (camera may even be showing its own on-screen "allow
    // this remote?" confirmation) can make the underlying bluedroid stack
    // retry GATT discovery repeatedly -- each retry adds to a fixed-size
    // characteristic cache (CONFIG_BT_GATTC_MAX_CACHE_CHAR) that never gets
    // pruned on a failed attempt, so it fills up and every discovery call
    // permanently fails with "char not added, no resources" for the rest
    // of the session. This was confirmed on hardware to happen on a
    // completely fresh (just erase_flash'd, never-bonded-before) device --
    // not something that built up over many sessions. Mirrors
    // CanonBLE::connectTo()'s c_authDone wait, which this codebase already
    // established fixes the same class of bug for Canon's MITM pairing.
    unsigned long deadline = millis() + 10000UL;
    while (!s_authDone && millis() < deadline) delay(20);
    if (!s_authDone) {
        Serial.println("[SonyBLE] auth timed out");
        s_client->disconnect();
        return false;
    }
    if (!s_authOk) {
        Serial.println("[SonyBLE] auth failed");
        s_client->disconnect();
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
    bool wasConnected = isConnected();
    if (!ensureConnected()) return false;
    // Only costs anything on an actual reconnect -- zero added latency for
    // the common already-connected case this was tuned for. See
    // SONY_RECONNECT_SETTLE_MS's comment.
    if (!wasConnected) delay(SONY_RECONNECT_SETTLE_MS);

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

// Bulb hold: same focus-then-shutter-down sequence as trigger(), but
// SHUTTER_UP is deliberately not sent here -- the freemote protocol keeps
// the shutter (and the camera's exposure, when it's in Bulb mode) open for
// as long as SHUTTER_DOWN is the last state sent. shutterRelease() sends
// the matching SHUTTER_UP/FOCUS_UP pair to close it.
bool SonyBLE::shutterPress() {
    bool wasConnected = isConnected();
    if (!ensureConnected()) return false;
    if (!wasConnected) delay(SONY_RECONNECT_SETTLE_MS);
    s_cmdChar->writeValue((uint8_t*)SONY_FOCUS_DOWN, 2, true);
    delay(SONY_FOCUS_SETTLE_MS);
    s_cmdChar->writeValue((uint8_t*)SONY_SHUTTER_DOWN, 2, true);
    return true;
}

bool SonyBLE::shutterRelease() {
    // Must reconnect if needed (not just check isConnected()) -- if the BLE
    // link dropped during a long bulb hold, the camera's shutter is still
    // physically open and this is the only chance to tell it to close.
    if (!ensureConnected()) return false;

    // HARDWARE-CONFIRMED on a Sony A7 IV (ILCE-7M4), with a live serial log
    // and the resulting EXIF: over BLE this camera IGNORES a bare "shutter
    // up" as a bulb-closing signal. It closes bulb only on the NEXT FULL
    // PRESS it receives. Symptom when this was a release-only command: the
    // write reported OK, the link stayed up the whole exposure, and yet the
    // frame ran PAST its configured length and only ended when the next
    // shot's press arrived -- that next press was spending itself closing
    // the previous exposure instead of starting a new one.
    //
    // Note this is the opposite of the WIRED G1/G2 path, where the release
    // port is a plain switch and holding it is what keeps the shutter open
    // (a toggle model was tried there and disproven -- see CLAUDE.md). Two
    // transports, two different rules for the same camera; do not "unify"
    // them.
    //
    // So: first complete the outstanding press cleanly (we sent FOCUS_DOWN +
    // SHUTTER_DOWN in shutterPress() and never released them), then send a
    // complete press as the actual closing event.
    s_cmdChar->writeValue((uint8_t*)SONY_SHUTTER_UP, 2, true);
    delay(SONY_RELEASE_GAP_MS);
    s_cmdChar->writeValue((uint8_t*)SONY_FOCUS_UP, 2, true);
    delay(SONY_RELEASE_GAP_MS);

    // The closing press.
    s_cmdChar->writeValue((uint8_t*)SONY_FOCUS_DOWN, 2, true);
    delay(SONY_FOCUS_SETTLE_MS);
    s_cmdChar->writeValue((uint8_t*)SONY_SHUTTER_DOWN, 2, true);
    delay(SONY_SHUTTER_HOLD_MS);
    s_cmdChar->writeValue((uint8_t*)SONY_SHUTTER_UP, 2, true);
    delay(SONY_RELEASE_GAP_MS);
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
