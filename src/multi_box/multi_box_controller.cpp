#include "multi_box_controller.h"
#include "multi_box.h"
#include "node_manager.h"
#include "flash_trigger.h"
#include "../common/hardware_config.h"
#include "../trigger_mode/trigger_mode.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstring>

MultiBoxController multiBoxController;

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct MBQueuedEvent {
    MBPacket pkt;
    uint8_t mac[6];
    unsigned long receivedAt;
};

// Fixed channel: none of these boxes ever join a real AP, so there is no
// external authority assigning a channel. Pinning to 1 explicitly guarantees
// every box agrees, rather than hoping WIFI_STA's default matches.
#define MB_ESPNOW_CHANNEL 1
#define MB_QUEUE_STALE_MS 3000

static QueueHandle_t s_queue = nullptr;
static uint16_t s_localSeq = 0;
static uint16_t s_sessionCounter = 0;

static void espNowRecvCallback(const uint8_t* mac, const uint8_t* data, int len) {
    if (!s_queue) return;
    if (len != (int)sizeof(MBPacket)) return;

    MBQueuedEvent ev;
    memcpy(&ev.pkt, data, sizeof(MBPacket));
    memcpy(ev.mac, mac, 6);
    ev.receivedAt = millis();
    xQueueSend(s_queue, &ev, 0);   // never block the WiFi task; drop if full
}

uint16_t MultiBoxController::nextSequence() {
    s_localSeq++;
    if (s_localSeq == 0) s_localSeq = 1;   // 0 reserved for "no sequence yet"
    return s_localSeq;
}

uint16_t MultiBoxController::nextSessionId() {
    s_sessionCounter++;
    if (s_sessionCounter == 0) s_sessionCounter = 1;
    return s_sessionCounter;
}

void MultiBoxController::sendTo(MBCommand cmd, const uint8_t mac[6], uint16_t sessionId) {
    MBPacket pkt = {};
    pkt.command = (uint8_t)cmd;
    pkt.nodeId = nodeManager.selfNodeId();
    pkt.role = (uint8_t)nodeManager.selfRole();
    pkt.sessionId = sessionId;
    pkt.sequence = nextSequence();
    pkt.timestamp = millis();
    esp_now_send(mac, (uint8_t*)&pkt, sizeof(pkt));
}

void MultiBoxController::broadcastTo(MBCommand cmd, uint16_t sessionId) {
    sendTo(cmd, BROADCAST_MAC, sessionId);
}

bool MultiBoxController::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    esp_wifi_set_channel(MB_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[MultiBox] esp_now_init failed");
        return false;
    }

    if (!s_queue) s_queue = xQueueCreate(16, sizeof(MBQueuedEvent));
    esp_now_register_recv_cb(espNowRecvCallback);

    nodeManager.begin();
    return true;
}

void MultiBoxController::end() {
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
}

// ============ Bulb open/close (mirrors Timelapse's non-blocking bulb hold —
// see timelapse.cpp startBulbExposure()/endBulbExposure() — duplicated here
// deliberately rather than reused, since Timelapse must not be touched) ====

void MultiBoxController::openBulb() {
    if (!acquireTriggerLock()) return;   // another mode/pulse holds it; drop this shot

    bool fireG2 = triggerMode.config.triggerEnabled;
    bool fireG1 = triggerMode.config.remoteEnabled;

    if (fireG2) digitalWrite(TRIGGER_G2_PIN, HIGH);
    if (fireG1) digitalWrite(TRIGGER_G1_PIN, HIGH);

    multiBox.state.bulbFiredG2 = fireG2;
    multiBox.state.bulbFiredG1 = fireG1;
    // Trigger lock is released in closeBulb()/forceCloseBulbIfExposing() —
    // it must stay held for the whole exposure, not just the pulse edges.
}

void MultiBoxController::closeBulb() {
    if (multiBox.state.bulbFiredG2) digitalWrite(TRIGGER_G2_PIN, LOW);
    if (multiBox.state.bulbFiredG1) digitalWrite(TRIGGER_G1_PIN, LOW);
    multiBox.state.bulbFiredG2 = false;
    multiBox.state.bulbFiredG1 = false;
    releaseTriggerLock();
}

void MultiBoxController::forceCloseBulbIfExposing() {
    if (multiBox.state.session != MultiBoxState::EXPOSING) return;
    closeBulb();
}

// ============ CENTER coordinator ============

void MultiBoxController::centerOnStart(const MBPacket& pkt, const uint8_t mac[6]) {
    MBPeer* sender = nodeManager.findByMac(mac);
    if (!sender || sender->role != MBRole::START) return;          // unknown/wrong-role sender
    if (!nodeManager.acceptSequence(sender->nodeId, pkt.sequence)) return;   // dup/stale
    if (multiBox.state.session != MultiBoxState::READY) return;    // only one session at a time

    uint16_t sid = nextSessionId();
    multiBox.state.currentSessionId = sid;
    multiBox.state.session = MultiBoxState::EXPOSING;
    multiBox.state.sessionStartMs = millis();
    multiBox.state.endRequested = false;

    openBulb();

    sendTo(MBCommand::START_ACK, mac, sid);
    broadcastTo(MBCommand::START, sid);   // tells every other node a session began
}

void MultiBoxController::centerOnEndDetect(const MBPacket& pkt, const uint8_t mac[6]) {
    MBPeer* sender = nodeManager.findByMac(mac);
    if (!sender || sender->role != MBRole::START) return;
    if (!nodeManager.acceptSequence(sender->nodeId, pkt.sequence)) return;
    if (multiBox.state.session != MultiBoxState::EXPOSING) return;
    if (pkt.sessionId != multiBox.state.currentSessionId) return;   // stale session

    multiBox.state.endRequested = true;
}

