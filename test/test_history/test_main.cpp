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
void loop() {
    // Heartbeat: keeps the USB CDC active so Windows doesn't power down the
    // port after the tests finish (the board drops off the bus when silent).
    delay(500);
    Serial.println("idle");
}
