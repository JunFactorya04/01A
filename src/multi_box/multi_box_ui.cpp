/**
 * @file multi_box_ui.cpp
 * @brief MULTI BOX UI — follows GEOPIX UI STANDARD (see display_mode_ui.cpp):
 * header y=2, rounded panel y=22, 20px item rows, bottom status panel
 * y=113 h=20, themed colors.
 */

#include "multi_box.h"
#include "multi_box_controller.h"
#include "node_manager.h"
#include "../factory_test/factory_test.h"
#include "../common/ui_theme.h"
#include "../common/battery_ui.h"

extern FactoryTest* _ft;

#define COLOR_BG        UI_BG
#define COLOR_TEXT      UI_FG
#define COLOR_BORDER    UI_BORDER
#define COLOR_HIGHLIGHT UI_AL
#define COLOR_GREEN     0x07E0
#define COLOR_RED       0xF800
#define COLOR_YELLOW    0xFFE0

#define ITEM_HEIGHT  20
#define ITEM_Y_START 27
#define VISIBLE_ROWS 4

static unsigned long s_blinkTimer = 0;
static bool s_blinkState = false;
static void tickBlink() {
    if (millis() - s_blinkTimer > 500) { s_blinkState = !s_blinkState; s_blinkTimer = millis(); }
}

static const char* roleName(MBRole r) {
    switch (r) {
        case MBRole::START:  return "START";
        case MBRole::FLASH:  return "FLASH";
        case MBRole::MAIN: return "MAIN";
        default:             return "NONE";
    }
}

static const char* sessionName(MultiBoxState::Session s) {
    switch (s) {
        case MultiBoxState::READY:    return "READY";
        case MultiBoxState::LOCKED:   return "LOCKED";
        case MultiBoxState::EXPOSING: return "EXPOSING";
        case MultiBoxState::REARM:    return "REARM";
        default:                      return "IDLE";
    }
}

static uint16_t sessionColor(MultiBoxState::Session s) {
    switch (s) {
        case MultiBoxState::READY:    return COLOR_GREEN;
        case MultiBoxState::LOCKED:
        case MultiBoxState::EXPOSING: return COLOR_YELLOW;
        case MultiBoxState::REARM:    return COLOR_BORDER;
        default:                      return COLOR_RED;
    }
}

static void header(const char* title, const char* hint) {
    LGFX_Sprite* c = _ft->_canvas;
    c->setFont(&fonts::efontCN_16);
    c->setTextDatum(top_center);
    c->setTextColor(COLOR_HIGHLIGHT);
    c->drawString(title, 120, 2);

    c->setTextDatum(top_left);
    c->setTextColor(COLOR_TEXT);
    c->drawString("<", 5, 2);

    // Battery, shared across every mode header (see battery_ui.h)
    drawBatteryBadge(c);

    if (hint) {
        c->setFont(&fonts::efontCN_10);
        c->setTextDatum(top_right);
        c->setTextColor(COLOR_BORDER);
        c->drawString(hint, 235, 5);
    }
    c->setTextDatum(top_left);
}

static void infoRow(int y, const char* label, const char* value, uint16_t valColor) {
    LGFX_Sprite* c = _ft->_canvas;
    c->setFont(&fonts::efontCN_12);
    c->setTextDatum(top_left);
    c->setTextColor(COLOR_TEXT);
    c->drawString(label, 16, y);
    c->setTextDatum(top_right);
    c->setTextColor(valColor);
    c->drawString(value, 222, y);
    c->setTextDatum(top_left);
}

// ============ MAIN SCREEN — one per role ============
// Every box runs the same firmware, so the role it was assigned over ESP-NOW
// is what decides which of these three screens it shows. They deliberately do
// NOT share a layout beyond the GEOPIX UI STANDARD frame: each role has a
// different job and a different set of numbers worth watching in the field.

