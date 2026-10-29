// D3D8 shaders: creation (handles), vs.1.1 / ps.1.1 -> GLSL translation (1.20 / ES 2 style).
#include "shader_translate.h"
#include <string.h>
#ifdef SH4_TRANSLATOR_STANDALONE
#define rt_log(...) ((void)0)
#else
extern void rt_log(const char *, ...);
#endif
static float shader_f32(uint32_t u) { float f; memcpy(&f, &u, sizeof f); return f; }
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

// ---------- tampon de texte ----------
typedef struct { char *s; size_t n, cap; } Str;
static void sp(Str *b, const char *fmt, ...) {
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int k = vsnprintf(b->s ? b->s + b->n : NULL, b->s ? b->cap - b->n : 0, fmt, ap);
        va_end(ap);
        if (b->s && b->n + (size_t)k < b->cap) { b->n += (size_t)k; return; }
        b->cap = (b->cap + (size_t)k + 1) * 2;
        b->s = realloc(b->s, b->cap);
    }
}

// ---------- registres ----------
static const char SW[4] = {'x', 'y', 'z', 'w'};
static char *swz(uint32_t tok, char *out) {
    uint32_t s = (tok >> 16) & 0xff;
    if (s == 0xe4) { out[0] = 0; return out; } // .xyzw
    out[0] = '.';
    for (int i = 0; i < 4; i++) out[1 + i] = SW[(s >> (2 * i)) & 3];
    out[5] = 0;
    return out;
}
static char *mask(uint32_t tok, char *out) {
    uint32_t m = (tok >> 16) & 0xf;
    int k = 0;
    if (m == 0xf) { out[0] = 0; return out; }
    out[k++] = '.';
    for (int i = 0; i < 4; i++)
        if (m & (1u << i)) out[k++] = SW[i];
    out[k] = 0;
    return out;
}

// Base name of a VS source register (before swizzle); `delta` offsets the index (m4x4...).
static int g_a0n;   // number of `mov a0.x` seen so far: each one gets its own int (A0_n), shark mishandles reassigning one address variable
static void a0name(char *o, size_t n, int off) { if (g_a0n) snprintf(o, n, "A0_%d+%d", g_a0n, off); else snprintf(o, n, "A0+%d", off); }
static int g_vs_nc;   // largest constant register read by the VS being translated (96 if relative addressing)
static void vs_regname(uint32_t tok, int delta, char *out, size_t n) {
    uint32_t type = (tok >> 28) & 7, num = (tok & 0x7ff) + (uint32_t)delta;
    switch (type) {
    case 0: snprintf(out, n, "r%u", num); break;
    case 1: snprintf(out, n, "v%u", num); break;
    case 2:
        if (tok & (1u << 13)) { g_vs_nc = MAX_VSC; char a[24]; a0name(a, sizeof a, (int)num); snprintf(out, n, "c[%s]", a); }
        else { if ((int)num + 1 > g_vs_nc) g_vs_nc = (int)num + 1; snprintf(out, n, "c[%u]", num); }
        break;
    case 3: if (g_a0n) snprintf(out, n, "vec4(float(A0_%d))", g_a0n); else snprintf(out, n, "vec4(float(A0))"); break;
    default: snprintf(out, n, "vec4(0.0)"); break;
    }
}
static void vs_src(Str *b, uint32_t tok, int delta) {
    char base[48], s[8];
    vs_regname(tok, delta, base, sizeof base);
    int neg = ((tok >> 24) & 0xf) == 1;
    sp(b, "%s(%s%s)", neg ? "-" : "", base, swz(tok, s));
}
static const char *vs_dstname(uint32_t tok, char *out, size_t n) {
    uint32_t type = (tok >> 28) & 7, num = tok & 0x7ff;
    static const char *ras[] = {"oPos", "oFog", "oPts"};
    switch (type) {
    case 0: snprintf(out, n, "r%u", num); break;
    case 3: snprintf(out, n, "A0"); break;
    case 4: snprintf(out, n, "%s", num < 3 ? ras[num] : "oPts"); break;
    case 5: snprintf(out, n, "oD%u", num & 1); break;
    case 6: snprintf(out, n, "oT%u", num & 7); break;
    default: snprintf(out, n, "rX"); break;
    }
    return out;
}

static int nargs_of(uint32_t op) {
    switch (op) {
    case 0: return 0;
    case 1: case 6: case 7: case 14: case 15: case 16: case 19: case 78: case 79: case 64: case 65: case 66: return 1;
    case 4: case 18: case 80: case 88: return 3;
    default: return 2;
    }
}

