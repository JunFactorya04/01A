/**
 * @file ota_update.h
 * @brief OTA UPDATE mode — WiFi connect + GitHub release firmware update
 * @date 2026-07-23
 *
 * Learned from M5Launcher onlineLauncher.cpp / settings.cpp:
 * - WiFi credentials persisted in NVS, key = CRC32 hex of SSID
 * - GitHub releases JSON -> pick version -> HTTPS download -> Update.h
 * - Any failure => Update.abort() => current firmware stays bootable
 */

#pragma once
#include <Arduino.h>

#define GEOPIX_FW_VERSION "v1.3"
#define OTA_GITHUB_API    "https://api.github.com/repos/JunFactorya04/01A/releases?per_page=6"
#define OTA_MAX_RELEASES  6
#define OTA_MAX_WIFI      8

struct OtaRelease {
    String tag;    // e.g. "v1.1-final"
    String name;   // release title
    String url;    // browser_download_url of *firmware*.bin asset
};

struct OtaWifiEntry {
    String ssid;
    int    rssi = 0;
    bool   open = false;
    bool   saved = false;   // credential exists in NVS
};

namespace OtaUpdate {
    // ── WiFi credential store (NVS "wifi", key = CRC32 hex of SSID) ──
    uint32_t crc32str(const String& s);
    bool     getSavedPassword(const String& ssid, String& pwdOut);
    void     savePassword(const String& ssid, const String& pwd);

    // ── WiFi ──
    int  scanNetworks(OtaWifiEntry* list, int maxN);       // blocking scan
    bool connect(const String& ssid, const String& pwd,
                 unsigned long timeoutMs = 15000);
    void wifiOff();                                        // full cleanup

    // ── GitHub releases ──
    // Returns count (0 on error). errOut has message on failure.
    int fetchReleases(OtaRelease* list, int maxN, String& errOut);

    // ── OTA flash ──
    // Progress callback: (bytesDone, bytesTotal). Returns true on success
    // (caller should reboot). On failure current firmware is untouched.
    typedef void (*OtaProgressFn)(size_t done, size_t total);
    bool flashFromUrl(const String& url, OtaProgressFn progress, String& errOut);
}
