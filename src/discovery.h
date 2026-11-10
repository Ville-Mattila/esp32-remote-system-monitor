#pragma once
#include <Arduino.h>
#include <functional>

// Sweeps the device's own /24 subnet for a LibreHardwareMonitor web server.
// Returns the host IP as a string ("192.168.1.50") or "" if none found.
// progress(current, total, hostBeingChecked) is called as the sweep advances.
String discoverLHM(uint16_t port,
                   std::function<void(int current, int total, const char *host)> progress);

// True if host:port answers data.json with LHM-shaped JSON. Used by discovery
// and by the dashboard to validate a saved server.
// If `why` is given, a failure reason is written there (e.g. "HTTP -1 (1201 ms)").
bool verifyLHM(const String &host, uint16_t port, String *why = nullptr);