// What this node is waiting for, in its own terms rather than the raw session
// enum -- "LOCKED" means something different to a START node than to MAIN.
static const char* roleStatusText(char* buf, size_t n) {
    MultiBoxState& st = multiBox.state;

    if (multiBox.config.role == MBRole::START) {
        switch (st.session) {
            case MultiBoxState::READY:
                return st.baselineCaptured ? "WATCHING" : "CALIBRATING";
            case MultiBoxState::LOCKED: return "SENT - WAIT MAIN";
            default: return "IDLE";
        }
    }

    if (multiBox.config.role == MBRole::FLASH) {
        if (st.flashPending) {
            long left = (long)(st.flashDueAtMs - millis());
            if (left < 0) left = 0;
            snprintf(buf, n, "FIRING IN %ldms", left);
            return buf;
        }
        if (!st.sessionActive) return "STANDBY";
        if (st.session == MultiBoxState::LOCKED) return "FIRED";
        return st.baselineCaptured ? "WATCHING" : "CALIBRATING";
    }

    if (multiBox.config.role == MBRole::MAIN) {
        switch (st.session) {
            case MultiBoxState::EXPOSING: {
                unsigned long sec = (millis() - st.sessionStartMs) / 1000UL;
                snprintf(buf, n, "BULB %lus", sec);
                return buf;
            }
            case MultiBoxState::REARM: return "REARM";
            case MultiBoxState::READY: return "ARMED";
            default: return "IDLE";
        }
    }
    return "NO ROLE";
}

static uint16_t roleStatusColor() {
    MultiBoxState& st = multiBox.state;
    if (multiBox.config.role == MBRole::NONE) return COLOR_RED;
    if (st.session == MultiBoxState::EXPOSING) return COLOR_YELLOW;
    if (st.session == MultiBoxState::LOCKED)   return COLOR_BORDER;
    if (st.session == MultiBoxState::READY)    return COLOR_GREEN;
    return COLOR_BORDER;
}

static void renderStatusPanel() {
    LGFX_Sprite* c = _ft->_canvas;
    char buf[24];

    // A second MAIN on the network breaks everything (both would open
    // sessions), and it is invisible from any single box's normal status --
    // so it takes over the panel rather than being tucked away somewhere.
    NodeManager::Conflict conf = nodeManager.conflict();
    if (conf != NodeManager::Conflict::NONE) {
        const char* msg = "CONFIG CONFLICT";
        switch (conf) {
            case NodeManager::Conflict::DUP_ID:
                msg = "ID IN USE - CHANGE ID"; break;
            case NodeManager::Conflict::DUP_MAIN:
                msg = "2x MAIN - CHANGE ROLE"; break;
            case NodeManager::Conflict::DUP_START:
                msg = "2x START - CHANGE ROLE"; break;
            default: break;
        }
        tickBlink();
        c->drawRoundRect(8, 113, 224, 20, 4, COLOR_RED);
        c->setFont(&fonts::efontCN_12);
        c->setTextDatum(middle_center);
        c->setTextColor(s_blinkState ? COLOR_RED : COLOR_BORDER);
        c->drawString(msg, 120, 123);
        c->setTextDatum(top_left);
        return;
    }

    const char* txt = roleStatusText(buf, sizeof(buf));

    c->drawRoundRect(8, 113, 224, 20, 4, COLOR_BORDER);
    c->setFont(&fonts::efontCN_12);
    c->setTextDatum(middle_center);
    c->setTextColor(roleStatusColor());
    c->drawString(txt, 120, 123);
    c->setTextDatum(top_left);
}

static void renderLinkRow(int y) {
    MBPeer* main = nodeManager.findCenter();
    bool linked = main && main->connected;
    infoRow(y, "MAIN LINK", linked ? "OK" : "---", linked ? COLOR_GREEN : COLOR_RED);
}

