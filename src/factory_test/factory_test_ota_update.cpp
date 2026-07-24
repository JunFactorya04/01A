/**
 * @file factory_test_ota_update.cpp
 * @brief OTA UPDATE mode — replaces WIFI SCAN menu entry
 * @date 2026-07-23
 *
 * Flow: scan WiFi -> pick SSID -> password (saved via CRC32 key / encoder
 * keyboard) -> connect -> list GitHub releases -> confirm -> flash -> reboot.
 * Any error leaves current firmware bootable (Update.abort inside core).
 *
 * Controls: rotate = navigate, short press = select, long press = back/exit.
 */

#include "factory_test.h"
#include "../ota_update/ota_update.h"
#include "../common/ui_theme.h"

extern FactoryTest* _ft;

// ============ STATE ============
enum class OtaScreen : uint8_t {
    WifiScan,      // scanning animation
    WifiList,      // pick SSID
    Password,      // encoder keyboard
    Connecting,
    FetchList,     // downloading release list
    ReleaseList,   // pick version
    Confirm,       // are you sure
    Flashing,      // progress bar
    Done,          // success -> reboot prompt
    Error,         // message + back
};

static OtaScreen     s_screen;
static OtaWifiEntry  s_wifi[OTA_MAX_WIFI];
static int           s_wifiCount = 0;
static int           s_sel = 0;
static OtaRelease    s_rel[OTA_MAX_RELEASES];
static int           s_relCount = 0;
static int           s_relSel = 0;
static String        s_ssid, s_pwd, s_err;
static size_t        s_flashDone = 0, s_flashTotal = 0;

// Password keyboard — M5Launcher-style QWERTY grid (12x4, caps toggle)
// adapted for GEOPIX encoder navigation.
#define KB_COLS  12
#define KB_ROWS  4
static const char KB_KEYS[KB_ROWS][KB_COLS][2] = {
    {{'1','!'},{'2','@'},{'3','#'},{'4','$'},{'5','%'},{'6','^'},
     {'7','&'},{'8','*'},{'9','('},{'0',')'},{'-','_'},{'=','+'}},
    {{'q','Q'},{'w','W'},{'e','E'},{'r','R'},{'t','T'},{'y','Y'},
     {'u','U'},{'i','I'},{'o','O'},{'p','P'},{'[','{'},{']','}'}},
    {{'a','A'},{'s','S'},{'d','D'},{'f','F'},{'g','G'},{'h','H'},
     {'j','J'},{'k','K'},{'l','L'},{';',':'},{'\'','"'},{'\\','|'}},
    {{'z','Z'},{'x','X'},{'c','C'},{'v','V'},{'b','B'},{'n','N'},
     {'m','M'},{',','<'},{'.','>'},{'/','?'},{'@','~'},{'.','`'}},
};
// Linear index: 0=OK, 1=CAP, 2=DEL, 3=BACK, 4.. = grid keys (row*12+col)
#define KB_OK    0
#define KB_CAP   1
#define KB_DEL   2
#define KB_BACK  3
#define KB_GRID  4
#define KB_TOTAL (KB_GRID + KB_ROWS * KB_COLS)
static int  s_kbIndex = KB_GRID;   // start on '1'
static bool s_kbCaps  = false;

// ============ RENDER HELPERS (Auto Shoot style palette) ============
#define COLOR_BG        UI_BG
#define COLOR_TEXT      UI_FG
#define COLOR_GREEN     0x07E0
#define COLOR_RED       0xF800
#define COLOR_YELLOW    0xFFE0
#define COLOR_BORDER    UI_BORDER
#define COLOR_HIGHLIGHT UI_AL

static void otaHeader(const char* title) {
    LGFX_Sprite* c = _ft->_canvas;
    c->setTextWrap(false);
    c->fillScreen(COLOR_BG);
    c->setFont(&fonts::efontCN_16);
    c->setTextDatum(top_center);
    c->setTextColor(COLOR_GREEN);
    c->drawString(title, 120, 2);
    c->setTextDatum(top_left);
    c->setTextColor(COLOR_TEXT);
    c->drawString("<", 5, 2);
}

