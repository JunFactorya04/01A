#include "multi_box.h"
#include "multi_box_controller.h"
#include "node_manager.h"
#include "flash_trigger.h"
#include "../auto_shoot/tf_luna.h"
#include <Preferences.h>
#include <math.h>

MultiBox multiBox;

#define NVS_NS         "multiBox"
#define KEY_THRESH_CM  "mbThreshCm"
#define KEY_MIN_BULB   "mbMinBulb"
#define KEY_MAX_BULB   "mbMaxBulb"
#define KEY_FLASH_DLY  "mbFlashDly"
#define KEY_REARM_MS   "mbRearmMs"

#define MB_BASELINE_SAMPLES 5

void MultiBox::loadConfig() {
    config.nodeId = nodeManager.selfNodeId();
    config.role = nodeManager.selfRole();

    Preferences p;
    p.begin(NVS_NS, true);
    config.detectThresholdCm = p.getUShort(KEY_THRESH_CM, 20);
    config.minBulbSec = p.getUShort(KEY_MIN_BULB, 2);
    config.maxBulbSec = p.getUShort(KEY_MAX_BULB, 30);
    config.rearmMs = p.getUShort(KEY_REARM_MS, 1000);
    config.flashDelayMs = p.getUShort(KEY_FLASH_DLY, 0);
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
    p.putUShort(KEY_THRESH_CM, config.detectThresholdCm);
    p.putUShort(KEY_MIN_BULB, config.minBulbSec);
    p.putUShort(KEY_MAX_BULB, config.maxBulbSec);
    p.putUShort(KEY_REARM_MS, config.rearmMs);
    p.putUShort(KEY_FLASH_DLY, config.flashDelayMs);
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

    if (config.role != MBRole::NONE && config.role != MBRole::MAIN) {
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

// The box you are holding when you set the system up is the one wired to the
// camera, so a box that goes looking for nodes while it has no role yet claims
// MAIN for itself. Without this, pairing left BOTH boxes at NONE and nothing
// worked until the user guessed that a role had to be set by hand, on the
// right box, before anything would link.
//
// Decided when the scan ENDS, not when it starts: by then we know whether a
// MAIN already answered, and if one did we must not become a second.
void MultiBox::autoClaimMainIfScanFinished() {
    static bool wasScanning = false;
    bool scanning = nodeManager.scanInProgress();
    bool justFinished = (wasScanning && !scanning);
    wasScanning = scanning;

    if (!justFinished) return;
    if (config.role != MBRole::NONE) return;   // already has a job; leave it alone

    for (uint8_t i = 0; i < nodeManager.scanResultCount(); i++) {
        NodeManager::Candidate* cand = nodeManager.scanResultAt(i);
        if (cand && cand->role == MBRole::MAIN) return;   // a MAIN exists -- do not become a second
    }

    config.role = MBRole::MAIN;
    nodeManager.setSelfRole(MBRole::MAIN);
    saveConfig();
}

// Changing a box's role mid-operation is a hard reset of everything that role
// was doing. Two things made this unsafe before:
//
//   1. If MAIN was EXPOSING, nothing closed the shutter. config.role flipped,
//      MAIN's logic stopped running, and the trigger lines stayed held with
//      the BLE shutter still pressed -- an exposure open forever.
//   2. config.role changed instantly but nodeManager's copy only caught up at
//      saveConfig(), so in between, the heartbeat advertised the OLD role to
//      the network while this box already behaved as the NEW one.
void MultiBox::applyRoleChange() {
    multiBoxController.forceCloseBulbIfExposing();

    nodeManager.setSelfRole(config.role);   // advertise the new role immediately

    state.session         = MultiBoxState::IDLE;
    state.sessionActive   = false;
    state.baselineCaptured = false;
    state.baselineSamples  = 0;
    state.endRequested    = false;
    state.flashPending    = false;
}

void MultiBox::applyRoleSensorLogic() {
    if (config.role == MBRole::NONE) return;
    if (state.session == MultiBoxState::LOCKED) return;   // ignore repeats while locked

    // What each role watches for, and when:
    //
    //   START  the athlete crossing the START line, but only while no session
    //          is running. On a hit it asks MAIN to open the shutter and then
    //          goes LOCKED -- it stops detecting entirely until MAIN reports
    //          the cycle finished, so one runner cannot open two sessions.
    //
    //   MAIN   the athlete crossing the FINISH line, which is MAIN itself:
    //          MAIN is the box wired to the camera and holds the shutter open
    //          between the two crossings. It only watches while EXPOSING --
    //          there is nothing to close before the shutter is open.
    //
    //   FLASH  the athlete passing its own position mid-run, to fire a flash.
    //          Watches while a session is active.
    bool shouldWatch;
    if (config.role == MBRole::START) {
        shouldWatch = !state.sessionActive;
    } else if (config.role == MBRole::MAIN) {
        shouldWatch = (state.session == MultiBoxState::EXPOSING);
    } else {   // FLASH
        shouldWatch = state.sessionActive;
    }

    tfLuna.update();

    if (!shouldWatch) {
        // Drop the baseline whenever this node is not watching, so the next
        // watch window re-measures the scene it is actually looking at.
        //
        // MAIN is the exception: its watch window opens the instant the
        // shutter does, and baselining takes MB_BASELINE_SAMPLES readings.
        // Re-measuring from scratch at that moment would spend the start of
        // every exposure blind -- exactly when the runner is closest to it.
        // So MAIN keeps baselining while it waits, and arrives at EXPOSING
        // with a settled reference.
        if (config.role == MBRole::MAIN && state.session != MultiBoxState::LOCKED) {
            captureBaselineStep();
        } else {
            state.baselineCaptured = false;
            state.baselineSamples = 0;
        }
        return;
    }

    if (!state.baselineCaptured) {
        captureBaselineStep();
        return;
    }

    if (!tfLuna.hasObject()) return;

    float deltaM = fabsf(tfLuna.getDistance() - state.baselineDistance);
    if (deltaM * 100.0f < config.detectThresholdCm) return;   // below threshold

    // Fire.
    if (config.role == MBRole::START) {
        multiBoxController.sendStartOrEnd(false);
        state.session = MultiBoxState::LOCKED;
        state.lockStartMs = millis();
    } else if (config.role == MBRole::MAIN) {
        // Finish line crossed. Ask the controller to end the exposure rather
        // than closing the shutter here: minBulbSec still has to be honoured,
        // and the controller owns that (a runner crossing early, or sensor
        // noise right after the shutter opened, must not cut the frame short).
        multiBoxController.requestEnd();
    } else {   // FLASH
        // Arm, don't fire. The actual pulse goes out in update() once
        // flashDelayMs has elapsed -- see MultiBoxState::flashPending.
        state.flashPending = true;
        state.flashDueAtMs = millis() + config.flashDelayMs;
        state.session = MultiBoxState::LOCKED;
        state.lockStartMs = millis();
    }
}

// One non-blocking step of baseline accumulation. Split out because MAIN now
// needs to run it outside its watch window too (see applyRoleSensorLogic()).
void MultiBox::captureBaselineStep() {
    if (state.baselineCaptured) return;
    if (!tfLuna.hasObject()) return;   // wait for a valid reading before baselining
    if (state.baselineSamples == 0) state.baselineDistance = 0.0f;
    state.baselineDistance += tfLuna.getDistance();
    state.baselineSamples++;
    if (state.baselineSamples >= MB_BASELINE_SAMPLES) {
        state.baselineDistance /= MB_BASELINE_SAMPLES;
        state.baselineCaptured = true;
    }
}

void MultiBox::update() {
    if (config.role == MBRole::NONE) return;

    nodeManager.tick();
    multiBoxController.update();
    autoClaimMainIfScanFinished();
    applyRoleSensorLogic();

    // FLASH: the delayed pulse. Checked every tick rather than slept through,
    // so the node keeps handling ESP-NOW and rendering while it waits.
    if (state.flashPending && (long)(millis() - state.flashDueAtMs) >= 0) {
        state.flashPending = false;
        flashTrigger.fire();
        multiBoxController.sendFlashFire();   // status/logging only; MAIN does not act on it
    }

    // Non-MAIN LOCKED->READY fallback: never stay locked forever if MAIN
    // never confirms SHOT_DONE (lost packet, MAIN rebooted mid-session).
    //
    // Two independent releases, because one alone is not enough:
    //   - MAIN went quiet. Now that there is a heartbeat, a missing MAIN is
    //     detectable in seconds. The old code only had the absolute cap
    //     below, which with Max Bulb set to its 900s maximum left a START
    //     node stuck for a quarter of an hour after MAIN simply lost power.
    //   - Absolute cap, for a MAIN that is still alive and heartbeating but
    //     whose SHOT_DONE never arrived.
    if (config.role != MBRole::MAIN && state.session == MultiBoxState::LOCKED) {
        MBPeer* main = nodeManager.findCenter();
        bool mainGone = (main == nullptr) || !main->connected;

        bool capReached =
            millis() - state.lockStartMs > (unsigned long)(config.maxBulbSec + 5) * 1000UL;

        if (mainGone || capReached) {
            state.session = MultiBoxState::READY;
            state.sessionActive = false;
            state.baselineCaptured = false;
            state.flashPending = false;
        }
    }
}

// ============ UI interaction ============

MBConnRow MultiBox::connRowKind(uint8_t visualIdx) const {
    // Identity rows are always first.
    if (visualIdx == 0) return MBConnRow::NODE_ID;
    if (visualIdx == 1) return MBConnRow::ROLE;

    // Role-specific block. Sized to match connRowCount() -- change both.
    uint8_t i = visualIdx - 2;
    if (config.role != MBRole::NONE) {
        if (i == 0) return MBConnRow::DETECT;
        i--;
        if (config.role == MBRole::FLASH) {
            if (i == 0) return MBConnRow::FLASH_DELAY;
            i--;
        }
        if (config.role == MBRole::MAIN) {
            if (i == 0) return MBConnRow::MIN_BULB;
            if (i == 1) return MBConnRow::MAX_BULB;
            if (i == 2) return MBConnRow::REARM;
            i -= 3;
        }
    }

    switch (i) {
        case 0:  return MBConnRow::PAIR;
        case 1:  return MBConnRow::NODES;
        default: return MBConnRow::BACK;
    }
}

void MultiBox::handleEncoderRotate(int delta) {
    if (editMode.state == MultiBoxEditMode::CONFIRM_EXIT ||
        editMode.state == MultiBoxEditMode::CONFIRM_DISRUPT) {
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
            applyRoleChange();
            // The role decides which rows exist at all, so a change here can
            // leave the cursor past the end of the new, shorter row set.
            if (editMode.connIndex >= connRowCount()) editMode.connIndex = connRowCount() - 1;
        } else if (row == MBConnRow::DETECT) {
            int v = (int)config.detectThresholdCm + delta * 5;
            if (v < 5) v = 5;
            if (v > 500) v = 500;
            config.detectThresholdCm = (uint16_t)v;
        } else if (row == MBConnRow::FLASH_DELAY) {
            int v = (int)config.flashDelayMs + delta * 50;
            if (v < 0) v = 0;
            if (v > 5000) v = 5000;
            config.flashDelayMs = (uint16_t)v;
        } else if (row == MBConnRow::MIN_BULB) {
            int v = (int)config.minBulbSec + delta;
            if (v < 1) v = 1;
            if (v > 900) v = 900;
            config.minBulbSec = (uint16_t)v;
            // Keep the safety cap above the floor, or closeBulb-if-due can
            // never be satisfied by the min and only ever fire on the max.
            if (config.maxBulbSec <= config.minBulbSec) config.maxBulbSec = config.minBulbSec + 1;
        } else if (row == MBConnRow::MAX_BULB) {
            int v = (int)config.maxBulbSec + delta;
            if (v <= (int)config.minBulbSec) v = config.minBulbSec + 1;
            if (v > 900) v = 900;
            config.maxBulbSec = (uint16_t)v;
        } else if (row == MBConnRow::REARM) {
            // Coarser steps once it is already seconds long, so the useful
            // overload-guard range (up to a minute) is reachable by hand.
            int step = (config.rearmMs >= 2000) ? 500 : 100;
            int v = (int)config.rearmMs + delta * step;
            if (v < 0) v = 0;
            if (v > 60000) v = 60000;
            config.rearmMs = (uint16_t)v;
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

    if (editMode.state == MultiBoxEditMode::CONFIRM_DISRUPT) {
        bool ok = (editMode.confirmChoice == 1);
        MultiBoxEditMode::PendingAction act = editMode.pending;
        editMode.pending = MultiBoxEditMode::PEND_NONE;

        if (!ok) {
            editMode.state = (act == MultiBoxEditMode::PEND_REMOVE_PEER)
                                 ? MultiBoxEditMode::PEER_LIST
                                 : MultiBoxEditMode::SELECTING;
            return;
        }

        if (act == MultiBoxEditMode::PEND_REMOVE_PEER) {
            MBPeer* p = nodeManager.peerAt(editMode.peerSel);
            if (p) { nodeManager.removePeer(p->nodeId); nodeManager.saveAll(); }
            if (nodeManager.peerCount() == 0) {
                editMode.state = MultiBoxEditMode::SELECTING;
            } else {
                if (editMode.peerSel >= nodeManager.peerCount())
                    editMode.peerSel = nodeManager.peerCount() - 1;
                editMode.state = MultiBoxEditMode::PEER_LIST;
            }
        } else {
            editMode.state = MultiBoxEditMode::EDITING;
        }
        return;
    }

    if (editMode.state == MultiBoxEditMode::PEER_LIST) {
        // Dropping a peer mid-run can strand a session -- confirm first.
        if (isBusy()) {
            editMode.pending = MultiBoxEditMode::PEND_REMOVE_PEER;
            editMode.confirmChoice = 0;
            editMode.state = MultiBoxEditMode::CONFIRM_DISRUPT;
            return;
        }
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
            // Identity. Changing either mid-run resets this box's job (and,
            // on MAIN, closes an open shutter), so it asks first rather than
            // silently tearing down a session the user is in the middle of.
            if (isBusy()) {
                editMode.pending = MultiBoxEditMode::PEND_EDIT_ROW;
                editMode.confirmChoice = 0;
                editMode.state = MultiBoxEditMode::CONFIRM_DISRUPT;
                return;
            }
            editMode.state = MultiBoxEditMode::EDITING;
            break;
        case MBConnRow::DETECT:
        case MBConnRow::FLASH_DELAY:
        case MBConnRow::MIN_BULB:
        case MBConnRow::MAX_BULB:
        case MBConnRow::REARM:
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
        case MultiBoxEditMode::CONFIRM_DISRUPT:
            // Long-press is "back out" everywhere else, so it cancels here
            // too. Without this the dialog falls through to the screen switch
            // below, which leaves the state machine on CONFIRM_DISRUPT while
            // the screen changes underneath it -- the dialog then renders
            // forever with no way out.
            editMode.pending = MultiBoxEditMode::PEND_NONE;
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
