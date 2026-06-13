#include <Arduino.h>
#include <unity.h>

void test_harness_runs(void) { TEST_ASSERT_EQUAL(4, 2 + 2); }

void setup() {
    delay(2000);  // allow USB CDC to come up before Unity prints
    UNITY_BEGIN();
    RUN_TEST(test_harness_runs);
    UNITY_END();
}
void loop() {
    // Heartbeat: keeps the USB CDC active so Windows doesn't power down the
    // port after the tests finish (the board drops off the bus when silent).
    delay(500);
    Serial.println("idle");
}
