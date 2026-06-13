#include "ui_setup.h"
#include "ui_theme.h"
#include "discovery.h"
#include "app_config.h"
#include <WiFi.h>
#include <cstring>

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------
namespace {

constexpr int ROW_H = 46, ROW_GAP = 6, LIST_Y = 44, VISIBLE_ROWS = 6;

struct Net { String ssid; int32_t rssi; };

void drawHeader(Arduino_Canvas *c, const char *title, const char *right) {
    c->setTextSize(2);
    c->setTextColor(COL_ACCENT);
    c->setCursor(12, 12);
    c->print(title);
    if (right) {
        c->setTextColor(COL_DIM);
        c->setCursor(SCREEN_W - 12 - (int)strlen(right) * 12, 12);
        c->print(right);
    }
}

void drawSignalBars(Arduino_Canvas *c, int x, int y, int32_t rssi, uint16_t col) {
    int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
    for (int b = 0; b < 4; b++) {
        int h = 6 + b * 5;
        uint16_t bc = (b < bars) ? col : COL_FIELD;
        c->fillRect(x + b * 8, y + 21 - h, 5, h, bc);
    }
}

int collectNetworks(Net *out, int maxOut) {
    int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/false);
    int m = 0;
    for (int i = 0; i < n && m < maxOut; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        bool dup = false;
        for (int j = 0; j < m; j++)
            if (out[j].ssid == ssid) { dup = true; if (WiFi.RSSI(i) > out[j].rssi) out[j].rssi = WiFi.RSSI(i); }
        if (!dup) out[m++] = { ssid, WiFi.RSSI(i) };
    }
    // sort by rssi desc (tiny n: insertion sort)
    for (int i = 1; i < m; i++)
        for (int j = i; j > 0 && out[j].rssi > out[j - 1].rssi; j--) {
            Net t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
        }
    WiFi.scanDelete();
    return m;
}

void drawList(Arduino_Canvas *c, Net *nets, int n, int offset) {
    c->fillScreen(COL_BG);
    drawHeader(c, "CHOOSE WIFI", n > VISIBLE_ROWS ? "swipe" : nullptr);
    for (int r = 0; r < VISIBLE_ROWS; r++) {
        int i = offset + r;
        if (i >= n) break;
        int y = LIST_Y + r * (ROW_H + ROW_GAP);
        c->drawRoundRect(8, y, SCREEN_W - 16, ROW_H, 9, COL_PANEL);
        c->setTextSize(2);
        String unused;
        bool known = appConfig.findKnown(nets[i].ssid, unused);
        if (known) {  // remembered network: tap joins without typing
            c->setTextColor(COL_ACCENT);
            c->setCursor(16, y + 14);
            c->print("*");
        }
        c->setTextColor(COL_TEXT);
        c->setCursor(known ? 32 : 20, y + 14);
        String shown = nets[i].ssid.length() > 23 ? nets[i].ssid.substring(0, 23) : nets[i].ssid;
        c->print(shown);
        drawSignalBars(c, SCREEN_W - 56, y + 12, nets[i].rssi, COL_ACCENT);
    }
    c->flush();
}

}  // namespace

// ---------------------------------------------------------------------------
// Network list screen
// ---------------------------------------------------------------------------
String setupPickNetwork(Arduino_Canvas *c, TouchInput &touch) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    static Net nets[24];
    c->fillScreen(COL_BG);
    drawHeader(c, "CHOOSE WIFI", "scanning");
    c->flush();
    int n = collectNetworks(nets, 24);
    int offset = 0;
    uint32_t lastScan = millis();
    drawList(c, nets, n, offset);

    while (true) {
        TouchEvent ev = touch.poll();
        if (ev == TouchEvent::Tap) {
            int row = (touch.y() - LIST_Y) / (ROW_H + ROW_GAP);
            int i = offset + row;
            if (touch.y() >= LIST_Y && row >= 0 && row < VISIBLE_ROWS && i < n)
                return nets[i].ssid;
        }
        if (ev == TouchEvent::SwipeUp && offset + VISIBLE_ROWS < n) { offset++; drawList(c, nets, n, offset); }
        if (ev == TouchEvent::SwipeDown && offset > 0)              { offset--; drawList(c, nets, n, offset); }
        if (millis() - lastScan > 8000 && n == 0) {  // auto-rescan only while empty
            lastScan = millis();
            n = collectNetworks(nets, 24);
            drawList(c, nets, n, offset);
        }
        delay(10);
    }
}

