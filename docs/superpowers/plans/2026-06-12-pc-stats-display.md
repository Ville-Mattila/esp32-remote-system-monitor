# PC Stats Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the AMOLED touch device into a desk display showing live PC stats (CPU/GPU load+temp graphs, RAM, network) fetched from LibreHardwareMonitor's web API, with fully on-device WiFi + server setup.

**Architecture:** Landscape 448×368 dashboard rendered with Arduino_GFX into a PSRAM canvas. A sequential flow in `main.cpp` (blocking setup screens → dashboard loop) replaces a formal state machine. Pure logic (value parsing, history buffers, JSON extraction) lives in `lib/stats_logic/` and is unit-tested **on the device** with PlatformIO's Unity test runner (`pio test`). Settings persist in NVS.

**Tech Stack:** Arduino core 3.3.x (pioarduino), Arduino_GFX 1.6.5 (CO5300), Arduino_DriveBus (CST816), ArduinoJson 7, Preferences (NVS), HTTPClient, lwip sockets for the discovery sweep.

**Spec:** `docs/superpowers/specs/2026-06-12-pc-stats-display-design.md`

**Hardware facts (do not re-derive):** board is the v2 revision — CO5300 display (`Arduino_CO5300`, col offset 16), CST816 touch at I2C 0x15. Upload port COM4. The TCA9554 expander must be initialized with raw Wire writes BEFORE touch begin, then `Wire.end()` before `Touch->begin()` (DriveBus re-inits Wire itself; double-begin wedges the I2C driver). Existing working init order is in `src/main.cpp` — preserve it exactly when refactoring.

**Conventions:**
- Build: `pio run -e amoled-18` (pio = `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`)
- Flash: `pio run -e amoled-18 -t upload`
- Tests (run ON the device, board must be connected): `pio test -e amoled-18`
- Serial capture for verification (replace SECONDS):
  `%USERPROFILE%\.platformio\penv\Scripts\python.exe -c "import serial,time;p=serial.Serial('COM4',115200,timeout=0.2);e=time.time()+SECONDS;d=b''"` … (use the capture snippet from Task 5 Step 4; it tolerates port re-enumeration)
- PlatformIO upload may print a `UnicodeEncodeError` traceback (cp1252 console) — **cosmetic**; trust the final `[SUCCESS]`/`[FAILED]` line.

---

### Task 1: Scaffolding — dependencies, test harness, module skeleton

**Files:**
- Modify: `platformio.ini`
- Create: `lib/stats_logic/library.json`
- Create: `test/test_smoke/test_main.cpp`

- [ ] **Step 1: Add ArduinoJson, upload port, and test settings to `platformio.ini`**

Append/modify the `[env:amoled-18]` section so the full file reads:

```ini
[platformio]
; The toolchain's linker mangles non-ASCII paths (this project lives under
; "C:\Työt"), so keep build output in an ASCII-only location.
workspace_dir = ${sysenv.USERPROFILE}\.pio-workspaces\amoled-1.8-starter

[env:amoled-18]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.38/platform-espressif32.zip
board = esp32-s3-devkitc-1
framework = arduino

board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
board_build.arduino.memory_type = qio_opi

monitor_speed = 115200
upload_speed = 921600
upload_port = COM4
test_speed = 115200

build_flags =
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1

; Pinned exactly: the CO5300/SH8601 drivers only exist in 1.5+, and the 1.5+/1.6+
; API renamed the color constants (BLACK -> RGB565_BLACK) and brightness call.
lib_deps =
    moononournation/GFX Library for Arduino @ 1.6.5
    bblanchon/ArduinoJson @ ^7.3.0
```

- [ ] **Step 2: Create the project-local pure-logic library**

`lib/stats_logic/library.json`:

```json
{
  "name": "stats_logic",
  "version": "1.0.0",
  "description": "Pure logic for the PC stats display: value parsing, history buffers, JSON extraction",
  "dependencies": { "bblanchon/ArduinoJson": "^7.3.0" }
}
```

- [ ] **Step 3: Write a trivial smoke test to prove the on-device test harness works**

`test/test_smoke/test_main.cpp`:

```cpp
#include <Arduino.h>
#include <unity.h>

void test_harness_runs(void) { TEST_ASSERT_EQUAL(4, 2 + 2); }

void setup() {
    delay(2000);  // allow USB CDC to come up before Unity prints
    UNITY_BEGIN();
    RUN_TEST(test_harness_runs);
    UNITY_END();
}
void loop() {}
```

- [ ] **Step 4: Run the test on the device**

Run: `pio test -e amoled-18 -f test_smoke`
Expected: `1 Tests 0 Failures 0 Ignored` and `[PASSED]`. (Board must be plugged in. If the runner can't sync, power-cycle the device and retry once.)

- [ ] **Step 5: Confirm the existing demo still builds**

Run: `pio run -e amoled-18`
Expected: `[SUCCESS]` (ArduinoJson added without conflicts).

- [ ] **Step 6: Commit**

```bash
git add platformio.ini lib/stats_logic/library.json test/test_smoke/test_main.cpp
git commit -m "chore: add ArduinoJson, stats_logic lib skeleton, on-device test harness"
```

---

### Task 2: Locale-tolerant value parser (TDD)

LibreHardwareMonitor returns values as locale-formatted strings — on this PC Finnish: `"60,9 °C"`, `"7,8 %"`, `"2,4 KB/s"`.

**Files:**
- Create: `lib/stats_logic/parse_value.h`
- Create: `lib/stats_logic/parse_value.cpp`
- Create: `test/test_parse/test_main.cpp`

- [ ] **Step 1: Write the failing tests**

`test/test_parse/test_main.cpp`:

```cpp
#include <Arduino.h>
#include <unity.h>
#include <cmath>
#include "parse_value.h"

void test_comma_decimal(void)   { TEST_ASSERT_FLOAT_WITHIN(0.01, 60.9, lhmParseNumber("60,9 °C")); }
void test_dot_decimal(void)     { TEST_ASSERT_FLOAT_WITHIN(0.01, 60.9, lhmParseNumber("60.9 °C")); }
void test_percent(void)         { TEST_ASSERT_FLOAT_WITHIN(0.01, 7.8, lhmParseNumber("7,8 %")); }
void test_gigabytes(void)       { TEST_ASSERT_FLOAT_WITHIN(0.01, 23.3, lhmParseNumber("23,3 GB")); }
void test_no_number_is_nan(void){ TEST_ASSERT_TRUE(isnan(lhmParseNumber("-"))); }
void test_null_is_nan(void)     { TEST_ASSERT_TRUE(isnan(lhmParseNumber(nullptr))); }

void test_bps_kb(void)  { TEST_ASSERT_FLOAT_WITHIN(1.0, 2.4 * 1024,            lhmParseBytesPerSecond("2,4 KB/s")); }
void test_bps_mb(void)  { TEST_ASSERT_FLOAT_WITHIN(1.0, 5.1 * 1024 * 1024,     lhmParseBytesPerSecond("5,1 MB/s")); }
void test_bps_b(void)   { TEST_ASSERT_FLOAT_WITHIN(0.1, 800.0,                 lhmParseBytesPerSecond("800,0 B/s")); }
void test_bps_nan(void) { TEST_ASSERT_TRUE(isnan(lhmParseBytesPerSecond("-"))); }

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_comma_decimal); RUN_TEST(test_dot_decimal); RUN_TEST(test_percent);
    RUN_TEST(test_gigabytes); RUN_TEST(test_no_number_is_nan); RUN_TEST(test_null_is_nan);
    RUN_TEST(test_bps_kb); RUN_TEST(test_bps_mb); RUN_TEST(test_bps_b); RUN_TEST(test_bps_nan);
    UNITY_END();
}
void loop() {}
```

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e amoled-18 -f test_parse`
Expected: build FAILS — `parse_value.h: No such file or directory`. (A compile failure is the red step here.)

- [ ] **Step 3: Implement the parser**

`lib/stats_logic/parse_value.h`:

```cpp
#pragma once

// Parsers for LibreHardwareMonitor's locale-formatted value strings.
// Both return NAN when no number can be found.
float lhmParseNumber(const char *s);          // "60,9 °C" -> 60.9
float lhmParseBytesPerSecond(const char *s);  // "2,4 KB/s" -> 2457.6 (bytes/s)
```

`lib/stats_logic/parse_value.cpp`:

```cpp
#include "parse_value.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

float lhmParseNumber(const char *s) {
    if (!s) return NAN;
    char buf[32];
    size_t n = 0;
    bool seenDigit = false;
    for (const char *p = s; *p && n < sizeof(buf) - 1; ++p) {
        if (*p >= '0' && *p <= '9') { buf[n++] = *p; seenDigit = true; }
        else if (*p == '-' && !seenDigit) { buf[n++] = *p; }
        else if ((*p == ',' || *p == '.') && seenDigit) { buf[n++] = '.'; }
        else if (seenDigit) break;  // number ended, unit follows
    }
    if (!seenDigit) return NAN;
    buf[n] = '\0';
    return strtof(buf, nullptr);
}

float lhmParseBytesPerSecond(const char *s) {
    float v = lhmParseNumber(s);
    if (std::isnan(v) || !s) return NAN;
    if (strstr(s, "GB/s")) return v * 1024.0f * 1024.0f * 1024.0f;
    if (strstr(s, "MB/s")) return v * 1024.0f * 1024.0f;
    if (strstr(s, "KB/s")) return v * 1024.0f;
    return v;  // plain B/s
}
```

- [ ] **Step 4: Run to verify pass**

Run: `pio test -e amoled-18 -f test_parse`
Expected: `10 Tests 0 Failures`.

- [ ] **Step 5: Commit**

```bash
git add lib/stats_logic/parse_value.h lib/stats_logic/parse_value.cpp test/test_parse/test_main.cpp
git commit -m "feat: locale-tolerant LHM value parser (TDD)"
```

---

### Task 3: History ring buffer (TDD)

**Files:**
- Create: `lib/stats_logic/history.h` (header-only)
- Create: `test/test_history/test_main.cpp`

- [ ] **Step 1: Write the failing tests**

`test/test_history/test_main.cpp`:

```cpp
#include <Arduino.h>
#include <unity.h>
#include "history.h"

void test_empty(void) {
    History h;
    TEST_ASSERT_EQUAL(0, h.count);
}

void test_push_and_read_in_order(void) {
    History h;
    h.push(1.0f); h.push(2.0f); h.push(3.0f);
    TEST_ASSERT_EQUAL(3, h.count);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0, h.at(0));  // oldest
    TEST_ASSERT_FLOAT_WITHIN(0.001, 3.0, h.at(2));  // newest
}

