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
#include "../auto_shoot/tf_luna.h"

extern FactoryTest* _ft;

#define COLOR_BG        UI_BG
#define COLOR_TEXT      UI_FG
#define COLOR_BORDER    UI_BORDER
#define COLOR_HIGHLIGHT UI_AL
#define COLOR_GREEN     0x07E0
#define COLOR_RED       0xF800
#define COLOR_ORANGE 0xFD20
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
        // Shot count rides along with the state: it is status, and after the
        // settings moved to ADVANCE this line is the only place left showing
        // how many frames the session has actually produced.
        unsigned long shots = (unsigned long)st.shotCount;
        switch (st.session) {
            case MultiBoxState::EXPOSING: {
                unsigned long sec = (millis() - st.sessionStartMs) / 1000UL;
                snprintf(buf, n, "BULB %lus   N%lu", sec, shots);
                return buf;
            }
            case MultiBoxState::REARM:
                snprintf(buf, n, "REARM   N%lu", shots); return buf;
            case MultiBoxState::READY:
                snprintf(buf, n, "ARMED   N%lu", shots); return buf;
            default:
                snprintf(buf, n, "IDLE   N%lu", shots); return buf;
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

// ---- shared sensor readout, borrowed from Auto Shoot and adapted ----
// Auto Shoot's zone bar marks an ABSOLUTE distance band (Range Min..Max).
// Multi Box triggers on DEVIATION FROM A BASELINE instead, so the same bar is
// redrawn around that: the baseline as a tick, the trigger threshold as a band
// either side of it, and the live reading as a marker that turns green once it
// is far enough from baseline to fire. Both keep Auto Shoot's fixed
// 0..TFLUNA_MAX_DISTANCE_M scale, so a zone sits where it physically is rather
// than being rescaled to whatever the numbers happen to be.
//
// Without this there was no live distance anywhere in MULTI BOX -- the box
// could not be aimed in the field at all.
static void renderSensorBar(int barX, int barY, int barW, int barH) {
    LGFX_Sprite* c = _ft->_canvas;
    const float span = TFLUNA_MAX_DISTANCE_M;

    auto distToX = [&](float d) -> int {
        if (d < 0.0f) d = 0.0f;
        if (d > span) d = span;
        return barX + (int)((d / span) * (float)(barW - 2)) + 1;
    };

    c->drawRect(barX, barY, barW, barH, COLOR_BORDER);

    if (multiBox.state.baselineCaptured) {
        float base = multiBox.state.baselineDistance;
        float thr  = multiBox.config.detectThresholdCm / 100.0f;

        // Trigger band: anything outside it counts as a crossing.
        int x0 = distToX(base - thr);
        int x1 = distToX(base + thr);
        if (x1 > x0) c->fillRect(x0, barY + 1, x1 - x0, barH - 2, COLOR_BORDER);

        int bx = distToX(base);
        c->drawFastVLine(bx, barY - 2, barH + 4, COLOR_HIGHLIGHT);
    }

    float live = tfLuna.getDistance();
    bool valid  = tfLuna.hasObject();
    bool firing = valid && multiBox.state.baselineCaptured &&
                  fabsf(live - multiBox.state.baselineDistance) * 100.0f >=
                      multiBox.config.detectThresholdCm;

    int mx = distToX(live);
    c->drawFastVLine(mx, barY - 1, barH + 2, !valid ? COLOR_RED
                                            : firing ? COLOR_GREEN : COLOR_TEXT);
}

// DIST / BASE rows plus the bar. Every role senses now, so all three screens
// show the same block and differ only in the row above it.
// Shared by all three role screens -- they differ in what sits above the
// block, not in how the sensor itself is presented.
static void renderSensorBlockAt(int yDist, int yBase, int yBar) {
    char buf[20];

    float live = tfLuna.getDistance();
    bool valid = tfLuna.hasObject();
    if (valid) snprintf(buf, sizeof(buf), "%.2fm", live);
    else       snprintf(buf, sizeof(buf), "NO SIGNAL");
    infoRow(yDist, "DIST", buf, valid ? COLOR_TEXT : COLOR_RED);

    if (multiBox.state.baselineCaptured) {
        snprintf(buf, sizeof(buf), "%.2fm  %ucm",
                 multiBox.state.baselineDistance, multiBox.config.detectThresholdCm);
        infoRow(yBase, "BASE", buf, COLOR_TEXT);
    } else {
        infoRow(yBase, "BASE", "CALIBRATING", COLOR_YELLOW);
    }

    renderSensorBar(16, yBar, 206, 8);
}

// Other boxes, at a glance. This is what the main screen was missing: from a
// START node in the field there was no way to see whether MAIN was even alive,
// and from MAIN no way to see which nodes had dropped.
//
// Compact by necessity -- one row, one initial per box (M/S/F) plus its link
// state, coloured. The full list with node ids lives on ADVANCE > Nodes.
// Cycle bar, ported from FOR DEVELOP where it was worked out against real
// hardware. Four phases, each its own colour, because the one that mattered
// most was invisible before: while MAIN is inside minBulbSec the sensor is
// ignored ENTIRELY, so a deliberately-ignored crossing and a missed one looked
// identical on screen. Naming the phase is what tells them apart.
static void renderCycleBar(int y) {
    LGFX_Sprite* c = _ft->_canvas;

    const char* phase = "";
    uint16_t    col   = COLOR_BORDER;
    float       frac  = 0.0f;
    char        cd[16] = "";

    switch (multiBox.state.session) {
        case MultiBoxState::EXPOSING: {
            unsigned long elapsed = millis() - multiBox.state.sessionStartMs;
            if (multiBox.state.endRequested) {
                long left = (long)(multiBox.state.endDueAtMs - millis());
                if (left < 0) left = 0;
                phase = "DETECTED";
                col   = COLOR_YELLOW;
                snprintf(cd, sizeof(cd), "%.1fs", left / 1000.0f);
                uint32_t total = multiBox.config.endDelayMs;
                frac = total ? 1.0f - ((float)left / (float)total) : 1.0f;
            } else {
                unsigned long minMs = (unsigned long)multiBox.config.minBulbSec * 1000UL;
                if (elapsed < minMs) {
                    phase = "MIN - SENSOR OFF";
                    col   = COLOR_BORDER;
                    snprintf(cd, sizeof(cd), "%.1fs", (minMs - elapsed) / 1000.0f);
                    frac  = (float)elapsed / (float)minMs;
                } else {
                    unsigned long total = (unsigned long)multiBox.config.maxBulbSec * 1000UL;
                    phase = "BULB - WATCHING";
                    col   = COLOR_ORANGE;
                    snprintf(cd, sizeof(cd), "%lus", elapsed / 1000UL);
                    frac  = total ? (float)elapsed / (float)total : 0.0f;
                }
            }
            break;
        }
        case MultiBoxState::REARM: {
            unsigned long elapsed = millis() - multiBox.state.sessionStartMs;
            unsigned long total   = multiBox.config.rearmMs;
            long left = (long)total - (long)elapsed;
            if (left < 0) left = 0;
            phase = "REST";
            snprintf(cd, sizeof(cd), "%.1fs", left / 1000.0f);
            frac = total ? (float)elapsed / (float)total : 1.0f;
            break;
        }
        case MultiBoxState::READY:
            phase = "ARMED"; col = COLOR_GREEN; frac = 0.0f; break;
        case MultiBoxState::LOCKED:
            phase = "WAIT MAIN"; col = COLOR_BORDER; frac = 0.0f; break;
        default:
            phase = "IDLE"; col = COLOR_RED; frac = 0.0f; break;
    }

    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    c->setFont(&fonts::efontCN_10);
    c->setTextDatum(top_left);
    c->setTextColor(col);
    c->drawString(phase, 16, y);
    if (cd[0]) {
        c->setTextDatum(top_right);
        c->setTextColor(COLOR_TEXT);
        c->drawString(cd, 222, y);
    }
    c->setTextDatum(top_left);

    const int barX = 16, barY = y + 12, barW = 206, barH = 6;
    c->drawRect(barX, barY, barW, barH, COLOR_BORDER);
    int fillW = (int)(frac * (barW - 2));
    if (fillW > 0) c->fillRect(barX + 1, barY + 1, fillW, barH - 2, col);
}

static void renderPeerSummary(int y) {
    LGFX_Sprite* c = _ft->_canvas;

    c->setFont(&fonts::efontCN_12);
    c->setTextDatum(top_left);
    c->setTextColor(COLOR_TEXT);
    c->drawString("NODES", 16, y);

    uint8_t n = nodeManager.peerCount();
    if (n == 0) {
        c->setTextDatum(top_right);
        c->setTextColor(COLOR_RED);
        c->drawString("NONE PAIRED", 222, y);
        c->setTextDatum(top_left);
        return;
    }

    // Right-aligned, drawn right-to-left so entries stay put as peers come
    // and go instead of shifting the whole row.
    int x = 222;
    for (int i = (int)n - 1; i >= 0; i--) {
        MBPeer* p = nodeManager.peerAt((uint8_t)i);
        if (!p) continue;

        char tag[8];
        const char* initial = (p->role == MBRole::MAIN)  ? "M"
                            : (p->role == MBRole::START) ? "S"
                            : (p->role == MBRole::FLASH) ? "F" : "?";
        snprintf(tag, sizeof(tag), "%s%s", initial, p->connected ? "+" : "-");

        c->setTextDatum(top_right);
        c->setTextColor(p->connected ? COLOR_GREEN : COLOR_RED);
        c->drawString(tag, x, y);
        x -= 26;
        if (x < 90) break;   // ran out of room
    }
    c->setTextDatum(top_left);
}


// --- START: the box on the start line ---
static void renderStartScreen() {
    LGFX_Sprite* c = _ft->_canvas;
    header("START NODE", "PRESS=ADVANCE");
    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);

    renderPeerSummary(27);
    renderSensorBlockAt(44, 61, 79);
    renderCycleBar(93);
    renderStatusPanel();
}

