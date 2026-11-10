#pragma once
#include <Arduino_GFX_Library.h>
#include "touch_input.h"

// Blocking setup screens. Each pumps touch + redraws internally and returns
// when the user completes or backs out of that screen.

// Scans and lists networks (strongest first; remembered ones marked with *).
// Returns chosen SSID.
String setupPickNetwork(Arduino_Canvas *c, TouchInput &touch);

// On-screen QWERTY password entry. Returns the password, or "" if user tapped back.
String setupEnterPassword(Arduino_Canvas *c, TouchInput &touch, const String &ssid);

// Joins the network, drawing status. True on success (got IP) within timeoutMs.
bool setupConnectWifi(Arduino_Canvas *c, const String &ssid, const String &password,
                      uint32_t timeoutMs = 15000);

// Full-screen message dismissed by a tap. With timeoutMs > 0 it also returns
// on its own after that long (footer shows `footer`). Returns true if tapped.
bool setupShowMessage(Arduino_Canvas *c, TouchInput &touch, const char *line1, const char *line2,
                      uint32_t timeoutMs = 0, const char *footer = "tap to continue");

// Runs the discovery sweep with a progress screen. Returns server IP or "".
String setupDiscoverServer(Arduino_Canvas *c);
