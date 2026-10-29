#pragma once
#include <stddef.h>
#include <stdint.h>

// The PC's MessageBoxA strings use its Western code page. Vita system dialogs
// require UTF-8. Stop only between complete UTF-8 characters when clipping.
static inline size_t rt_cp1252_utf8(char *dst, size_t capacity, const char *src) {
    static const uint16_t extended[32] = {
        0x20ac,0xfffd,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,
        0x02c6,0x2030,0x0160,0x2039,0x0152,0xfffd,0x017d,0xfffd,
        0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
        0x02dc,0x2122,0x0161,0x203a,0x0153,0xfffd,0x017e,0x0178
    };
    size_t n = 0;
    if (!capacity) return 0;
    for (; src && *src; ++src) {
        unsigned ch = (unsigned char)*src;
        if (ch >= 0x80 && ch <= 0x9f) ch = extended[ch - 0x80];
        unsigned bytes = ch < 0x80 ? 1 : ch < 0x800 ? 2 : 3;
        if (bytes >= capacity - n) break;
        if (bytes == 1) dst[n++] = (char)ch;
        else if (bytes == 2) {
            dst[n++] = (char)(0xc0 | ch >> 6);
            dst[n++] = (char)(0x80 | (ch & 63));
        } else {
            dst[n++] = (char)(0xe0 | ch >> 12);
            dst[n++] = (char)(0x80 | ((ch >> 6) & 63));
            dst[n++] = (char)(0x80 | (ch & 63));
        }
    }
    dst[n] = 0;
    return n;
}