// bgra: input registers fed by D3DCOLORs (BGRA in memory) according to the declaration.
char *vs_translate_ex(const uint32_t *t, uint32_t bgra, int *err);
char *vs_translate(const uint32_t *t, int *err) { return vs_translate_ex(t, 0, err); }
char *vs_translate_ex(const uint32_t *t, uint32_t bgra, int *err) {
    Str b = {0}, body = {0};
    *err = 0;
    g_vs_nc = 0;
    g_a0n = 0;
    if ((t[0] & 0xffff0000u) != 0xfffe0000u) { *err = 1; return NULL; }
    uint32_t used_v = 0, used_r = 0;
    int i = 1;
    for (; t[i] != 0x0000ffffu; ) {
        uint32_t ins = t[i], op = ins & 0xffff;
        if (op == 0xfffe) { i += 1 + (int)((ins >> 16) & 0x7fff); continue; }
        int na = nargs_of(op);
        uint32_t dst = t[i + 1];
        const uint32_t *s = &t[i + 2];
        char dn[16], dm[8], tmp[8];
        vs_dstname(dst, dn, sizeof dn);
        mask(dst, dm);
        if (((dst >> 28) & 7) == 0) used_r |= 1u << (dst & 31);
        for (int k = 0; k < na; k++) {
            uint32_t ty = (s[k] >> 28) & 7;
            if (ty == 1) used_v |= 1u << (s[k] & 15);
            if (ty == 0) used_r |= 1u << (s[k] & 31);
        }
        if (((dst >> 28) & 7) == 3) { // mov a0.x, src
            int nn = g_a0n + 1;   // the source may itself read the previous a0
            sp(&body, "  int A0_%d = int(floor(", nn);
            vs_src(&body, s[0], 0);
            g_a0n = nn;
            sp(&body, ".x + 0.5));\n");
            i += 2 + na;
            continue;
        }
        sp(&body, "  %s%s = (", dn, dm);
        switch (op) {
        case 1: vs_src(&body, s[0], 0); break;
        case 2: vs_src(&body, s[0], 0); sp(&body, " + "); vs_src(&body, s[1], 0); break;
        case 3: vs_src(&body, s[0], 0); sp(&body, " - "); vs_src(&body, s[1], 0); break;
        case 4: vs_src(&body, s[0], 0); sp(&body, " * "); vs_src(&body, s[1], 0); sp(&body, " + "); vs_src(&body, s[2], 0); break;
        case 5: vs_src(&body, s[0], 0); sp(&body, " * "); vs_src(&body, s[1], 0); break;
        case 6: sp(&body, "vec4(rcp_("); vs_src(&body, s[0], 0); sp(&body, ".w))"); break;
        case 7: sp(&body, "vec4(rsq_("); vs_src(&body, s[0], 0); sp(&body, ".w))"); break;
        case 8: sp(&body, "vec4(dot(("); vs_src(&body, s[0], 0); sp(&body, ").xyz, ("); vs_src(&body, s[1], 0); sp(&body, ").xyz))"); break;
        case 9: sp(&body, "vec4(dot("); vs_src(&body, s[0], 0); sp(&body, ", "); vs_src(&body, s[1], 0); sp(&body, "))"); break;
        case 10: sp(&body, "min("); vs_src(&body, s[0], 0); sp(&body, ", "); vs_src(&body, s[1], 0); sp(&body, ")"); break;
        case 11: sp(&body, "max("); vs_src(&body, s[0], 0); sp(&body, ", "); vs_src(&body, s[1], 0); sp(&body, ")"); break;
        case 12: sp(&body, "vec4(lessThan("); vs_src(&body, s[0], 0); sp(&body, ", "); vs_src(&body, s[1], 0); sp(&body, "))"); break;
        case 13: sp(&body, "vec4(greaterThanEqual("); vs_src(&body, s[0], 0); sp(&body, ", "); vs_src(&body, s[1], 0); sp(&body, "))"); break;
        case 14: case 78: sp(&body, "vec4(exp2("); vs_src(&body, s[0], 0); sp(&body, ".w))"); break;
        case 15: case 79: sp(&body, "vec4(log2(abs("); vs_src(&body, s[0], 0); sp(&body, ".w)))"); break;
        case 19: sp(&body, "fract("); vs_src(&body, s[0], 0); sp(&body, ")"); break;
        case 20: case 21: case 22: case 23: case 24: {
            // m4x4 / m4x3 / m3x4 / m3x3 / m3x2: dot products with consecutive constants
            int rows = op == 20 ? 4 : op == 21 ? 3 : op == 22 ? 4 : op == 23 ? 3 : 2;
            int three = op >= 22;
            sp(&body, "vec4(");
            for (int r = 0; r < 4; r++) {
                if (r) sp(&body, ", ");
                if (r >= rows) { sp(&body, "0.0"); continue; }
                sp(&body, three ? "dot((" : "dot(");
                vs_src(&body, s[0], 0);
                sp(&body, three ? ").xyz, (" : ", ");
                vs_src(&body, s[1], r);
                sp(&body, three ? ").xyz)" : ")");
            }
            sp(&body, ")");
            break;
        }
        default:
            sp(&body, "vec4(0.0)");
            *err = 2;
            rt_log("[vs] unsupported opcode %u\n", op);
            break;
        }
        sp(&body, ")%s;\n", dm);
        (void)tmp;
        i += 2 + na;
    }
    for (int v = 0; v < 16; v++)
        if (used_v & (1u << v)) sp(&b, "attribute vec4 aV%d;\n", v);
    sp(&b, "uniform vec4 c[96];\nuniform vec4 uFlip;\n"
           "varying vec4 vD0;\nvarying vec4 vD1;\nvarying vec4 vT0;\nvarying vec4 vT1;\nvarying vec4 vT2;\nvarying vec4 vT3;\nvarying float vFog;\n"
           "float rcp_(float x) { return x == 0.0 ? 1.0e30 : 1.0 / x; }\n"
           "float rsq_(float x) { return x == 0.0 ? 1.0e30 : inversesqrt(abs(x)); }\n"
           "void main() {\n  int A0 = 0;\n"
           "  vec4 oPos = vec4(0.0);\n  vec4 oFog = vec4(1.0);\n  vec4 oPts = vec4(1.0);\n  vec4 oD0 = vec4(1.0);\n  vec4 oD1 = vec4(0.0);\n"
           "  vec4 oT0 = vec4(0.0, 0.0, 0.0, 1.0);\n  vec4 oT1 = oT0;\n  vec4 oT2 = oT0;\n  vec4 oT3 = oT0;\n"
           "  vec4 oT4 = oT0;\n  vec4 oT5 = oT0;\n  vec4 oT6 = oT0;\n  vec4 oT7 = oT0;\n");
    for (int r = 0; r < 12; r++)
        if (used_r & (1u << r)) sp(&b, "  vec4 r%d = vec4(0.0);\n", r);
    for (int v = 0; v < 16; v++)
        if (used_v & (1u << v)) sp(&b, "  vec4 v%d = aV%d%s;\n", v, v, (bgra >> v) & 1 ? ".zyxw" : "");
    sp(&b, "%s", body.s ? body.s : "");
    sp(&b, "  gl_Position = vec4(oPos.x, oPos.y * uFlip.x, oPos.z * 2.0 - oPos.w, oPos.w);\n"
           "  vD0 = clamp(oD0, 0.0, 1.0); vD1 = clamp(oD1, 0.0, 1.0);\n"
           "  vT0 = oT0; vT1 = oT1; vT2 = oT2; vT3 = oT3; vFog = clamp(oFog.x, 0.0, 1.0);\n}\n");
    free(body.s);
    return b.s;
}

