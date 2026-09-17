/**
 * @file multi_box.h
 * @brief MULTI BOX mode — config/state/UI-nav structs + per-loop update
 * @date 2026-09-06
 *
 * Same Config/State/EditMode triple every other mode in this codebase uses
 * (AutoShoot, Timelapse, TriggerMode, Setting). Identity (node id + role)
 * is the one thing NOT owned here — NodeManager is the source of truth for
 * that, since it also drives ESP-NOW peer registration; MultiBoxConfig's
 * copies are an edit buffer pushed into NodeManager on save (see
 * multi_box.cpp saveConfig()).
 */

#pragma once
#include <Arduino.h>
#include "multi_box_protocol.h"

// ============ CONFIG (persisted) ============
struct MultiBoxConfig {
    uint8_t      nodeId = 0;                          // edit buffer; authority is NodeManager
    MBRole       role = MBRole::NONE;                 // edit buffer; authority is NodeManager
    uint16_t     detectThresholdCm = 20;               // START/FLASH/MAIN: baseline delta to fire
    // MAIN overload guards. Both matter when people cross in a QUEUE rather
    // than one at a time -- that is the case that can otherwise fire shots
    // back-to-back faster than the camera can write them.
    //
    //   minBulbSec  floor on a single exposure. Someone still standing in
    //               front of MAIN's sensor when the NEXT exposure opens would
    //               otherwise end it instantly; this guarantees every frame
    //               gets at least this long.
    //   rearmMs     floor on the rest between shots. MAIN refuses every START
    //               command while it is counting this down (centerOnStart()
    //               only accepts one in READY), so raising it directly caps
    //               the shot rate no matter how many people cross.
    uint16_t     minBulbSec = 2;                       // MAIN: 1-900s
    uint16_t     maxBulbSec = 30;                      // MAIN: safety cap, never open longer
    uint16_t     rearmMs = 1000;                       // MAIN: 0-60000ms

    // FLASH: how long after the athlete is detected the flash actually fires.
    // The sensor sees them ARRIVING at the box; the shot usually wants them a
    // little further along, so this shifts the illumination to where the
    // subject should be rather than where they were first seen.
    uint16_t     flashDelayMs = 0;                     // FLASH: 0-5000ms
};

// ============ CONNECTION STATE — deliberately separate from session state ====
enum class MBConnState : uint8_t { DISCONNECTED = 0, LINKED = 1 };

// ============ RUNTIME / SHOOTING SESSION STATE ============
struct MultiBoxState {
    enum Session { IDLE = 0, READY, LOCKED, EXPOSING, REARM } session = IDLE;

    MBConnState connState = MBConnState::DISCONNECTED;
    uint16_t    currentSessionId = 0;

    bool sessionActive = false;     // non-MAIN: true between session-start and SHOT_DONE/DISARM
    float baselineDistance = 0.0f;
    bool  baselineCaptured = false;
    uint8_t baselineSamples = 0;    // non-blocking accumulation while (re)capturing

    unsigned long sessionStartMs = 0; // MAIN: bulb-open time; also reused as rearm-start
    unsigned long lockStartMs = 0;    // START/FLASH: when this node fired
    bool endRequested = false;        // MAIN: END_DETECT received, waiting for min time
    bool bulbFiredG1 = false;
    bool bulbFiredG2 = false;
    bool bulbFiredBLE = false;   // BLE shutter press landed -> must be released

    // FLASH: detection happened, the flash has not fired yet. Deliberately a
    // timestamp + flag rather than a delay() -- blocking here would stall
    // ESP-NOW receive handling and rendering for the whole delay.
    bool          flashPending = false;
    unsigned long flashDueAtMs = 0;

    unsigned long lastFlashAtMs = 0;  // MAIN: for status display only
    uint32_t shotCount = 0;
};

// ============ EDIT MODE (UI navigation) ============
struct MultiBoxEditMode {
    enum EditState {
        SELECTING = 0,   // browsing the current screen's rows
        EDITING,         // adjusting the highlighted row's value
        SCANNING,        // discovery scan in progress / showing results
        PEER_LIST,       // browsing paired peers (for removal)
        CONFIRM_EXIT,    // "EXIT MULTI BOX?" dialog
        CONFIRM_DISRUPT, // editing something that would disturb a run in progress
    } state = SELECTING;

    enum Screen { MAIN = 0, CONNECTION = 1 } screen = MAIN;

    uint8_t selectedIndex = 0;   // MAIN: always 0 (single "> CONNECTION" row)
    uint8_t connIndex = 0;       // CONNECTION rows: 0=NodeId 1=Role 2=Pair/AddNode
                                  //   3=Nodes(N) 4=Back
    uint8_t scanSel = 0;         // index into NodeManager scan results
    uint8_t peerSel = 0;         // index into NodeManager peer table
    uint8_t confirmChoice = 0;   // 0=CANCEL 1=OK, on either confirm dialog

    // What CONFIRM_DISRUPT will do if the user picks OK. Held rather than
    // acted on immediately, which is the whole point of the dialog.
    enum PendingAction : uint8_t { PEND_NONE = 0, PEND_EDIT_ROW, PEND_REMOVE_PEER } pending = PEND_NONE;
};

// CONNECTION screen rows. The set shown DEPENDS ON THE ASSIGNED ROLE -- every
// box runs identical firmware, so the role it was given is what decides which
// settings it shows. START/FLASH get the detection threshold; MAIN also gets
// the exposure limits, because MAIN is the box holding the shutter.
//
// Kept behind connRowKind()/connRowCount() (rather than raw indices) so input
// handling and rendering can never disagree about what sits at a given visual
// row -- the row set changes underneath them whenever the role changes.
enum class MBConnRow : uint8_t {
    NODE_ID, ROLE,
    DETECT,                      // START / FLASH / MAIN
    FLASH_DELAY,                 // FLASH only
    MIN_BULB, MAX_BULB, REARM,   // MAIN only
    PAIR, NODES, BACK,
};

class MultiBox {
public:
    MultiBoxConfig   config;
    MultiBoxState    state;
    MultiBoxEditMode editMode;

    void init();       // entering the mode: bring up ESP-NOW, load config
    void teardown();   // leaving the mode (normal, non-exit-dialog path): just persist
    void loadConfig();
    void saveConfig();

    void update();     // per-loop tick

    // UI interaction
    void handleEncoderRotate(int delta);
    void handleButtonPress();
    void handleButtonLongPress();

    bool inConnectionScreen() const { return editMode.screen == MultiBoxEditMode::CONNECTION; }

    // 2 identity rows + role-specific rows + 3 trailing rows.
    // Anything that would disturb work in progress needs confirming first:
    // a session is open, a node is waiting on MAIN, or a flash is armed.
    bool isBusy() const {
        return state.session == MultiBoxState::EXPOSING ||
               state.session == MultiBoxState::LOCKED ||
               state.sessionActive || state.flashPending;
    }

    uint8_t connRowCount() const {
        if (config.role == MBRole::NONE)  return 5;           // no role: nothing to tune yet
        if (config.role == MBRole::MAIN)  return 9;           // + detect + min/max bulb + rearm
        if (config.role == MBRole::FLASH) return 7;           // + detect + flash delay
        return 6;                                             // START: + detect
    }
    MBConnRow connRowKind(uint8_t visualIdx) const;

private:
    void applyRoleChange();
    void autoClaimMainIfScanFinished();
    void applyRoleSensorLogic();
    void captureBaselineStep();
    void handleConnectionPress();
    void handleConnectionRotate(int delta);
};

extern MultiBox multiBox;
