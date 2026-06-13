#include "parse_value.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

float lhmParseNumber(const char *s) {
    if (!s) return NAN;
    char buf[32];
    size_t n = 0;
    bool seenDigit = false;
    bool seenSign  = false;
    for (const char *p = s; *p && n < sizeof(buf) - 1; ++p) {
        if (*p >= '0' && *p <= '9') {
            buf[n++] = *p; seenDigit = true;
        } else if (*p == '-' && !seenDigit) {
            // ASCII hyphen-minus as sign; a second sign before any digit is illegal
            if (seenSign) return NAN;
            buf[n++] = '-'; seenSign = true;
        } else if ((unsigned char)*p == 0xE2 && !seenDigit) {
            // U+2212 MINUS SIGN (UTF-8: E2 88 92) used by fi-FI locale
            if ((unsigned char)*(p+1) == 0x88 && (unsigned char)*(p+2) == 0x92) {
                if (seenSign) return NAN;
                buf[n++] = '-'; seenSign = true;
                p += 2;  // skip the remaining two bytes of the 3-byte sequence
            }
        } else if ((*p == ',' || *p == '.') && seenDigit) {
            buf[n++] = '.';
        } else if (seenDigit) {
            break;  // number ended, unit follows
        }
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
