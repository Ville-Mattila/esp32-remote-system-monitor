#include "stats_client.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// data.json is ~56 KB; parse it into PSRAM, not internal heap.
struct PsramAllocator : ArduinoJson::Allocator {
    void *allocate(size_t n) override { return ps_malloc(n); }
    void deallocate(void *p) override { free(p); }
    void *reallocate(void *p, size_t n) override { return ps_realloc(p, n); }
};
static PsramAllocator psramAlloc;

void StatsClient::begin(const String &host, uint16_t port) {
    _url = "http://" + host + ":" + String(port) + "/data.json";
}

bool StatsClient::fetch(Stats &out) {
    HTTPClient http;
    WiFiClient client;
    http.setConnectTimeout(1500);
    http.setTimeout(2500);
    http.useHTTP10(true);  // plain stream (no chunked encoding) for ArduinoJson
    if (!http.begin(client, _url)) return false;
    int code = http.GET();
    if (code != 200) { http.end(); return false; }

    JsonDocument doc(&psramAlloc);
    DeserializationError err = deserializeJson(
        doc, http.getStream(), DeserializationOption::NestingLimit(20));
    http.end();
    if (err) {
        Serial.printf("stats: json error %s\n", err.c_str());
        return false;
    }
    Stats fresh;
    int found = extractStats(doc.as<JsonVariantConst>(), fresh);
    if (found == 0) return false;
    out = fresh;
    return true;
}