static void otaFooter(const char* hint) {
    LGFX_Sprite* c = _ft->_canvas;
    c->setFont(&fonts::efontCN_10);
    c->setTextDatum(bottom_center);
    c->setTextColor(COLOR_BORDER);
    c->drawString(hint, 120, 133);
    c->setTextDatum(top_left);
}

static void otaMessage(const char* title, const String& msg, uint16_t color) {
    otaHeader(title);
    LGFX_Sprite* c = _ft->_canvas;
    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);
    c->setFont(&fonts::efontCN_12);
    c->setTextColor(color);
    c->setTextDatum(middle_center);
    // Wrap long messages roughly at 30 chars
    if (msg.length() <= 30) {
        c->drawString(msg.c_str(), 120, 62);
    } else {
        c->drawString(msg.substring(0, 30).c_str(), 120, 54);
        c->drawString(msg.substring(30).c_str(), 120, 70);
    }
    c->setTextDatum(top_left);
}

// ============ RENDER SCREENS ============
// WiFi list has a virtual last entry: BACK (red) -> exit to main UI
static void renderWifiList() {
    otaHeader("OTA - WIFI");
    LGFX_Sprite* c = _ft->_canvas;
    c->setFont(&fonts::efontCN_12);

    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);

    int total = s_wifiCount + 1;   // +1 = BACK entry
    int firstVisible = (s_sel > 5) ? s_sel - 5 : 0;
    for (int row = 0; row < 6; row++) {
        int i = firstVisible + row;
        if (i >= total) break;
        int y = 27 + row * 13;
        bool isSel = (i == s_sel);

        if (i == s_wifiCount) {
            // BACK entry (red)
            if (isSel) c->fillRoundRect(10, y - 1, 220, 12, 3, COLOR_RED);
            c->setTextColor(isSel ? COLOR_BG : COLOR_RED);
            c->drawString("< BACK", 16, y + 1);
            continue;
        }

        if (isSel) c->fillRoundRect(10, y - 1, 220, 12, 3, COLOR_BORDER);
        c->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);
        String label = s_wifi[i].ssid;
        if (label.length() > 20) label = label.substring(0, 20);
        if (s_wifi[i].saved) label += " *";
        if (s_wifi[i].open)  label += " (open)";
        c->drawString(label.c_str(), 16, y + 1);
        // RSSI
        char rbuf[8];
        snprintf(rbuf, sizeof(rbuf), "%d", s_wifi[i].rssi);
        c->setTextDatum(top_right);
        c->setTextColor(isSel ? COLOR_BG
                              : (s_wifi[i].rssi > -75 ? COLOR_GREEN : COLOR_YELLOW));
        c->drawString(rbuf, 226, y + 1);
        c->setTextDatum(top_left);
    }

    if (s_wifiCount == 0) {
        c->setTextDatum(middle_center);
        c->setTextColor(COLOR_TEXT);
        c->drawString("No networks found", 120, 70);
        c->setTextDatum(top_left);
    }

    otaFooter("Press: select | Hold: exit");
    _ft->_canvas_update();
}

