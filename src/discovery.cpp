#include "discovery.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

bool verifyLHM(const String &host, uint16_t port, String *why) {
    uint32_t t0 = millis();
    HTTPClient http;
    WiFiClient client;
    http.setConnectTimeout(1200);
    http.setTimeout(1500);
    http.useHTTP10(true);
    if (!http.begin(client, "http://" + host + ":" + String(port) + "/data.json")) {
        if (why) *why = "bad URL";
        return false;
    }
    int code = http.GET();
    bool ok = false;
    if (code == 200) {
        // Read just the first chunk and look for the LHM root marker.
        uint8_t buf[96];
        int n = http.getStreamPtr()->readBytes(buf, sizeof(buf) - 1);
        buf[n > 0 ? n : 0] = 0;
        ok = strstr((const char *)buf, "\"Text\":\"Sensor\"") != nullptr ||
             strstr((const char *)buf, "\"id\":0") != nullptr;
    }
    http.end();
    if (!ok && why) {
        // Negative codes are HTTPClient errors: -1 = couldn't connect, -11 = read timeout.
        *why = code == 200 ? String("not LHM") : "HTTP " + String(code);
        *why += " (" + String(millis() - t0) + " ms)";
    }
    return ok;
}

// Non-blocking TCP connect sweep. Batches of 8: lwip allows only ~10 sockets
// total, so larger batches silently fail to open and skip addresses. Candidate
// hosts are verified only AFTER all batch sockets are closed, so the HTTP
// verification connection always has a free socket.
String discoverLHM(uint16_t port,
                   std::function<void(int, int, const char *)> progress) {
    uint32_t local = (uint32_t)WiFi.localIP();      // octets little-endian
    uint32_t base = local & 0x00FFFFFF;             // x.y.z.0
    uint8_t selfLast = (local >> 24) & 0xFF;
    Serial.printf("discovery: own IP %s, sweeping /24\n",
                  WiFi.localIP().toString().c_str());

    constexpr int BATCH = 8;
    for (int start = 1; start <= 254; start += BATCH) {
        int socks[BATCH];
        uint8_t lasts[BATCH];
        int nb = 0;
        for (int last = start; last < start + BATCH && last <= 254; last++) {
            if (last == selfLast) continue;
            int s = socket(AF_INET, SOCK_STREAM, 0);
            if (s < 0) { Serial.printf("discovery: socket() failed at .%d\n", last); continue; }
            fcntl(s, F_SETFL, O_NONBLOCK);
            struct sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = base | ((uint32_t)last << 24);
            connect(s, (struct sockaddr *)&addr, sizeof(addr));  // EINPROGRESS
            socks[nb] = s; lasts[nb] = last; nb++;
        }

        IPAddress probe(base | ((uint32_t)start << 24));
        progress(start, 254, probe.toString().c_str());

        // Wait up to 500 ms for connects, remembering which hosts accepted.
        uint8_t candidates[BATCH];
        int nc = 0;
        uint32_t t0 = millis();
        while (millis() - t0 < 500) {
            bool anyOpen = false;
            for (int i = 0; i < nb; i++) {
                if (socks[i] < 0) continue;
                anyOpen = true;
                fd_set wset; FD_ZERO(&wset); FD_SET(socks[i], &wset);
                struct timeval tv = {0, 1000};
                if (select(socks[i] + 1, nullptr, &wset, nullptr, &tv) > 0) {
                    int err = 0; socklen_t len = sizeof(err);
                    getsockopt(socks[i], SOL_SOCKET, SO_ERROR, &err, &len);
                    close(socks[i]); socks[i] = -1;
                    if (err == 0) candidates[nc++] = lasts[i];
                }
            }
            if (!anyOpen) break;
            delay(5);
        }
        // Free every remaining socket BEFORE verification needs one.
        for (int i = 0; i < nb; i++) if (socks[i] >= 0) close(socks[i]);

        for (int i = 0; i < nc; i++) {
            IPAddress ip(base | ((uint32_t)candidates[i] << 24));
            Serial.printf("discovery: %s accepted, verifying...\n", ip.toString().c_str());
            if (verifyLHM(ip.toString(), port)) {
                Serial.printf("discovery: FOUND %s\n", ip.toString().c_str());
                return ip.toString();
            }
            Serial.println("discovery: not LHM");
        }
    }
    Serial.println("discovery: sweep finished, nothing found");
    return "";
}