// ---------------------------------------------------------------------------
// Connect + message screens
// ---------------------------------------------------------------------------
bool setupConnectWifi(Arduino_Canvas *c, const String &ssid, const String &password,
                      uint32_t timeoutMs) {
    c->fillScreen(COL_BG);
    drawHeader(c, "CONNECTING", nullptr);
    c->setTextSize(2);
    c->setTextColor(COL_TEXT);
    c->setCursor(12, 60);
    c->print(ssid);
    c->flush();

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), password.c_str());
    uint32_t start = millis();
    int dots = 0;
    while (millis() - start < timeoutMs) {
        if (WiFi.status() == WL_CONNECTED) return true;
        c->fillRect(12, 100, 200, 20, COL_BG);
        c->setCursor(12, 100);
        c->setTextColor(COL_ACCENT);
        for (int i = 0; i < (dots % 6) + 1; i++) c->print(".");
        c->flush();
        dots++;
        delay(400);
    }
    WiFi.disconnect();
    return false;
}

String setupDiscoverServer(Arduino_Canvas *c) {
    c->fillScreen(COL_BG);
    drawHeader(c, "FINDING YOUR PC", nullptr);
    c->setTextSize(2);
    c->setTextColor(COL_DIM);
    c->setCursor(12, 320);
    c->print("LibreHardwareMonitor, port 8085");
    c->flush();

    String ip = discoverLHM(8085, [c](int cur, int total, const char *host) {
        int barW = SCREEN_W - 80;
        c->fillRect(40, 160, barW, 12, COL_FIELD);
        c->fillRect(40, 160, barW * cur / total, 12, COL_ACCENT);
        c->fillRect(12, 200, SCREEN_W - 24, 22, COL_BG);
        c->setTextSize(2);
        c->setTextColor(COL_TEXT);
        c->setCursor(120, 200);
        c->print(host);
        c->flush();
    });
    return ip;
}

void setupShowMessage(Arduino_Canvas *c, TouchInput &touch, const char *line1, const char *line2) {
    c->fillScreen(COL_BG);
    c->setTextSize(2);
    c->setTextColor(COL_ACCENT);
    c->setCursor(12, 130); c->print(line1);
    c->setTextColor(COL_TEXT);
    c->setCursor(12, 170); if (line2) c->print(line2);
    c->setTextColor(COL_DIM);
    c->setCursor(12, 330); c->print("tap to continue");
    c->flush();
    while (touch.poll() != TouchEvent::Tap) delay(10);
}