static void renderPassword() {
    LGFX_Sprite* c = _ft->_canvas;
    c->setTextWrap(false);
    c->fillScreen(COLOR_BG);

    // Password text line (top)
    c->setFont(&fonts::efontCN_12);
    c->setTextDatum(top_left);
    c->setTextColor(COLOR_TEXT);
    String shown = s_pwd;
    if (shown.length() > 26) shown = "..." + shown.substring(shown.length() - 23);
    c->drawString(shown.c_str(), 8, 2);
    c->drawFastHLine(8, 16, 224, COLOR_BORDER);

    // Function row: OK | CAP | DEL | BACK (red)  — no captions
    struct { const char* txt; int idx; bool red; } fnBtn[4] = {
        {"OK", KB_OK, false}, {"CAP", KB_CAP, false},
        {"DEL", KB_DEL, false}, {"BACK", KB_BACK, true}
    };
    for (int i = 0; i < 4; i++) {
        int bx = 8 + i * 57, bw = 51, by = 20, bh = 17;
        bool isSel = (s_kbIndex == fnBtn[i].idx);
        bool capOn = (fnBtn[i].idx == KB_CAP && s_kbCaps);
        uint16_t accent = fnBtn[i].red ? COLOR_RED : COLOR_HIGHLIGHT;
        if (isSel) {
            c->fillRoundRect(bx, by, bw, bh, 3, accent);
        } else if (capOn) {
            c->fillRoundRect(bx, by, bw, bh, 3, COLOR_BORDER);
        } else {
            c->drawRoundRect(bx, by, bw, bh, 3, fnBtn[i].red ? COLOR_RED : COLOR_BORDER);
        }
        c->setTextDatum(middle_center);
        c->setTextColor((isSel || capOn) ? COLOR_BG
                                         : (fnBtn[i].red ? COLOR_RED : COLOR_TEXT));
        c->drawString(fnBtn[i].txt, bx + bw / 2, by + bh / 2);
    }

    // Key grid 12x4 — bigger font, tight cells
    c->setFont(&fonts::efontCN_16);
    const int gx = 0, gy = 40, cw = 20, ch = 23;
    for (int r = 0; r < KB_ROWS; r++) {
        for (int col = 0; col < KB_COLS; col++) {
            int idx = KB_GRID + r * KB_COLS + col;
            int x = gx + col * cw, y = gy + r * ch;
            bool isSel = (s_kbIndex == idx);
            if (isSel) c->fillRoundRect(x, y + 1, cw, ch - 2, 3, COLOR_HIGHLIGHT);
            char ch2[2] = {KB_KEYS[r][col][s_kbCaps ? 1 : 0], 0};
            c->setTextDatum(middle_center);
            c->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);
            c->drawString(ch2, x + cw / 2, y + ch / 2);
        }
    }
    c->setTextDatum(top_left);

    _ft->_canvas_update();
}

static void renderReleaseList() {
    otaHeader("SELECT VERSION");
    LGFX_Sprite* c = _ft->_canvas;
    c->setFont(&fonts::efontCN_12);

    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);

    // Current version banner
    c->setTextColor(COLOR_BORDER);
    c->drawString(("Current: " GEOPIX_FW_VERSION), 16, 27);

    int firstVisible = (s_relSel > 4) ? s_relSel - 4 : 0;
    for (int row = 0; row < 5; row++) {
        int i = firstVisible + row;
        if (i >= s_relCount) break;
        int y = 42 + row * 13;
        bool isSel = (i == s_relSel);
        if (isSel) c->fillRoundRect(10, y - 1, 220, 12, 3, COLOR_BORDER);
        c->setTextColor(isSel ? COLOR_BG : COLOR_TEXT);
        String label = s_rel[i].tag;
        if (s_rel[i].name.length() > 0 && s_rel[i].name != s_rel[i].tag)
            label += " " + s_rel[i].name;
        if (label.length() > 30) label = label.substring(0, 30);
        c->drawString(label.c_str(), 16, y + 1);
    }
    otaFooter("Press: select | Hold: exit");
    _ft->_canvas_update();
}

static void renderConfirm() {
    otaHeader("CONFIRM UPDATE");
    LGFX_Sprite* c = _ft->_canvas;
    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);
    c->setFont(&fonts::efontCN_12);
    c->setTextDatum(middle_center);
    c->setTextColor(COLOR_TEXT);
    c->drawString(("Install " + s_rel[s_relSel].tag + "?").c_str(), 120, 48);
    c->setTextColor(COLOR_YELLOW);
    c->drawString("Do not power off during", 120, 74);
    c->drawString("update. Failure = safe.", 120, 88);
    c->setTextDatum(top_left);
    otaFooter("Press: START | Hold: back");
    _ft->_canvas_update();
}

