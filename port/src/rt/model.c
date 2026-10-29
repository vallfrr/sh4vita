// Native implementations of the retained GOG v5 model bounds and UV passes.
// The generated passes remain the fallback for unusual overlapping spans.
#include "rt.h"
#include <stdlib.h>

int rt_original_model_uv(void) {
    static int original=-1;
    if(original<0) {
        const char *s=getenv("SH4_ORIGINAL_MODEL_UV");
        original=s && atoi(s)!=0;
    }
    return original;
}

static int uv_identity_enabled(void) {
    static int enabled=-1;
    if (enabled<0) { const char *s=getenv("SH4_NATIVE_UV_IDENTITY");enabled=s && atoi(s)!=0; }
    return enabled;
}

static int model_enabled(int pass) {
    if(pass==2 && rt_original_model_uv()) return 0;
    static int mode = -1;
    if (mode < 0) {
        const char *s = getenv("SH4_NATIVE_VERTICES");
        mode = !s ? 0 : !strcmp(s, "1") ? 3 : !strcmp(s, "bounds") ? 1 : !strcmp(s, "uv") ? 2 : 0;
    }
    return mode & pass;
}

int rt_native_model_bounds_enabled(void) { return model_enabled(1); }
int rt_native_model_uv_enabled(void) { return model_enabled(2); }

static int span_valid(uint32_t p, uint64_t n) {
    return p <= g_mem_size && n <= g_mem_size - p;
}
static int spans_overlap(uint32_t a, uint64_t an, uint32_t b, uint64_t bn) {
    return an && bn && (uint64_t)a < (uint64_t)b + bn && (uint64_t)b < (uint64_t)a + an;
}

int nat_model_bounds(CPU *c) {
    if (!model_enabled(1)) return 0;
    uint8_t *const mb = g_mem;
    uint32_t src = c->ecx, n = c->edx, sp = c->esp;
    uint64_t bytes = (uint64_t)n * 64;
    if (sp < 16 || !span_valid(sp - 16, 88) || !span_valid(src, bytes) ||
        spans_overlap(src, bytes, sp - 16, 88)) return 0;

    uint32_t max_x_word = R32(sp + 0x34u), max_y_word = R32(sp + 0x1cu);
    uint32_t min_x_word = R32(sp + 0x30u), min_y_word = R32(sp + 0x28u);
    float max_x = rt_f32(max_x_word), max_y = rt_f32(max_y_word);
    float min_x = rt_f32(min_x_word), min_y = rt_f32(min_y_word);
    const uint8_t *p = mb + src;
    for (uint32_t i = 0; i < n; ++i, p += 64) {
        uint32_t xword, yword;
        memcpy(&xword, p + 0x18, 4);
        memcpy(&yword, p + 0x1c, 4);
        float x = rt_f32(xword), y = rt_f32(yword);
        if (x > max_x) { max_x = x; max_x_word = xword; }
        if (y > max_y) { max_y = y; max_y_word = yword; }
        if (x < min_x) { min_x = x; min_x_word = xword; }
        if (y < min_y) { min_y = y; min_y_word = yword; }
    }
    W32(sp + 0x34u, max_x_word); W32(sp + 0x1cu, max_y_word);
    W32(sp + 0x30u, min_x_word); W32(sp + 0x28u, min_y_word);
    c->ecx = src + n * 64u;
    c->edi = 0;
    W32(sp + 0x20u, c->ecx);
    return 1;
}

