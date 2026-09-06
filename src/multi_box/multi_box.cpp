#include "multi_box.h"
#include "multi_box_controller.h"
#include "node_manager.h"
#include "flash_trigger.h"
#include "../auto_shoot/tf_luna.h"
#include <Preferences.h>
#include <math.h>

MultiBox multiBox;

#define NVS_NS         "multiBox"
#define KEY_SIGNAL     "mbSignal"
#define KEY_THRESH_CM  "mbThreshCm"
#define KEY_MIN_BULB   "mbMinBulb"
#define KEY_MAX_BULB   "mbMaxBulb"
#define KEY_REARM_MS   "mbRearmMs"

#define MB_BASELINE_SAMPLES 5

void MultiBox::loadConfig() {
    config.nodeId = nodeManager.selfNodeId();
    config.role = nodeManager.selfRole();

    Preferences p;
    p.begin(NVS_NS, true);
    config.signalMode = (MBSignalMode)p.getUChar(KEY_SIGNAL, (uint8_t)MBSignalMode::EMIT_START);
    config.detectThresholdCm = p.getUShort(KEY_THRESH_CM, 20);
    config.minBulbSec = p.getUShort(KEY_MIN_BULB, 2);
    config.maxBulbSec = p.getUShort(KEY_MAX_BULB, 30);
    config.rearmMs = p.getUShort(KEY_REARM_MS, 1000);
    p.end();

    if (config.minBulbSec < 1) config.minBulbSec = 1;
    if (config.maxBulbSec < config.minBulbSec) config.maxBulbSec = config.minBulbSec + 1;
}

void MultiBox::saveConfig() {
    nodeManager.setSelfNodeId(config.nodeId);
    nodeManager.setSelfRole(config.role);
    nodeManager.saveAll();

    Preferences p;
    p.begin(NVS_NS);
    p.putUChar(KEY_SIGNAL, (uint8_t)config.signalMode);
    p.putUShort(KEY_THRESH_CM, config.detectThresholdCm);
    p.putUShort(KEY_MIN_BULB, config.minBulbSec);
    p.putUShort(KEY_MAX_BULB, config.maxBulbSec);
    p.putUShort(KEY_REARM_MS, config.rearmMs);
    p.end();
}

void MultiBox::init() {
    loadConfig();
    state = MultiBoxState();
    editMode = MultiBoxEditMode();

    if (multiBoxController.begin()) {
        state.connState = MBConnState::DISCONNECTED;
        state.session = (config.role == MBRole::NONE) ? MultiBoxState::IDLE : MultiBoxState::READY;
    }

    if (config.role != MBRole::NONE && config.role != MBRole::CENTER) {
        tfLuna.begin();
    }
}

void MultiBox::teardown() {
    saveConfig();
}

// ============ Per-role sensor logic (START/FLASH only) ============
// Baseline is captured fresh every time this node BECOMES relevant to watch
// (mode entry / re-arm for an EMIT_START node; the instant a session starts
// for an EMIT_END or FLASH node) -- never a fixed value from setup, matching
// "baseline is measured when the system is armed."

