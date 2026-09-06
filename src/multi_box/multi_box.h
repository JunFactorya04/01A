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
    MBSignalMode signalMode = MBSignalMode::EMIT_START; // only used when role == START
    uint16_t     detectThresholdCm = 20;               // START/FLASH: baseline delta to fire
    uint16_t     minBulbSec = 2;                       // CENTER: minimum exposure time
    uint16_t     maxBulbSec = 30;                      // CENTER: safety cap, never open longer
    uint16_t     rearmMs = 1000;                       // CENTER: cooldown before READY again
};

// ============ CONNECTION STATE — deliberately separate from session state ====
enum class MBConnState : uint8_t { DISCONNECTED = 0, LINKED = 1 };

// ============ RUNTIME / SHOOTING SESSION STATE ============
struct MultiBoxState {
    enum Session { IDLE = 0, READY, LOCKED, EXPOSING, REARM } session = IDLE;

    MBConnState connState = MBConnState::DISCONNECTED;
    uint16_t    currentSessionId = 0;

    bool sessionActive = false;     // non-CENTER: true between session-start and SHOT_DONE/DISARM
    float baselineDistance = 0.0f;
    bool  baselineCaptured = false;
    uint8_t baselineSamples = 0;    // non-blocking accumulation while (re)capturing

    unsigned long sessionStartMs = 0; // CENTER: bulb-open time; also reused as rearm-start
    unsigned long lockStartMs = 0;    // START/FLASH: when this node fired
    bool endRequested = false;        // CENTER: END_DETECT received, waiting for min time
    bool bulbFiredG1 = false;
    bool bulbFiredG2 = false;

    unsigned long lastFlashAtMs = 0;  // CENTER: for status display only
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
    } state = SELECTING;

    enum Screen { MAIN = 0, CONNECTION = 1 } screen = MAIN;

    uint8_t selectedIndex = 0;   // MAIN: always 0 (single "> CONNECTION" row)
    uint8_t connIndex = 0;       // CONNECTION rows: 0=NodeId 1=Role 2=Signal(START only)
                                  //   3=Pair/AddNode 4=Nodes(N) 5=Back
    uint8_t scanSel = 0;         // index into NodeManager scan results
    uint8_t peerSel = 0;         // index into NodeManager peer table
    uint8_t confirmChoice = 0;   // 0=CANCEL 1=OK, on CONFIRM_EXIT
};

// CONNECTION screen rows are positional, not fixed-index: the SIGNAL row
// only exists (at visual position 2) when role == START, so everything
// after it shifts up by one when it's hidden. Both input handling
// (multi_box.cpp) and rendering (multi_box_ui.cpp) go through this so the
// two can never disagree about what's at a given visual row.
enum class MBConnRow : uint8_t { NODE_ID, ROLE, SIGNAL, PAIR, NODES, BACK };

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

    bool signalRowVisible() const { return config.role == MBRole::START; }
    uint8_t connRowCount() const { return signalRowVisible() ? 6 : 5; }
    MBConnRow connRowKind(uint8_t visualIdx) const;

private:
    void applyRoleSensorLogic();
    void handleConnectionPress();
    void handleConnectionRotate(int delta);
};

extern MultiBox multiBox;
