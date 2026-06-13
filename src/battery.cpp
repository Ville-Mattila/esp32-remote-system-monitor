#include "battery.h"
#include <Wire.h>

static const uint8_t AXP2101_ADDR = 0x34;
static const uint8_t REG_BAT_PERCENT = 0xA4;

int batteryPercent() {
    Wire.beginTransmission(AXP2101_ADDR);
    Wire.write(REG_BAT_PERCENT);
    if (Wire.endTransmission(false) != 0) return -1;  // repeated start
    if (Wire.requestFrom(AXP2101_ADDR, (uint8_t)1) != 1) return -1;
    int v = Wire.read();
    // 0 usually means "no battery attached" on this PMIC; treat as unknown.
    return (v >= 1 && v <= 100) ? v : -1;
}