void MultiBoxController::centerCloseBulbIfDue() {
    if (multiBox.state.session == MultiBoxState::EXPOSING) {
        unsigned long elapsed = millis() - multiBox.state.sessionStartMs;
        bool minReached = elapsed >= (unsigned long)multiBox.config.minBulbSec * 1000UL;
        bool maxReached = elapsed >= (unsigned long)multiBox.config.maxBulbSec * 1000UL;

        if ((multiBox.state.endRequested && minReached) || maxReached) {
            closeBulb();
            multiBox.state.shotCount++;
            multiBox.state.session = MultiBoxState::REARM;
            multiBox.state.sessionStartMs = millis();   // reused as rearm-start
            broadcastTo(MBCommand::SHOT_DONE, multiBox.state.currentSessionId);
        }
        return;
    }

    if (multiBox.state.session == MultiBoxState::REARM) {
        if (millis() - multiBox.state.sessionStartMs >= multiBox.config.rearmMs) {
            multiBox.state.session = MultiBoxState::READY;
            broadcastTo(MBCommand::READY, 0);
        }
    }
}

// ============ Non-CENTER node reacting to CENTER's broadcasts ============

void MultiBoxController::nodeOnCenterBroadcast(const MBPacket& pkt, const uint8_t mac[6]) {
    MBPeer* sender = nodeManager.findByMac(mac);
    if (!sender || sender->role != MBRole::CENTER) return;   // only trust our paired CENTER
    if (!nodeManager.acceptSequence(sender->nodeId, pkt.sequence)) return;

    switch ((MBCommand)pkt.command) {
        case MBCommand::START:
            multiBox.state.currentSessionId = pkt.sessionId;
            multiBox.state.sessionActive = true;
            multiBox.state.baselineCaptured = false;   // re-baseline for this session
            if (multiBox.config.role == MBRole::START &&
                multiBox.config.signalMode == MBSignalMode::EMIT_START) {
                // The node that fired the START itself already locked at send
                // time; this just keeps it in sync if it somehow missed that.
                multiBox.state.session = MultiBoxState::LOCKED;
            }
            break;

        case MBCommand::SHOT_DONE:
        case MBCommand::DISARM:
            multiBox.state.sessionActive = false;
            multiBox.state.session = MultiBoxState::READY;
            multiBox.state.baselineCaptured = false;
            break;

        case MBCommand::READY:
            multiBox.state.connState = MBConnState::LINKED;
            break;

        default:
            break;
    }
}

// ============ Dispatch ============

void MultiBoxController::handleIncoming(const MBPacket& pkt, const uint8_t mac[6], unsigned long receivedAt) {
    if (millis() - receivedAt > MB_QUEUE_STALE_MS) return;   // backlog guard

    MBCommand cmd = (MBCommand)pkt.command;

    if (cmd == MBCommand::HELLO) {
        if (nodeManager.selfRole() != MBRole::NONE) {
            sendTo(MBCommand::HELLO_ACK, mac, 0);
        }
        return;
    }
    if (cmd == MBCommand::HELLO_ACK) {
        if (nodeManager.scanInProgress()) {
            nodeManager.onDiscovered(pkt.nodeId, (MBRole)pkt.role, mac);
        }
        return;
    }
    if (cmd == MBCommand::PING) {
        sendTo(MBCommand::PONG, mac, 0);
        return;
    }
    if (cmd == MBCommand::PONG) {
        nodeManager.markSeen(pkt.nodeId);
        return;
    }

    if (!nodeManager.isKnownMac(mac)) return;   // everything else needs a paired sender

    if (nodeManager.selfRole() == MBRole::CENTER) {
        switch (cmd) {
            case MBCommand::START:      centerOnStart(pkt, mac); break;
            case MBCommand::END_DETECT: centerOnEndDetect(pkt, mac); break;
            case MBCommand::FLASH_FIRE: multiBox.state.lastFlashAtMs = millis(); break;
            case MBCommand::DISARM:
                forceCloseBulbIfExposing();
                multiBox.state.session = MultiBoxState::READY;
                break;
            default: break;
        }
    } else {
        nodeOnCenterBroadcast(pkt, mac);
    }
}

void MultiBoxController::update() {
    if (!s_queue) return;

    MBQueuedEvent ev;
    // Bounded drain so a burst of traffic can't starve the rest of the loop.
    for (int i = 0; i < 10 && xQueueReceive(s_queue, &ev, 0) == pdTRUE; i++) {
        handleIncoming(ev.pkt, ev.mac, ev.receivedAt);
    }

    if (nodeManager.selfRole() == MBRole::CENTER) {
        centerCloseBulbIfDue();
    }
}

// ============ Node-side send helpers ============

void MultiBoxController::sendStartOrEnd(bool isEnd) {
    MBPeer* center = nodeManager.findCenter();
    if (!center) return;
    sendTo(isEnd ? MBCommand::END_DETECT : MBCommand::START, center->mac,
           multiBox.state.currentSessionId);
}

void MultiBoxController::sendFlashFire() {
    MBPeer* center = nodeManager.findCenter();
    if (!center) return;
    sendTo(MBCommand::FLASH_FIRE, center->mac, multiBox.state.currentSessionId);
}

void MultiBoxController::broadcastDisarm() {
    broadcastTo(MBCommand::DISARM, multiBox.state.currentSessionId);
}

void MultiBoxController::requestExit() {
    forceCloseBulbIfExposing();
    broadcastDisarm();
    multiBox.state.session = MultiBoxState::IDLE;
    multiBox.state.sessionActive = false;
    multiBox.state.connState = MBConnState::DISCONNECTED;
    end();
}
