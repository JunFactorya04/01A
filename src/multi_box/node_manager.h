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

// Heartbeat. Nothing used to send PING at all -- the handlers existed but no
// traffic ever carried them -- which left two things broken: a peer's role was
// frozen at whatever it was when you paired, and `connected` was set true once
// and never cleared, so an unplugged box still showed LINKED forever.
#define MB_HEARTBEAT_MS   2000
#define MB_PEER_TIMEOUT_MS 7000   // ~3 missed heartbeats

struct MBPeer {
    uint8_t nodeId = 0;
    MBRole  role   = MBRole::NONE;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};

    // Runtime-only, never persisted.
    unsigned long lastSeen = 0;
    bool          connected = false;
    unsigned long roleSavedAt = 0;   // NVS write rate limit, see refreshPeerRole()
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
    bool    isSelfMac(const uint8_t mac[6]);
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

    // ----- conflicts -----
    // Configuration clashes that break the system but are invisible from any
    // single box's normal status, so they are surfaced instead of guessed at.
    // Self-clearing: re-flagged while the clash persists, expires once it stops.
    enum class Conflict : uint8_t { NONE = 0, DUP_ID, DUP_MAIN, DUP_START };
    Conflict conflict() const;
    void     noteConflict(Conflict c);

    // ----- sequence / replay validation (also used by MultiBoxController) -----
    // Keyed by MAC, NOT by node id. Node ids are user-assigned and nothing
    // enforces uniqueness, so two boxes sharing one would share a sequence
    // counter and reject each other's packets as duplicates -- an
    // intermittent failure with no visible cause. A MAC cannot be mistyped.
    // false = unknown sender, or duplicate/out-of-order sequence -> reject.
    bool acceptSequence(const uint8_t mac[6], uint16_t seq);
    void markSeen(uint8_t nodeId);

    // Adopt the role a peer just told us it has. Every packet carries the
    // sender's CURRENT role, so this is what lets a role change on one box
    // propagate to the others without re-pairing -- the case that used to
    // silently break the whole system (pair first, assign roles after, and
    // findCenter() would never find the MAIN).
    void refreshPeerRole(const uint8_t mac[6], MBRole role);

    // A packet claiming this node id arrived from a DIFFERENT mac -> two boxes
    // are configured with the same id.
    bool nodeIdClashes(uint8_t nodeId, const uint8_t mac[6]);

    // True when this box is MAIN and another box also claims MAIN. The system
    // only tolerates one; this is surfaced on screen rather than silently
    // picking a winner.


private:
    uint8_t _selfNodeId = 0;
    MBRole  _selfRole = MBRole::NONE;

    MBPeer  _peers[MB_MAX_PEERS];
    uint8_t _peerCount = 0;

    Conflict      _conflict = Conflict::NONE;
    unsigned long _conflictAt = 0;
    unsigned long _lastHeartbeat = 0;
    bool          _scanning = false;
    unsigned long _scanDeadline = 0;
    Candidate     _scanResults[MB_MAX_PEERS];
    uint8_t       _scanResultCount = 0;

    void registerEspNowPeer(const uint8_t mac[6]);
    void unregisterEspNowPeer(const uint8_t mac[6]);
};

extern NodeManager nodeManager;
