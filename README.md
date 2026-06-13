# ESP32 Remote System Monitor

A standalone WiFi desk display for your PC's vitals, built on the Waveshare
**ESP32-S3-Touch-AMOLED-1.8**. It reads **LibreHardwareMonitor's web API**
and shows CPU and GPU as scrolling history graphs (utilization area +
temperature line), RAM and network tiles, on a 448x368 landscape AMOLED —
no cables to the PC, no app on the PC beyond LibreHardwareMonitor itself.

**Zero-config on-device setup:** scan WiFi → tap your network → type the
password on an on-screen keyboard → the device sweeps the LAN and finds the
LibreHardwareMonitor server by itself. Everything persists; later boots go
straight to the dashboard.

> Verified working 2026-06-13 on the **v2 hardware revision** of the board
> (see below), running wireless on battery against a Windows 11 PC.

## Features

- **Four swipeable pages:** dashboard · full-screen network graphs (auto-scaled)
  · giant-numbers focus view · clock + connection info (NTP, works with PC off)
- **Adaptive sensors:** any Intel/AMD CPU; NVIDIA → AMD → Intel GPU priority
  (discrete first); busiest non-Bluetooth network adapter — no per-machine config
- **Touch:** swipe left/right = pages, double-tap = flip 180° (persisted),
  long-press ~2 s = WiFi setup, on-screen QWERTY with shift + symbol layers
- **Remembers up to 6 WiFi networks** — known ones rejoin with one tap, no typing
- **Self-healing:** OFFLINE badge + automatic LAN re-discovery when the PC's IP
  changes; WiFi auto-reconnect
- **AMOLED burn-in protection** (1-px frame drift every 3 min) and the device's
  own battery percentage on screen
- Locale-tolerant value parsing (comma decimals, Unicode minus — tested against
  a Finnish-locale LHM)

## How it works

- Polls `http://<pc>:8085/data.json` once per second; ~56 KB JSON parsed in
  PSRAM; sensor selection logic in `lib/stats_logic/stats_extract.cpp`.
- Net values are bytes/s (auto-scaled K/M, 1024-based like LHM itself).
- Design spec and implementation plan: `docs/superpowers/`.

## PC-side requirements (one-time)

1. **LibreHardwareMonitor** running with **Options → Remote Web Server** on
   port 8085.
2. **Windows Firewall inbound rule** for TCP 8085 (elevated PowerShell):
   `New-NetFirewallRule -DisplayName "LibreHardwareMonitor 8085" -Direction Inbound -Protocol TCP -LocalPort 8085 -Action Allow`
   Without this the device's discovery sweep finds nothing — this was the #1
   gotcha in practice.
3. Recommended: LHM's "Run at startup" option, so the display survives reboots.
4. The display's WiFi is **2.4 GHz only** (ESP32-S3 limitation) — it must end
   up on the same subnet as the PC.

## Board revision matters!

Waveshare shipped two hardware revisions under the same product name, with the
same pinout but different display and touch controllers:

| | Original | **v2 (this project targets v2)** |
|---|---|---|
| Display controller | SH8601 | **CO5300** (16-px column offset) |
| Touch controller | FT3168 @ 0x38 | **CST816 @ 0x15** |

If you have the original revision, swap `Arduino_CO5300` back to
`Arduino_SH8601` (no offsets) and `Arduino_CST816x`/`CST816T_DEVICE_ADDRESS`
back to `Arduino_FT3x68`/`FT3168_DEVICE_ADDRESS` in `src/main.cpp`. You can
check which revision you have by running an I2C scan: touch at 0x15 = v2,
touch at 0x38 = original. Waveshare's official examples live at
https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8 (`Arduino-v3.3.5-v2`
folder for the v2 revision).

## Hardware facts

- **SoC:** ESP32-S3R8 - 16MB flash + **8MB octal PSRAM**
  (`board_build.arduino.memory_type = qio_opi`).
- **Display:** 368x448 AMOLED over **QSPI** (not a normal SPI TFT). Driven by
  `Arduino_GFX` (`Arduino_ESP32QSPI` + `Arduino_CO5300`). Brightness via
  `gfx->setBrightness(0..255)`.
