#include "ui_dashboard.h"
#include "ui_theme.h"
#include <cmath>
#include <cstring>
#include <cstdio>

namespace {

// Layout: two graph panels + bottom tile row, 8 px margins/gaps.
//   panel1 y=8 h=124, panel2 y=140 h=124, bottom y=272 h=88
constexpr int M = 8;
constexpr int PANEL_W = SCREEN_W - 2 * M;   // 432
constexpr int PANEL_H = 124;
constexpr int P1_Y = 8, P2_Y = 140, BOT_Y = 272, BOT_H = 88;

void drawValueText(Arduino_Canvas *c, int xRight, int y, const char *txt,
                   uint16_t color, uint8_t size) {
    int w = strlen(txt) * 6 * size;
    c->setTextColor(color);
    c->setTextSize(size);
    c->setCursor(xRight - w, y);
    c->print(txt);
}


void drawGraphPanel(Arduino_Canvas *c, int py, const char *label,
                    uint16_t color, uint16_t fillColor,
                    const History &load, const History &temp,
                    float loadNow, float tempNow, bool offline) {
    c->drawRoundRect(M, py, PANEL_W, PANEL_H, 10, COL_PANEL);

    // Graph area inside the panel, below the header line.
    const int gx = M + 8, gy = py + 30, gw = PANEL_W - 16, gh = PANEL_H - 38;
    uint16_t loadCol = offline ? COL_DIM : fillColor;
    uint16_t tempCol = offline ? COL_DIM : COL_TEMP;

    // Utilization: filled vertical bars, one per sample (~4 px wide).
    for (int i = 0; i < load.count; i++) {
        float v = load.at(i);
        if (std::isnan(v)) continue;
        int x = gx + (int)((float)i * gw / (HISTORY_LEN - 1));
        int hpx = (int)(v / 100.0f * gh);
        if (hpx > gh) hpx = gh;
        if (hpx < 1) hpx = 1;
        c->drawFastVLine(x, gy + gh - hpx, hpx, loadCol);
        c->drawFastVLine(x + 1, gy + gh - hpx, hpx, loadCol);
        c->drawFastVLine(x + 2, gy + gh - hpx, hpx, loadCol);
        c->drawFastVLine(x + 3, gy + gh - hpx, hpx, loadCol);
    }
    // Temperature: 2-px polyline over the same 0-100 scale.
    for (int i = 1; i < temp.count; i++) {
        float a = temp.at(i - 1), b = temp.at(i);
        if (std::isnan(a) || std::isnan(b)) continue;
        int x0 = gx + (int)((float)(i - 1) * gw / (HISTORY_LEN - 1));
        int x1 = gx + (int)((float)i * gw / (HISTORY_LEN - 1));
        int y0 = gy + gh - (int)(a / 100.0f * gh);
        int y1 = gy + gh - (int)(b / 100.0f * gh);
        c->drawLine(x0, y0, x1, y1, tempCol);
        c->drawLine(x0, y0 + 1, x1, y1 + 1, tempCol);
    }

    // Header: label left, current values right.
    c->setTextSize(2);
    c->setTextColor(offline ? COL_DIM : color);
    c->setCursor(M + 12, py + 8);
    c->print(label);

    char lbuf[16], tbuf[16];
    if (std::isnan(loadNow)) snprintf(lbuf, sizeof(lbuf), "--%%");
    else snprintf(lbuf, sizeof(lbuf), "%.0f%%", loadNow);
    if (std::isnan(tempNow)) snprintf(tbuf, sizeof(tbuf), "--");
    else snprintf(tbuf, sizeof(tbuf), "%.0f", tempNow);

    // temp (orange, with degree circle), right-aligned; load to its left
    int tw = (int)(strlen(tbuf) * 6 + 6) * 3;
    drawValueText(c, M + PANEL_W - 12, py + 6, tbuf,
                  offline ? COL_DIM : COL_TEMP, 3);
    c->drawCircle(M + PANEL_W - 12 - 2, py + 8, 3, offline ? COL_DIM : COL_TEMP);
    drawValueText(c, M + PANEL_W - 12 - tw - 14, py + 6, lbuf,
                  offline ? COL_DIM : color, 3);
}

void drawArrow(Arduino_Canvas *c, int x, int y, bool down, uint16_t col) {
    if (down) c->fillTriangle(x, y, x + 10, y, x + 5, y + 10, col);
    else      c->fillTriangle(x, y + 10, x + 10, y + 10, x + 5, y, col);
}

}  // namespace

