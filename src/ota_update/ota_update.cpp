/**
 * @file ota_update.cpp
 * @brief OTA core — WiFi credentials (CRC32 keys), GitHub releases, Update.h flash
 * @date 2026-07-23
 */

#include "ota_update.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_wifi.h>

namespace OtaUpdate {

// ============ CRC32 (same algorithm as M5Launcher settings.cpp) ============
uint32_t crc32str(const String& s) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < s.length(); i++) {
        crc ^= (uint8_t)s[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else         crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

static String crcKey(const String& ssid) {
    char key[12];
    snprintf(key, sizeof(key), "%08lX", (unsigned long)crc32str(ssid));
    return String(key);   // 8 chars, well under NVS 15-char limit
}

bool getSavedPassword(const String& ssid, String& pwdOut) {
    Preferences prefs;
    prefs.begin("wifi", true);
    String key = crcKey(ssid);
    bool has = prefs.isKey(key.c_str());
    if (has) pwdOut = prefs.getString(key.c_str(), "");
    prefs.end();
    return has;
}

void savePassword(const String& ssid, const String& pwd) {
    Preferences prefs;
    prefs.begin("wifi");
    prefs.putString(crcKey(ssid).c_str(), pwd);
    prefs.end();
}

// ============ WIFI ============
int scanNetworks(OtaWifiEntry* list, int maxN) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int n = WiFi.scanNetworks();
    int count = 0;
    for (int i = 0; i < n && count < maxN; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;

        // Dedupe by SSID (keep strongest)
        bool dup = false;
        for (int k = 0; k < count; k++)
            if (list[k].ssid == ssid) { dup = true; break; }
        if (dup) continue;

        list[count].ssid  = ssid;
        list[count].rssi  = WiFi.RSSI(i);
        list[count].open  = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        String dummy;
        list[count].saved = getSavedPassword(ssid, dummy);
        count++;
    }
    WiFi.scanDelete();
    return count;
}

bool connect(const String& ssid, const String& pwd, unsigned long timeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pwd.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) return false;
        delay(200);
    }
    return true;
}

void wifiOff() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_wifi_clear_ap_list();
}

// ============ JSON HELPERS (minimal, no ArduinoJson dependency) ============
// Extract the string value following "key":" starting at position `from`.
static String jsonStr(const String& src, const char* key, int from, int* posOut = nullptr) {
    String pat = String("\"") + key + "\":\"";
    int p = src.indexOf(pat, from);
    if (p < 0) { if (posOut) *posOut = -1; return ""; }
    p += pat.length();
    int e = src.indexOf('"', p);
    if (e < 0) { if (posOut) *posOut = -1; return ""; }
    if (posOut) *posOut = e;
    return src.substring(p, e);
}

// ============ GITHUB RELEASES ============
int fetchReleases(OtaRelease* list, int maxN, String& errOut) {
    if (WiFi.status() != WL_CONNECTED) { errOut = "WiFi not connected"; return 0; }

    WiFiClientSecure client;
    client.setInsecure();   // M5Launcher pattern; assets are integrity-checked by Update

    HTTPClient http;
    http.useHTTP10(true);
    if (!http.begin(client, OTA_GITHUB_API)) { errOut = "HTTP begin failed"; return 0; }
    http.addHeader("User-Agent", "Geopix-OTA");
    http.addHeader("Accept", "application/vnd.github+json");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        errOut = "HTTP " + String(code);
        http.end();
        return 0;
    }

    String payload = http.getString();
    http.end();

    // Walk each release object: tag_name, name, then find its firmware asset url
    int count = 0, pos = 0;
    while (count < maxN) {
        int tagEnd;
        String tag = jsonStr(payload, "tag_name", pos, &tagEnd);
        if (tagEnd < 0) break;

        String name = jsonStr(payload, "name", tagEnd);

        // Find firmware*.bin download url within this release block
        // (search until next "tag_name" or end of payload)
        int nextTag = payload.indexOf("\"tag_name\":\"", tagEnd);
        int blockEnd = (nextTag < 0) ? payload.length() : nextTag;
        String url = "";
        int p = tagEnd;
        while (p < blockEnd) {
            int q;
            String u = jsonStr(payload, "browser_download_url", p, &q);
            if (q < 0 || q > blockEnd) break;
            if (u.indexOf("firmware") >= 0 && u.endsWith(".bin")) { url = u; break; }
            p = q;
        }

        if (url.length() > 0) {
            list[count].tag  = tag;
            list[count].name = name;
            list[count].url  = url;
            count++;
        }
        pos = tagEnd;
        if (nextTag < 0) break;
        pos = nextTag;
    }

    if (count == 0) errOut = "No firmware assets found";
    return count;
}

// ============ OTA FLASH ============
// SAFE-BY-DESIGN: writes go to the inactive OTA partition (app0/app1).
// The boot partition is switched ONLY after Update.end() validates the
// full image. Any error before that => abort => current firmware intact.
bool flashFromUrl(const String& url, OtaProgressFn progress, String& errOut) {
    if (WiFi.status() != WL_CONNECTED) { errOut = "WiFi not connected"; return false; }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    // GitHub asset URLs 302-redirect to objects.githubusercontent.com
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);
    if (!http.begin(client, url)) { errOut = "HTTP begin failed"; return false; }
    http.addHeader("User-Agent", "Geopix-OTA");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        errOut = "HTTP " + String(code);
        http.end();
        return false;
    }

    int total = http.getSize();
    if (total <= 0) { errOut = "Bad content length"; http.end(); return false; }

    if (!Update.begin(total)) {
        errOut = "No space: " + String(Update.errorString());
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t written = 0;
    uint8_t buf[1024];
    unsigned long lastData = millis();

    while (written < (size_t)total) {
        size_t avail = stream->available();
        if (avail) {
            size_t toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
            int r = stream->readBytes(buf, toRead);
            if (r <= 0) break;
            if (Update.write(buf, r) != (size_t)r) {
                errOut = "Write: " + String(Update.errorString());
                Update.abort();
                http.end();
                return false;
            }
            written += r;
            lastData = millis();
            if (progress) progress(written, total);
        } else {
            if (!http.connected() && written < (size_t)total) break;
            if (millis() - lastData > 20000) { break; }   // 20s stall
            delay(5);
        }
    }
    http.end();

    if (written != (size_t)total) {
        errOut = "Incomplete " + String(written) + "/" + String(total);
        Update.abort();
        return false;
    }

    if (!Update.end(true)) {
        errOut = "Verify: " + String(Update.errorString());
        Update.abort();
        return false;
    }

    return true;   // boot partition switched, caller reboots
}

}   // namespace OtaUpdate