void test_wraps_and_keeps_newest(void) {
    History h;
    for (int i = 0; i < HISTORY_LEN + 10; i++) h.push((float)i);
    TEST_ASSERT_EQUAL(HISTORY_LEN, h.count);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 10.0, h.at(0));                 // oldest surviving
    TEST_ASSERT_FLOAT_WITHIN(0.001, HISTORY_LEN + 9, h.at(HISTORY_LEN - 1));  // newest
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_empty);
    RUN_TEST(test_push_and_read_in_order);
    RUN_TEST(test_wraps_and_keeps_newest);
    UNITY_END();
}
void loop() {}
```

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e amoled-18 -f test_history`
Expected: build FAILS — `history.h: No such file or directory`.

- [ ] **Step 3: Implement**

`lib/stats_logic/history.h`:

```cpp
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
```

- [ ] **Step 4: Run to verify pass**

Run: `pio test -e amoled-18 -f test_history`
Expected: `3 Tests 0 Failures`.

- [ ] **Step 5: Commit**

```bash
git add lib/stats_logic/history.h test/test_history/test_main.cpp
git commit -m "feat: history ring buffer (TDD)"
```

---

### Task 4: JSON stats extraction (TDD)

Walks LibreHardwareMonitor's `data.json` tree, fills a `Stats` struct. CPU/GPU/RAM sensors matched by exact `SensorId`; network sensors found by adapter *name* ("WiFi") because NIC SensorIds embed an unstable GUID.

**Files:**
- Create: `lib/stats_logic/stats_extract.h`
- Create: `lib/stats_logic/stats_extract.cpp`
- Create: `test/test_extract/test_main.cpp`

- [ ] **Step 1: Write the failing tests** (fixture mimics the real tree shape, verified against the live API 2026-06-12)

`test/test_extract/test_main.cpp`:

```cpp
#include <Arduino.h>
#include <unity.h>
#include <ArduinoJson.h>
#include <cmath>
#include "stats_extract.h"

// Trimmed but structurally faithful data.json: computer node containing CPU,
// RAM, GPU, and two NICs (only the one named "WiFi" must be used).
static const char FIXTURE[] = R"JSON(
{"id":0,"Text":"Sensor","Children":[
 {"id":1,"Text":"EXAMPLE-PC","Children":[
  {"id":3,"Text":"AMD Ryzen AI 9 HX 370 w/ Radeon 890M","Children":[
   {"id":10,"Text":"Temperatures","Children":[
    {"id":11,"Text":"Core (Tctl/Tdie)","SensorId":"/amdcpu/0/temperature/2","Value":"60,9 °C","Children":[]}]},
   {"id":12,"Text":"Load","Children":[
    {"id":13,"Text":"CPU Total","SensorId":"/amdcpu/0/load/0","Value":"7,8 %","Children":[]}]}]},
  {"id":20,"Text":"Total Memory","Children":[
   {"id":21,"Text":"Data","Children":[
    {"id":22,"Text":"Memory Used","SensorId":"/ram/data/0","Value":"23,3 GB","Children":[]},
    {"id":23,"Text":"Memory Available","SensorId":"/ram/data/1","Value":"7,8 GB","Children":[]}]}]},
  {"id":30,"Text":"NVIDIA GeForce RTX 5070 Laptop GPU","Children":[
   {"id":31,"Text":"Temperatures","Children":[
    {"id":32,"Text":"GPU Core","SensorId":"/gpu-nvidia/0/temperature/0","Value":"56,4 °C","Children":[]}]},
   {"id":33,"Text":"Load","Children":[
    {"id":34,"Text":"GPU Core","SensorId":"/gpu-nvidia/0/load/0","Value":"4,0 %","Children":[]}]}]},
  {"id":40,"Text":"Bluetooth Network Connection","Children":[
   {"id":41,"Text":"Throughput","Children":[
    {"id":42,"Text":"Upload Speed","SensorId":"/nic/x/throughput/7","Value":"9999,0 KB/s","Children":[]}]}]},
  {"id":50,"Text":"WiFi","Children":[
   {"id":51,"Text":"Throughput","Children":[
    {"id":52,"Text":"Upload Speed","SensorId":"/nic/y/throughput/7","Value":"0,3 KB/s","Children":[]},
    {"id":53,"Text":"Download Speed","SensorId":"/nic/y/throughput/8","Value":"2,4 KB/s","Children":[]}]}]}]}]}
)JSON";

static Stats parseFixture(const char *json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, err.code(), "fixture must parse");
    Stats s;
    extractStats(doc.as<JsonVariantConst>(), s);
    return s;
}

void test_cpu(void)  { Stats s = parseFixture(FIXTURE);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 7.8, s.cpuLoad);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 60.9, s.cpuTemp); }
void test_gpu(void)  { Stats s = parseFixture(FIXTURE);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 4.0, s.gpuLoad);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 56.4, s.gpuTemp); }
void test_ram(void)  { Stats s = parseFixture(FIXTURE);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 23.3, s.ramUsedGB);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 31.1, s.ramTotalGB); }  // used + available
void test_net_uses_wifi_adapter_only(void) { Stats s = parseFixture(FIXTURE);
    TEST_ASSERT_FLOAT_WITHIN(1.0, 2.4 * 1024, s.netDownBps);
    TEST_ASSERT_FLOAT_WITHIN(1.0, 0.3 * 1024, s.netUpBps); }  // NOT the Bluetooth 9999
void test_count_and_validity(void) { Stats s = parseFixture(FIXTURE);
    TEST_ASSERT_TRUE(s.anyValid()); }
void test_missing_sensor_is_nan(void) {
    Stats s = parseFixture(R"({"id":0,"Text":"Sensor","Children":[]})");
    TEST_ASSERT_TRUE(isnan(s.cpuLoad));
    TEST_ASSERT_FALSE(s.anyValid()); }

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_cpu); RUN_TEST(test_gpu); RUN_TEST(test_ram);
    RUN_TEST(test_net_uses_wifi_adapter_only);
    RUN_TEST(test_count_and_validity); RUN_TEST(test_missing_sensor_is_nan);
    UNITY_END();
}
void loop() {}
```

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e amoled-18 -f test_extract`
Expected: build FAILS — `stats_extract.h: No such file or directory`.

- [ ] **Step 3: Implement**

`lib/stats_logic/stats_extract.h`:

```cpp
#pragma once
#include <ArduinoJson.h>
#include <cmath>

// SensorIds verified against the live API on EXAMPLE-PC, 2026-06-12.
#define LHM_ID_CPU_LOAD  "/amdcpu/0/load/0"
#define LHM_ID_CPU_TEMP  "/amdcpu/0/temperature/2"
#define LHM_ID_GPU_LOAD  "/gpu-nvidia/0/load/0"
#define LHM_ID_GPU_TEMP  "/gpu-nvidia/0/temperature/0"
#define LHM_ID_RAM_USED  "/ram/data/0"
#define LHM_ID_RAM_AVAIL "/ram/data/1"
#define LHM_NET_ADAPTER  "WiFi"  // NIC ids embed an unstable GUID, match by name

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
// were found (ramTotalGB counts via ramUsed+ramAvail).
int extractStats(JsonVariantConst root, Stats &out);
```

`lib/stats_logic/stats_extract.cpp`:

```cpp
#include "stats_extract.h"
#include "parse_value.h"
#include <cstring>

namespace {

struct Ctx {
    Stats *out;
    int found = 0;
    float ramAvail = NAN;
};

void take(float &slot, const char *value, Ctx &c, bool isBps = false) {
    float v = isBps ? lhmParseBytesPerSecond(value) : lhmParseNumber(value);
    if (!std::isnan(v)) { slot = v; c.found++; }
}

void walk(JsonVariantConst node, Ctx &c, bool insideNetAdapter) {
    const char *text = node["Text"];
    JsonArrayConst children = node["Children"];

    if (children.size() == 0) {  // leaf sensor
        const char *sid = node["SensorId"];
        const char *value = node["Value"];
        if (sid && value) {
            if      (!strcmp(sid, LHM_ID_CPU_LOAD))  take(c.out->cpuLoad, value, c);
            else if (!strcmp(sid, LHM_ID_CPU_TEMP))  take(c.out->cpuTemp, value, c);
            else if (!strcmp(sid, LHM_ID_GPU_LOAD))  take(c.out->gpuLoad, value, c);
            else if (!strcmp(sid, LHM_ID_GPU_TEMP))  take(c.out->gpuTemp, value, c);
            else if (!strcmp(sid, LHM_ID_RAM_USED))  take(c.out->ramUsedGB, value, c);
            else if (!strcmp(sid, LHM_ID_RAM_AVAIL)) { c.ramAvail = lhmParseNumber(value); }
            else if (insideNetAdapter && text) {
                if (!strcmp(text, "Download Speed")) take(c.out->netDownBps, value, c, true);
                if (!strcmp(text, "Upload Speed"))   take(c.out->netUpBps, value, c, true);
            }
        }
        return;
    }
    bool isAdapter = (text && !strcmp(text, LHM_NET_ADAPTER));
    for (JsonVariantConst child : children) {
        walk(child, c, insideNetAdapter || isAdapter);
    }
}

}  // namespace

