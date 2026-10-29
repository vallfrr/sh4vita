#pragma once
#include <stdint.h>

typedef struct {
    uint32_t frame;
    unsigned sampled, score, cursor;
} Sh4PadContext;

// Hysteresis measures sampled frames, not the number of GetDeviceState calls.
// Repeated reads in one frame must not switch between camera and pointer mode.
static inline int sh4_pad_cursor(Sh4PadContext *s, uint32_t frame, int pointer_on) {
    if (!s->sampled || s->frame != frame) {
        s->sampled = 1;
        s->frame = frame;
        if (pointer_on) { if (s->score < 10) ++s->score; }
        else if (s->score) --s->score;
        if (s->score >= 6) s->cursor = 1;
        else if (s->score <= 1) s->cursor = 0;
    }
    return (int)s->cursor;
}
