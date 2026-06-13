#pragma once
#include <Arduino.h>
#include "stats_extract.h"

// Fetches and parses data.json from the configured server.
class StatsClient {
public:
    void begin(const String &host, uint16_t port = 8085);
    // Blocking (~150-600 ms). Returns true and fills `out` on success.
    bool fetch(Stats &out);

private:
    String _url;
};