int extractStats(JsonVariantConst root, Stats &out) {
    Ctx c; c.out = &out;
    walk(root, c, false);
    if (!std::isnan(out.ramUsedGB) && !std::isnan(c.ramAvail)) {
        out.ramTotalGB = out.ramUsedGB + c.ramAvail;
        c.found++;
    }
    return c.found;
}
```

- [ ] **Step 4: Run to verify pass**

Run: `pio test -e amoled-18 -f test_extract`
Expected: `6 Tests 0 Failures`.

- [ ] **Step 5: Run ALL tests to check nothing regressed**

Run: `pio test -e amoled-18`
Expected: all suites pass (smoke + parse + history + extract).

- [ ] **Step 6: Commit**

```bash
git add lib/stats_logic/stats_extract.h lib/stats_logic/stats_extract.cpp test/test_extract/test_main.cpp
git commit -m "feat: data.json stats extraction (TDD)"
```

---

### Task 5: Board init refactor + landscape canvas + static dashboard

Moves the proven hardware bring-up out of `main.cpp` into a module, adds the PSRAM canvas in landscape, and renders the dashboard with FAKE data to validate layout on the panel. The bouncing-box demo is retired here (it remains in git history).

**Files:**
- Create: `src/board_init.h`, `src/board_init.cpp`
- Create: `src/ui_theme.h`
- Create: `src/ui_dashboard.h`, `src/ui_dashboard.cpp`
- Rewrite: `src/main.cpp`

- [ ] **Step 1: Extract hardware bring-up into `board_init`**

`src/board_init.h`:

```cpp
#pragma once
#include <Arduino_GFX_Library.h>
#include <Arduino_DriveBus_Library.h>

// Initializes display (CO5300 via QSPI), PSRAM canvas (landscape), expander,
// and CST816 touch — in the validated order. Halts with serial logging on
// unrecoverable failure.
void boardInit();

extern Arduino_Canvas *canvas;             // draw here, then canvas->flush()
extern Arduino_CO5300 *panel;              // for setBrightness only
extern std::unique_ptr<Arduino_IIC> Touch; // CST816
```

`src/board_init.cpp` (init order copied verbatim from the working demo — do not reorder):

```cpp
#include "board_init.h"
#include <Wire.h>
#include "pin_config.h"
#include "ui_theme.h"

Arduino_Canvas *canvas = nullptr;
Arduino_CO5300 *panel = nullptr;
std::unique_ptr<Arduino_IIC> Touch;

static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

// TCA9554 expander: raw Wire writes (a legacy-I2C-driver library here aborts
// the boot on core 3.3+ — see README "sharp edges").
static const uint8_t TCA9554_ADDR = 0x20;
static void tca9554_write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

static void touchInterrupt() { Touch->IIC_Interrupt_Flag = true; }

void boardInit() {
    // 1. Expander first: touch reset is gated behind it.
    Wire.begin(IIC_SDA, IIC_SCL);
    tca9554_write(0x03, (uint8_t)~0x07);  // P0-P2 outputs
    tca9554_write(0x01, 0x00);            // pulse low
    delay(20);
    tca9554_write(0x01, 0x07);            // then high

    // 2. Release the bus BEFORE DriveBus re-inits it (double begin() wedges
    //    the new I2C driver with ESP_ERR_INVALID_STATE).
    Wire.end();
    delay(50);

    // 3. Touch (CST816 at 0x15 on this board revision).
    static std::shared_ptr<Arduino_IIC_DriveBus> iicBus =
        std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
    Touch.reset(new Arduino_CST816x(iicBus, CST816T_DEVICE_ADDRESS,
                                    DRIVEBUS_DEFAULT_VALUE, TP_INT, touchInterrupt));
    while (Touch->begin() == false) {
        Serial.println("CST816x init failed, retrying...");
        delay(1000);
    }
    Serial.println("CST816x init OK");

    // 4. Display + landscape canvas (frame buffer lands in PSRAM).
    panel = new Arduino_CO5300(bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT,
                               16, 0, 0, 0);
    canvas = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel);
    if (!canvas->begin(GFX_NOT_DEFINED)) {
        Serial.println("FATAL: canvas->begin() failed (framebuffer alloc?)");
        while (true) delay(1000);
    }
    canvas->setRotation(UI_ROTATION);  // 448x368 logical
    panel->setBrightness(UI_BRIGHTNESS);
    Serial.printf("canvas %dx%d, free heap %u, free psram %u\n",
                  canvas->width(), canvas->height(),
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
}
```

- [ ] **Step 2: Create the theme**

`src/ui_theme.h`:

```cpp
#pragma once
#include <Arduino_GFX_Library.h>

// Landscape orientation. If during bring-up the image is upside-down relative
// to how the device sits (USB-C cable direction), change 1 -> 3 here and flip
// TOUCH_* in touch_input.cpp accordingly.
#define UI_ROTATION   1
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
```

- [ ] **Step 3: Implement the dashboard renderer**

`src/ui_dashboard.h`:

```cpp
#pragma once
#include <Arduino_GFX_Library.h>
#include "stats_extract.h"
#include "history.h"

struct DashHistories {
    History cpuLoad, cpuTemp, gpuLoad, gpuTemp;
};

// Renders the full dashboard into the canvas and flushes it.
// offline=true dims values and shows the OFFLINE badge.
void dashboardDraw(Arduino_Canvas *c, const Stats &s, const DashHistories &h, bool offline);
```

`src/ui_dashboard.cpp`:

```cpp
#include "ui_dashboard.h"
#include "ui_theme.h"
#include <cmath>

namespace {

// Layout: two graph panels + bottom tile row, 8 px margins/gaps.
//   panel1 y=8 h=124, panel2 y=140 h=124, bottom y=272 h=88
constexpr int M = 8;
constexpr int PANEL_W = SCREEN_W - 2 * M;   // 432
constexpr int PANEL_H = 124;
constexpr int P1_Y = 8, P2_Y = 140, BOT_Y = 272, BOT_H = 88;

void drawValueText(Arduino_Canvas *c, int xRight, int y, const char *txt,
                   uint16_t color, uint8_t size) {
    int w = strlen(txt) * 6 * size;
    c->setTextColor(color);
    c->setTextSize(size);
    c->setCursor(xRight - w, y);
    c->print(txt);
}

void fmt1(char *buf, size_t n, float v, const char *suffix) {
    if (std::isnan(v)) snprintf(buf, n, "--%s", suffix);
    else snprintf(buf, n, "%.1f%s", v, suffix);
}

void drawGraphPanel(Arduino_Canvas *c, int py, const char *label,
                    uint16_t color, uint16_t fillColor,
                    const History &load, const History &temp,
                    float loadNow, float tempNow, bool offline) {
    c->drawRoundRect(M, py, PANEL_W, PANEL_H, 10, COL_PANEL);

    // Graph area inside the panel, below the header line.
    const int gx = M + 8, gy = py + 30, gw = PANEL_W - 16, gh = PANEL_H - 38;
    uint16_t loadCol = offline ? COL_DIM : fillColor;
    uint16_t tempCol = offline ? COL_DIM : COL_TEMP;

    // Utilization: filled vertical bars, one per sample (~4 px wide).
    for (int i = 0; i < load.count; i++) {
        float v = load.at(i);
        if (std::isnan(v)) continue;
        int x = gx + (int)((float)i * gw / (HISTORY_LEN - 1));
        int hpx = (int)(v / 100.0f * gh);
        if (hpx > gh) hpx = gh;
        c->drawFastVLine(x, gy + gh - hpx, hpx, loadCol);
        c->drawFastVLine(x + 1, gy + gh - hpx, hpx, loadCol);
        c->drawFastVLine(x + 2, gy + gh - hpx, hpx, loadCol);
        c->drawFastVLine(x + 3, gy + gh - hpx, hpx, loadCol);
    }
    // Temperature: 2-px polyline over the same 0-100 scale.
    for (int i = 1; i < temp.count; i++) {
        float a = temp.at(i - 1), b = temp.at(i);
        if (std::isnan(a) || std::isnan(b)) continue;
        int x0 = gx + (int)((float)(i - 1) * gw / (HISTORY_LEN - 1));
        int x1 = gx + (int)((float)i * gw / (HISTORY_LEN - 1));
        int y0 = gy + gh - (int)(a / 100.0f * gh);
        int y1 = gy + gh - (int)(b / 100.0f * gh);
        c->drawLine(x0, y0, x1, y1, tempCol);
        c->drawLine(x0, y0 + 1, x1, y1 + 1, tempCol);
    }

    // Header: label left, current values right.
    c->setTextSize(2);
    c->setTextColor(offline ? COL_DIM : color);
    c->setCursor(M + 12, py + 8);
    c->print(label);

    char lbuf[16], tbuf[16];
    if (std::isnan(loadNow)) snprintf(lbuf, sizeof(lbuf), "--%%");
    else snprintf(lbuf, sizeof(lbuf), "%.0f%%", loadNow);
    if (std::isnan(tempNow)) snprintf(tbuf, sizeof(tbuf), "--");
    else snprintf(tbuf, sizeof(tbuf), "%.0f", tempNow);

    // temp (orange, with degree circle), right-aligned; load to its left
    int tw = (strlen(tbuf) * 6 + 6) * 3;
    drawValueText(c, M + PANEL_W - 12, py + 6, tbuf,
                  offline ? COL_DIM : COL_TEMP, 3);
    c->drawCircle(M + PANEL_W - 12 - 2, py + 8, 3, offline ? COL_DIM : COL_TEMP);
    drawValueText(c, M + PANEL_W - 12 - tw - 14, py + 6, lbuf,
                  offline ? COL_DIM : color, 3);
}

void drawArrow(Arduino_Canvas *c, int x, int y, bool down, uint16_t col) {
    if (down) c->fillTriangle(x, y, x + 10, y, x + 5, y + 10, col);
    else      c->fillTriangle(x, y + 10, x + 10, y + 10, x + 5, y, col);
}

}  // namespace

