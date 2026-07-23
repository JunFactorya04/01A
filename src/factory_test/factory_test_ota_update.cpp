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

// Password keyboard
static const char* KB_CHARS =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    "!@#$%^&*()-_=+.,:;?/";
static int s_kbIndex = 0;          // index into KB_CHARS + 2 virtual keys
#define KB_LEN   ((int)strlen(KB_CHARS))
#define KB_DEL   (KB_LEN)          // virtual: delete
#define KB_DONE  (KB_LEN + 1)      // virtual: done

// ============ RENDER HELPERS ============
static void otaHeader(const char* title) {
    LGFX_Sprite* c = _ft->_canvas;
    c->fillScreen(UI_BG);
    c->fillRect(0, 0, 240, 22, 0x762424);
    c->setFont(&fonts::efontCN_16);
    c->setTextDatum(top_center);
    c->setTextColor(0xF6A4A4);
    c->drawString(title, 120, 3);
    c->setTextDatum(top_left);
}

static void otaFooter(const char* hint) {
    LGFX_Sprite* c = _ft->_canvas;
    c->setFont(&fonts::efontCN_10);
    c->setTextDatum(bottom_center);
    c->setTextColor(UI_BORDER);
    c->drawString(hint, 120, 133);
    c->setTextDatum(top_left);
}

static void otaMessage(const char* title, const String& msg, uint16_t color) {
    otaHeader(title);
    LGFX_Sprite* c = _ft->_canvas;
    c->setFont(&fonts::efontCN_12);
    c->setTextColor(color);
    c->setTextDatum(middle_center);
    // Wrap long messages roughly at 30 chars
    if (msg.length() <= 30) {
        c->drawString(msg.c_str(), 120, 65);
    } else {
        c->drawString(msg.substring(0, 30).c_str(), 120, 55);
        c->drawString(msg.substring(30).c_str(), 120, 75);
    }
    c->setTextDatum(top_left);
}

// ============ RENDER SCREENS ============
static void renderWifiList() {
    otaHeader("OTA - SELECT WIFI");
    LGFX_Sprite* c = _ft->_canvas;
    c->setFont(&fonts::efontCN_12);

    if (s_wifiCount == 0) {
        c->setTextColor(UI_FG);
        c->drawString("No networks found", 12, 55);
        otaFooter("Hold: exit");
    } else {
        int firstVisible = (s_sel > 4) ? s_sel - 4 : 0;
        for (int row = 0; row < 5; row++) {
            int i = firstVisible + row;
            if (i >= s_wifiCount) break;
            int y = 28 + row * 18;
            bool isSel = (i == s_sel);
            if (isSel) c->fillRect(0, y - 2, 240, 18, UI_BORDER);
            c->setTextColor(isSel ? UI_AL : UI_FG);
            String label = s_wifi[i].ssid;
            if (label.length() > 20) label = label.substring(0, 20);
            if (s_wifi[i].saved) label += " *";
            if (s_wifi[i].open)  label += " (open)";
            c->drawString(label.c_str(), 12, y);
            // RSSI
            char rbuf[8];
            snprintf(rbuf, sizeof(rbuf), "%d", s_wifi[i].rssi);
            c->setTextDatum(top_right);
            c->setTextColor(s_wifi[i].rssi > -75 ? 0x07E0 : 0xFD20);
            c->drawString(rbuf, 228, y);
            c->setTextDatum(top_left);
        }
        otaFooter("Press: select | Hold: exit");
    }
    _ft->_canvas_update();
}

static void renderPassword() {
    otaHeader("WIFI PASSWORD");
    LGFX_Sprite* c = _ft->_canvas;

    // Entered password (masked tail visible)
    c->setFont(&fonts::efontCN_12);
    c->setTextColor(UI_FG);
    String shown = s_pwd;
    if (shown.length() > 26) shown = "..." + shown.substring(shown.length() - 23);
    c->drawString(shown.c_str(), 12, 30);
    c->drawFastHLine(12, 46, 216, UI_BORDER);

    // Current key (big)
    c->setFont(&fonts::efontCN_24);
    c->setTextDatum(middle_center);
    c->setTextColor(UI_AL);
    if (s_kbIndex == KB_DEL)       c->drawString("[DEL]", 120, 78);
    else if (s_kbIndex == KB_DONE) c->drawString("[DONE]", 120, 78);
    else {
        char ch[2] = {KB_CHARS[s_kbIndex], 0};
        c->drawString(ch, 120, 78);
        // neighbours for context
        c->setFont(&fonts::efontCN_12);
        c->setTextColor(UI_BORDER);
        if (s_kbIndex > 0) {
            char p[2] = {KB_CHARS[s_kbIndex - 1], 0};
            c->drawString(p, 70, 78);
        }
        if (s_kbIndex < KB_LEN - 1) {
            char nx[2] = {KB_CHARS[s_kbIndex + 1], 0};
            c->drawString(nx, 170, 78);
        }
    }
    c->setTextDatum(top_left);

    otaFooter("Rotate: char | Press: add | Hold: connect");
    _ft->_canvas_update();
}

