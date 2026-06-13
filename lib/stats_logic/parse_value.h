#pragma once

// Parsers for LibreHardwareMonitor's locale-formatted value strings.
// Both return NAN when no number can be found.
//
// lhmParseNumber:
//   - Accepts comma or dot as decimal separator (fi-FI uses comma).
//   - Accepts ASCII hyphen-minus ('-') or U+2212 MINUS SIGN (UTF-8 0xE2 0x88 0x92)
//     as a leading negative sign. The fi-FI .NET/ICU runtime formats negatives with
//     U+2212. A second sign or a sign after digits is treated as end-of-number and
//     produces NAN (no digit seen) or truncation.
//   - Thousands separators are NOT supported; "1,234" parses as 1.234 (treated as
//     decimal separator), not 1234.
//   - Stops at the first non-numeric character after digits have begun (units are
//     ignored).
//
// lhmParseBytesPerSecond:
//   - Multipliers are binary (matching LHM's formatter):
//       KB/s = 1024 B/s, MB/s = 1024^2 B/s, GB/s = 1024^3 B/s.
float lhmParseNumber(const char *s);          // "60,9 °C"  -> 60.9
float lhmParseBytesPerSecond(const char *s);  // "2,4 KB/s" -> 2457.6 (bytes/s)
