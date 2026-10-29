// D3D8 shader handles; translation is shared with the PC data preparer.
#include "d3d8.h"
#include "shader_translate.h"
#include <stdlib.h>
#include <stdio.h>

// ---------- handles ----------
#define MAX_VS 1024
static VShader *vss[MAX_VS];
static PShader *pss[256];
static uint64_t source_hash(const char *s) {
    uint64_t h = 0xcbf29ce484222325ull;
    if (s) for (; *s; s++) h = (h ^ (uint8_t)*s) * 0x100000001b3ull;
    return h;
}

uint32_t vs_create(uint32_t decl, uint32_t func) {
    HOSTFN;
    int i;
    for (i = 1; i < MAX_VS && vss[i]; i++) {}
    if (i == MAX_VS) return 0;
    VShader *v = calloc(1, sizeof *v);
    if (!v) return 0;
    for (int k = 0; k < 64; k++) {
        v->decl[k] = R32(decl + 4u * k);
        v->ndecl = k + 1;
        if (v->decl[k] == 0xffffffffu) break;
    }
    uint32_t bgra = 0;
    for (int k = 0; k < v->ndecl; k++) {
        uint32_t tk = v->decl[k];
        if ((tk >> 29) == 2 && !(tk & (1u << 28)) && ((tk >> 16) & 0xf) == 4) bgra |= 1u << (tk & 15);
        if ((tk >> 29) == 4) k += (int)((tk >> 25) & 0xf) * 4; // D3DVSD_CONST: skip the data
    }
    if (func) {
        int err;
        v->glsl = vs_translate_ex((const uint32_t *)(mb + func), bgra, &err);
        v->nc = vs_constant_count();
        if (err) rt_log("[vs] traduction partielle du shader 0x%08x (err %d)\n", func, err);
        if (getenv("SH4_VSDUMP") && v->glsl) {   // diagnostics: GLSL + declaration of every VS (vsdump_<handle>.glsl)
            rt_log("[shader-bank] func=%08x decl=%08x bgra=%x nc=%d\n", func, decl, bgra, v->nc);
            char fn[64];
            snprintf(fn, sizeof fn, "vsdump_%03x.glsl", (unsigned)(i << 1) | 1u);
            FILE *f = fopen(fn, "w");
            if (f) {
                fprintf(f, "// func=%08x declptr=%08x bgra=%x nc=%d decl:", func, decl, bgra, v->nc);
                for (int k = 0; k < v->ndecl; k++) fprintf(f, " %08x", v->decl[k]);
                fprintf(f, "\n%s", v->glsl);
                fclose(f);
            }
        }
    } else {
        v->is_fixed = 1;
        d3d_log_once("function-less vertex shader (declaration + fixed pipeline)");
    }
    v->handle = (uint32_t)(i << 1) | 1u;
    v->source_hash = source_hash(v->glsl);
    vss[i] = v;
    return v->handle;
}
VShader *vs_get(uint32_t h) { return (h & 1) && (h >> 1) < MAX_VS ? vss[h >> 1] : NULL; }
void vs_delete(uint32_t h) {
    VShader *v = vs_get(h);
    if (v) { free(v->glsl); free(v); vss[h >> 1] = NULL; }
}

uint32_t ps_create(uint32_t func) {
    HOSTFN;
    int i;
    for (i = 1; i < 256 && pss[i]; i++) {}
    if (i == 256) return 0;
    PShader *p = calloc(1, sizeof *p);
    int err;
    p->glsl_body = ps_translate((const uint32_t *)(mb + func), &err);
    if (err) rt_log("[ps] traduction partielle du shader 0x%08x (err %d)\n", func, err);
    p->handle = (uint32_t)i;
    p->source_hash = source_hash(p->glsl_body);
    pss[i] = p;
    return p->handle;
}
PShader *ps_get(uint32_t h) { return h && h < 256 ? pss[h] : NULL; }
void ps_delete(uint32_t h) {
    PShader *p = ps_get(h);
    if (p) { free(p->glsl_body); free(p); pss[h] = NULL; }
}