void MultiBox::applyRoleSensorLogic() {
    if (config.role == MBRole::NONE || config.role == MBRole::CENTER) return;
    if (state.session == MultiBoxState::LOCKED) return;   // ignore repeats while locked

    bool shouldWatch;
    if (config.role == MBRole::START) {
        shouldWatch = (config.signalMode == MBSignalMode::EMIT_START)
                          ? !state.sessionActive
                          : state.sessionActive;
    } else {   // FLASH
        shouldWatch = state.sessionActive;
    }

    tfLuna.update();

    if (!shouldWatch) {
        state.baselineCaptured = false;
        state.baselineSamples = 0;
        return;
    }

    if (!state.baselineCaptured) {
        if (!tfLuna.hasObject()) return;   // wait for a valid reading before baselining
        if (state.baselineSamples == 0) state.baselineDistance = 0.0f;
        state.baselineDistance += tfLuna.getDistance();
        state.baselineSamples++;
        if (state.baselineSamples >= MB_BASELINE_SAMPLES) {
            state.baselineDistance /= MB_BASELINE_SAMPLES;
            state.baselineCaptured = true;
        }
        return;
    }

    if (!tfLuna.hasObject()) return;

    float deltaM = fabsf(tfLuna.getDistance() - state.baselineDistance);
    if (deltaM * 100.0f < config.detectThresholdCm) return;   // below threshold

    // Fire.
    if (config.role == MBRole::START) {
        bool isEnd = (config.signalMode == MBSignalMode::EMIT_END);
        multiBoxController.sendStartOrEnd(isEnd);
    } else {
        flashTrigger.fire();
        multiBoxController.sendFlashFire();
    }
    state.session = MultiBoxState::LOCKED;
    state.lockStartMs = millis();
}

void MultiBox::update() {
    if (config.role == MBRole::NONE) return;

    nodeManager.tick();
    multiBoxController.update();
    applyRoleSensorLogic();

    // Non-CENTER LOCKED->READY fallback: if CENTER never confirms SHOT_DONE
    // (lost packet, CENTER rebooted mid-session) don't stay locked forever.
    if (config.role != MBRole::CENTER && state.session == MultiBoxState::LOCKED) {
        if (millis() - state.lockStartMs > (unsigned long)(config.maxBulbSec + 5) * 1000UL) {
            state.session = MultiBoxState::READY;
            state.sessionActive = false;
            state.baselineCaptured = false;
        }
    }
}

// ============ UI interaction ============

MBConnRow MultiBox::connRowKind(uint8_t visualIdx) const {
    if (signalRowVisible()) {
        switch (visualIdx) {
            case 0: return MBConnRow::NODE_ID;
            case 1: return MBConnRow::ROLE;
            case 2: return MBConnRow::SIGNAL;
            case 3: return MBConnRow::PAIR;
            case 4: return MBConnRow::NODES;
            default: return MBConnRow::BACK;
        }
    }
    switch (visualIdx) {
        case 0: return MBConnRow::NODE_ID;
        case 1: return MBConnRow::ROLE;
        case 2: return MBConnRow::PAIR;
        case 3: return MBConnRow::NODES;
        default: return MBConnRow::BACK;
    }
}

void MultiBox::handleEncoderRotate(int delta) {
    if (editMode.state == MultiBoxEditMode::CONFIRM_EXIT) {
        editMode.confirmChoice = (editMode.confirmChoice == 0) ? 1 : 0;
        return;
    }

    if (editMode.screen == MultiBoxEditMode::MAIN) return;   // MAIN has nothing to rotate

    handleConnectionRotate(delta);
}

void MultiBox::handleConnectionRotate(int delta) {
    if (editMode.state == MultiBoxEditMode::SCANNING) {
        uint8_t n = nodeManager.scanResultCount();
        if (n == 0) return;
        int v = (int)editMode.scanSel + (delta > 0 ? 1 : -1);
        if (v < 0) v = n - 1;
        if (v >= (int)n) v = 0;
        editMode.scanSel = (uint8_t)v;
        return;
    }

    if (editMode.state == MultiBoxEditMode::PEER_LIST) {
        uint8_t n = nodeManager.peerCount();
        if (n == 0) return;
        int v = (int)editMode.peerSel + (delta > 0 ? 1 : -1);
        if (v < 0) v = n - 1;
        if (v >= (int)n) v = 0;
        editMode.peerSel = (uint8_t)v;
        return;
    }

    if (editMode.state == MultiBoxEditMode::EDITING) {
        MBConnRow row = connRowKind(editMode.connIndex);
        if (row == MBConnRow::NODE_ID) {
            int v = (int)config.nodeId + delta;
            if (v < 1) v = 1;
            if (v > 250) v = 250;
            config.nodeId = (uint8_t)v;
        } else if (row == MBConnRow::ROLE) {
            int v = (int)config.role + (delta > 0 ? 1 : -1);
            if (v < 0) v = 3;
            if (v > 3) v = 0;
            config.role = (MBRole)v;
        } else if (row == MBConnRow::SIGNAL) {
            config.signalMode = (config.signalMode == MBSignalMode::EMIT_START)
                                     ? MBSignalMode::EMIT_END
                                     : MBSignalMode::EMIT_START;
        }
        return;
    }

    // SELECTING: move between rows.
    int v = (int)editMode.connIndex + (delta > 0 ? 1 : -1);
    int count = connRowCount();
    if (v < 0) v = count - 1;
    if (v >= count) v = 0;
    editMode.connIndex = (uint8_t)v;
}

