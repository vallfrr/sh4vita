// Scalar ray/triangle intersection: retain the recompiled x87 operation order and
// float spills, while avoiding nine guest indirect calls in the collision loop.
#include "rt.h"
#include <stdlib.h>

extern void F_00573a20_orig(CPU *c);
extern unsigned g_frame;
#define NO_FMA __attribute__((optimize("fp-contract=off")))
static NO_FMA double dot3(const float *a, const float *b) {
    double z = (double)a[2] * b[2], y = (double)a[1] * b[1];
    return (z + y) + (double)a[0] * b[0];
}
static NO_FMA void cross3(float *r, const float *a, const float *b) {
    r[0] = (float)((double)b[2] * a[1] - (double)a[2] * b[1]);
    r[1] = (float)((double)a[2] * b[0] - (double)b[2] * a[0]);
    r[2] = (float)((double)a[0] * b[1] - (double)b[0] * a[1]);
}

// fp-contract=off is needed even on test hosts which support fused multiply-add.
NO_FMA
static void raytri_fast(CPU *c) {
    HOSTFN;
    static int enabled = -1;
    if (enabled < 0) { const char *s = getenv("SH4_RAYTRI"); enabled = !s || atoi(s) != 0; }
    if (!enabled || R32(0x13dc3dc) != 0x401ec0 || R32(0x13dc3e8) != 0x401570 || R32(0x13dc3ec) != 0x41abc0) {
        F_00573a20_orig(c); return;
    }
    uint32_t a[8];
    for (unsigned i = 0; i < 8; i++) a[i] = ARG(i + 1);
    // Keep unusual aliasing, unaligned and nonfinite/extreme inputs on the original path.
    for (unsigned i = 0; i < 8; i++) {
        if ((a[i] & 3) || a[i] > g_mem_size - (i < 5 ? 16u : 4u) ||
            (a[i] >= c->esp - 256u && a[i] < c->esp + 36u)) {
            F_00573a20_orig(c); return;
        }
        if (i < 5) for (unsigned k = 0; k < 3; k++) {
            // Positive IEEE float bit order also rejects infinity/NaN, without a VFP status read.
            if ((R32(a[i] + k * 4) & 0x7fffffffu) > 0x501502f9u) { F_00573a20_orig(c); return; }
        }
    }
    // These constants belong to the executable; do not silently reinterpret patched constants.
    if (R32(0x5ce064) != 0 || R32(0x5b729c) != 0x3f800000 || R32(0x5b79cc) != 0x3727c5ac) {
        F_00573a20_orig(c); return;
    }
    W32(a[0] + 12, 0); W32(a[4] + 12, 0); W32(a[3] + 12, 0); W32(a[2] + 12, 0);
    const float *origin = (const float *)(mb + a[0]), *dir = (const float *)(mb + a[1]);
    const float *v0 = (const float *)(mb + a[2]), *v1 = (const float *)(mb + a[3]), *v2 = (const float *)(mb + a[4]);
    float e1[3], e2[3], p[3], s[3], q[3];
    for (unsigned k = 0; k < 3; k++) { e1[k] = (float)((double)v1[k] - v0[k]); e2[k] = (float)((double)v2[k] - v0[k]); }
    cross3(p, dir, e2);
    double det = dot3(e1, p), ca = fabs(det), cb = (double)rt_f32(R32(0x5b79cc));
    unsigned hit = 0;
    if (ca < cb) goto end;
    float inv = (float)(1.0 / det);
    for (unsigned k = 0; k < 3; k++) s[k] = (float)((double)origin[k] - v0[k]);
    double u = dot3(s, p) * (double)inv;
    float uf = (float)u;
    ca = u; cb = 0; if (ca < cb) goto end;
    ca = uf; cb = 1; if (ca > cb) goto end;
    cross3(q, s, e1);
    double v = dot3(dir, q) * (double)inv;
    float vf = (float)v;
    ca = v; cb = 0; if (ca < cb) goto end;
    ca = (double)vf + uf; cb = 1; if (ca > cb) goto end;
    if (a[5]) W32(a[5], rt_u32f(uf));
    if (a[6]) W32(a[6], rt_u32f(vf));
    double t = dot3(e2, q) * (double)inv;
    if (a[7]) W32(a[7], rt_u32f((float)t));
    ca = t; cb = 0; hit = t >= 0;
end:
    rt_fcom(c, ca, cb);
    c->eax = hit;
    c->esp += 4;  // cdecl: caller owns the eight arguments
}

static void raytri_verify(CPU *c, unsigned target) {
    HOSTFN;
    uint32_t a[8]; unsigned size[8];
    uint8_t before[8][16], expected[8][16];
    for (unsigned i = 0; i < 8; i++) {
        a[i] = ARG(i + 1); size[i] = i < 5 ? 16 : a[i] ? 4 : 0;
        if (size[i]) memcpy(before[i], mb + a[i], size[i]);
    }
    CPU initial = *c;
    F_00573a20_orig(c);
    CPU reference = *c;
    for (unsigned i = 0; i < 8; i++) if (size[i]) memcpy(expected[i], mb + a[i], size[i]);
    for (unsigned i = 0; i < 8; i++) if (size[i]) memcpy(mb + a[i], before[i], size[i]);
    *c = initial;
    raytri_fast(c);
    int ok = c->eax == reference.eax && c->esp == reference.esp && c->ebx == reference.ebx &&
             c->esi == reference.esi && c->edi == reference.edi && c->ebp == reference.ebp &&
             c->ftop == reference.ftop && c->fsw == reference.fsw;
    for (unsigned i = 0; i < 8; i++) if (size[i] && memcmp(expected[i], mb + a[i], size[i])) ok = 0;
    for (unsigned i = 0; i < 6; i++) {
        unsigned slot = (initial.ftop + i) % 8;
        if (memcmp(&c->st[slot], &reference.st[slot], sizeof(double))) ok = 0;
    }
    static unsigned checked;
    if (!ok) rt_fatal("[raytri-verify] mismatch at call %u, result %u/%u status %x/%x", checked, c->eax, reference.eax, c->fsw, reference.fsw);
    if (++checked == target) rt_log("[raytri-verify] frame=%u %u real calls: bit-identical outputs, results, x87 status and preserved registers\n", g_frame, checked);
}

void nat_raytri(CPU *c) {
    static unsigned verify_left, verify_target;
    static int init;
    if (!init) {
        init = 1;
        const char *s = getenv("SH4_RAYTRI_VERIFY");
        if (s) verify_left = verify_target = (unsigned)strtoul(s, NULL, 10);
        s = getenv("SH4_RAYTRI");
        rt_log("[raytri] scalar native=%d verification_calls=%u\n", !s || atoi(s) != 0, verify_target);
    }
    if (verify_left) { raytri_verify(c, verify_target); verify_left--; }
    else raytri_fast(c);
}
