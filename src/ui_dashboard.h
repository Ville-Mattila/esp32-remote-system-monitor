#pragma once
#include <Arduino_GFX_Library.h>
#include "stats_extract.h"
#include "history.h"

struct DashHistories {
    History cpuLoad, cpuTemp, gpuLoad, gpuTemp;
    History netDown, netUp;  // bytes/s, for the network page
};

// Renders the full dashboard into the canvas (no flush — the caller presents
// via presentCanvas() so the burn-in shift applies). offline=true dims values
// and shows the OFFLINE badge (replacing the battery readout).
// batteryPct: 1..100 shown top-center, <=0 hidden.
void dashboardDraw(Arduino_Canvas *c, const Stats &s, const DashHistories &h, bool offline,
                   int batteryPct);

// Shared bits used by the other view pages:
void dashboardChrome(Arduino_Canvas *c, bool offline, int batteryPct);  // badge / battery
void fmtBytesPerSec(char *buf, size_t n, float bps);                    // "713K" / "2.3M"