void MultiBox::handleButtonPress() {
    if (editMode.screen == MultiBoxEditMode::MAIN) {
        editMode.screen = MultiBoxEditMode::CONNECTION;
        editMode.state = MultiBoxEditMode::SELECTING;
        editMode.connIndex = 0;
        return;
    }
    handleConnectionPress();
}

void MultiBox::handleConnectionPress() {
    if (editMode.state == MultiBoxEditMode::EDITING) {
        editMode.state = MultiBoxEditMode::SELECTING;
        return;
    }

    if (editMode.state == MultiBoxEditMode::SCANNING) {
        if (!nodeManager.scanInProgress()) {
            NodeManager::Candidate* c = nodeManager.scanResultAt(editMode.scanSel);
            if (c) {
                nodeManager.addOrUpdatePeer(c->nodeId, c->role, c->mac);
                nodeManager.saveAll();
            }
        }
        editMode.state = MultiBoxEditMode::SELECTING;
        return;
    }

    if (editMode.state == MultiBoxEditMode::PEER_LIST) {
        MBPeer* p = nodeManager.peerAt(editMode.peerSel);
        if (p) {
            nodeManager.removePeer(p->nodeId);
            nodeManager.saveAll();
        }
        if (nodeManager.peerCount() == 0) {
            editMode.state = MultiBoxEditMode::SELECTING;
        } else if (editMode.peerSel >= nodeManager.peerCount()) {
            editMode.peerSel = nodeManager.peerCount() - 1;
        }
        return;
    }

    // SELECTING
    switch (connRowKind(editMode.connIndex)) {
        case MBConnRow::NODE_ID:
        case MBConnRow::ROLE:
        case MBConnRow::SIGNAL:
            editMode.state = MultiBoxEditMode::EDITING;
            break;
        case MBConnRow::PAIR:
            nodeManager.startScan();
            editMode.state = MultiBoxEditMode::SCANNING;
            editMode.scanSel = 0;
            break;
        case MBConnRow::NODES:
            if (nodeManager.peerCount() > 0) {
                editMode.state = MultiBoxEditMode::PEER_LIST;
                editMode.peerSel = 0;
            }
            break;
        case MBConnRow::BACK:
            editMode.screen = MultiBoxEditMode::MAIN;
            editMode.selectedIndex = 0;
            saveConfig();
            break;
    }
}

void MultiBox::handleButtonLongPress() {
    switch (editMode.state) {
        case MultiBoxEditMode::EDITING:
            editMode.state = MultiBoxEditMode::SELECTING;
            return;
        case MultiBoxEditMode::SCANNING:
            nodeManager.stopScan();
            editMode.state = MultiBoxEditMode::SELECTING;
            return;
        case MultiBoxEditMode::PEER_LIST:
            editMode.state = MultiBoxEditMode::SELECTING;
            return;
        default:
            break;
    }

    if (editMode.screen == MultiBoxEditMode::CONNECTION) {
        editMode.screen = MultiBoxEditMode::MAIN;
        editMode.selectedIndex = 0;
        saveConfig();
    }
    // screen==MAIN && state==SELECTING: nothing to do -- FactoryTest's wrapper
    // opens CONFIRM_EXIT before ever calling in here for that case.
}