// ---------- ps.1.1 ----------
static void ps_src(Str *b, uint32_t tok, const char *defs) {
    uint32_t type = (tok >> 28) & 7, num = tok & 0x7ff, mod = (tok >> 24) & 0xf;
    char base[24], s[8];
    switch (type) {
    case 0: snprintf(base, sizeof base, "r%u", num); break;
    case 1: snprintf(base, sizeof base, "vD%u", num & 1); break;
    case 2: snprintf(base, sizeof base, "%s%u", defs[num & 7] ? "pd" : "pc[", num & 7); if (!defs[num & 7]) strcat(base, "]"); break;
    case 3: snprintf(base, sizeof base, "t%u", num & 3); break;
    default: snprintf(base, sizeof base, "vec4(0.0)"); break;
    }
    swz(tok, s);
    switch (mod) {
    case 0: sp(b, "%s%s", base, s); break;
    case 1: sp(b, "(-%s%s)", base, s); break;
    case 2: sp(b, "(%s%s - 0.5)", base, s); break;
    case 3: sp(b, "(0.5 - %s%s)", base, s); break;
    case 4: sp(b, "(2.0 * %s%s - 1.0)", base, s); break;
    case 5: sp(b, "(1.0 - 2.0 * %s%s)", base, s); break;
    case 6: sp(b, "(1.0 - %s%s)", base, s); break;
    case 7: sp(b, "(2.0 * %s%s)", base, s); break;
    case 8: sp(b, "(-2.0 * %s%s)", base, s); break;
    default: sp(b, "%s%s", base, s); break;
    }
}