static void renderFlashing() {
    otaHeader("UPDATING...");
    LGFX_Sprite* c = _ft->_canvas;
    c->drawRoundRect(8, 22, 224, 86, 5, COLOR_BORDER);

    int pct = (s_flashTotal > 0) ? (int)((uint64_t)s_flashDone * 100 / s_flashTotal) : 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%  (%u KB / %u KB)", pct,
             (unsigned)(s_flashDone / 1024), (unsigned)(s_flashTotal / 1024));

    c->setFont(&fonts::efontCN_12);
    c->setTextDatum(middle_center);
    c->setTextColor(COLOR_TEXT);
    c->drawString(s_rel[s_relSel].tag.c_str(), 120, 38);
    c->setTextColor(COLOR_GREEN);
    c->drawString(buf, 120, 56);
    c->setTextDatum(top_left);

    // Progress bar
    int barW = 200, barX = 20, barY = 70;
    c->drawRect(barX, barY, barW, 12, COLOR_BORDER);
    int fill = (s_flashTotal > 0) ? (int)((uint64_t)(barW - 2) * s_flashDone / s_flashTotal) : 0;
    if (fill > 0) c->fillRect(barX + 1, barY + 1, fill, 10, COLOR_HIGHLIGHT);

    c->setFont(&fonts::efontCN_10);
    c->setTextDatum(middle_center);
    c->setTextColor(COLOR_YELLOW);
    c->drawString("DO NOT POWER OFF", 120, 98);
    c->setTextDatum(top_left);

    _ft->_canvas_update();
}

static void otaProgress(size_t done, size_t total) {
    s_flashDone = done;
    s_flashTotal = total;
    static unsigned long last = 0;
    if (millis() - last > 200) {   // throttle redraw
        renderFlashing();
        last = millis();
    }
}

