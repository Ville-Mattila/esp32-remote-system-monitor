#pragma once

// Battery percentage from the AXP2101 fuel gauge (I2C 0x34, register 0xA4).
// Returns 1..100, or -1 when unreadable / no battery. Uses the shared Wire
// bus; call only from the main loop (no concurrent I2C users).
int batteryPercent();