- **Touch:** CST816 capacitive controller over I2C (`IIC_SDA 15`, `IIC_SCL 14`).
- Other chips on the same I2C bus: **TCA9554** expander (0x20), **AXP2101**
  PMIC (0x34), **QMI8658** IMU (0x6B), **PCF85063** RTC (0x51), **ES8311**
  audio codec (0x18, I2S for audio).

## Why touch needs the TCA9554 expander

The touch controller's reset line is gated behind the **TCA9554 I2C expander**,
not a direct GPIO. Boot order: start I2C -> configure the expander and pulse
its lines low->high -> *then* the touch chip responds. The expander is driven
with **raw `Wire` register writes** in `main.cpp` (registers 0x01/0x03) - do
NOT add an expander library that uses the legacy IDF I2C driver (e.g.
`ESP32_IO_Expander` 0.x): mixing it with `Wire` on Arduino core 3.3+ aborts at
boot with `i2c: CONFLICT! driver_ng is not allowed to be used with this old
driver`.

## Other sharp edges this project already handles

- **`Wire.end()` before `Touch->begin()`:** the vendored `Arduino_DriveBus`
  calls `Wire.begin()` itself; a second `begin()` on core 3.x wedges the new
  I2C driver (`ESP_ERR_INVALID_STATE` on every transaction). `main.cpp` ends
  the bus after the expander writes so DriveBus can re-init it cleanly.
- **Non-ASCII project path:** the xtensa linker mangles paths like `C:\Työt`,
  so `platformio.ini` moves the build workspace to
  `%USERPROFILE%\.pio-workspaces\amoled-1.8-starter`.
- **Version pins:** `GFX Library for Arduino` is pinned to exactly **1.6.5**
  (1.4.x lacks the CO5300/SH8601 drivers; the 1.5+ API renamed colors to
  `RGB565_*` and brightness to `setBrightness`). The platform is pinned to a
  pioarduino release with Arduino core 3.3.x.

## Vendored libraries (in `lib/`)

- **Arduino_DriveBus** 1.0.1 - touch driver (`Arduino_CST816x`). Uses `Wire`,
  so it's compatible with core 3.3+.

`Arduino_GFX` is pulled from the registry via `lib_deps`.

## One-time machine setup

1. Install **VS Code** + the **PlatformIO IDE** extension (bundles its own
   Python + PlatformIO Core).
2. **USB driver:** the Type-C port is the ESP32-S3's native USB; on Windows 11
   it enumerates as a COM port with no extra driver.

## Build & flash

1. Open this folder in VS Code. PlatformIO installs everything on first build.
2. Plug in via USB-C, power **ON** (hold **PWR** ~6s).
3. **PlatformIO: Upload** (or `pio run -t upload`), then Serial Monitor at 115200.
   If auto-detection fails, add `--upload-port COM4` (or whatever your port is).

## Troubleshooting

- **Flashed OK but nothing ever runs (silent serial, blank screen):** the chip
  may be stuck in download mode - this happens if it was plugged in with BOOT
  held. Unplug, make sure BOOT isn't pressed, replug. Verify with:
  `python -m esptool --port COM4 --before no-reset read-mac` - if it connects,
  the chip is in download mode (the app is NOT running).
- **Upload fails / no COM port:** hold **BOOT**, plug in, release - this forces
  download mode for flashing. Power-cycle afterwards (see above!).
- **Boot-loops:** check `board_build.arduino.memory_type = qio_opi` (correct
  for the R8's octal PSRAM) and watch serial for I2C driver-conflict aborts.
- **Blank screen, firmware running fine:** wrong display controller for your
  board revision - see "Board revision matters!" above.

## Adding the rest of the hardware (roadmap)

The other onboard peripherals use libraries Waveshare bundles in their v2
examples (`SensorLib`, `XPowersLib`) - vendor them into `lib/` the same way:

- **IMU / RTC:** `SensorLib` (QMI8658 motion, PCF85063 clock), same I2C bus.
- **Power:** `XPowersLib` (AXP2101 - battery %, charge state, voltages).
- **Audio:** ES8311 codec on I2S (`MCLK 16`, `BCLK 9`, `WS 45`, `DO 8`,
  `DI 10`, amp enable `PA 46`).
- **LVGL:** add `lvgl` and wire `Arduino_GFX` as the flush callback plus the
  CST816 read as the input device (Waveshare's v2 LVGL examples use lvgl 8.x).
