#pragma once
#include <Arduino_GFX_Library.h>
#include "stats_extract.h"
#include "ui_dashboard.h"

#define FW_VERSION "v1.3.1"

// Additional swipeable view pages. Like dashboardDraw, these render into the
// canvas without flushing; the caller presents via presentCanvas().

// Full-size download/upload history graphs with auto-scaling.
void netViewDraw(Arduino_Canvas *c, const Stats &s, const DashHistories &h,
                 bool offline, int batteryPct);

// Giant CPU/GPU numbers readable from across the room.
void focusViewDraw(Arduino_Canvas *c, const Stats &s, bool offline, int batteryPct);

// Clock (NTP) + connection/device info. Useful even when the PC is off.
void infoViewDraw(Arduino_Canvas *c, bool offline, int batteryPct);

// Page indicator dots, bottom center. Call after the page body.
void drawPageDots(Arduino_Canvas *c, int page, int nPages);

// Flushes the canvas to the panel with a slow 1-px drift (cycles every ~3 min)
// so static UI elements never burn into the AMOLED.
void presentCanvas(Arduino_Canvas *c);
