#pragma once
#include <stdio.h>

// Test builds can exercise the physical-to-action path, rather than injecting
// a finished DI button and accidentally bypassing chord suppression.
static inline unsigned sh4_pad_script_buttons(const char *script, unsigned frame,
                                               unsigned physical, unsigned valid_mask) {
    for (const char *p = script; p && *p;) {
        unsigned start, mask, duration;
        int used = 0;
        if (sscanf(p, "%u:%x:%u%n", &start, &mask, &duration, &used) != 3 ||
            (p[used] && p[used] != ',') || !duration || (mask & ~valid_mask)) break;
        if (frame >= start && frame - start < duration) physical |= mask;
        p += used;
        if (*p == ',') ++p;
    }
    return physical;
}
