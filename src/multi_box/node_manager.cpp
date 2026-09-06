#include "node_manager.h"
#include <esp_now.h>
#include <WiFi.h>
#include <Preferences.h>
#include <cstring>

#define NVS_NS        "multiBox"
#define KEY_SELF_ID   "mbSelfId"
#define KEY_SELF_ROLE "mbSelfRole"
#define KEY_PEER_CNT  "mbPeerCnt"
#define KEY_PEERS     "mbPeers"

NodeManager nodeManager;

typedef struct __attribute__((packed)) {
    uint8_t nodeId;
    uint8_t role;
    uint8_t mac[6];
} PersistedPeer;

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void NodeManager::registerEspNowPeer(const uint8_t mac[6]) {
    if (esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t info = {};
    memcpy(info.peer_addr, mac, 6);
    info.channel = 0;   // use whatever channel the radio is already on
    info.ifidx = WIFI_IF_STA;
    info.encrypt = false;
    esp_now_add_peer(&info);
}

void NodeManager::unregisterEspNowPeer(const uint8_t mac[6]) {
    if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
}

void NodeManager::begin() {
    Preferences p;
    p.begin(NVS_NS, true);
    _selfNodeId = p.getUChar(KEY_SELF_ID, 0);
    _selfRole   = (MBRole)p.getUChar(KEY_SELF_ROLE, (uint8_t)MBRole::NONE);
    _peerCount  = p.getUChar(KEY_PEER_CNT, 0);
    if (_peerCount > MB_MAX_PEERS) _peerCount = MB_MAX_PEERS;

    PersistedPeer buf[MB_MAX_PEERS];
    size_t got = p.getBytes(KEY_PEERS, buf, sizeof(buf));
    p.end();

    uint8_t loaded = (uint8_t)(got / sizeof(PersistedPeer));
    if (loaded < _peerCount) _peerCount = loaded;

    for (uint8_t i = 0; i < _peerCount; i++) {
        _peers[i] = MBPeer();
        _peers[i].nodeId = buf[i].nodeId;
        _peers[i].role   = (MBRole)buf[i].role;
        memcpy(_peers[i].mac, buf[i].mac, 6);
    }

    // Auto-generate a stable node id on first boot (never 0 -- 0 means unset).
    if (_selfNodeId == 0) {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        _selfNodeId = mac[5] == 0 ? 1 : mac[5];
    }

    registerEspNowPeer(BROADCAST_MAC);
    for (uint8_t i = 0; i < _peerCount; i++) registerEspNowPeer(_peers[i].mac);
}

void NodeManager::saveAll() {
    Preferences p;
    p.begin(NVS_NS);
    p.putUChar(KEY_SELF_ID, _selfNodeId);
    p.putUChar(KEY_SELF_ROLE, (uint8_t)_selfRole);
    p.putUChar(KEY_PEER_CNT, _peerCount);

    PersistedPeer buf[MB_MAX_PEERS];
    for (uint8_t i = 0; i < _peerCount; i++) {
        buf[i].nodeId = _peers[i].nodeId;
        buf[i].role   = (uint8_t)_peers[i].role;
        memcpy(buf[i].mac, _peers[i].mac, 6);
    }
    p.putBytes(KEY_PEERS, buf, sizeof(PersistedPeer) * _peerCount);
    p.end();
}

MBPeer* NodeManager::peerAt(uint8_t idx) {
    return (idx < _peerCount) ? &_peers[idx] : nullptr;
}

MBPeer* NodeManager::findByNodeId(uint8_t nodeId) {
    for (uint8_t i = 0; i < _peerCount; i++)
        if (_peers[i].nodeId == nodeId) return &_peers[i];
    return nullptr;
}

MBPeer* NodeManager::findByMac(const uint8_t mac[6]) {
    for (uint8_t i = 0; i < _peerCount; i++)
        if (memcmp(_peers[i].mac, mac, 6) == 0) return &_peers[i];
    return nullptr;
}

MBPeer* NodeManager::findCenter() {
    for (uint8_t i = 0; i < _peerCount; i++)
        if (_peers[i].role == MBRole::CENTER) return &_peers[i];
    return nullptr;
}

bool NodeManager::isKnownMac(const uint8_t mac[6]) {
    return findByMac(mac) != nullptr;
}

bool NodeManager::addOrUpdatePeer(uint8_t nodeId, MBRole role, const uint8_t mac[6]) {
    MBPeer* existing = findByNodeId(nodeId);
    if (!existing) existing = findByMac(mac);

    if (existing) {
        existing->role = role;
        memcpy(existing->mac, mac, 6);
    } else {
        if (_peerCount >= MB_MAX_PEERS) return false;
        _peers[_peerCount] = MBPeer();
        _peers[_peerCount].nodeId = nodeId;
        _peers[_peerCount].role = role;
        memcpy(_peers[_peerCount].mac, mac, 6);
        _peerCount++;
    }
    registerEspNowPeer(mac);
    return true;
}

bool NodeManager::removePeer(uint8_t nodeId) {
    for (uint8_t i = 0; i < _peerCount; i++) {
        if (_peers[i].nodeId == nodeId) {
            unregisterEspNowPeer(_peers[i].mac);
            for (uint8_t j = i; j < _peerCount - 1; j++) _peers[j] = _peers[j + 1];
            _peerCount--;
            return true;
        }
    }
    return false;
}

uint8_t NodeManager::connectedCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < _peerCount; i++)
        if (_peers[i].connected) n++;
    return n;
}

