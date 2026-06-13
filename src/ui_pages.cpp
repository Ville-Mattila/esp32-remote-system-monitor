#include "ui_pages.h"
#include "ui_theme.h"
#include "app_config.h"
#include "pin_config.h"
#include <WiFi.h>
#include <time.h>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace {

constexpr int M = 8;
constexpr int PANEL_W = SCREEN_W - 2 * M;  // 432

// One auto-scaled bytes/s history graph panel.
void drawNetPanel(Arduino_Canvas *c, int py, int ph, const char *label,
                  const History &hist, float nowBps, bool offline) {
    c->drawRoundRect(M, py, PANEL_W, ph, 10, COL_PANEL);

    const int gx = M + 8, gy = py + 32, gw = PANEL_W - 16, gh = ph - 40;

    // Scale to the window's max, with a 10 KB/s floor so idle isn't noise.
    float maxv = 10.0f * 1024;
    for (int i = 0; i < hist.count; i++) {
        float v = hist.at(i);
        if (!std::isnan(v) && v > maxv) maxv = v;
    }
    maxv *= 1.1f;

    uint16_t fill = offline ? COL_DIM : COL_NET_FILL;
    for (int i = 0; i < hist.count; i++) {
        float v = hist.at(i);
        if (std::isnan(v)) continue;
        int x = gx + (int)((float)i * gw / (HISTORY_LEN - 1));
        int hpx = (int)(v / maxv * gh);
        if (hpx > gh) hpx = gh;
        if (hpx < 1) hpx = 1;
        for (int k = 0; k < 4; k++)
            c->drawFastVLine(x + k, gy + gh - hpx, hpx, fill);
    }

    char buf[16];
    c->setTextSize(2);
    c->setTextColor(offline ? COL_DIM : COL_NET);
    c->setCursor(M + 12, py + 8);
    c->print(label);
    // window max, small, next to the label
    fmtBytesPerSec(buf, sizeof(buf), maxv / 1.1f);
    c->setTextColor(COL_DIM);
    c->setCursor(M + 12 + 7 * 12, py + 8);
    c->printf("max %s", buf);
    // current value, big, right-aligned
    fmtBytesPerSec(buf, sizeof(buf), nowBps);
    int w = (int)strlen(buf) * 6 * 3;
    c->setTextSize(3);
    c->setTextColor(offline ? COL_DIM : COL_TEXT);
    c->setCursor(M + PANEL_W - 12 - w, py + 6);
    c->print(buf);
}

void bigMetricRow(Arduino_Canvas *c, int y, const char *label, uint16_t color,
                  float load, float temp, bool offline) {
    c->setTextSize(3);
    c->setTextColor(offline ? COL_DIM : color);
    c->setCursor(M + 8, y);
    c->print(label);

    char buf[8];
    if (std::isnan(load)) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.0f", load);
    c->setTextSize(9);  // 54x72 px digits
    c->setTextColor(offline ? COL_DIM : COL_TEXT);
    int w = (int)strlen(buf) * 6 * 9;
    c->setCursor(M + 200 - w, y);
    c->print(buf);
    c->setTextSize(4);
    c->setTextColor(offline ? COL_DIM : color);
    c->setCursor(M + 206, y + 40);
    c->print("%");

    if (std::isnan(temp)) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.0f", temp);
    c->setTextSize(6);
    c->setTextColor(offline ? COL_DIM : COL_TEMP);
    int tw = (int)strlen(buf) * 6 * 6;
    c->setCursor(SCREEN_W - M - 40 - tw, y + 12);
    c->print(buf);
    c->drawCircle(SCREEN_W - M - 32, y + 16, 5, offline ? COL_DIM : COL_TEMP);
    c->setTextSize(4);
    c->setCursor(SCREEN_W - M - 26, y + 18);
    c->print("C");
}

}  // namespace

void netViewDraw(Arduino_Canvas *c, const Stats &s, const DashHistories &h,
                 bool offline, int batteryPct) {
    c->fillScreen(COL_BG);
    drawNetPanel(c, 8, 168, "DOWN", h.netDown, s.netDownBps, offline);
    drawNetPanel(c, 184, 168, "UP", h.netUp, s.netUpBps, offline);
    dashboardChrome(c, offline, batteryPct);
}