// --- FLASH: a box somewhere along the run ---
static void renderFlashScreen() {
    LGFX_Sprite* c = _ft->_canvas;
    header("FLASH NODE", "PRESS=ADVANCE");
    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);

    renderPeerSummary(27);
    renderSensorBlockAt(44, 61, 79);
    renderCycleBar(93);
    renderStatusPanel();
}

// --- MAIN: the box holding the camera ---
static void renderMainScreen() {
    LGFX_Sprite* c = _ft->_canvas;
    header("MAIN NODE", "PRESS=ADVANCE");
    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);

    renderPeerSummary(27);

    char buf[24];
    float live  = tfLuna.getDistance();
    bool  valid = tfLuna.hasObject();
    if (valid) snprintf(buf, sizeof(buf), "%.2fm", live);
    else       snprintf(buf, sizeof(buf), "NO SIGNAL");
    infoRow(44, "DIST", buf, valid ? COLOR_TEXT : COLOR_RED);

    if (multiBox.state.baselineCaptured) {
        snprintf(buf, sizeof(buf), "%.2fm  %ucm",
                 multiBox.state.baselineDistance, multiBox.config.detectThresholdCm);
        infoRow(61, "BASE", buf, COLOR_TEXT);
    } else {
        infoRow(61, "BASE", "CALIBRATING", COLOR_YELLOW);
    }

    renderSensorBar(16, 79, 206, 8);
    renderCycleBar(93);
    renderStatusPanel();
}