void dashboardDraw(Arduino_Canvas *c, const Stats &s, const DashHistories &h, bool offline) {
    c->fillScreen(COL_BG);

    drawGraphPanel(c, P1_Y, "CPU", COL_CPU, COL_CPU_FILL,
                   h.cpuLoad, h.cpuTemp, s.cpuLoad, s.cpuTemp, offline);
    drawGraphPanel(c, P2_Y, "GPU", COL_GPU, COL_GPU_FILL,
                   h.gpuLoad, h.gpuTemp, s.gpuLoad, s.gpuTemp, offline);

    // Bottom row: RAM tile (left, 45%) + NET tile (right, 55%).
    const int ramW = (PANEL_W - 8) * 45 / 100;
    const int netX = M + ramW + 8, netW = PANEL_W - ramW - 8;
    c->drawRoundRect(M, BOT_Y, ramW, BOT_H, 10, COL_PANEL);
    c->drawRoundRect(netX, BOT_Y, netW, BOT_H, 10, COL_PANEL);

    c->setTextSize(2);
    c->setTextColor(offline ? COL_DIM : COL_RAM);
    c->setCursor(M + 12, BOT_Y + 8);
    c->print("RAM");
    char buf[32];
    if (std::isnan(s.ramUsedGB) || std::isnan(s.ramTotalGB)) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%.1f/%.0f GB", s.ramUsedGB, s.ramTotalGB);
    c->setTextSize(3);
    c->setTextColor(offline ? COL_DIM : COL_TEXT);
    c->setCursor(M + 12, BOT_Y + 38);
    c->print(buf);

    c->setTextSize(2);
    c->setTextColor(offline ? COL_DIM : COL_NET);
    c->setCursor(netX + 12, BOT_Y + 8);
    c->print("NET Mbit/s");
    float dn = s.netDownBps * 8.0f / 1e6f, up = s.netUpBps * 8.0f / 1e6f;
    uint16_t vc = offline ? COL_DIM : COL_TEXT;
    drawArrow(c, netX + 12, BOT_Y + 42, true, offline ? COL_DIM : COL_NET);
    fmt1(buf, sizeof(buf), dn, "");
    c->setTextSize(3); c->setTextColor(vc);
    c->setCursor(netX + 28, BOT_Y + 38); c->print(buf);
    drawArrow(c, netX + netW / 2 + 4, BOT_Y + 42, false, offline ? COL_DIM : COL_NET);
    fmt1(buf, sizeof(buf), up, "");
    c->setCursor(netX + netW / 2 + 20, BOT_Y + 38); c->print(buf);

    if (offline) {
        const char *txt = "OFFLINE";
        int w = strlen(txt) * 6 * 2 + 20;
        int x = (SCREEN_W - w) / 2;
        c->fillRoundRect(x, 2, w, 22, 6, COL_ACCENT);
        c->setTextSize(2);
        c->setTextColor(COL_TEXT);
        c->setCursor(x + 10, 6);
        c->print(txt);
    }

    c->flush();
}
```

- [ ] **Step 4: Rewrite `main.cpp` to show the dashboard with fake animated data**

`src/main.cpp`:

```cpp
#include <Arduino.h>
#include "board_init.h"
#include "ui_dashboard.h"
#include <cmath>

static DashHistories hist;

void setup() {
    Serial.begin(115200);
    Serial.println("PC stats display booting (fake-data stage)...");
    boardInit();
}

void loop() {
    static uint32_t t = 0;
    t++;
    Stats s;
    s.cpuLoad = 50 + 40 * sinf(t * 0.10f);
    s.cpuTemp = 60 + 15 * sinf(t * 0.05f);
    s.gpuLoad = 50 + 45 * sinf(t * 0.07f + 1);
    s.gpuTemp = 55 + 20 * sinf(t * 0.04f + 2);
    s.ramUsedGB = 18.2f; s.ramTotalGB = 31.1f;
    s.netDownBps = 5.2e6f; s.netUpBps = 0.4e6f;
    hist.cpuLoad.push(s.cpuLoad); hist.cpuTemp.push(s.cpuTemp);
    hist.gpuLoad.push(s.gpuLoad); hist.gpuTemp.push(s.gpuTemp);

    dashboardDraw(canvas, s, hist, false);
    delay(200);  // fast-forward so the graphs fill quickly during verification
}
```

- [ ] **Step 5: Build and flash**

Run: `pio run -e amoled-18 -t upload`
Expected: `[SUCCESS]`; serial shows `canvas 448x368` and nonzero free psram.

- [ ] **Step 6: USER VERIFICATION (ask the user — cannot be checked from the PC):**
Confirm on the device: landscape dashboard with two panels (cyan CPU area + orange line, magenta GPU area + orange line) animating like waves, RAM/NET tiles below, no flicker. Ask whether the image orientation matches how the device sits (USB-C direction); if upside-down, change `UI_ROTATION` to 3, reflash, reconfirm.

- [ ] **Step 7: Commit**

```bash
git add src/ platformio.ini
git commit -m "feat: landscape canvas + dashboard renderer (fake data), board_init module"
```

---

### Task 6: Touch input — tap, long-press, swipe

**Files:**
- Create: `src/touch_input.h`, `src/touch_input.cpp`
- Modify: `src/main.cpp` (temporary debug overlay)

- [ ] **Step 1: Implement the touch wrapper**

`src/touch_input.h`:

```cpp
#pragma once
#include <Arduino_DriveBus_Library.h>

enum class TouchEvent { None, Tap, LongPress, SwipeUp, SwipeDown };

// Polls the CST816 and turns raw touches into events, in landscape coords.
// Call poll() every loop iteration (it is cheap when nothing is touching).
class TouchInput {
public:
    void begin(Arduino_IIC *driver) { _drv = driver; }
    TouchEvent poll();
    int16_t x() const { return _x; }  // landscape coords (0..447)
    int16_t y() const { return _y; }  // landscape coords (0..367)
    bool isDown() const { return _down; }

private:
    Arduino_IIC *_drv = nullptr;
    bool _down = false, _longFired = false;
    uint32_t _downAtMs = 0;
    int16_t _x = 0, _y = 0, _startX = 0, _startY = 0;
};
```

`src/touch_input.cpp`:

```cpp
#include "touch_input.h"
#include "ui_theme.h"
#include "pin_config.h"

// Raw panel coords are portrait (x 0..367, y 0..447). Map to landscape for
// UI_ROTATION 1. If UI_ROTATION is 3, use: lx = (447 - rawY); ly = rawX.
static void mapToLandscape(int32_t rawX, int32_t rawY, int16_t &lx, int16_t &ly) {
#if UI_ROTATION == 1
    lx = (int16_t)rawY;
    ly = (int16_t)(LCD_WIDTH - 1 - rawX);
#else
    lx = (int16_t)(LCD_HEIGHT - 1 - rawY);
    ly = (int16_t)rawX;
#endif
}

