#include "app_config.h"
#include <Preferences.h>
#include <esp_system.h>

AppConfig appConfig;

static Preferences prefs;
static const char *NS = "statsdisp";

bool AppConfig::findKnown(const String &s, String &passOut) const {
    for (int i = 0; i < knownCount; i++) {
        if (knownSsid[i] == s) { passOut = knownPass[i]; return true; }
    }
    return false;
}

void AppConfig::load() {
    prefs.begin(NS, true);
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("pass", "");
    serverHost = prefs.getString("server", "");
    rotation = prefs.getUChar("rot", 1);
    crashCount = prefs.getUInt("crash_n", 0);
    powerOnCount = prefs.getUInt("pwr_n", 0);
    lastCrashReason = prefs.getUChar("crash_r", 0);
    lastCrashUptimeS = prefs.getUInt("crash_up", 0);
    if (rotation != 1 && rotation != 3) rotation = 1;
    knownCount = prefs.getInt("known_n", 0);
    if (knownCount > MAX_KNOWN) knownCount = MAX_KNOWN;
    for (int i = 0; i < knownCount; i++) {
        char ks[8], kp[8];
        snprintf(ks, sizeof(ks), "k%ds", i);
        snprintf(kp, sizeof(kp), "k%dp", i);
        knownSsid[i] = prefs.getString(ks, "");
        knownPass[i] = prefs.getString(kp, "");
    }
    prefs.end();

    // Migration: a previously saved single network becomes the known list.
    if (knownCount == 0 && ssid.length() > 0) {
        knownSsid[0] = ssid; knownPass[0] = password; knownCount = 1;
        persistKnown();
    }
}

void AppConfig::persistKnown() {
    prefs.begin(NS, false);
    prefs.putInt("known_n", knownCount);
    for (int i = 0; i < knownCount; i++) {
        char ks[8], kp[8];
        snprintf(ks, sizeof(ks), "k%ds", i);
        snprintf(kp, sizeof(kp), "k%dp", i);
        prefs.putString(ks, knownSsid[i]);
        prefs.putString(kp, knownPass[i]);
    }
    prefs.end();
}

void AppConfig::saveWifi(const String &newSsid, const String &newPassword) {
    // Move-to-front, dropping a previous entry for the same SSID.
    int from = -1;
    for (int i = 0; i < knownCount; i++)
        if (knownSsid[i] == newSsid) { from = i; break; }
    int shiftEnd = (from >= 0) ? from : (knownCount < MAX_KNOWN ? knownCount : MAX_KNOWN - 1);
    for (int i = shiftEnd; i > 0; i--) {
        knownSsid[i] = knownSsid[i - 1];
        knownPass[i] = knownPass[i - 1];
    }
    knownSsid[0] = newSsid;
    knownPass[0] = newPassword;
    if (from < 0 && knownCount < MAX_KNOWN) knownCount++;

    ssid = newSsid; password = newPassword;
    prefs.begin(NS, false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.end();
    persistKnown();
}

void AppConfig::saveServer(const String &host) {
    serverHost = host;
    prefs.begin(NS, false);
    prefs.putString("server", serverHost);
    prefs.end();
}

void AppConfig::saveRotation(uint8_t r) {
    rotation = r;
    prefs.begin(NS, false);
    prefs.putUChar("rot", rotation);
    prefs.end();
}

void AppConfig::recordResetReason() {
    esp_reset_reason_t r = esp_reset_reason();
    bool crash = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
                 r == ESP_RST_WDT || r == ESP_RST_BROWNOUT || r == ESP_RST_UNKNOWN;
    prefs.begin(NS, false);
    uint32_t prevUptime = prefs.getUInt("uptime", 0);
    if (r == ESP_RST_POWERON) {  // power lost (e.g. monitor USB port switched off)
        powerOnCount++;
        prefs.putUInt("pwr_n", powerOnCount);
    }
    if (crash) {
        crashCount++;
        lastCrashReason = (uint8_t)r;
        lastCrashUptimeS = prevUptime;
        prefs.putUInt("crash_n", crashCount);
        prefs.putUChar("crash_r", lastCrashReason);
        prefs.putUInt("crash_up", lastCrashUptimeS);
    }
    prefs.putUInt("uptime", 0);
    prefs.end();
    Serial.printf("reset reason %d (%s), previous run ~%lu s, crashes so far %lu\n",
                  (int)r, crash ? "CRASH" : "normal", (unsigned long)prevUptime,
                  (unsigned long)crashCount);
}

void AppConfig::saveUptime(uint32_t seconds) {
    prefs.begin(NS, false);
    prefs.putUInt("uptime", seconds);
    prefs.end();
}

void AppConfig::clearServer() {
    serverHost = "";
    prefs.begin(NS, false);
    prefs.remove("server");
    prefs.end();
}

void AppConfig::clearAll() {
    ssid = ""; password = ""; serverHost = "";
    knownCount = 0;
    prefs.begin(NS, false);
    prefs.clear();
    prefs.end();
}