// ============ MODE ENTRY ============
void FactoryTest::_ota_update_test() {
    _reset_mode_input_state();
    _ota_enc_last_pos = _enc.getCount();

    s_screen = OtaScreen::WifiScan;
    s_sel = 0; s_relSel = 0; s_kbIndex = KB_GRID; s_kbCaps = false;
    s_pwd = ""; s_err = "";

    bool exitMode = false;
    while (!exitMode) {
        switch (s_screen) {

        case OtaScreen::WifiScan: {
            otaMessage("OTA - WIFI SCAN", "Scanning networks...", COLOR_TEXT);
            _canvas_update();
            s_wifiCount = OtaUpdate::scanNetworks(s_wifi, OTA_MAX_WIFI);
            s_sel = 0;
            s_screen = OtaScreen::WifiList;
            break;
        }

        case OtaScreen::WifiList: {
            renderWifiList();
            int delta = _read_encoder_delta(_ota_enc_last_pos, true);
            if (delta != 0) {
                s_sel += (delta > 0) ? 1 : -1;
                if (s_sel < 0) s_sel = 0;
                if (s_sel > s_wifiCount) s_sel = s_wifiCount;   // last = BACK
            }
            ButtonEvent ev = _read_mode_button_event();
            if (ev == ButtonEvent::LongPress) { exitMode = true; }
            else if (ev == ButtonEvent::ShortPress) {
                _tone(1000, 50);
                if (s_sel == s_wifiCount) {                     // BACK -> main UI
                    exitMode = true;
                } else {
                    s_ssid = s_wifi[s_sel].ssid;
                    if (s_wifi[s_sel].open) {
                        s_pwd = "";
                        s_screen = OtaScreen::Connecting;
                    } else if (s_wifi[s_sel].saved &&
                               OtaUpdate::getSavedPassword(s_ssid, s_pwd)) {
                        s_screen = OtaScreen::Connecting;   // auto-connect with saved
                    } else {
                        s_pwd = "";
                        s_kbIndex = KB_GRID;
                        s_kbCaps = false;
                        s_screen = OtaScreen::Password;
                    }
                }
            }
            delay(10);
            break;
        }

        case OtaScreen::Password: {
            renderPassword();
            int delta = _read_encoder_delta(_ota_enc_last_pos, true);
            if (delta != 0) {
                s_kbIndex += (delta > 0) ? 1 : -1;
                if (s_kbIndex < 0) s_kbIndex = KB_TOTAL - 1;
                if (s_kbIndex >= KB_TOTAL) s_kbIndex = 0;
            }
            ButtonEvent ev = _read_mode_button_event();
            if (ev == ButtonEvent::ShortPress) {
                if (s_kbIndex == KB_OK) {
                    s_screen = OtaScreen::Connecting;
                } else if (s_kbIndex == KB_CAP) {
                    s_kbCaps = !s_kbCaps;
                } else if (s_kbIndex == KB_DEL) {
                    if (s_pwd.length() > 0) s_pwd.remove(s_pwd.length() - 1);
                } else if (s_kbIndex == KB_BACK) {
                    s_pwd = "";
                    s_screen = OtaScreen::WifiList;     // back to WiFi list
                } else {
                    int k = s_kbIndex - KB_GRID;
                    s_pwd += KB_KEYS[k / KB_COLS][k % KB_COLS][s_kbCaps ? 1 : 0];
                }
            } else if (ev == ButtonEvent::LongPress) {
                s_screen = OtaScreen::Connecting;       // hold = connect
            }
            delay(10);
            break;
        }

        case OtaScreen::Connecting: {
            otaMessage("CONNECTING", "Joining " + s_ssid + "...", COLOR_TEXT);
            _canvas_update();
            if (OtaUpdate::connect(s_ssid, s_pwd)) {
                if (!s_wifi[s_sel].open)
                    OtaUpdate::savePassword(s_ssid, s_pwd);   // CRC32 key store
                s_screen = OtaScreen::FetchList;
            } else {
                s_err = "WiFi connect failed";
                s_screen = OtaScreen::Error;
            }
            break;
        }

        case OtaScreen::FetchList: {
            otaMessage("GITHUB", "Fetching releases...", COLOR_TEXT);
            _canvas_update();
            s_relCount = OtaUpdate::fetchReleases(s_rel, OTA_MAX_RELEASES, s_err);
            if (s_relCount > 0) {
                s_relSel = 0;
                s_screen = OtaScreen::ReleaseList;
            } else {
                s_screen = OtaScreen::Error;
            }
            break;
        }

        case OtaScreen::ReleaseList: {
            renderReleaseList();
            int delta = _read_encoder_delta(_ota_enc_last_pos, true);
            if (delta != 0) {
                s_relSel += (delta > 0) ? 1 : -1;
                if (s_relSel < 0) s_relSel = 0;
                if (s_relSel >= s_relCount) s_relSel = s_relCount - 1;
            }
            ButtonEvent ev = _read_mode_button_event();
            if (ev == ButtonEvent::LongPress) { exitMode = true; }
            else if (ev == ButtonEvent::ShortPress) {
                _tone(1000, 50);
                s_screen = OtaScreen::Confirm;
            }
            delay(10);
            break;
        }

        case OtaScreen::Confirm: {
            renderConfirm();
            ButtonEvent ev = _read_mode_button_event();
            if (ev == ButtonEvent::LongPress) { s_screen = OtaScreen::ReleaseList; }
            else if (ev == ButtonEvent::ShortPress) {
                _tone(2000, 100);
                s_flashDone = 0; s_flashTotal = 0;
                s_screen = OtaScreen::Flashing;
            }
            delay(10);
            break;
        }

        case OtaScreen::Flashing: {
            renderFlashing();
            bool ok = OtaUpdate::flashFromUrl(s_rel[s_relSel].url, otaProgress, s_err);
            if (ok) s_screen = OtaScreen::Done;
            else    s_screen = OtaScreen::Error;
            break;
        }

        case OtaScreen::Done: {
            otaMessage("UPDATE OK", "Press button to reboot", COLOR_GREEN);
            _canvas_update();
            ButtonEvent ev = _read_mode_button_event();
            if (ev == ButtonEvent::ShortPress || ev == ButtonEvent::LongPress) {
                OtaUpdate::wifiOff();
                delay(200);
                ESP.restart();
            }
            delay(10);
            break;
        }

        case OtaScreen::Error: {
            // Current firmware is untouched — safe to just go back.
            otaMessage("ERROR (SAFE)", s_err, COLOR_RED);
            otaFooter("Press: retry | Hold: exit");
            _canvas_update();
            ButtonEvent ev = _read_mode_button_event();
            if (ev == ButtonEvent::LongPress) exitMode = true;
            else if (ev == ButtonEvent::ShortPress) s_screen = OtaScreen::WifiScan;
            delay(10);
            break;
        }
        }
    }

    // Cleanup: radios off so other modes are unaffected
    OtaUpdate::wifiOff();
    _reset_mode_input_state();
}