// ---------------------------------------------------------------------------
// On-screen QWERTY keyboard
// ---------------------------------------------------------------------------
namespace {

// Three layers: 0 = lowercase, 1 = uppercase (shift), 2 = symbols.
static const char *KB_ROW1[3] = {"qwertyuiop", "QWERTYUIOP", "1234567890"};
static const char *KB_ROW2[3] = {"asdfghjkl",  "ASDFGHJKL",  "-_.:;!?@#"};
static const char *KB_ROW3[3] = {"zxcvbnm",    "ZXCVBNM",    "+*/=()[]&"};

constexpr int KB_Y = 130;          // keyboard block top
constexpr int KEY_H = 48, KEY_GAP = 4;

struct Key { int x, y, w, h; char ch; uint8_t action; };  // action: 0 char, 1 shift, 2 layer, 3 backspace, 4 space, 5 ok, 6 back
constexpr int MAX_KEYS = 40;

int buildKeys(Key *keys, int layer) {
    int n = 0;
    const char *rows[3] = {KB_ROW1[layer], KB_ROW2[layer], KB_ROW3[layer]};
    for (int r = 0; r < 3; r++) {
        int len = (int)strlen(rows[r]);
        int keyW = (SCREEN_W - 8 - (len - 1) * KEY_GAP - (r == 2 ? 2 * 52 : 0)) / len;
        int x = 4 + (r == 1 ? keyW / 2 : 0) + (r == 2 ? 52 : 0);
        int y = KB_Y + r * (KEY_H + KEY_GAP);
        for (int i = 0; i < len; i++) {
            keys[n++] = { x, y, keyW, KEY_H, rows[r][i], 0 };
            x += keyW + KEY_GAP;
        }
        if (r == 2) {  // shift left of Z row, backspace right
            keys[n++] = { 4, y, 44, KEY_H, 0, 1 };
            keys[n++] = { SCREEN_W - 4 - 44, y, 44, KEY_H, 0, 3 };
        }
    }
    int y = KB_Y + 3 * (KEY_H + KEY_GAP);
    keys[n++] = { 4,   y, 70,  KEY_H, 0, 2 };                      // 12# / abc
    keys[n++] = { 78,  y, 90,  KEY_H, 0, 6 };                      // back
    keys[n++] = { 172, y, 152, KEY_H, ' ', 4 };                    // space
    keys[n++] = { 328, y, SCREEN_W - 4 - 328, KEY_H, 0, 5 };       // OK
    return n;
}

void drawKeyboard(Arduino_Canvas *c, const String &ssid, const String &entered,
                  Key *keys, int nKeys, int layer) {
    c->fillScreen(COL_BG);
    c->setTextSize(2);
    c->setTextColor(COL_ACCENT);
    c->setCursor(8, 8);
    c->print(ssid.length() > 22 ? ssid.substring(0, 22) : ssid);

    // password field (last 24 chars visible) with cursor
    c->drawRoundRect(8, 40, SCREEN_W - 16, 44, 8, COL_PANEL);
    c->setTextColor(COL_TEXT);
    c->setCursor(18, 52);
    String shown = entered.length() > 24 ? entered.substring(entered.length() - 24) : entered;
    c->print(shown);
    int cx = 18 + (int)shown.length() * 12;
    c->drawFastVLine(cx + 2, 48, 28, COL_ACCENT);

    for (int i = 0; i < nKeys; i++) {
        const Key &k = keys[i];
        uint16_t border = (k.action == 5) ? COL_ACCENT : COL_PANEL;
        c->drawRoundRect(k.x, k.y, k.w, k.h, 7, border);
        c->setTextColor(k.action == 5 ? COL_ACCENT : COL_TEXT);
        c->setTextSize(2);
        const char *lbl = nullptr;
        char chbuf[2] = {k.ch, 0};
        switch (k.action) {
            case 0: lbl = chbuf; break;
            case 1: lbl = "^";  break;
            case 2: lbl = (layer == 2) ? "abc" : "12#"; break;
            case 3: lbl = "<x"; break;
            case 4: lbl = "_"; break;
            case 5: lbl = "OK"; break;
            case 6: lbl = "back"; break;
        }
        int tw = (int)strlen(lbl) * 12;
        c->setCursor(k.x + (k.w - tw) / 2, k.y + 16);
        c->print(lbl);
    }
    c->flush();
}

}  // namespace

String setupEnterPassword(Arduino_Canvas *c, TouchInput &touch, const String &ssid) {
    String entered = "";
    int layer = 0;
    bool shiftOnce = false;
    static Key keys[MAX_KEYS];
    int nKeys = buildKeys(keys, layer);
    drawKeyboard(c, ssid, entered, keys, nKeys, layer);

    while (true) {
        TouchEvent ev = touch.poll();
        if (ev != TouchEvent::Tap) { delay(5); continue; }
        for (int i = 0; i < nKeys; i++) {
            const Key &k = keys[i];
            if (touch.x() < k.x || touch.x() >= k.x + k.w ||
                touch.y() < k.y || touch.y() >= k.y + k.h) continue;
            switch (k.action) {
                case 0: case 4:
                    entered += k.ch;
                    if (shiftOnce && layer == 1) { layer = 0; shiftOnce = false; nKeys = buildKeys(keys, layer); }
                    break;
                case 1:  // shift toggles lower<->upper
                    layer = (layer == 1) ? 0 : 1; shiftOnce = (layer == 1);
                    nKeys = buildKeys(keys, layer);
                    break;
                case 2:  // symbols toggle
                    layer = (layer == 2) ? 0 : 2; shiftOnce = false;
                    nKeys = buildKeys(keys, layer);
                    break;
                case 3: if (entered.length()) entered.remove(entered.length() - 1); break;
                case 5: return entered;       // OK
                case 6: return "";            // back
            }
            drawKeyboard(c, ssid, entered, keys, nKeys, layer);
            break;
        }
    }
}