// --- no role assigned yet ---
static void renderNoRoleScreen() {
    LGFX_Sprite* c = _ft->_canvas;
    header("MULTI BOX", "PRESS=ADVANCE");
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
        case MBConnRow::RANGE_ON:
            snprintf(label, n, "Range Filter");
            snprintf(value, n, "%s", multiBox.config.rangeFilterEnabled ? "ON" : "OFF");
            break;
        case MBConnRow::RANGE_MIN:
            snprintf(label, n, "Range Min");
            snprintf(value, n, "%.2fm", multiBox.config.rangeMinCm / 100.0f);
            break;
        case MBConnRow::RANGE_MAX:
            snprintf(label, n, "Range Max");
            snprintf(value, n, "%.2fm", multiBox.config.rangeMaxCm / 100.0f);
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
        case MBConnRow::END_DELAY:
            snprintf(label, n, "End Delay");
            if (multiBox.config.endDelayMs >= 1000)
                snprintf(value, n, "%.1fs", multiBox.config.endDelayMs / 1000.0f);
            else
                snprintf(value, n, "%ums", multiBox.config.endDelayMs);
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
    header("ADVANCE", "HOLD=BACK");
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

    // n items plus a BACK row at the end. BACK exists because long-press was
    // previously the only way out of here, which is not discoverable.
    uint8_t count = n + 1;
    uint8_t sel   = multiBox.editMode.scanSel;
    if (sel >= count) sel = count - 1;

    // Windowed, same firstVisible pattern the ADVANCE list uses. Without it
    // this drew only the first VISIBLE_ROWS entries while selection could run
    // to MB_MAX_PEERS -- entries past the fourth were selectable but invisible.
    uint8_t firstVisible = 0;
    if (sel >= VISIBLE_ROWS) firstVisible = sel - VISIBLE_ROWS + 1;

    for (uint8_t i = firstVisible; i < count && i < firstVisible + VISIBLE_ROWS; i++) {
        bool isSel = (i == sel);
        int y = ITEM_Y_START + (i - firstVisible) * ITEM_HEIGHT;
        if (isSel) c->fillRoundRect(10, y - 1, 220, 18, 3, COLOR_BORDER);

        char line[32];
        if (i == n) {
            snprintf(line, sizeof(line), "< BACK");
        } else {
            NodeManager::Candidate* cand = nodeManager.scanResultAt(i);
            if (!cand) continue;
            snprintf(line, sizeof(line), "ID:%u  %s", cand->nodeId, roleName(cand->role));
        }
        c->setFont(&fonts::efontCN_16);
        c->setTextDatum(top_left);
        c->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);
        c->drawString(line, 16, y + 1);
    }

    if (firstVisible + VISIBLE_ROWS < count) {
        c->setTextDatum(top_right);
        c->setTextColor(COLOR_BORDER);
        c->drawString("v", 226, 92);
        c->setTextDatum(top_left);
    }
}