TouchEvent TouchInput::poll() {
    int32_t fingers = _drv->IIC_Read_Device_Value(
        _drv->Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);
    bool touching = fingers > 0;

    if (touching) {
        int32_t rx = _drv->IIC_Read_Device_Value(
            _drv->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
        int32_t ry = _drv->IIC_Read_Device_Value(
            _drv->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
        mapToLandscape(rx, ry, _x, _y);
    }

    if (touching && !_down) {                    // press started
        _down = true; _longFired = false;
        _downAtMs = millis();
        _startX = _x; _startY = _y;
        return TouchEvent::None;
    }
    if (touching && _down) {                     // held
        int dx = abs(_x - _startX), dy = abs(_y - _startY);
        if (!_longFired && millis() - _downAtMs >= 2000 && dx < 20 && dy < 20) {
            _longFired = true;
            return TouchEvent::LongPress;
        }
        return TouchEvent::None;
    }
    if (!touching && _down) {                    // released
        _down = false;
        if (_longFired) return TouchEvent::None; // long-press already consumed it
        int dy = _y - _startY;
        uint32_t heldMs = millis() - _downAtMs;
        if (abs(dy) >= 40) return dy < 0 ? TouchEvent::SwipeUp : TouchEvent::SwipeDown;
        if (heldMs <= 400 && abs(_x - _startX) < 16 && abs(dy) < 16) return TouchEvent::Tap;
    }
    return TouchEvent::None;
}
```

- [ ] **Step 2: Temporary debug overlay in `main.cpp`**

Add to `src/main.cpp` (keep the fake dashboard; additions marked):

```cpp
#include "touch_input.h"          // add at top
static TouchInput touch;          // add at file scope

// in setup(), after boardInit():
    touch.begin(Touch.get());

// REPLACE the existing loop() with:
void loop() {
    static uint32_t t = 0;
    static uint32_t lastFrame = 0;

    TouchEvent ev = touch.poll();
    if (ev == TouchEvent::Tap)       Serial.printf("TAP at %d,%d\n", touch.x(), touch.y());
    if (ev == TouchEvent::LongPress) Serial.println("LONG PRESS");
    if (ev == TouchEvent::SwipeUp)   Serial.println("SWIPE UP");
    if (ev == TouchEvent::SwipeDown) Serial.println("SWIPE DOWN");

    if (millis() - lastFrame > 200) {
        lastFrame = millis();
        t++;
        Stats s;
        s.cpuLoad = 50 + 40 * sinf(t * 0.10f);
        s.cpuTemp = 60 + 15 * sinf(t * 0.05f);
        s.gpuLoad = 50 + 45 * sinf(t * 0.07f + 1);
        s.gpuTemp = 55 + 20 * sinf(t * 0.04f + 2);
        s.ramUsedGB = 18.2f; s.ramTotalGB = 31.1f;
        s.netDownBps = 5.2e6f; s.netUpBps = 0.4e6f;
        hist.cpuLoad.push(s.cpuLoad); hist.cpuTemp.push(s.cpuTemp);
        hist.gpuLoad.push(s.gpuLoad); hist.gpuTemp.push(s.gpuTemp);
        dashboardDraw(canvas, s, hist, false);
        // debug: draw last touch point
        if (touch.isDown()) { canvas->fillCircle(touch.x(), touch.y(), 6, COL_ACCENT); canvas->flush(); }
    }
    delay(5);
}
```

Also add `#include "ui_theme.h"` to main.cpp if not present.

- [ ] **Step 3: Flash and verify over serial + screen**

Run: `pio run -e amoled-18 -t upload`, then capture serial ~20 s while the user touches the screen.
**USER VERIFICATION:** ask the user to (a) tap all four corners — the red dot must appear UNDER the finger in each corner (if mirrored/swapped, fix `mapToLandscape` per the comment and reflash); (b) hold 2 s → serial prints `LONG PRESS`; (c) swipe up/down → serial prints the swipe lines.

- [ ] **Step 4: Commit**

```bash
git add src/touch_input.h src/touch_input.cpp src/main.cpp
git commit -m "feat: touch input events (tap, long-press, swipe) with landscape mapping"
```

---

### Task 7: Persistent settings (NVS)

**Files:**
- Create: `src/app_config.h`, `src/app_config.cpp`
- Modify: `src/main.cpp` (one-boot serial verification, then removed in Task 8)

- [ ] **Step 1: Implement**

`src/app_config.h`:

```cpp
#pragma once
#include <Arduino.h>

// Persisted settings (NVS namespace "statsdisp"): WiFi credentials and the
// discovered LibreHardwareMonitor server address.
struct AppConfig {
    String ssid, password, serverHost;

    bool hasWifi() const { return ssid.length() > 0; }
    bool hasServer() const { return serverHost.length() > 0; }

    void load();
    void saveWifi(const String &newSsid, const String &newPassword);
    void saveServer(const String &host);
    void clearServer();
    void clearAll();
};

extern AppConfig appConfig;
```

`src/app_config.cpp`:

```cpp
#include "app_config.h"
#include <Preferences.h>

AppConfig appConfig;

static Preferences prefs;
static const char *NS = "statsdisp";

void AppConfig::load() {
    prefs.begin(NS, true);
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("pass", "");
    serverHost = prefs.getString("server", "");
    prefs.end();
}

void AppConfig::saveWifi(const String &newSsid, const String &newPassword) {
    ssid = newSsid; password = newPassword;
    prefs.begin(NS, false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.end();
}

void AppConfig::saveServer(const String &host) {
    serverHost = host;
    prefs.begin(NS, false);
    prefs.putString("server", serverHost);
    prefs.end();
}

void AppConfig::clearServer() {
    serverHost = "";
    prefs.begin(NS, false);
    prefs.remove("server");
    prefs.end();
}

void AppConfig::clearAll() {
    ssid = ""; password = ""; serverHost = "";
    prefs.begin(NS, false);
    prefs.clear();
    prefs.end();
}
```

- [ ] **Step 2: Verify round-trip on device**

Add temporarily at the END of `setup()` in `main.cpp`:

```cpp
    appConfig.load();
    Serial.printf("config: ssid='%s' server='%s'\n", appConfig.ssid.c_str(), appConfig.serverHost.c_str());
    if (!appConfig.hasWifi()) {
        appConfig.saveWifi("TestNet", "TestPass");
        Serial.println("config: wrote test values, REBOOT DEVICE to verify persistence");
    } else if (appConfig.ssid == "TestNet") {
        Serial.println("config: PERSISTENCE OK");
        appConfig.clearAll();
        Serial.println("config: cleared");
    }
```

(plus `#include "app_config.h"` at the top.)

Flash, capture serial → expect `wrote test values`. Power-cycle (or `esptool ... read-mac` to force reset), capture again → expect `PERSISTENCE OK` then `cleared`.

- [ ] **Step 3: Remove the temporary block** (keep `appConfig.load()` and the first printf).

- [ ] **Step 4: Build clean**

Run: `pio run -e amoled-18`
Expected: `[SUCCESS]`.

- [ ] **Step 5: Commit**

```bash
git add src/app_config.h src/app_config.cpp src/main.cpp
git commit -m "feat: persistent settings in NVS (wifi credentials, server host)"
```

---

### Task 8: Setup UI — shared widgets + WiFi network list

**Files:**
- Create: `src/ui_setup.h`, `src/ui_setup.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Implement header + list screen**

`src/ui_setup.h`:

```cpp
#pragma once
#include <Arduino_GFX_Library.h>
#include "touch_input.h"

// Blocking setup screens. Each pumps touch + redraws internally and returns
// when the user completes or backs out of that screen.

// Scans and lists networks (strongest first). Returns chosen SSID.
String setupPickNetwork(Arduino_Canvas *c, TouchInput &touch);

// On-screen QWERTY password entry. Returns the password, or "" if user tapped back.
String setupEnterPassword(Arduino_Canvas *c, TouchInput &touch, const String &ssid);

// Joins the network, drawing status. True on success (got IP) within timeoutMs.
bool setupConnectWifi(Arduino_Canvas *c, const String &ssid, const String &password,
                      uint32_t timeoutMs = 15000);

// Full-screen message with an OK-style tap-anywhere dismissal.
void setupShowMessage(Arduino_Canvas *c, TouchInput &touch, const char *line1, const char *line2);
```

`src/ui_setup.cpp` (list screen part — the keyboard is Task 9):

```cpp
#include "ui_setup.h"
#include "ui_theme.h"
#include <WiFi.h>

namespace {

constexpr int ROW_H = 46, ROW_GAP = 6, LIST_Y = 44, VISIBLE_ROWS = 6;

struct Net { String ssid; int32_t rssi; };

void drawHeader(Arduino_Canvas *c, const char *title, const char *right) {
    c->setTextSize(2);
    c->setTextColor(COL_ACCENT);
    c->setCursor(12, 12);
    c->print(title);
    if (right) {
        c->setTextColor(COL_DIM);
        c->setCursor(SCREEN_W - 12 - strlen(right) * 12, 12);
        c->print(right);
    }
}

void drawSignalBars(Arduino_Canvas *c, int x, int y, int32_t rssi, uint16_t col) {
    int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
    for (int b = 0; b < 4; b++) {
        int h = 6 + b * 5;
        uint16_t bc = (b < bars) ? col : COL_FIELD;
        c->fillRect(x + b * 8, y + 21 - h, 5, h, bc);
    }
}

int collectNetworks(Net *out, int maxOut) {
    int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/false);
    int m = 0;
    for (int i = 0; i < n && m < maxOut; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        bool dup = false;
        for (int j = 0; j < m; j++)
            if (out[j].ssid == ssid) { dup = true; if (WiFi.RSSI(i) > out[j].rssi) out[j].rssi = WiFi.RSSI(i); }
        if (!dup) out[m++] = { ssid, WiFi.RSSI(i) };
    }
    // sort by rssi desc (tiny n: insertion sort)
    for (int i = 1; i < m; i++)
        for (int j = i; j > 0 && out[j].rssi > out[j - 1].rssi; j--) {
            Net t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
        }
    WiFi.scanDelete();
    return m;
}

void drawList(Arduino_Canvas *c, Net *nets, int n, int offset) {
    c->fillScreen(COL_BG);
    drawHeader(c, "CHOOSE WIFI", n > VISIBLE_ROWS ? "swipe" : nullptr);
    for (int r = 0; r < VISIBLE_ROWS; r++) {
        int i = offset + r;
        if (i >= n) break;
        int y = LIST_Y + r * (ROW_H + ROW_GAP);
        c->drawRoundRect(8, y, SCREEN_W - 16, ROW_H, 9, i == offset ? COL_ACCENT : COL_PANEL);
        c->setTextSize(2);
        c->setTextColor(COL_TEXT);
        c->setCursor(20, y + 14);
        String shown = nets[i].ssid.length() > 24 ? nets[i].ssid.substring(0, 24) : nets[i].ssid;
        c->print(shown);
        drawSignalBars(c, SCREEN_W - 56, y + 12, nets[i].rssi, COL_ACCENT);
    }
    c->flush();
}

}  // namespace

String setupPickNetwork(Arduino_Canvas *c, TouchInput &touch) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    static Net nets[24];
    c->fillScreen(COL_BG);
    drawHeader(c, "CHOOSE WIFI", "scanning");
    c->flush();
    int n = collectNetworks(nets, 24);
    int offset = 0;
    uint32_t lastScan = millis();
    drawList(c, nets, n, offset);

    while (true) {
        TouchEvent ev = touch.poll();
        if (ev == TouchEvent::Tap) {
            int row = (touch.y() - LIST_Y) / (ROW_H + ROW_GAP);
            int i = offset + row;
            if (touch.y() >= LIST_Y && row >= 0 && row < VISIBLE_ROWS && i < n)
                return nets[i].ssid;
        }
        if (ev == TouchEvent::SwipeUp && offset + VISIBLE_ROWS < n) { offset++; drawList(c, nets, n, offset); }
        if (ev == TouchEvent::SwipeDown && offset > 0)              { offset--; drawList(c, nets, n, offset); }
        if (millis() - lastScan > 8000 && n == 0) {  // auto-rescan only while empty
            lastScan = millis();
            n = collectNetworks(nets, 24);
            drawList(c, nets, n, offset);
        }
        delay(10);
    }
}