// --- START: the box on the start line ---
static void renderStartScreen() {
    LGFX_Sprite* c = _ft->_canvas;
    header("START NODE", "PRESS=SETUP");
    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);

    char buf[16];
    infoRow(29, "JOB", "OPEN SHUTTER", COLOR_TEXT);
    snprintf(buf, sizeof(buf), "%ucm", multiBox.config.detectThresholdCm);
    infoRow(49, "DETECT", buf, COLOR_TEXT);
    infoRow(69, "SENSOR", multiBox.state.baselineCaptured ? "READY" : "CALIBRATING",
            multiBox.state.baselineCaptured ? COLOR_GREEN : COLOR_YELLOW);
    renderLinkRow(89);

    renderStatusPanel();
}

// --- FLASH: a box somewhere along the run ---
static void renderFlashScreen() {
    LGFX_Sprite* c = _ft->_canvas;
    header("FLASH NODE", "PRESS=SETUP");
    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);

    char buf[16];
    infoRow(29, "JOB", "FIRE FLASH", COLOR_TEXT);
    snprintf(buf, sizeof(buf), "%ucm", multiBox.config.detectThresholdCm);
    infoRow(49, "DETECT", buf, COLOR_TEXT);
    snprintf(buf, sizeof(buf), "%ums", multiBox.config.flashDelayMs);
    infoRow(69, "DELAY", buf, COLOR_TEXT);
    infoRow(89, "SENSOR", multiBox.state.baselineCaptured ? "READY" : "CALIBRATING",
            multiBox.state.baselineCaptured ? COLOR_GREEN : COLOR_YELLOW);

    renderStatusPanel();
}

// --- MAIN: the box holding the camera ---
static void renderMainScreen() {
    LGFX_Sprite* c = _ft->_canvas;
    header("MAIN NODE", "PRESS=SETUP");
    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);

    char buf[20];
    snprintf(buf, sizeof(buf), "%u-%us", multiBox.config.minBulbSec, multiBox.config.maxBulbSec);
    infoRow(29, "BULB", buf, COLOR_TEXT);

    // MAIN closes the shutter with its OWN sensor, so its calibration state
    // matters as much as a START node's -- a MAIN that never baselined can
    // only ever end an exposure on the maxBulbSec safety cap.
    infoRow(49, "FINISH SENSOR", multiBox.state.baselineCaptured ? "READY" : "CALIBRATING",
            multiBox.state.baselineCaptured ? COLOR_GREEN : COLOR_YELLOW);

    // The rest window is the shot-rate cap, so it belongs on the main screen
    // next to the exposure limits rather than buried in setup.
    if (multiBox.config.rearmMs >= 1000)
        snprintf(buf, sizeof(buf), "%.1fs", multiBox.config.rearmMs / 1000.0f);
    else
        snprintf(buf, sizeof(buf), "%ums", multiBox.config.rearmMs);
    infoRow(69, "REST", buf, COLOR_TEXT);

    snprintf(buf, sizeof(buf), "%lu  N%u", (unsigned long)multiBox.state.shotCount,
             nodeManager.connectedCount());
    infoRow(89, "SHOTS", buf, COLOR_TEXT);

    renderStatusPanel();
}

// --- no role assigned yet ---
static void renderNoRoleScreen() {
    LGFX_Sprite* c = _ft->_canvas;
    header("MULTI BOX", "PRESS=SETUP");
    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);

    char buf[16];
    infoRow(29, "ROLE", "NOT SET", COLOR_RED);
    snprintf(buf, sizeof(buf), "%u", multiBox.config.nodeId);
    infoRow(49, "NODE ID", buf, COLOR_TEXT);
    infoRow(69, "SET A ROLE IN", "SETUP >", COLOR_BORDER);
    snprintf(buf, sizeof(buf), "%u", nodeManager.connectedCount());
    infoRow(89, "NODES SEEN", buf, COLOR_TEXT);

    renderStatusPanel();
}

