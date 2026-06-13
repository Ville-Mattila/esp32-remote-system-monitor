#pragma once
#include <Arduino_GFX_Library.h>

// Landscape orientation default (1 or 3 = 180° apart). The runtime value lives
// in uiRotation (set from saved config, toggled by double-tap on the dashboard).
#define UI_ROTATION   1
extern uint8_t uiRotation;  // defined in board_init.cpp
#define UI_BRIGHTNESS 200
#define SCREEN_W      448
#define SCREEN_H      368

// Chrome accent (user request: red) — setup screens, status, badges.
#define COL_ACCENT    RGB565(231, 76, 76)
#define COL_BG        RGB565_BLACK
#define COL_TEXT      RGB565_WHITE
#define COL_DIM       RGB565(120, 120, 120)
#define COL_PANEL     RGB565(34, 44, 50)    // panel border
#define COL_FIELD     RGB565(16, 24, 29)    // input field / bar background

// Data series (kept distinct from the chrome accent for readability).
#define COL_CPU       RGB565(57, 197, 207)   // cyan
#define COL_CPU_FILL  RGB565(10, 48, 53)     // dark cyan area fill
#define COL_GPU       RGB565(232, 90, 173)   // magenta
#define COL_GPU_FILL  RGB565(58, 18, 36)     // dark magenta area fill
#define COL_TEMP      RGB565(255, 159, 67)   // orange temperature line
#define COL_RAM       RGB565(123, 216, 143)  // green
#define COL_NET       RGB565(232, 201, 90)   // yellow
#define COL_NET_FILL  RGB565(58, 50, 22)     // dark yellow area fill