// Bytes/s -> compact display: "713K" (KB/s) below 1 MB/s, "2.3M" (MB/s) above.
// 1024-based units, matching LibreHardwareMonitor's own formatting.
void fmtBytesPerSec(char *buf, size_t n, float bps) {
    if (std::isnan(bps)) { snprintf(buf, n, "--"); return; }
    float kb = bps / 1024.0f;
    if (kb >= 1024.0f) snprintf(buf, n, "%.1fM", kb / 1024.0f);
    else snprintf(buf, n, "%.0fK", kb);
}

// OFFLINE badge (when offline) or device battery (when online), top center.
void dashboardChrome(Arduino_Canvas *c, bool offline, int batteryPct) {
    if (offline) {
        const char *txt = "OFFLINE";
        int w = (int)strlen(txt) * 6 * 2 + 20;
        int x = (SCREEN_W - w) / 2;
        c->fillRoundRect(x, 2, w, 22, 6, COL_ACCENT);
        c->setTextSize(2);
        c->setTextColor(COL_TEXT);
        c->setCursor(x + 10, 6);
        c->print(txt);
    } else if (batteryPct > 0) {
        char bbuf[8];
        snprintf(bbuf, sizeof(bbuf), "%d%%", batteryPct);
        int w = (int)strlen(bbuf) * 6 * 2;
        c->setTextSize(2);
        c->setTextColor(batteryPct <= 15 ? COL_ACCENT : COL_DIM);
        c->setCursor((SCREEN_W - w) / 2, 10);
        c->print(bbuf);
    }
}

void dashboardDraw(Arduino_Canvas *c, const Stats &s, const DashHistories &h, bool offline,
                   int batteryPct) {
    c->fillScreen(COL_BG);

    drawGraphPanel(c, P1_Y, "CPU", COL_CPU, COL_CPU_FILL,
                   h.cpuLoad, h.cpuTemp, s.cpuLoad, s.cpuTemp, offline);
    drawGraphPanel(c, P2_Y, "GPU", COL_GPU, COL_GPU_FILL,
                   h.gpuLoad, h.gpuTemp, s.gpuLoad, s.gpuTemp, offline);

    // Bottom row: RAM tile (left, 45%) + NET tile (right, 55%).
    const int ramW = (PANEL_W - 8) * 45 / 100;
    const int netX = M + ramW + 8, netW = PANEL_W - ramW - 8;
    c->drawRoundRect(M, BOT_Y, ramW, BOT_H, 10, COL_PANEL);
    c->drawRoundRect(netX, BOT_Y, netW, BOT_H, 10, COL_PANEL);

    c->setTextSize(2);
    c->setTextColor(offline ? COL_DIM : COL_RAM);
    c->setCursor(M + 12, BOT_Y + 8);
    c->print("RAM");
    char buf[32];
    if (std::isnan(s.ramUsedGB) || std::isnan(s.ramTotalGB)) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.1f/%.0f GB", s.ramUsedGB, s.ramTotalGB);
    c->setTextSize(3);
    c->setTextColor(offline ? COL_DIM : COL_TEXT);
    c->setCursor(M + 12, BOT_Y + 38);
    c->print(buf);

    c->setTextSize(2);
    c->setTextColor(offline ? COL_DIM : COL_NET);
    c->setCursor(netX + 12, BOT_Y + 8);
    c->print("NET B/s");
    uint16_t vc = offline ? COL_DIM : COL_TEXT;
    drawArrow(c, netX + 12, BOT_Y + 42, true, offline ? COL_DIM : COL_NET);
    fmtBytesPerSec(buf, sizeof(buf), s.netDownBps);
    c->setTextSize(3); c->setTextColor(vc);
    c->setCursor(netX + 28, BOT_Y + 38); c->print(buf);
    drawArrow(c, netX + netW / 2 + 4, BOT_Y + 42, false, offline ? COL_DIM : COL_NET);
    fmtBytesPerSec(buf, sizeof(buf), s.netUpBps);
    c->setCursor(netX + netW / 2 + 20, BOT_Y + 38); c->print(buf);

    dashboardChrome(c, offline, batteryPct);
}