bool NodeManager::acceptSequence(uint8_t nodeId, uint16_t seq) {
    MBPeer* p = findByNodeId(nodeId);
    if (!p) return false;   // unknown sender -- reject

    if (!p->haveSeq) {
        p->haveSeq = true;
        p->lastSeq = seq;
        p->lastSeen = millis();
        p->connected = true;
        return true;
    }

    int16_t d = (int16_t)(seq - p->lastSeq);
    if (d <= 0) return false;   // duplicate or stale/out-of-order

    p->lastSeq = seq;
    p->lastSeen = millis();
    p->connected = true;
    return true;
}

void NodeManager::markSeen(uint8_t nodeId) {
    MBPeer* p = findByNodeId(nodeId);
    if (!p) return;
    p->lastSeen = millis();
    p->connected = true;
}

// ---- discovery scan ----

void NodeManager::startScan(unsigned int windowMs) {
    _scanning = true;
    _scanDeadline = millis() + windowMs;
    _scanResultCount = 0;

    MBPacket pkt = {};
    pkt.command = (uint8_t)MBCommand::HELLO;
    pkt.nodeId = _selfNodeId;
    pkt.role = (uint8_t)_selfRole;
    pkt.sessionId = 0;
    pkt.sequence = 0;
    pkt.timestamp = millis();
    esp_now_send(BROADCAST_MAC, (uint8_t*)&pkt, sizeof(pkt));
}

void NodeManager::stopScan() {
    _scanning = false;
}

void NodeManager::tick() {
    if (_scanning && millis() >= _scanDeadline) _scanning = false;
}

NodeManager::Candidate* NodeManager::scanResultAt(uint8_t idx) {
    return (idx < _scanResultCount) ? &_scanResults[idx] : nullptr;
}

void NodeManager::onDiscovered(uint8_t nodeId, MBRole role, const uint8_t mac[6]) {
    for (uint8_t i = 0; i < _scanResultCount; i++) {
        if (memcmp(_scanResults[i].mac, mac, 6) == 0) {
            _scanResults[i].nodeId = nodeId;
            _scanResults[i].role = role;
            return;
        }
    }
    if (_scanResultCount >= MB_MAX_PEERS) return;
    _scanResults[_scanResultCount].nodeId = nodeId;
    _scanResults[_scanResultCount].role = role;
    memcpy(_scanResults[_scanResultCount].mac, mac, 6);
    _scanResultCount++;
}