static void renderMain() {
    switch (multiBox.config.role) {
        case MBRole::START: renderStartScreen(); break;
        case MBRole::FLASH: renderFlashScreen(); break;
        case MBRole::MAIN:  renderMainScreen();  break;
        default:            renderNoRoleScreen(); break;
    }
}

// ============ CONNECTION (flat scrollable rows) ============
static void connRowLabelValue(MBConnRow row, char* label, char* value, size_t n) {
    switch (row) {
        case MBConnRow::NODE_ID:
            snprintf(label, n, "Node ID");
            snprintf(value, n, "%u", multiBox.config.nodeId);
            break;
        case MBConnRow::ROLE:
            snprintf(label, n, "Role");
            snprintf(value, n, "%s", roleName(multiBox.config.role));
            break;
        case MBConnRow::DETECT:
            snprintf(label, n, "Detect");
            snprintf(value, n, "%ucm", multiBox.config.detectThresholdCm);
            break;
        case MBConnRow::FLASH_DELAY:
            snprintf(label, n, "Flash Delay");
            snprintf(value, n, "%ums", multiBox.config.flashDelayMs);
            break;
        case MBConnRow::MIN_BULB:
            snprintf(label, n, "Min Bulb");
            snprintf(value, n, "%us", multiBox.config.minBulbSec);
            break;
        case MBConnRow::MAX_BULB:
            snprintf(label, n, "Max Bulb");
            snprintf(value, n, "%us", multiBox.config.maxBulbSec);
            break;
        case MBConnRow::REARM:
            snprintf(label, n, "Rest");
            if (multiBox.config.rearmMs >= 1000)
                snprintf(value, n, "%.1fs", multiBox.config.rearmMs / 1000.0f);
            else
                snprintf(value, n, "%ums", multiBox.config.rearmMs);
            break;
        case MBConnRow::PAIR:
            snprintf(label, n, "Pair / Add Node");
            snprintf(value, n, ">");
            break;
        case MBConnRow::NODES:
            snprintf(label, n, "Nodes");
            snprintf(value, n, "(%u) >", nodeManager.peerCount());
            break;
        case MBConnRow::BACK:
            snprintf(label, n, "Back");
            snprintf(value, n, "<");
            break;
    }
}

static void renderConnectionRow(uint8_t visualRow, MBConnRow kind, bool isSel, bool isEdit) {
    LGFX_Sprite* c = _ft->_canvas;
    int y = ITEM_Y_START + visualRow * ITEM_HEIGHT;
    char label[24], value[16];
    connRowLabelValue(kind, label, value, sizeof(label));

    if (isSel) c->fillRoundRect(10, y - 1, 220, 18, 3, isEdit ? COLOR_HIGHLIGHT : COLOR_BORDER);

    c->setFont(&fonts::efontCN_16);
    c->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);
    c->setTextDatum(top_left);
    c->drawString(label, 16, y + 1);

    c->setTextDatum(top_right);
    if (isEdit && s_blinkState) c->setTextColor(COLOR_BG);
    else if (isEdit) c->setTextColor(COLOR_YELLOW);
    else c->setTextColor(isSel ? COLOR_BG : COLOR_GREEN);
    c->drawString(value, 216, y + 1);
    c->setTextDatum(top_left);
}

