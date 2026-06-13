# PC Stats Display — Design

**Date:** 2026-06-12
**Target:** Waveshare ESP32-S3-Touch-AMOLED-1.8 (v2 revision: CO5300 display, CST816 touch)
**Project:** `amoled-1.8-starter` (PlatformIO, Arduino core 3.3.x via pioarduino, Arduino_GFX 1.6.5)

## Goal

The device sits on the desk in **landscape (448×368)** and shows live stats from the
user's Windows laptop: CPU load + temp, GPU load + temp, RAM usage,
and network down/up. Data comes from **LibreHardwareMonitor's web API**
(`GET http://<pc>:8085/data.json`) over WiFi, polled **once per second**.

First-run setup happens **entirely on the device**: scan WiFi → tap network → type
password on an on-screen keyboard → automatic discovery of the LibreHardwareMonitor
server on the LAN. Everything is persisted; later boots go straight to the dashboard.

## Architecture

Rendering: **plain Arduino_GFX** into a full-screen `Arduino_Canvas` frame buffer in
PSRAM (368×448×2 ≈ 322 KB), flushed once per frame — no flicker, no LVGL dependency.
Landscape is achieved with canvas rotation (`gfx` rotation 1 or 3; pick whichever puts
USB-C in the preferred direction during bring-up; expose as a constant).

A small state machine in `main.cpp` drives everything:

```
BOOT ──(no saved WiFi)──────────────► SETUP_WIFI_LIST ──tap──► SETUP_PASSWORD
  │                                        ▲                        │ OK
  └──(saved WiFi)──► CONNECTING ◄──────────┴────(back)──────────────┘
                        │ ok                │ fail (bad password → back to list)
                        ▼
                  DISCOVERING ──(saved server answers? skip sweep)──► DASHBOARD
                        │ found: save IP                                 │
                        └────────────────────────────────────────────►──┤
                                                                         │ long-press ≥2 s
                                  SETUP_WIFI_LIST ◄──────────────────────┘
```

Dashboard-side failures: HTTP errors → OFFLINE badge + retry every 5 s; after 3
consecutive failures of the saved server, re-run discovery sweep in the background;
WiFi drop → auto-reconnect (status shown in the corner).

### Modules (each a .h/.cpp pair under `src/`)

| Module | Responsibility | Depends on |
|---|---|---|
| `app_config` | Saved settings (SSID, password, server IP) in NVS via `Preferences`; load/save/clear | Preferences |
| `stats_client` | HTTP GET + ArduinoJson parse (PSRAM allocator) → `Stats` struct; locale-tolerant value parsing | HTTPClient, ArduinoJson |
| `discovery` | Sweep the /24 subnet for port 8085 answering `data.json` (parallel sockets, ~15 s worst case); progress callback | WiFiClient |
| `history` | Fixed ring buffers (one per graphed series, 104 samples ≈ 1.7 min) | — |
| `ui_theme` | Colors, fonts, layout constants | — |
| `ui_setup` | WiFi list screen, QWERTY keyboard screen, discovery progress screen | Arduino_GFX, touch |
| `ui_dashboard` | The stats dashboard (graphs, tiles, badges) | Arduino_GFX, history |
| `touch_input` | CST816 wrapper: tap, swipe (for list scroll), long-press detection | Arduino_DriveBus |
| `main.cpp` | State machine, WiFi lifecycle, 1 Hz poll timer | all of the above |

`ui_*` modules only consume plain data (`Stats`, history buffers, scan results) —
no networking; `stats_client`/`discovery` do no drawing.

## Data: sensors and parsing