bool setupConnectWifi(Arduino_Canvas *c, const String &ssid, const String &password,
                      uint32_t timeoutMs) {
    c->fillScreen(COL_BG);
    drawHeader(c, "CONNECTING", nullptr);
    c->setTextSize(2);
    c->setTextColor(COL_TEXT);
    c->setCursor(12, 60);
    c->print(ssid);
    c->flush();

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), password.c_str());
    uint32_t start = millis();
    int dots = 0;
    while (millis() - start < timeoutMs) {
        if (WiFi.status() == WL_CONNECTED) return true;
        c->fillRect(12, 100, 200, 20, COL_BG);
        c->setCursor(12, 100);
        c->setTextColor(COL_ACCENT);
        for (int i = 0; i < (dots % 6) + 1; i++) c->print(".");
        c->flush();
        dots++;
        delay(400);
    }
    WiFi.disconnect();
    return false;
}

void setupShowMessage(Arduino_Canvas *c, TouchInput &touch, const char *line1, const char *line2) {
    c->fillScreen(COL_BG);
    c->setTextSize(2);
    c->setTextColor(COL_ACCENT);
    c->setCursor(12, 130); c->print(line1);
    c->setTextColor(COL_TEXT);
    c->setCursor(12, 170); if (line2) c->print(line2);
    c->setTextColor(COL_DIM);
    c->setCursor(12, 330); c->print("tap to continue");
    c->flush();
    while (touch.poll() != TouchEvent::Tap) delay(10);
}
```

- [ ] **Step 2: Wire a temporary test into `main.cpp`**

Replace `loop()` with:

```cpp
void loop() {
    String ssid = setupPickNetwork(canvas, touch);
    Serial.printf("picked: '%s'\n", ssid.c_str());
    setupShowMessage(canvas, touch, "You picked:", ssid.c_str());
}
```

(add `#include "ui_setup.h"`.)

- [ ] **Step 3: Flash and verify**

Run: `pio run -e amoled-18 -t upload`, capture serial 30 s.
**USER VERIFICATION:** real nearby networks listed strongest-first with bars; swipe scrolls if >6; tapping a row shows the "You picked" screen and serial prints the SSID.

- [ ] **Step 4: Commit**

```bash
git add src/ui_setup.h src/ui_setup.cpp src/main.cpp
git commit -m "feat: setup UI - wifi scan list, connect helper, message screen"
```

---

### Task 9: Setup UI — on-screen keyboard

**Files:**
- Modify: `src/ui_setup.cpp` (add the keyboard implementation)
- Modify: `src/main.cpp` (extend the temporary test)

- [ ] **Step 1: Add the keyboard to `ui_setup.cpp`** (append inside the file, implementing the declared `setupEnterPassword`)

```cpp
namespace {

// Three layers: 0 = lowercase, 1 = uppercase (shift), 2 = symbols.
static const char *KB_ROW1[3] = {"qwertyuiop", "QWERTYUIOP", "1234567890"};
static const char *KB_ROW2[3] = {"asdfghjkl",  "ASDFGHJKL",  "-_.:;!?@#"};
static const char *KB_ROW3[3] = {"zxcvbnm",    "ZXCVBNM",    "+*/=()[]&"};

constexpr int KB_Y = 130;          // keyboard block top
constexpr int KEY_H = 48, KEY_GAP = 4;

struct Key { int x, y, w, h; char ch; uint8_t action; };  // action: 0 char, 1 shift, 2 layer, 3 backspace, 4 space, 5 ok, 6 back
constexpr int MAX_KEYS = 40;

int buildKeys(Key *keys, int layer) {
    int n = 0;
    const char *rows[3] = {KB_ROW1[layer], KB_ROW2[layer], KB_ROW3[layer]};
    for (int r = 0; r < 3; r++) {
        int len = strlen(rows[r]);
        int keyW = (SCREEN_W - 8 - (len - 1) * KEY_GAP - (r == 2 ? 2 * 52 : 0)) / len;
        int x = 4 + (r == 1 ? keyW / 2 : 0) + (r == 2 ? 52 : 0);
        int y = KB_Y + r * (KEY_H + KEY_GAP);
        for (int i = 0; i < len; i++) {
            keys[n++] = { x, y, keyW, KEY_H, rows[r][i], 0 };
            x += keyW + KEY_GAP;
        }
        if (r == 2) {  // shift left of Z row, backspace right
            keys[n++] = { 4, y, 44, KEY_H, 0, 1 };
            keys[n++] = { SCREEN_W - 4 - 44, y, 44, KEY_H, 0, 3 };
        }
    }
    int y = KB_Y + 3 * (KEY_H + KEY_GAP);
    keys[n++] = { 4,   y, 70,  KEY_H, 0, 2 };                      // 123# / abc
    keys[n++] = { 78,  y, 90,  KEY_H, 0, 6 };                      // back
    keys[n++] = { 172, y, 152, KEY_H, ' ', 4 };                    // space
    keys[n++] = { 328, y, SCREEN_W - 4 - 328, KEY_H, 0, 5 };       // OK
    return n;
}

void drawKeyboard(Arduino_Canvas *c, const String &ssid, const String &entered,
                  Key *keys, int nKeys, int layer) {
    c->fillScreen(COL_BG);
    c->setTextSize(2);
    c->setTextColor(COL_ACCENT);
    c->setCursor(8, 8);
    c->print(ssid.length() > 22 ? ssid.substring(0, 22) : ssid);

    // password field (last 24 chars visible) with cursor
    c->drawRoundRect(8, 40, SCREEN_W - 16, 44, 8, COL_PANEL);
    c->setTextColor(COL_TEXT);
    c->setCursor(18, 52);
    String shown = entered.length() > 24 ? entered.substring(entered.length() - 24) : entered;
    c->print(shown);
    int cx = 18 + shown.length() * 12;
    c->drawFastVLine(cx + 2, 48, 28, COL_ACCENT);

    for (int i = 0; i < nKeys; i++) {
        const Key &k = keys[i];
        uint16_t border = (k.action == 5) ? COL_ACCENT : COL_PANEL;
        c->drawRoundRect(k.x, k.y, k.w, k.h, 7, border);
        c->setTextColor(k.action == 5 ? COL_ACCENT : COL_TEXT);
        c->setTextSize(2);
        const char *lbl = nullptr;
        char chbuf[2] = {k.ch, 0};
        switch (k.action) {
            case 0: case 4: lbl = chbuf; break;
            case 1: lbl = "^";  break;
            case 2: lbl = (layer == 2) ? "abc" : "12#"; break;
            case 3: lbl = "<x"; break;
            case 5: lbl = "OK"; break;
            case 6: lbl = "back"; break;
        }
        if (k.action == 4) lbl = "_";
        int tw = strlen(lbl) * 12;
        c->setCursor(k.x + (k.w - tw) / 2, k.y + 16);
        c->print(lbl);
    }
    c->flush();
}

}  // namespace

String setupEnterPassword(Arduino_Canvas *c, TouchInput &touch, const String &ssid) {
    String entered = "";
    int layer = 0;
    bool shiftOnce = false;
    static Key keys[MAX_KEYS];
    int nKeys = buildKeys(keys, layer);
    drawKeyboard(c, ssid, entered, keys, nKeys, layer);

    while (true) {
        TouchEvent ev = touch.poll();
        if (ev != TouchEvent::Tap) { delay(5); continue; }
        for (int i = 0; i < nKeys; i++) {
            const Key &k = keys[i];
            if (touch.x() < k.x || touch.x() >= k.x + k.w ||
                touch.y() < k.y || touch.y() >= k.y + k.h) continue;
            switch (k.action) {
                case 0: case 4:
                    entered += k.ch;
                    if (shiftOnce && layer == 1) { layer = 0; shiftOnce = false; nKeys = buildKeys(keys, layer); }
                    break;
                case 1:  // shift toggles lower<->upper
                    layer = (layer == 1) ? 0 : 1; shiftOnce = (layer == 1);
                    nKeys = buildKeys(keys, layer);
                    break;
                case 2:  // symbols toggle
                    layer = (layer == 2) ? 0 : 2; shiftOnce = false;
                    nKeys = buildKeys(keys, layer);
                    break;
                case 3: if (entered.length()) entered.remove(entered.length() - 1); break;
                case 5: return entered;       // OK
                case 6: return "";            // back
            }
            drawKeyboard(c, ssid, entered, keys, nKeys, layer);
            break;
        }
    }
}
```

- [ ] **Step 2: Extend the temporary `main.cpp` test loop**