static void renderConnectionList() {
    tickBlink();
    LGFX_Sprite* c = _ft->_canvas;
    header("CONNECTION", "HOLD=BACK");
    c->drawRoundRect(8, 22, 224, 88, 5, COLOR_BORDER);

    uint8_t count = multiBox.connRowCount();
    uint8_t sel = multiBox.editMode.connIndex;
    int firstVisible = 0;
    if (sel >= VISIBLE_ROWS) firstVisible = sel - VISIBLE_ROWS + 1;
    if (firstVisible > count - VISIBLE_ROWS) firstVisible = count > VISIBLE_ROWS ? count - VISIBLE_ROWS : 0;
    if (firstVisible < 0) firstVisible = 0;

    bool editing = multiBox.editMode.state == MultiBoxEditMode::EDITING;

    for (uint8_t i = firstVisible; i < count && i < firstVisible + VISIBLE_ROWS; i++) {
        MBConnRow kind = multiBox.connRowKind(i);
        renderConnectionRow(i - firstVisible, kind, i == sel, editing && i == sel);
    }

    if (firstVisible > 0) {
        c->setFont(&fonts::efontCN_10);
        c->setTextDatum(top_center);
        c->setTextColor(COLOR_BORDER);
        c->drawString("^", 220, 23);
    }
    if (firstVisible + VISIBLE_ROWS < count) {
        c->setFont(&fonts::efontCN_10);
        c->setTextDatum(top_center);
        c->setTextColor(COLOR_BORDER);
        c->drawString("v", 220, 100);
    }
    c->setTextDatum(top_left);
}

// ============ SCANNING ============
static void renderScanning() {
    LGFX_Sprite* c = _ft->_canvas;
    header("ADD NODE", "HOLD=BACK");
    c->drawRoundRect(8, 22, 224, 88, 5, COLOR_BORDER);

    if (nodeManager.scanInProgress()) {
        c->setFont(&fonts::efontCN_16);
        c->setTextDatum(middle_center);
        c->setTextColor(COLOR_TEXT);
        c->drawString("Scanning...", 120, 65);
        c->setTextDatum(top_left);
        return;
    }

    uint8_t n = nodeManager.scanResultCount();
    if (n == 0) {
        c->setFont(&fonts::efontCN_12);
        c->setTextDatum(middle_center);
        c->setTextColor(COLOR_BORDER);
        c->drawString("No nodes found", 120, 65);
        c->setTextDatum(top_left);
        return;
    }

    for (uint8_t i = 0; i < n && i < VISIBLE_ROWS; i++) {
        NodeManager::Candidate* cand = nodeManager.scanResultAt(i);
        if (!cand) continue;
        bool isSel = (i == multiBox.editMode.scanSel);
        int y = ITEM_Y_START + i * ITEM_HEIGHT;
        if (isSel) c->fillRoundRect(10, y - 1, 220, 18, 3, COLOR_BORDER);
        char line[32];
        snprintf(line, sizeof(line), "ID:%u  %s", cand->nodeId, roleName(cand->role));
        c->setFont(&fonts::efontCN_16);
        c->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);
        c->drawString(line, 16, y + 1);
    }
}

// ============ PEER LIST ============
static void renderPeerList() {
    LGFX_Sprite* c = _ft->_canvas;
    header("NODES", "PRESS=REMOVE");
    c->drawRoundRect(8, 22, 224, 88, 5, COLOR_BORDER);

    uint8_t n = nodeManager.peerCount();
    for (uint8_t i = 0; i < n && i < VISIBLE_ROWS; i++) {
        MBPeer* p = nodeManager.peerAt(i);
        if (!p) continue;
        bool isSel = (i == multiBox.editMode.peerSel);
        int y = ITEM_Y_START + i * ITEM_HEIGHT;
        if (isSel) c->fillRoundRect(10, y - 1, 220, 18, 3, COLOR_BORDER);
        char line[24];
        snprintf(line, sizeof(line), "ID:%u  %s", p->nodeId, roleName(p->role));
        c->setFont(&fonts::efontCN_16);
        c->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);
        c->setTextDatum(top_left);
        c->drawString(line, 16, y + 1);

        c->setTextDatum(top_right);
        c->setTextColor(isSel ? COLOR_BG : (p->connected ? COLOR_GREEN : COLOR_BORDER));
        c->drawString(p->connected ? "LINKED" : "---", 216, y + 1);
        c->setTextDatum(top_left);
    }
}

