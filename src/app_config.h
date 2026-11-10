#pragma once
#include <Arduino.h>

// Persisted settings (NVS namespace "statsdisp"): known WiFi networks
// (most-recently-used first, up to MAX_KNOWN) and the discovered
// LibreHardwareMonitor server address. `ssid`/`password` mirror the
// most recently used network for quick boot.
struct AppConfig {
    static const int MAX_KNOWN = 6;

    String ssid, password, serverHost;
    String knownSsid[MAX_KNOWN], knownPass[MAX_KNOWN];
    int knownCount = 0;
    uint8_t rotation = 1;  // landscape orientation: 1 or 3 (180° apart)
    // Crash log: unexpected resets (panic, watchdog, brownout) since the
    // counter was created, and the reason for the most recent one.
    uint32_t crashCount = 0;
    uint32_t powerOnCount = 0;     // power-loss restarts (not crashes)
    uint8_t lastCrashReason = 0;   // esp_reset_reason_t
    uint32_t lastCrashUptimeS = 0; // how long the previous run lasted (approx.)

    bool hasWifi() const { return ssid.length() > 0; }
    bool hasServer() const { return serverHost.length() > 0; }

    // True + fills passOut if this SSID has a remembered password.
    bool findKnown(const String &s, String &passOut) const;

    void load();
    // Remembers the network (MRU front, evicting the oldest) and sets it
    // as the current one. Persists everything.
    void saveWifi(const String &newSsid, const String &newPassword);
    void saveServer(const String &host);
    void saveRotation(uint8_t r);
    // Call once at boot: counts the reset if it was unexpected.
    void recordResetReason();
    // Call periodically: remembers uptime so a later crash can report it.
    void saveUptime(uint32_t seconds);
    void clearServer();
    void clearAll();

private:
    void persistKnown();
};

extern AppConfig appConfig;