// ============ PEER LIST ============
static void renderPeerList() {
    LGFX_Sprite* c = _ft->_canvas;
    header("NODES", "PRESS=REMOVE");
    c->drawRoundRect(8, 22, 224, 88, 5, COLOR_BORDER);

    uint8_t n = nodeManager.peerCount();
    uint8_t count = n + 1;              // + BACK
    uint8_t sel = multiBox.editMode.peerSel;
    if (sel >= count) sel = count - 1;

    uint8_t firstVisible = 0;
    if (sel >= VISIBLE_ROWS) firstVisible = sel - VISIBLE_ROWS + 1;

    for (uint8_t i = firstVisible; i < count && i < firstVisible + VISIBLE_ROWS; i++) {
        bool isSel = (i == sel);
        int y = ITEM_Y_START + (i - firstVisible) * ITEM_HEIGHT;
        if (isSel) c->fillRoundRect(10, y - 1, 220, 18, 3, COLOR_BORDER);

        c->setFont(&fonts::efontCN_16);
        c->setTextDatum(top_left);
        c->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);

        if (i == n) {
            c->drawString("< BACK", 16, y + 1);
            continue;
        }

        MBPeer* p = nodeManager.peerAt(i);
        if (!p) continue;
        char line[24];
        snprintf(line, sizeof(line), "ID:%u  %s", p->nodeId, roleName(p->role));
        c->drawString(line, 16, y + 1);

        c->setTextDatum(top_right);
        c->setTextColor(isSel ? COLOR_BG : (p->connected ? COLOR_GREEN : COLOR_BORDER));
        c->drawString(p->connected ? "LINKED" : "---", 216, y + 1);
        c->setTextDatum(top_left);
    }

    if (firstVisible + VISIBLE_ROWS < count) {
        c->setTextDatum(top_right);
        c->setTextColor(COLOR_BORDER);
        c->drawString("v", 226, 92);
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