char *ps_translate(const uint32_t *t, int *err) {
    Str b = {0};
    char defs[8] = {0};
    *err = 0;
    if ((t[0] & 0xffff0000u) != 0xffff0000u) { *err = 1; return NULL; }
    sp(&b, "  vec4 r0 = vec4(0.0);\n  vec4 r1 = vec4(0.0);\n  vec4 t0 = vec4(0.0);\n  vec4 t1 = vec4(0.0);\n  vec4 t2 = vec4(0.0);\n  vec4 t3 = vec4(0.0);\n");
    for (int i = 1; t[i] != 0x0000ffffu;) {
        uint32_t ins = t[i], op = ins & 0xffff;
        if (op == 0xfffe) { i += 1 + (int)((ins >> 16) & 0x7fff); continue; }
        if (op == 0) { i++; continue; }
        if (op == 81) { // def cN, x, y, z, w
            uint32_t r = t[i + 1] & 7;
            defs[r] = 1;
            sp(&b, "  vec4 pd%u = vec4(%.9g, %.9g, %.9g, %.9g);\n", r, shader_f32(t[i + 2]), shader_f32(t[i + 3]),
               shader_f32(t[i + 4]), shader_f32(t[i + 5]));
            i += 6;
            continue;
        }
        int na = nargs_of(op);
        uint32_t dst = t[i + 1];
        const uint32_t *s = &t[i + 2];
        uint32_t dnum = dst & 0x7ff, dtype = (dst >> 28) & 7;
        char dn[8], dm[8];
        snprintf(dn, sizeof dn, "%c%u", dtype == 3 ? 't' : 'r', dnum & 3);
        mask(dst, dm);
        if (op == 66) { // tex tN
            sp(&b, "  t%u = texture2D(s%u, vT%u.xy);\n", dnum & 3, dnum & 3, dnum & 3);
            i += 2;
            continue;
        }
        if (op == 64) { sp(&b, "  t%u = clamp(vT%u, 0.0, 1.0);\n", dnum & 3, dnum & 3); i += 2; continue; }
        if (op == 65) { sp(&b, "  if (any(lessThan(vT%u.xyz, vec3(0.0)))) discard;\n", dnum & 3); i += 2; continue; }
        int sat = (dst >> 20) & 1;
        int shift = (int)((dst >> 24) & 0xf);
        sp(&b, "  %s%s = (", dn, dm);
        if (sat) sp(&b, "clamp(");
        sp(&b, "(");
        switch (op) {
        case 1: ps_src(&b, s[0], defs); break;
        case 2: ps_src(&b, s[0], defs); sp(&b, " + "); ps_src(&b, s[1], defs); break;
        case 3: ps_src(&b, s[0], defs); sp(&b, " - "); ps_src(&b, s[1], defs); break;
        case 4: ps_src(&b, s[0], defs); sp(&b, " * "); ps_src(&b, s[1], defs); sp(&b, " + "); ps_src(&b, s[2], defs); break;
        case 5: ps_src(&b, s[0], defs); sp(&b, " * "); ps_src(&b, s[1], defs); break;
        case 8: sp(&b, "vec4(dot(("); ps_src(&b, s[0], defs); sp(&b, ").xyz, ("); ps_src(&b, s[1], defs); sp(&b, ").xyz))"); break;
        case 9: sp(&b, "vec4(dot("); ps_src(&b, s[0], defs); sp(&b, ", "); ps_src(&b, s[1], defs); sp(&b, "))"); break;
        case 18: sp(&b, "mix("); ps_src(&b, s[2], defs); sp(&b, ", "); ps_src(&b, s[1], defs); sp(&b, ", "); ps_src(&b, s[0], defs); sp(&b, ")"); break;
        case 80: sp(&b, "("); ps_src(&b, s[0], defs); sp(&b, ").w > 0.5 ? "); ps_src(&b, s[1], defs); sp(&b, " : "); ps_src(&b, s[2], defs); break;
        case 88: sp(&b, "mix("); ps_src(&b, s[1], defs); sp(&b, ", "); ps_src(&b, s[2], defs); sp(&b, ", vec4(greaterThanEqual("); ps_src(&b, s[0], defs); sp(&b, ", vec4(0.0))))"); break;
        default: sp(&b, "vec4(0.0)"); *err = 2; rt_log("[ps] unsupported opcode %u\n", op); break;
        }
        sp(&b, ")");
        if (shift == 1) sp(&b, " * 2.0"); else if (shift == 2) sp(&b, " * 4.0"); else if (shift == 15) sp(&b, " * 0.5");
        if (sat) sp(&b, ", 0.0, 1.0)");
        sp(&b, ")%s;\n", dm);
        i += 2 + na;
    }
    sp(&b, "  col = r0;\n");
    return b.s;
}


int vs_constant_count(void) { return g_vs_nc > MAX_VSC ? MAX_VSC : g_vs_nc; }
void shader_translate_free(void *p) { free(p); }