void focusViewDraw(Arduino_Canvas *c, const Stats &s, bool offline, int batteryPct) {
    c->fillScreen(COL_BG);
    bigMetricRow(c, 50, "CPU", COL_CPU, s.cpuLoad, s.cpuTemp, offline);
    c->drawFastHLine(M, 184, PANEL_W, COL_PANEL);
    bigMetricRow(c, 230, "GPU", COL_GPU, s.gpuLoad, s.gpuTemp, offline);
    dashboardChrome(c, offline, batteryPct);
}

void infoViewDraw(Arduino_Canvas *c, bool offline, int batteryPct) {
    c->fillScreen(COL_BG);

    struct tm t;
    char buf[48];
    if (getLocalTime(&t, 10)) {
        snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        c->setTextSize(9);
        c->setTextColor(COL_TEXT);
        int w = 5 * 6 * 9;
        c->setCursor((SCREEN_W - w) / 2, 56);
        c->print(buf);
        snprintf(buf, sizeof(buf), "%02d.%02d.%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
        c->setTextSize(3);
        c->setTextColor(COL_DIM);
        w = (int)strlen(buf) * 6 * 3;
        c->setCursor((SCREEN_W - w) / 2, 144);
        c->print(buf);
    } else {
        c->setTextSize(4);
        c->setTextColor(COL_DIM);
        c->setCursor((SCREEN_W - 5 * 6 * 4) / 2, 80);
        c->print("--:--");
    }

    c->setTextSize(2);
    int y = 210;
    auto line = [&](const char *l, const String &v) {
        c->setTextColor(COL_ACCENT);
        c->setCursor(M + 8, y);
        c->print(l);
        c->setTextColor(COL_TEXT);
        c->setCursor(M + 8 + 12 * 9, y);
        c->print(v);
        y += 26;
    };
    snprintf(buf, sizeof(buf), "%s  (%d dBm)", WiFi.SSID().c_str(), (int)WiFi.RSSI());
    line("WiFi", buf);
    line("Device IP", WiFi.localIP().toString());
    line("PC", appConfig.serverHost.length() ? appConfig.serverHost : String("--"));
    String batt = batteryPct > 0 ? String(batteryPct) + "%" : String("--");
    snprintf(buf, sizeof(buf), "%s   FW %s", batt.c_str(), FW_VERSION);
    line("Battery", buf);
    if (offline) {
        c->setTextColor(COL_ACCENT);
        c->setCursor(M + 8, y + 6);
        c->print("PC is offline");
    }
}

void drawPageDots(Arduino_Canvas *c, int page, int nPages) {
    int total = nPages * 14 - 8;
    int x = (SCREEN_W - total) / 2;
    for (int i = 0; i < nPages; i++) {
        if (i == page) c->fillCircle(x + i * 14 + 3, 363, 3, COL_ACCENT);
        else           c->fillCircle(x + i * 14 + 3, 363, 2, COL_DIM);
    }
}

void presentCanvas(Arduino_Canvas *c) {
    // Burn-in protection: drift the whole frame by 0-1 px on each axis,
    // advancing every 3 minutes. Imperceptible, but no pixel shows a static
    // image forever. The exposed top row / left column are blacked out.
    static const int8_t OFF[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    static int idx = 0;
    static uint32_t lastShift = 0;
    if (millis() - lastShift > 180000) {
        lastShift = millis();
        idx = (idx + 1) & 3;
    }
    int ox = OFF[idx][0], oy = OFF[idx][1];
    if (ox || oy) {
        uint16_t *fb = c->getFramebuffer();
        size_t shift = (size_t)oy * LCD_WIDTH + ox;
        memmove(fb + shift, fb, ((size_t)LCD_WIDTH * LCD_HEIGHT - shift) * 2);
        if (oy) memset(fb, 0, (size_t)oy * LCD_WIDTH * 2);
        if (ox) for (int yy = oy; yy < LCD_HEIGHT; yy++) fb[(size_t)yy * LCD_WIDTH] = 0;
    }
    c->flush();
}