```cpp
void loop() {
    String ssid = setupPickNetwork(canvas, touch);
    String pass = setupEnterPassword(canvas, touch, ssid);
    Serial.printf("ssid='%s' pass='%s'\n", ssid.c_str(), pass.c_str());
    if (pass.length() == 0) return;  // back -> rescan
    bool ok = setupConnectWifi(canvas, ssid, pass);
    Serial.printf("connect: %s, IP %s\n", ok ? "OK" : "FAIL",
                  WiFi.localIP().toString().c_str());
    setupShowMessage(canvas, touch, ok ? "Connected!" : "Failed - check password",
                     ok ? WiFi.localIP().toString().c_str() : ssid.c_str());
}
```

(add `#include <WiFi.h>`.)

- [ ] **Step 3: Flash and verify end-to-end WiFi join**

Run: `pio run -e amoled-18 -t upload`; capture serial ~60 s while the user does setup.
**USER VERIFICATION:** type the real WiFi password on the device (test shift + symbol layers + backspace while at it), tap OK → screen shows "Connected!" with an IP, serial shows `connect: OK, IP 192.168.x.x`. Also verify the wrong-password path shows "Failed".

- [ ] **Step 4: Commit**

```bash
git add src/ui_setup.cpp src/main.cpp
git commit -m "feat: on-screen QWERTY keyboard, end-to-end wifi join from device"
```

---

### Task 10: PC-side check + server discovery sweep

**Files:**
- Create: `src/discovery.h`, `src/discovery.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Verify the PC accepts remote connections (run on the PC, not the device)**

Run: `netstat -ano | findstr :8085`
Expected: a line with `0.0.0.0:8085` (listening on all interfaces).
If it shows only `127.0.0.1:8085`: ask the user to open LibreHardwareMonitor → Options → Remote Web Server → check it allows remote access, then re-run.
Then add a firewall rule (will prompt UAC; ask the user to accept):

```powershell
Start-Process powershell -Verb RunAs -ArgumentList '-Command','New-NetFirewallRule -DisplayName "LibreHardwareMonitor 8085" -Direction Inbound -Protocol TCP -LocalPort 8085 -Action Allow'
```

Verify: `Get-NetFirewallRule -DisplayName "LibreHardwareMonitor 8085"` shows `Enabled : True`.

- [ ] **Step 2: Implement the discovery module**

`src/discovery.h`:

```cpp
#pragma once
#include <Arduino.h>
#include <functional>

// Sweeps the device's own /24 subnet for a LibreHardwareMonitor web server.
// Returns the host IP as a string ("192.168.1.50") or "" if none found.
// progress(current, total, hostBeingChecked) is called as the sweep advances.
String discoverLHM(uint16_t port,
                   std::function<void(int current, int total, const char *host)> progress);

// True if host:port answers data.json with LHM-shaped JSON. Used by discovery
// and by the dashboard to validate a saved server.
bool verifyLHM(const String &host, uint16_t port);
```

`src/discovery.cpp`:

```cpp
#include "discovery.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

bool verifyLHM(const String &host, uint16_t port) {
    HTTPClient http;
    WiFiClient client;
    http.setConnectTimeout(1200);
    http.setTimeout(1500);
    http.useHTTP10(true);
    if (!http.begin(client, "http://" + host + ":" + String(port) + "/data.json")) return false;
    int code = http.GET();
    bool ok = false;
    if (code == 200) {
        // Read just the first chunk and look for the LHM root marker.
        uint8_t buf[96];
        int n = http.getStreamPtr()->readBytes(buf, sizeof(buf) - 1);
        buf[n > 0 ? n : 0] = 0;
        ok = strstr((const char *)buf, "\"Text\":\"Sensor\"") != nullptr ||
             strstr((const char *)buf, "\"id\":0") != nullptr;
    }
    http.end();
    return ok;
}

// Non-blocking TCP connect sweep in batches of 16, ~400 ms per batch.
String discoverLHM(uint16_t port,
                   std::function<void(int, int, const char *)> progress) {
    uint32_t local = (uint32_t)WiFi.localIP();      // network byte order (little-endian octets)
    uint32_t base = local & 0x00FFFFFF;             // x.y.z.0
    uint8_t selfLast = (local >> 24) & 0xFF;

    constexpr int BATCH = 16;
    for (int start = 1; start <= 254; start += BATCH) {
        int socks[BATCH];
        uint8_t lasts[BATCH];
        int nb = 0;
        for (int last = start; last < start + BATCH && last <= 254; last++) {
            if (last == selfLast) continue;
            int s = socket(AF_INET, SOCK_STREAM, 0);
            if (s < 0) continue;
            fcntl(s, F_SETFL, O_NONBLOCK);
            struct sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = base | ((uint32_t)last << 24);
            connect(s, (struct sockaddr *)&addr, sizeof(addr));  // returns EINPROGRESS
            socks[nb] = s; lasts[nb] = last; nb++;
        }

        IPAddress probe(base | ((uint32_t)start << 24));
        progress(start, 254, probe.toString().c_str());

        // Wait up to 400 ms for any connection to complete.
        uint32_t t0 = millis();
        String found = "";
        while (millis() - t0 < 400 && found.length() == 0) {
            for (int i = 0; i < nb; i++) {
                if (socks[i] < 0) continue;
                fd_set wset; FD_ZERO(&wset); FD_SET(socks[i], &wset);
                struct timeval tv = {0, 1000};
                if (select(socks[i] + 1, nullptr, &wset, nullptr, &tv) > 0) {
                    int err = 0; socklen_t len = sizeof(err);
                    getsockopt(socks[i], SOL_SOCKET, SO_ERROR, &err, &len);
                    close(socks[i]); socks[i] = -1;
                    if (err == 0) {
                        IPAddress ip(base | ((uint32_t)lasts[i] << 24));
                        if (verifyLHM(ip.toString(), port)) { found = ip.toString(); break; }
                    }
                }
            }
            delay(5);
        }
        for (int i = 0; i < nb; i++) if (socks[i] >= 0) close(socks[i]);
        if (found.length()) return found;
    }
    return "";
}
```

- [ ] **Step 3: Add the discovery progress screen to `ui_setup.h/.cpp`**

Add to `ui_setup.h`:

```cpp
// Runs the discovery sweep with a progress screen. Returns server IP or "".
String setupDiscoverServer(Arduino_Canvas *c);
```

Add to `ui_setup.cpp`:

```cpp
#include "discovery.h"

String setupDiscoverServer(Arduino_Canvas *c) {
    c->fillScreen(COL_BG);
    drawHeader(c, "FINDING YOUR PC", nullptr);
    c->setTextSize(2);
    c->setTextColor(COL_DIM);
    c->setCursor(12, 320);
    c->print("LibreHardwareMonitor, port 8085");
    c->flush();

    String ip = discoverLHM(8085, [c](int cur, int total, const char *host) {
        int barW = SCREEN_W - 80;
        c->fillRect(40, 160, barW, 12, COL_FIELD);
        c->fillRect(40, 160, barW * cur / total, 12, COL_ACCENT);
        c->fillRect(12, 200, SCREEN_W - 24, 22, COL_BG);
        c->setTextSize(2);
        c->setTextColor(COL_TEXT);
        c->setCursor(120, 200);
        c->print(host);
        c->flush();
    });
    return ip;
}
```

- [ ] **Step 4: Temporary `main.cpp` test — full setup chain**

```cpp
void loop() {
    appConfig.load();
    if (!appConfig.hasWifi()) {
        while (true) {
            String ssid = setupPickNetwork(canvas, touch);
            String pass = setupEnterPassword(canvas, touch, ssid);
            if (pass.length() == 0) continue;
            if (setupConnectWifi(canvas, ssid, pass)) { appConfig.saveWifi(ssid, pass); break; }
            setupShowMessage(canvas, touch, "Couldn't connect", "Check the password");
        }
    } else {
        setupConnectWifi(canvas, appConfig.ssid, appConfig.password);
    }
    String ip = setupDiscoverServer(canvas);
    Serial.printf("discovered: '%s'\n", ip.c_str());
    setupShowMessage(canvas, touch, ip.length() ? "Found your PC!" : "Nothing found",
                     ip.c_str());
    delay(5000);
}
```

- [ ] **Step 5: Flash and verify**

Run: `pio run -e amoled-18 -t upload`; capture serial ~90 s.
**USER VERIFICATION:** progress bar sweeps; within ~30 s the screen shows "Found your PC!" with this PC's LAN IP; serial prints `discovered: '192.168.x.x'`. (If nothing found: re-check Step 1 firewall/binding.)

- [ ] **Step 6: Commit**

```bash
git add src/discovery.h src/discovery.cpp src/ui_setup.h src/ui_setup.cpp src/main.cpp
git commit -m "feat: LAN discovery sweep for the LHM server + progress screen"
```

---

### Task 11: Stats client (HTTP + JSON on device)

**Files:**
- Create: `src/stats_client.h`, `src/stats_client.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Implement**

`src/stats_client.h`:

```cpp
#pragma once
#include <Arduino.h>
#include "stats_extract.h"

// Fetches and parses data.json from the configured server.
class StatsClient {
public:
    void begin(const String &host, uint16_t port = 8085);
    // Blocking (~150-600 ms). Returns true and fills `out` on success.
    bool fetch(Stats &out);

private:
    String _url;
};
```

`src/stats_client.cpp`:

