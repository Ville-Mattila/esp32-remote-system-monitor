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

void test_negative_ascii(void)       { TEST_ASSERT_FLOAT_WITHIN(0.01, -12.3, lhmParseNumber("-12,3 \xC2\xB0""C")); }
void test_negative_unicode(void)     { TEST_ASSERT_FLOAT_WITHIN(0.01, -12.3, lhmParseNumber("\xE2\x88\x92""12,3 \xC2\xB0""C")); }
void test_double_hyphen_nan(void)    { TEST_ASSERT_TRUE(isnan(lhmParseNumber("--5"))); }
void test_empty_is_nan(void)         { TEST_ASSERT_TRUE(isnan(lhmParseNumber(""))); }
void test_multi_separator_pins(void) { TEST_ASSERT_FLOAT_WITHIN(0.01, 1.2, lhmParseNumber("1,2,3")); }
void test_bps_gb(void)               { TEST_ASSERT_FLOAT_WITHIN(1024.0, 1.5f * 1024 * 1024 * 1024, lhmParseBytesPerSecond("1,5 GB/s")); }

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_comma_decimal); RUN_TEST(test_dot_decimal); RUN_TEST(test_percent);
    RUN_TEST(test_gigabytes); RUN_TEST(test_no_number_is_nan); RUN_TEST(test_null_is_nan);
    RUN_TEST(test_bps_kb); RUN_TEST(test_bps_mb); RUN_TEST(test_bps_b); RUN_TEST(test_bps_nan);
    RUN_TEST(test_negative_ascii); RUN_TEST(test_negative_unicode);
    RUN_TEST(test_double_hyphen_nan); RUN_TEST(test_empty_is_nan);
    RUN_TEST(test_multi_separator_pins); RUN_TEST(test_bps_gb);
    UNITY_END();
}
void loop() {
    // Heartbeat: keeps the USB CDC active so Windows doesn't power down the
    // port after the tests finish (the board drops off the bus when silent).
    delay(500);
    Serial.println("idle");
}
