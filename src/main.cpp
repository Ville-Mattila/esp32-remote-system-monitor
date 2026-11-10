#include <Arduino.h>
#include <WiFi.h>
#include "board_init.h"
#include "app_config.h"
#include "touch_input.h"
#include "ui_setup.h"
#include "ui_dashboard.h"
#include "stats_client.h"
#include "discovery.h"
#include "ui_theme.h"
#include "ui_pages.h"
#include "battery.h"
#include <time.h>

static TouchInput touch;
static StatsClient statsClient;
static DashHistories hist;

// Runs the interactive setup until WiFi is connected; saves credentials.
// Remembered networks (marked * in the list) join without retyping.
static void runWifiSetup() {
    while (true) {
        String ssid = setupPickNetwork(canvas, touch);

        String pass;
        if (appConfig.findKnown(ssid, pass)) {
            if (setupConnectWifi(canvas, ssid, pass)) {
                appConfig.saveWifi(ssid, pass);  // bump to most-recent
                return;
            }
            // Stale saved password: fall through to the keyboard.
            setupShowMessage(canvas, touch, "Saved password didn't work", ssid.c_str());
        }

        pass = setupEnterPassword(canvas, touch, ssid);
        if (pass.length() == 0) continue;  // back -> rescan
        if (setupConnectWifi(canvas, ssid, pass)) {
            appConfig.saveWifi(ssid, pass);
            return;
        }
        setupShowMessage(canvas, touch, "Couldn't connect", "Check the password");
    }
}

// Ensures a server is known and answering; runs discovery as needed.
static bool ensureServer() {
    if (appConfig.hasServer() && verifyLHM(appConfig.serverHost, 8085)) return true;
    String ip = setupDiscoverServer(canvas);
    if (ip.length() == 0) return false;
    appConfig.saveServer(ip);
    return true;
}

// Dashboard loop with swipeable pages. Returns when the user long-presses
// (to re-enter setup).
static void runDashboard() {
    statsClient.begin(appConfig.serverHost);
    Stats last;
    int failStreak = 0;
    int battPct = -1;
    int page = 0;                 // 0 dash, 1 net, 2 focus, 3 clock/info
    constexpr int N_PAGES = 4;
    bool offline = false;
    uint32_t lastFetch = 0, lastRediscover = 0, lastBatt = 0, lastTapMs = 0;
    static uint32_t lastUptimeSave = 0;

    auto drawPage = [&]() {
        switch (page) {
            case 0: dashboardDraw(canvas, last, hist, offline, battPct); break;
            case 1: netViewDraw(canvas, last, hist, offline, battPct); break;
            case 2: focusViewDraw(canvas, last, offline, battPct); break;
            case 3: infoViewDraw(canvas, offline, battPct); break;
        }
        drawPageDots(canvas, page, N_PAGES);
        presentCanvas(canvas);
    };

    while (true) {
        TouchEvent ev = touch.poll();
        if (ev == TouchEvent::LongPress) return;
        if (ev == TouchEvent::SwipeLeft)  { page = (page + 1) % N_PAGES; drawPage(); }
        if (ev == TouchEvent::SwipeRight) { page = (page + N_PAGES - 1) % N_PAGES; drawPage(); }
        if (ev == TouchEvent::Tap) {
            if (millis() - lastTapMs < 400) {  // double-tap: flip 180°
                uiRotation = (uiRotation == 1) ? 3 : 1;
                canvas->setRotation(uiRotation);
                appConfig.saveRotation(uiRotation);
                drawPage();
                lastTapMs = 0;
            } else {
                lastTapMs = millis();
            }
        }

        // Remember uptime every 10 min (cheap on flash wear) so the crash
        // log can say how long the device ran before a crash.
        if (millis() - lastUptimeSave >= 600000) {
            lastUptimeSave = millis();
            appConfig.saveUptime(millis() / 1000);
        }

        if (millis() - lastBatt >= 10000 || lastBatt == 0) {  // fuel gauge is slow-moving
            lastBatt = millis();
            battPct = batteryPercent();
        }

        if (millis() - lastFetch >= 1000) {
            lastFetch = millis();
            Stats s;
            if (statsClient.fetch(s)) {
                failStreak = 0;
                offline = false;
                last = s;
                hist.cpuLoad.push(s.cpuLoad); hist.cpuTemp.push(s.cpuTemp);
                hist.gpuLoad.push(s.gpuLoad); hist.gpuTemp.push(s.gpuTemp);
                hist.netDown.push(s.netDownBps); hist.netUp.push(s.netUpBps);
                drawPage();
            } else {
                failStreak++;
                offline = true;
                drawPage();
                // After repeated failures the PC's IP may have changed:
                // re-discover at most every 30 s (sweep blocks ~10-30 s;
                // graphs are paused while offline anyway).
                if (failStreak >= 3 && WiFi.status() == WL_CONNECTED &&
                    millis() - lastRediscover > 30000) {
                    lastRediscover = millis();
                    String ip = setupDiscoverServer(canvas);
                    if (ip.length()) {
                        appConfig.saveServer(ip);
                        statsClient.begin(ip);
                        failStreak = 0;
                    }
                }
            }
        }

        if (WiFi.status() != WL_CONNECTED) {
            // setAutoReconnect handles the rejoin; just reflect it in the UI.
            offline = true;
            drawPage();
            delay(500);
        }
        delay(5);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("PC stats display booting...");
    appConfig.load();  // before boardInit: it provides the saved rotation
    appConfig.recordResetReason();
    uiRotation = appConfig.rotation;
    boardInit();
    touch.begin(Touch.get());
    Serial.printf("config: ssid='%s' server='%s' rot=%u\n",
                  appConfig.ssid.c_str(), appConfig.serverHost.c_str(), uiRotation);
}

void loop() {
    if (!appConfig.hasWifi()) {
        runWifiSetup();
    } else if (!setupConnectWifi(canvas, appConfig.ssid, appConfig.password)) {
        // Router may just be down (e.g. after a power cut): retry on our own,
        // and only go to the network picker if the user asks for it.
        if (setupShowMessage(canvas, touch, "Saved WiFi unreachable", appConfig.ssid.c_str(),
                             30000, "retrying in 30 s - tap to pick")) {
            runWifiSetup();
        } else {
            return;  // loop() runs again -> reconnect attempt
        }
    }

    // Clock for the info page (Finland timezone, DST-aware).
    configTzTime("EET-2EEST,M3.5.0/3,M10.5.0/4", "pool.ntp.org");

    // Keep retrying on our own: after a reboot the PC may be asleep or LHM
    // not started yet. A tap retries immediately.
    while (!ensureServer()) {
        setupShowMessage(canvas, touch,
                         "No LibreHardwareMonitor found",
                         "Is the PC on?",
                         30000, "retrying in 30 s - tap to retry now");
    }

    runDashboard();  // returns on long-press

    // Long-press: back to WiFi setup (keeps saved config until replaced).
    runWifiSetup();
    appConfig.clearServer();  // network changed -> server IP may differ
}
