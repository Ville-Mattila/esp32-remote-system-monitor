#pragma once
#include <ArduinoJson.h>
#include <cmath>

// Adaptive sensor extraction from LibreHardwareMonitor's data.json.
// Works across hardware vendors instead of hard-coding one machine:
//  - CPU: first /amdcpu/ or /intelcpu/; load from "CPU Total"; temperature by
//    preference: Core (Tctl/Tdie) > CPU Package > Core Average > Core Max > any.
//  - GPU: best vendor present, discrete first: gpu-nvidia > gpu-amd > gpu-intel;
//    "GPU Core" load and temperature.
//  - RAM: /ram/data/0 (used) + /ram/data/1 (available), universal in LHM.
//  - Network: the adapter with the most traffic (lifetime data downloaded,
//    then current throughput); Bluetooth adapters are ignored.

struct Stats {
    float cpuLoad = NAN, cpuTemp = NAN;
    float gpuLoad = NAN, gpuTemp = NAN;
    float ramUsedGB = NAN, ramTotalGB = NAN;
    float netDownBps = NAN, netUpBps = NAN;
    bool anyValid() const {
        return !std::isnan(cpuLoad) || !std::isnan(gpuLoad) ||
               !std::isnan(ramUsedGB) || !std::isnan(netDownBps);
    }
};

// Fills `out` from a parsed data.json root. Returns how many of the 8 fields
// were filled.
int extractStats(JsonVariantConst root, Stats &out);