int nat_model_uv(CPU *c) {
    if (!model_enabled(2)) return 0;
    uint8_t *const mb = g_mem;
    uint32_t sp = c->esp, src = c->ecx, dst = c->edi - 0x1cu;
    if (!span_valid(c->esi, 0x48) || sp < 16 || !span_valid(sp - 16, 88)) return 0;
    uint32_t n = R32(c->esi + 0x44u);
    uint64_t bytes = (uint64_t)n * 64;
    if (!span_valid(src, bytes) || !span_valid(dst, bytes) ||
        spans_overlap(src, bytes, dst, bytes) ||
        spans_overlap(src, bytes, sp - 16, 88) ||
        spans_overlap(dst, bytes, sp - 16, 88) ||
        spans_overlap(dst, bytes, 0x5b7b08u, 24)) return 0;

    double min_x = (double)rt_f32(R32(sp + 0x30u));
    double min_y = (double)rt_f32(R32(sp + 0x28u));
    double max_y = (double)rt_f32(R32(sp + 0x1cu));
    double range_x = (double)rt_f32(R32(sp + 0x24u));
    double range_y = (double)rt_f32(R32(sp + 0x10u));
    double offset_x = (double)rt_f32(R32(sp + 0x40u));
    double offset_y = (double)rt_f32(R32(sp + 0x44u));
    double lo = (double)rt_f32(R32(0x5b7b18u));
    double hi = (double)rt_f32(R32(0x5b7b14u));
    double wrap = (double)rt_f32(R32(0x5b7b10u));
    double special = rt_f64(R64(0x5b7b08u));
    double status_a = 0, status_b = 0;
    int x_normalize = range_x > lo && range_x < hi;
    int x_wrap = range_x > hi;
    int y_mode = range_y > lo && range_y < hi ? 1 : range_y > hi ? 2 : max_y == special ? 3 : 4;
    // For float-derived coordinates, the double difference and its quarter
    // remain far from double underflow/overflow. Multiplying and dividing by
    // the same exact power of two preserves every finite bit. Keep the
    // original arithmetic for nonfinite coordinates and all other scales.
    int quarter=uv_identity_enabled() && wrap==0.25;
    int x_identity=quarter && range_x==0.25;
    int y_identity=quarter && range_y==0.25;
    const uint8_t *restrict in = mb + src;
    uint8_t *restrict out = mb + dst;
    for (uint32_t i = 0; i < n; ++i, in += 64, out += 64) {
        memcpy(out, in, 64);
        uint32_t xword, yword;
        memcpy(&xword, in + 0x18, 4); memcpy(&yword, in + 0x1c, 4);
        double sx = (double)rt_f32(xword), sy = (double)rt_f32(yword);
        double xdiff = sx - min_x, ydiff = sy - min_y;
        double xo = xdiff;
        if (x_normalize) {
            if (!x_identity || !isfinite(xdiff)) { xo = xdiff * wrap; xo = xo / range_x; }
        }
        else if (x_wrap && !(xdiff < hi)) xo -= wrap;
        xo += offset_x;
        uint32_t xout = rt_u32f((float)xo);
        memcpy(out + 0x18, &xout, 4);
        double yo;
        if (y_mode == 1) {
            yo=ydiff;
            if (!y_identity || !isfinite(ydiff)) { yo = ydiff * wrap; yo = yo / range_y; }
            status_a = range_y; status_b = hi;
            yo += offset_y;
        } else if (y_mode == 2) {
            yo = ydiff;
            if (!(ydiff < hi)) yo -= wrap;
            status_a = ydiff; status_b = hi;
            yo += offset_y;
        } else {
            status_a = max_y; status_b = special;
            if (y_mode == 3) { yo = offset_y; yo += sy; }
            else { yo = ydiff; yo += offset_y; }
        }
        uint32_t yout = rt_u32f((float)yo);
        memcpy(out + 0x1c, &yout, 4);
    }
    if (n) {
        W32(sp - 4u, 0x40u); W32(sp - 8u, src + (n - 1u) * 64u);
        W32(sp - 12u, dst + (n - 1u) * 64u); W32(sp - 16u, 0x429164u);
    }
    rt_fcom(c, status_a, status_b);
    c->eax = c->edx = n;
    c->ecx = src + n * 64u;
    c->edi += n * 64u;
    W32(sp + 0x20u, c->ecx); W32(sp + 0x14u, n);
    return 1;
}