```cpp
#include "stats_client.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// data.json is ~56 KB; parse it into PSRAM, not internal heap.
struct PsramAllocator : ArduinoJson::Allocator {
    void *allocate(size_t n) override { return ps_malloc(n); }
    void deallocate(void *p) override { free(p); }
    void *reallocate(void *p, size_t n) override { return ps_realloc(p, n); }
};
static PsramAllocator psramAlloc;

void StatsClient::begin(const String &host, uint16_t port) {
    _url = "http://" + host + ":" + String(port) + "/data.json";
}

bool StatsClient::fetch(Stats &out) {
    HTTPClient http;
    WiFiClient client;
    http.setConnectTimeout(1500);
    http.setTimeout(2500);
    http.useHTTP10(true);  // plain stream (no chunked encoding) for ArduinoJson
    if (!http.begin(client, _url)) return false;
    int code = http.GET();
    if (code != 200) { http.end(); return false; }

    JsonDocument doc(&psramAlloc);
    DeserializationError err = deserializeJson(
        doc, http.getStream(), DeserializationOption::NestingLimit(20));
    http.end();
    if (err) {
        Serial.printf("stats: json error %s\n", err.c_str());
        return false;
    }
    Stats fresh;
    int found = extractStats(doc.as<JsonVariantConst>(), fresh);
    if (found == 0) return false;
    out = fresh;
    return true;
}
```

- [ ] **Step 2: Temporary `main.cpp` loop — print live stats at 1 Hz**

After the setup/discovery chain from Task 10 succeeds (`ip` non-empty), replace the trailing `setupShowMessage` + delay with:

```cpp
    appConfig.saveServer(ip);
    StatsClient sc;
    sc.begin(appConfig.serverHost);
    while (true) {
        Stats s;
        uint32_t t0 = millis();
        bool ok = sc.fetch(s);
        Serial.printf("fetch %s in %lums: cpu %.1f%% %.1fC | gpu %.1f%% %.1fC | ram %.1f/%.1f | net v%.2f ^%.2f Mbit\n",
                      ok ? "OK" : "FAIL", millis() - t0,
                      s.cpuLoad, s.cpuTemp, s.gpuLoad, s.gpuTemp,
                      s.ramUsedGB, s.ramTotalGB,
                      s.netDownBps * 8 / 1e6, s.netUpBps * 8 / 1e6);
        delay(1000);
    }
```

(add `#include "stats_client.h"`.)

- [ ] **Step 3: Flash and verify values against LibreHardwareMonitor's window**

Run: `pio run -e amoled-18 -t upload`; capture serial ~60 s.
Expected: one `fetch OK in <600>ms` line per second with plausible numbers.
**USER VERIFICATION:** numbers match what LibreHardwareMonitor's own window shows (within rounding / a second of lag).

- [ ] **Step 4: Commit**

```bash
git add src/stats_client.h src/stats_client.cpp src/main.cpp
git commit -m "feat: stats client - HTTP fetch + PSRAM JSON parse at 1 Hz"
```

---

### Task 12: Final wiring — full application flow

**Files:**
- Rewrite: `src/main.cpp` (final form)

- [ ] **Step 1: Write the final `main.cpp`**

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include "board_init.h"
#include "app_config.h"
#include "touch_input.h"
#include "ui_setup.h"
#include "ui_dashboard.h"
#include "stats_client.h"
#include "ui_theme.h"

static TouchInput touch;
static StatsClient statsClient;
static DashHistories hist;

// Runs the interactive setup until WiFi is connected; saves credentials.
static void runWifiSetup() {
    while (true) {
        String ssid = setupPickNetwork(canvas, touch);
        String pass = setupEnterPassword(canvas, touch, ssid);
        if (pass.length() == 0) continue;  // back -> rescan
        if (setupConnectWifi(canvas, ssid, pass)) {
            appConfig.saveWifi(ssid, pass);
            return;
        }
        setupShowMessage(canvas, touch, "Couldn't connect", "Check the password");
    }
}

// Ensures a server is known and answering; runs discovery as needed.
static bool ensureServer() {
    if (appConfig.hasServer() && verifyLHM(appConfig.serverHost, 8085)) return true;
    String ip = setupDiscoverServer(canvas);
    if (ip.length() == 0) return false;
    appConfig.saveServer(ip);
    return true;
}

// Dashboard loop. Returns when the user long-presses (to re-enter setup).
static void runDashboard() {
    statsClient.begin(appConfig.serverHost);
    Stats last;
    int failStreak = 0;
    uint32_t lastFetch = 0, lastRediscover = 0;

    while (true) {
        TouchEvent ev = touch.poll();
        if (ev == TouchEvent::LongPress) return;

        if (millis() - lastFetch >= 1000) {
            lastFetch = millis();
            Stats s;
            if (statsClient.fetch(s)) {
                failStreak = 0;
                last = s;
                hist.cpuLoad.push(s.cpuLoad); hist.cpuTemp.push(s.cpuTemp);
                hist.gpuLoad.push(s.gpuLoad); hist.gpuTemp.push(s.gpuTemp);
                dashboardDraw(canvas, last, hist, false);
            } else {
                failStreak++;
                dashboardDraw(canvas, last, hist, true);
                // After repeated failures the PC's IP may have changed:
                // re-discover at most every 30 s (sweep is blocking ~10-30 s;
                // graphs are paused while offline anyway).
                if (failStreak >= 3 && WiFi.status() == WL_CONNECTED &&
                    millis() - lastRediscover > 30000) {
                    lastRediscover = millis();
                    String ip = setupDiscoverServer(canvas);
                    if (ip.length()) {
                        appConfig.saveServer(ip);
                        statsClient.begin(ip);
                        failStreak = 0;
                    }
                }
            }
        }

        if (WiFi.status() != WL_CONNECTED) {
            // setAutoReconnect handles rejoin; just reflect it in the UI.
            dashboardDraw(canvas, last, hist, true);
            delay(500);
        }
        delay(5);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("PC stats display booting...");
    boardInit();
    touch.begin(Touch.get());
    appConfig.load();
    Serial.printf("config: ssid='%s' server='%s'\n",
                  appConfig.ssid.c_str(), appConfig.serverHost.c_str());
}

void loop() {
    if (!appConfig.hasWifi()) {
        runWifiSetup();
    } else if (!setupConnectWifi(canvas, appConfig.ssid, appConfig.password)) {
        setupShowMessage(canvas, touch, "Saved WiFi unreachable", appConfig.ssid.c_str());
        runWifiSetup();
    }

    while (!ensureServer()) {
        setupShowMessage(canvas, touch,
                         "No LibreHardwareMonitor found",
                         "Enable Remote Web Server, tap to retry");
    }

    runDashboard();  // returns on long-press

    // Long-press: back to WiFi setup (keeps saved config until replaced).
    runWifiSetup();
    appConfig.clearServer();  // network changed -> server IP may differ
}
```

- [ ] **Step 2: Build and flash**

Run: `pio run -e amoled-18 -t upload`
Expected: `[SUCCESS]`.

- [ ] **Step 3: USER VERIFICATION — full happy path**

Ask the user to confirm: first boot enters setup (NVS was cleared in Task 7; if not, long-press from the dashboard) → pick network → type password → discovery finds the PC → live dashboard with real numbers updating every second; graphs scroll left over ~2 minutes.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: full application flow - setup, discovery, live dashboard"
```

---

### Task 13: Failure drills, soak, docs

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Failure drills (with the user, one at a time)**

1. Close LibreHardwareMonitor on the PC → within ~3 s the OFFLINE badge appears, values dim. Reopen LHM → recovers within ~5 s (allow up to ~35 s if a rediscovery sweep started).
2. Reboot the device (re-power) → goes straight to the dashboard, no setup.
3. Long-press the dashboard ≥2 s → WiFi setup list opens.
4. While on the dashboard, run a CPU+GPU load (game/benchmark) for a minute → utilization areas and temperature lines visibly climb on both graphs.

- [ ] **Step 2: Soak test**

Leave running 2+ hours. Then capture serial for 30 s: fetches still `OK`, and add a temporary heap print if desired. Pass criteria: display still updating, no reboot (uptime visible via continuous graph history).

- [ ] **Step 3: Update README**

Replace the "What this project is" intro of `README.md`: the project is now the PC stats display (LibreHardwareMonitor over WiFi, on-device setup, layout per the spec). Keep the board-revision section, the sharp-edges section, build/flash instructions, and troubleshooting verbatim. Add a short "How it works" section: setup flow, 1 Hz polling, long-press to reconfigure, and the PC-side requirements (LHM Remote Web Server + firewall rule 8085 + LHM run-at-startup recommended).

- [ ] **Step 4: Final commit**

```bash
git add README.md
git commit -m "docs: README for the PC stats display"
```

---

## Self-review notes (resolved during planning)

- **Spec coverage:** setup flow (Tasks 8-10), dashboard layout C v2 (Task 5), sensors + locale parsing (Tasks 2-4, 11), persistence (Task 7), long-press (Tasks 6, 12), offline + rediscovery (Task 12), PC-side firewall (Task 10), verification plan (Tasks 5-13 verification steps + Task 13 drills).
- **Type consistency:** `Stats`/`History`/`DashHistories` defined once (Tasks 3-5) and used unchanged in Tasks 11-12; `setupDiscoverServer`/`verifyLHM` signatures consistent between Tasks 10 and 12.
- **Known deviation from spec:** rediscovery sweep runs synchronously with the discovery screen instead of "in the background" — acceptable because graphs are paused while offline anyway; noted in Task 12 code comment.
- **Risk flags:** (1) `Arduino_Canvas` framebuffer allocation — Task 5 Step 5 checks the serial log; if `canvas->begin()` fails, the fallback is allocating the buffer manually with `ps_malloc` via the canvas constructor that accepts an external buffer. (2) On-device Unity tests over USB CDC — Task 1 proves the harness with a smoke test before anything depends on it. (3) Touch coordinate mapping — Task 6 has an explicit corner-tap calibration step with documented fix.
