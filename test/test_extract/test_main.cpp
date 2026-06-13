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
void loop() {
    // Heartbeat: keeps the USB CDC active so Windows doesn't power down the
    // port after the tests finish (the board drops off the bus when silent).
    delay(500);
    Serial.println("idle");
}
