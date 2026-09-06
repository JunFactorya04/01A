/**
 * @file node_manager.h
 * @brief MULTI BOX peer table — persistence, ESP-NOW peer registration,
 *        discovery scan bookkeeping, sequence/replay validation
 * @date 2026-09-06
 */

#pragma once
#include <Arduino.h>
#include "multi_box_protocol.h"

#define MB_MAX_PEERS 8

struct MBPeer {
    uint8_t nodeId = 0;
    MBRole  role   = MBRole::NONE;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};

    // Runtime-only, never persisted.
    unsigned long lastSeen = 0;
    bool          connected = false;
    uint16_t      lastSeq = 0;
    bool          haveSeq = false;
};

class NodeManager {
public:
    void begin();   // load identity + peer table from NVS, register ESP-NOW peers
    void saveAll(); // persist self identity + peer table

    // ----- self identity -----
    uint8_t selfNodeId() const { return _selfNodeId; }
    void    setSelfNodeId(uint8_t id) { _selfNodeId = id; }
    MBRole  selfRole() const { return _selfRole; }
    void    setSelfRole(MBRole role) { _selfRole = role; }

    // ----- peer table -----
    uint8_t peerCount() const { return _peerCount; }
    MBPeer* peerAt(uint8_t idx);
    MBPeer* findByNodeId(uint8_t nodeId);
    MBPeer* findByMac(const uint8_t mac[6]);
    MBPeer* findCenter();
    bool    addOrUpdatePeer(uint8_t nodeId, MBRole role, const uint8_t mac[6]);
    bool    removePeer(uint8_t nodeId);
    bool    isKnownMac(const uint8_t mac[6]);
    uint8_t connectedCount() const;

    // ----- discovery scan (non-blocking) -----
    // startScan() broadcasts HELLO once; tick() must be called every loop
    // while scanning to advance the timeout. HELLO_ACK replies collected via
    // onDiscovered() (called by MultiBoxController's recv handler).
    struct Candidate {
        uint8_t nodeId = 0;
        MBRole  role = MBRole::NONE;
        uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    };

    void    startScan(unsigned int windowMs = 4000);
    void    stopScan();
    bool    scanInProgress() const { return _scanning; }
    void    tick();
    uint8_t scanResultCount() const { return _scanResultCount; }
    Candidate* scanResultAt(uint8_t idx);
    void    onDiscovered(uint8_t nodeId, MBRole role, const uint8_t mac[6]);

    // ----- sequence / replay validation (also used by MultiBoxController) -----
    // false = unknown sender, or duplicate/out-of-order sequence -> reject.
    bool acceptSequence(uint8_t nodeId, uint16_t seq);
    void markSeen(uint8_t nodeId);

private:
    uint8_t _selfNodeId = 0;
    MBRole  _selfRole = MBRole::NONE;

    MBPeer  _peers[MB_MAX_PEERS];
    uint8_t _peerCount = 0;

    bool          _scanning = false;
    unsigned long _scanDeadline = 0;
    Candidate     _scanResults[MB_MAX_PEERS];
    uint8_t       _scanResultCount = 0;

    void registerEspNowPeer(const uint8_t mac[6]);
    void unregisterEspNowPeer(const uint8_t mac[6]);
};

extern NodeManager nodeManager;