// ============ DISRUPTIVE-CHANGE CONFIRM ============
static void renderConfirmDisrupt() {
    LGFX_Sprite* c = _ft->_canvas;
    header("SHOT IN PROGRESS", nullptr);
    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);

    bool removing = multiBox.editMode.pending == MultiBoxEditMode::PEND_REMOVE_PEER;

    c->setFont(&fonts::efontCN_12);
    c->setTextDatum(top_center);
    c->setTextColor(COLOR_TEXT);
    c->drawString(removing ? "Removing this node will" : "Changing this will reset", 120, 38);
    c->drawString(removing ? "interrupt the run." : "this box and end the run.", 120, 54);
    c->setTextDatum(top_left);

    bool cancelSel = multiBox.editMode.confirmChoice == 0;
    int y = 78;

    c->fillRoundRect(20, y, 90, 20, 4, cancelSel ? COLOR_HIGHLIGHT : COLOR_BG);
    c->drawRoundRect(20, y, 90, 20, 4, COLOR_BORDER);
    c->setTextDatum(middle_center);
    c->setTextColor(cancelSel ? COLOR_BG : COLOR_TEXT);
    c->drawString("CANCEL", 65, y + 10);

    c->fillRoundRect(130, y, 90, 20, 4, cancelSel ? COLOR_BG : COLOR_HIGHLIGHT);
    c->drawRoundRect(130, y, 90, 20, 4, COLOR_BORDER);
    c->setTextColor(cancelSel ? COLOR_TEXT : COLOR_BG);
    c->drawString("CONTINUE", 175, y + 10);
    c->setTextDatum(top_left);
}

// ============ EXIT CONFIRM ============
static void renderConfirmExit() {
    LGFX_Sprite* c = _ft->_canvas;
    header("EXIT MULTI BOX?", nullptr);
    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);

    c->setFont(&fonts::efontCN_12);
    c->setTextDatum(top_center);
    c->setTextColor(COLOR_TEXT);
    c->drawString("ESP-NOW will disconnect.", 120, 40);
    c->setTextDatum(top_left);

    bool cancelSel = multiBox.editMode.confirmChoice == 0;
    int y = 78;

    c->fillRoundRect(20, y, 90, 20, 4, cancelSel ? COLOR_HIGHLIGHT : COLOR_BG);
    c->drawRoundRect(20, y, 90, 20, 4, COLOR_BORDER);
    c->setTextDatum(middle_center);
    c->setTextColor(cancelSel ? COLOR_BG : COLOR_TEXT);
    c->drawString("CANCEL", 65, y + 10);

    c->fillRoundRect(130, y, 90, 20, 4, cancelSel ? COLOR_BG : COLOR_HIGHLIGHT);
    c->drawRoundRect(130, y, 90, 20, 4, COLOR_BORDER);
    c->setTextColor(cancelSel ? COLOR_TEXT : COLOR_BG);
    c->drawString("OK", 175, y + 10);
    c->setTextDatum(top_left);
}

// ============ ENTRY ============
void renderMultiBoxUI() {
    if (!_ft || !_ft->_canvas) return;
    _ft->_canvas->setTextWrap(false);
    _ft->_canvas->fillScreen(COLOR_BG);

    if (multiBox.editMode.state == MultiBoxEditMode::CONFIRM_DISRUPT) {
        renderConfirmDisrupt();
        return;
    }
    if (multiBox.editMode.state == MultiBoxEditMode::CONFIRM_EXIT) {
        renderConfirmExit();
    } else if (multiBox.editMode.screen == MultiBoxEditMode::MAIN) {
        renderMain();
    } else if (multiBox.editMode.state == MultiBoxEditMode::SCANNING) {
        renderScanning();
    } else if (multiBox.editMode.state == MultiBoxEditMode::PEER_LIST) {
        renderPeerList();
    } else {
        renderConnectionList();
    }

    _ft->_canvas_update();
}

void initMultiBoxUI() {
    multiBox.editMode = MultiBoxEditMode();
}