Sensor values are located by **`SensorId`** in the JSON tree (verified live on the
user's machine 2026-06-12):

| Display value | SensorId | Sample |
|---|---|---|
| CPU load % | `/amdcpu/0/load/0` ("CPU Total") | `7,8 %` |
| CPU temp °C | `/amdcpu/0/temperature/2` ("Core (Tctl/Tdie)") | `60,9 °C` |
| GPU load % | `/gpu-nvidia/0/load/0` ("GPU Core") | `4,0 %` |
| GPU temp °C | `/gpu-nvidia/0/temperature/0` ("GPU Core") | `56,4 °C` |
| RAM used GB | `/ram/data/0` ("Memory Used") | `23,3 GB` |
| RAM total GB | `/ram/data/0` + `/ram/data/1` (used + available) | `31,1 GB` |
| Net down | `/nic/.../throughput/8` ("Download Speed") of the adapter named "WiFi" | `2,4 KB/s` |
| Net up | `/nic/.../throughput/7` ("Upload Speed") of the same adapter | `0,3 KB/s` |

Parsing notes:
- `data.json` is ~56 KB; parse with ArduinoJson `JsonDocument` backed by PSRAM.
- Values are **locale-formatted strings** (Finnish: `"60,9 °C"`). Parser strips the
  unit, accepts both `,` and `.` decimals.
- NIC SensorIds contain an unstable adapter GUID → locate network sensors by walking
  to the hardware node whose `Text == "WiFi"` and reading its Throughput children,
  not by hard-coded GUID. All other sensors use exact SensorId match.
- Net speeds arrive as KB/s or MB/s (unit varies with magnitude); convert the
  parsed value + unit to bytes/s, then display as **Mbit/s** (`bytes/s × 8 / 1e6`).
- Missing sensor (LHM update renames something): show `--` for that value, log the
  missing ID to serial, keep the rest working.

## Setup flow details

**WiFi list screen** — networks sorted by RSSI (strongest first), signal-bar glyphs,
duplicates merged, rescan every ~6 s while idle, swipe to scroll when >6 networks.
Tap a row → password screen (open networks skip straight to connect).

**Keyboard screen** — password field with cursor (characters visible), full QWERTY,
`⇧` shift layer (uppercase) and `123#` layer (digits + common symbols, enough for
WPA passwords). Keys ≈ 42×44 px. `OK ✓` connects; `‹ back` returns to the list.
Wrong password (connect fails) → toast "Couldn't connect — check password" → back
to keyboard with the previous entry preserved.

**Discovery screen** — after WiFi connects, sweep the device's own /24 subnet:
try TCP connect to port 8085 on each host (batches of ~16, ~250 ms timeout), on
connect do `GET /data.json` and accept if the response starts with `{"id":0`.
Show progress bar + current address. Found → save IP, brief "Found <PC name> ✓",
go to dashboard. Full sweep with nothing found → "No LibreHardwareMonitor found"
screen with a Retry button + hint text about enabling Remote Web Server.

## Dashboard (approved layout C v2)

448×368, black background. Vertical stack:

1. **CPU panel** (flex 1): scrolling history graph — utilization as filled cyan
   area, temperature as 2–3 px orange line, both on a 0–100 scale. Top-right
   current values: `37%` (cyan) `62°` (orange). Label "CPU" top-left.
2. **GPU panel** (flex 1): same, magenta fill for utilization, orange temp line.
3. **Bottom row** (64 px): RAM tile (green accent) `18.2/32 GB`; NET tile (yellow
   accent) `↓42.1 ↑3.4 Mbit/s`.

Graphs append one sample per poll (1 Hz), scroll left, ~104 visible samples.
OFFLINE state: values dim to grey, red "OFFLINE" badge top-center, graphs pause.
Long-press anywhere ≥2 s → setup (WiFi list).

### Theme

- Background pure black (AMOLED), panel borders dark grey.
- **Chrome accent: red** (#E74C4C) — used on setup screens (titles, selection
  highlight, OK button, progress bar), status text, OFFLINE badge.
- Data series keep their own colors for readability: CPU cyan, GPU magenta,
  temperature orange, RAM green, NET yellow.
- Brightness fixed at a comfortable level (constant in `ui_theme`).

## PC-side requirements

- LibreHardwareMonitor running with **Options → Remote Web Server** enabled on
  port 8085 and reachable from the LAN (not just localhost).
- **Windows Firewall inbound rule** allowing TCP 8085 (likely needed; part of
  bring-up verification, requires an elevated shell once).
- LHM must start with Windows for the display to be useful long-term (user task;
  LHM has a "Run at startup" option).

## Verification plan

Staged, each over serial before moving on:
1. Setup flow on device: scan shows real networks; password entry joins WiFi
   (verify wrong-password path too); discovery finds the PC and prints its IP.
2. `stats_client`: parsed values printed once per second match LibreHardwareMonitor's
   own UI within rounding.
3. Dashboard: numbers update at 1 Hz; run a CPU/GPU stress (game or benchmark) and
   watch utilization + temperature climb on the graphs.
4. Failure drills: close LHM → OFFLINE badge appears, reopen → recovers; reboot
   device → goes straight to dashboard; long-press → setup reopens; change WiFi
   password on router (or forget network) → falls back to setup gracefully.
5. Long soak: leave running for hours; no reboots/leaks (watch free heap on serial).

## Out of scope (this iteration)

- Battery operation tuning (assumed USB-powered at the desk).
- More sensors (SSD temp, fan RPM, battery) — the sensor table makes adding easy.
- LVGL, animations, multiple pages/themes, OTA updates.
- HTTPS/auth on the LHM API (it offers none).
