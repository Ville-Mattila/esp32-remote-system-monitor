#pragma once

// Fixed-size ring buffer for one graphed series. 104 samples at 1 Hz ≈ 1.7 min,
// chosen so each sample maps to ~4 px in the 416-px-wide graph area.
constexpr int HISTORY_LEN = 104;

struct History {
    float samples[HISTORY_LEN] = {};
    int count = 0;  // valid samples, saturates at HISTORY_LEN
    int head = 0;   // next write position

    void push(float v) {
        samples[head] = v;
        head = (head + 1) % HISTORY_LEN;
        if (count < HISTORY_LEN) count++;
    }

    // i = 0 -> oldest valid sample, i = count-1 -> newest. No bounds check.
    float at(int i) const {
        int start = (head - count + HISTORY_LEN) % HISTORY_LEN;
        return samples[(start + i) % HISTORY_LEN];
    }
};