static void renderReleaseList() {
    otaHeader("SELECT VERSION");
    LGFX_Sprite* c = _ft->_canvas;
    c->setFont(&fonts::efontCN_12);

    // Current version banner
    c->setTextColor(UI_BORDER);
    c->drawString(("Current: " GEOPIX_FW_VERSION), 12, 26);

    int firstVisible = (s_relSel > 3) ? s_relSel - 3 : 0;
    for (int row = 0; row < 4; row++) {
        int i = firstVisible + row;
        if (i >= s_relCount) break;
        int y = 44 + row * 18;
        bool isSel = (i == s_relSel);
        if (isSel) c->fillRect(0, y - 2, 240, 18, UI_BORDER);
        c->setTextColor(isSel ? UI_AL : UI_FG);
        String label = s_rel[i].tag;
        if (s_rel[i].name.length() > 0 && s_rel[i].name != s_rel[i].tag)
            label += " " + s_rel[i].name;
        if (label.length() > 30) label = label.substring(0, 30);
        c->drawString(label.c_str(), 12, y);
    }
    otaFooter("Press: select | Hold: exit");
    _ft->_canvas_update();
}

static void renderConfirm() {
    otaHeader("CONFIRM UPDATE");
    LGFX_Sprite* c = _ft->_canvas;
    c->setFont(&fonts::efontCN_12);
    c->setTextDatum(middle_center);
    c->setTextColor(UI_FG);
    c->drawString(("Install " + s_rel[s_relSel].tag + "?").c_str(), 120, 50);
    c->setTextColor(0xFFE0);
    c->drawString("Do not power off during", 120, 72);
    c->drawString("update. Failure = safe.", 120, 86);
    c->setTextDatum(top_left);
    otaFooter("Press: START | Hold: back");
    _ft->_canvas_update();
}

static void renderFlashing() {
    otaHeader("UPDATING...");
    LGFX_Sprite* c = _ft->_canvas;

    int pct = (s_flashTotal > 0) ? (int)((uint64_t)s_flashDone * 100 / s_flashTotal) : 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%  (%u KB / %u KB)", pct,
             (unsigned)(s_flashDone / 1024), (unsigned)(s_flashTotal / 1024));

    c->setFont(&fonts::efontCN_12);
    c->setTextDatum(middle_center);
    c->setTextColor(UI_FG);
    c->drawString(s_rel[s_relSel].tag.c_str(), 120, 45);
    c->drawString(buf, 120, 62);
    c->setTextDatum(top_left);

    // Progress bar
    int barW = 216, barX = 12, barY = 78;
    c->drawRect(barX, barY, barW, 12, UI_BORDER);
    int fill = (s_flashTotal > 0) ? (int)((uint64_t)(barW - 2) * s_flashDone / s_flashTotal) : 0;
    if (fill > 0) c->fillRect(barX + 1, barY + 1, fill, 10, UI_AL);

    c->setFont(&fonts::efontCN_10);
    c->setTextDatum(middle_center);
    c->setTextColor(0xFFE0);
    c->drawString("DO NOT POWER OFF", 120, 105);
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
    s_sel = 0; s_relSel = 0; s_kbIndex = 0;
    s_pwd = ""; s_err = "";

    bool exitMode = false;
    while (!exitMode) {
        switch (s_screen) {

        case OtaScreen::WifiScan: {
            otaMessage("OTA - WIFI SCAN", "Scanning networks...", UI_FG);
            _canvas_update();
            s_wifiCount = OtaUpdate::scanNetworks(s_wifi, OTA_MAX_WIFI);
            s_sel = 0;
            s_screen = OtaScreen::WifiList;
            break;
        }

        case OtaScreen::WifiList: {
            renderWifiList();
            int delta = _read_encoder_delta(_ota_enc_last_pos, true);
            if (delta != 0 && s_wifiCount > 0) {
                s_sel += (delta > 0) ? 1 : -1;
                if (s_sel < 0) s_sel = 0;
                if (s_sel >= s_wifiCount) s_sel = s_wifiCount - 1;
            }
            ButtonEvent ev = _read_mode_button_event();
            if (ev == ButtonEvent::LongPress) { exitMode = true; }
            else if (ev == ButtonEvent::ShortPress && s_wifiCount > 0) {
                _tone(1000, 50);
                s_ssid = s_wifi[s_sel].ssid;
                if (s_wifi[s_sel].open) {
                    s_pwd = "";
                    s_screen = OtaScreen::Connecting;
                } else if (s_wifi[s_sel].saved &&
                           OtaUpdate::getSavedPassword(s_ssid, s_pwd)) {
                    s_screen = OtaScreen::Connecting;   // auto-connect with saved
                } else {
                    s_pwd = "";
                    s_kbIndex = 0;
                    s_screen = OtaScreen::Password;
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
                if (s_kbIndex < 0) s_kbIndex = KB_DONE;
                if (s_kbIndex > KB_DONE) s_kbIndex = 0;
            }
            ButtonEvent ev = _read_mode_button_event();
            if (ev == ButtonEvent::ShortPress) {
                if (s_kbIndex == KB_DEL) {
                    if (s_pwd.length() > 0) s_pwd.remove(s_pwd.length() - 1);
                } else if (s_kbIndex == KB_DONE) {
                    s_screen = OtaScreen::Connecting;
                } else {
                    s_pwd += KB_CHARS[s_kbIndex];
                }
            } else if (ev == ButtonEvent::LongPress) {
                s_screen = OtaScreen::Connecting;       // hold = connect
            }
            delay(10);
            break;
        }

        case OtaScreen::Connecting: {
            otaMessage("CONNECTING", "Joining " + s_ssid + "...", UI_FG);
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
            otaMessage("GITHUB", "Fetching releases...", UI_FG);
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
            otaMessage("UPDATE OK", "Press button to reboot", 0x07E0);
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
            otaMessage("ERROR (SAFE)", s_err, 0xF800);
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
