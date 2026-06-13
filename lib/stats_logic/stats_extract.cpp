#include "stats_extract.h"
#include "parse_value.h"
#include <cstring>

namespace {

bool startsWith(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

// Lower = more preferred CPU temperature sensor.
int cpuTempRank(const char *text) {
    if (!strcmp(text, "Core (Tctl/Tdie)")) return 0;  // AMD
    if (!strcmp(text, "CPU Package")) return 1;       // Intel
    if (!strcmp(text, "Core Average")) return 2;
    if (!strcmp(text, "Core Max")) return 3;
    return 4;
}

// Lower = more preferred GPU vendor (discrete before integrated).
int gpuRank(const char *sid) {
    if (startsWith(sid, "/gpu-nvidia/")) return 0;
    if (startsWith(sid, "/gpu-amd/")) return 1;
    if (startsWith(sid, "/gpu-intel/")) return 2;
    return -1;  // not a GPU
}

constexpr int MAX_NICS = 8;

struct NicCand {
    char key[44] = {0};   // GUID part of the SensorId
    char name[28] = {0};  // adapter display name (to exclude Bluetooth)
    float down = NAN, up = NAN;
    float totalDlGB = 0;  // lifetime data, identifies the active adapter at idle
};

struct Walker {
    Stats *out;
    float ramAvail = NAN;
    int cpuTempBest = 99;
    int gpuLoadBestRank = 99, gpuTempBestRank = 99;
    NicCand nics[MAX_NICS];
    int nicCount = 0;

    NicCand *nicFor(const char *sid, const char *adapterName) {
        // sid looks like /nic/{GUID}/throughput/7 — key on the GUID part.
        const char *guid = sid + 5;
        const char *end = strchr(guid, '/');
        size_t len = end ? (size_t)(end - guid) : strlen(guid);
        if (len >= sizeof(nics[0].key)) len = sizeof(nics[0].key) - 1;
        for (int i = 0; i < nicCount; i++)
            if (!strncmp(nics[i].key, guid, len) && nics[i].key[len] == 0) return &nics[i];
        if (nicCount >= MAX_NICS) return nullptr;
        NicCand &n = nics[nicCount++];
        memcpy(n.key, guid, len);
        n.key[len] = 0;
        if (adapterName) strncpy(n.name, adapterName, sizeof(n.name) - 1);
        return &n;
    }
};

void handleLeaf(Walker &w, const char *sid, const char *text, const char *value,
                const char *adapterName) {
    Stats &o = *w.out;

    if (startsWith(sid, "/amdcpu/") || startsWith(sid, "/intelcpu/")) {
        if (strstr(sid, "/load/") && !strcmp(text, "CPU Total")) {
            float v = lhmParseNumber(value);
            if (!std::isnan(v)) o.cpuLoad = v;
        } else if (strstr(sid, "/temperature/")) {
            int rank = cpuTempRank(text);
            float v = lhmParseNumber(value);
            if (!std::isnan(v) && rank < w.cpuTempBest) {
                w.cpuTempBest = rank;
                o.cpuTemp = v;
            }
        }
        return;
    }

    int gr = gpuRank(sid);
    if (gr >= 0 && !strcmp(text, "GPU Core")) {
        float v = lhmParseNumber(value);
        if (std::isnan(v)) return;
        if (strstr(sid, "/load/") && gr < w.gpuLoadBestRank) {
            w.gpuLoadBestRank = gr;
            o.gpuLoad = v;
        } else if (strstr(sid, "/temperature/") && gr < w.gpuTempBestRank) {
            w.gpuTempBestRank = gr;
            o.gpuTemp = v;
        }
        return;
    }

    if (!strcmp(sid, "/ram/data/0")) { o.ramUsedGB = lhmParseNumber(value); return; }
    if (!strcmp(sid, "/ram/data/1")) { w.ramAvail = lhmParseNumber(value); return; }

    if (startsWith(sid, "/nic/")) {
        NicCand *n = w.nicFor(sid, adapterName);
        if (!n) return;
        if (!strcmp(text, "Download Speed"))   n->down = lhmParseBytesPerSecond(value);
        else if (!strcmp(text, "Upload Speed")) n->up = lhmParseBytesPerSecond(value);
        else if (!strcmp(text, "Data Downloaded")) {
            float v = lhmParseNumber(value);
            if (!std::isnan(v)) n->totalDlGB = v;
        }
    }
}

// grandText = the hardware node two levels above a sensor leaf
// (adapter > category ("Throughput") > leaf), used for the NIC name.
void walk(JsonVariantConst node, Walker &w, const char *parentText, const char *grandText) {
    const char *text = node["Text"];
    JsonArrayConst children = node["Children"];

    if (children.size() == 0) {
        const char *sid = node["SensorId"];
        const char *value = node["Value"];
        if (sid && value && text) handleLeaf(w, sid, text, value, grandText);
        return;
    }
    for (JsonVariantConst child : children) {
        walk(child, w, text ? text : "", parentText);
    }
}

}  // namespace

int extractStats(JsonVariantConst root, Stats &out) {
    Walker w;
    w.out = &out;
    walk(root, w, "", "");

    if (!std::isnan(out.ramUsedGB) && !std::isnan(w.ramAvail))
        out.ramTotalGB = out.ramUsedGB + w.ramAvail;

    // Pick the network adapter with the most traffic; ignore Bluetooth.
    float bestScore = -1;
    for (int i = 0; i < w.nicCount; i++) {
        NicCand &n = w.nics[i];
        if (strstr(n.name, "Bluetooth")) continue;
        if (std::isnan(n.down) && std::isnan(n.up)) continue;
        float score = n.totalDlGB * 1e6f +
                      (std::isnan(n.down) ? 0 : n.down) +
                      (std::isnan(n.up) ? 0 : n.up);
        if (score > bestScore) {
            bestScore = score;
            out.netDownBps = n.down;
            out.netUpBps = n.up;
        }
    }

    int found = 0;
    const float vals[8] = {out.cpuLoad, out.cpuTemp, out.gpuLoad, out.gpuTemp,
                           out.ramUsedGB, out.ramTotalGB, out.netDownBps, out.netUpBps};
    for (float v : vals) if (!std::isnan(v)) found++;
    return found;
}
