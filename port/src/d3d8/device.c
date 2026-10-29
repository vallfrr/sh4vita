// IDirect3DDevice8: state, resources and render calls (the GL rendering is in draw.c).
#include "d3d8.h"
#include "prep.h"
#include "audit.h"
#include "plat.h"
#include <stdlib.h>

Device *g_dev;
void d3d_fill_caps(uint32_t p);
extern const gfn tex_vt[], surf_vt[], vb_vt[], ib_vt[];

#define M(name) static void dev_##name(CPU *c)
#define DEV Device *d = com_get_kind(ARG(1), K_DEVICE); if (!d) rt_fatal("device invalide %08x [%08x %08x %08x %08x] (g_dev=%p guest %08x) kind=%d obj=%p", ARG(1), R32(ARG(1)), R32(ARG(1) + 4), R32(ARG(1) + 8), R32(ARG(1) + 12), (void *)g_dev, g_dev ? g_dev->com.guest : 0, com_get(ARG(1)) ? com_get(ARG(1))->kind : -1, (void *)com_get(ARG(1)))

#define addref d3d_addref
uint64_t g_t_begin, g_t_body; // profiling: start of the current frame, accumulated body

M(QueryInterface) { STDRET(3, 0x80004002u); }
M(AddRef) { HOSTFN; DEV; STDRET(1, ++d->com.refs); }
M(Release) { HOSTFN; DEV; STDRET(1, d->com.refs > 1 ? --d->com.refs : 1); }
M(TestCooperativeLevel) { STDRET(1, HR_OK); }
M(GetAvailableTextureMem) { STDRET(1, 128u << 20); }
M(ResourceManagerDiscardBytes) { STDRET(2, HR_OK); }
M(GetDirect3D) {
    HOSTFN;
    extern void *g_d3d_obj;
    if (!g_d3d_obj) { W32(ARG(2), 0); STDRET(2, HR_FAIL); }
    d3d_addref(g_d3d_obj);
    W32(ARG(2), ((ComObj *)g_d3d_obj)->guest);
    STDRET(2, HR_OK);
}
M(GetDeviceCaps) { HOSTFN; d3d_fill_caps(ARG(2)); STDRET(2, HR_OK); }
M(GetDisplayMode) {
    HOSTFN; DEV;
    uint32_t p = ARG(2);
    W32(p, d->bb_w); W32(p + 4, d->bb_h); W32(p + 8, 60); W32(p + 12, FMT_X8R8G8B8);
    STDRET(2, HR_OK);
}
M(GetCreationParameters) {
    HOSTFN;
    uint32_t p = ARG(2);
    W32(p, 0); W32(p + 4, 1); W32(p + 8, 0x00010010u); W32(p + 12, 0x40);
    STDRET(2, HR_OK);
}
M(SetCursorProperties) { STDRET(4, HR_OK); }
M(SetCursorPosition) { STDRET(4, HR_OK); }
// bShow != 0: on-screen pointer in use -> dinput.c puts every direction on the left stick (g_cursor_seen)
M(ShowCursor) {
    HOSTFN; extern unsigned g_cursor_seen, g_frame;
    static unsigned last = 2;
    unsigned s = ARG(2) & 1;
    if (s) g_cursor_seen = g_frame + 1;
    if (s != last) { last = s; rt_log("[cursor] show=%u frame %u\n", s, g_frame); }
    STDRET(2, 0);
}
M(CreateAdditionalSwapChain) { d3d_log_once("CreateAdditionalSwapChain"); STDRET(3, D3DERR_INVALIDCALL); }
// ponytail: the backbuffer size does not change; SH4_RES re-imposes the resolution that the game option (0x524320) just overwrote.
M(Reset) { HOSTFN; DEV; rt_log("[d3d8] Reset %ux%u\n", R32(ARG(2)), R32(ARG(2) + 4)); rt_apply_res(); prep_gfx_init(); STDRET(2, HR_OK); }
M(Present) {
    HOSTFN; DEV;
    if (g_t_begin) g_t_body += SDL_GetPerformanceCounter() - g_t_begin;
    g_t_begin = 0;
    g_frame++;
    rt_keep_res();
    // Laboratory schedule, e.g. "4200:7,4700:8". Advance before calling the
    // original blocking player: it recursively Presents while a movie runs.
    static const char *pm;
    static int movie_test_active;
    if (!pm) pm = getenv("SH4_PLAY_MOVIE") ? getenv("SH4_PLAY_MOVIE") : "";
    unsigned pf = 0, pn = 0; int consumed = 0;
    if (!movie_test_active && *pm && sscanf(pm, "%u:%u%n", &pf, &pn, &consumed) == 2 &&
        pf && pn < 14 && (!pm[consumed] || pm[consumed] == ',') && g_frame >= pf && g_main_cpu) {
        pm += consumed;
        if (*pm == ',') pm++;
        movie_test_active = 1;
        rt_log("[test] playing cutscene %u\n", pn);
        rt_guest_call(g_main_cpu, 0x412ef0, 1, pn);
        rt_log("[test] cutscene finished\n");
        movie_test_active = 0;
    }
    extern void rt_controls_present(void), console_ui_finish_frame(void);
    rt_controls_present();
    console_ui_finish_frame();
    prep_present();
    prep_gdrain();
    rt_audit_frame(g_frame);
    (void)d;
    STDRET(5, HR_OK);
}
M(GetBackBuffer) {
    HOSTFN; DEV;
    addref(d->backbuffer);
    W32(ARG(4), d->backbuffer->com.guest);
    STDRET(4, HR_OK);
}
M(GetRasterStatus) { HOSTFN; W32(ARG(2), 1); W32(ARG(2) + 4, 0); STDRET(2, HR_OK); }
M(SetGammaRamp) {
    HOSTFN; DEV;
    // The render consumer owns a separate Device, including the final-blit gamma ramp.
    if (prep_set(d->gamma, mb + ARG(3), sizeof d->gamma) && g_audit_checks)
        rt_log("[audit-gamma] red=%u/%u/%u green=%u/%u/%u blue=%u/%u/%u\n",
               d->gamma[0][0], d->gamma[0][128], d->gamma[0][255],
               d->gamma[1][0], d->gamma[1][128], d->gamma[1][255],
               d->gamma[2][0], d->gamma[2][128], d->gamma[2][255]);
    STDRET(3, 0);
}
M(GetGammaRamp) { HOSTFN; DEV; memcpy(mb + ARG(2), d->gamma, sizeof d->gamma); STDRET(2, 0); }

M(CreateTexture) {
    HOSTFN;
    static int s_first = 1;
    if (s_first) { s_first = 0; rt_log("[d3d8] first CreateTexture %ux%u fmt=%u\n", ARG(2), ARG(3), ARG(6)); }
    Texture *t = tex_create(ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7));
    if (!t) STDRET(8, D3DERR_INVALIDCALL);
    W32(ARG(8), t->com.guest);
    STDRET(8, HR_OK);
}
M(CreateVolumeTexture) { HOSTFN; d3d_log_once("CreateVolumeTexture"); W32(ARG(9), 0); STDRET(9, D3DERR_NOTAVAILABLE); }
M(CreateCubeTexture) { HOSTFN; d3d_log_once("CreateCubeTexture"); W32(ARG(7), 0); STDRET(7, D3DERR_NOTAVAILABLE); }
M(CreateVertexBuffer) {
    HOSTFN;
    Buffer *b = buf_create(ARG(2), ARG(3), ARG(4), ARG(5), 0);
    W32(ARG(6), b->com.guest);
    STDRET(6, HR_OK);
}
M(CreateIndexBuffer) {
    HOSTFN;
    Buffer *b = buf_create(ARG(2), ARG(3), ARG(4), ARG(5), 1);
    W32(ARG(6), b->com.guest);
    STDRET(6, HR_OK);
}
M(CreateRenderTarget) {
    HOSTFN;
    Surface *s = surf_create(ARG(2), ARG(3), ARG(4), 0);
    s->usage = 1; // D3DUSAGE_RENDERTARGET : stockage GL
    W32(ARG(7), surf_wrap(s));
    STDRET(7, HR_OK);
}
M(CreateDepthStencilSurface) {
    HOSTFN;
    Surface *s = surf_create(ARG(2), ARG(3), ARG(4), 1);
    W32(ARG(6), surf_wrap(s));
    STDRET(6, HR_OK);
}
M(CreateImageSurface) {
    HOSTFN;
    Surface *s = surf_create(ARG(2), ARG(3), ARG(4), 0);
    W32(ARG(5), surf_wrap(s));
    STDRET(5, HR_OK);
}
void surf_copy_rects(Surface *src, uint32_t rects, uint32_t n, Surface *dst, uint32_t points);
M(CopyRects) {
    HOSTFN;
    Surface *src = com_get_kind(ARG(2), K_SURFACE), *dst = com_get_kind(ARG(5), K_SURFACE);
    if (!src || !dst) STDRET(6, D3DERR_INVALIDCALL);
    surf_copy_rects(src, ARG(3), ARG(4), dst, ARG(6));
    STDRET(6, HR_OK);
}
M(UpdateTexture) { d3d_log_once("UpdateTexture"); STDRET(3, HR_OK); }
M(GetFrontBuffer) { d3d_log_once("GetFrontBuffer"); STDRET(2, HR_OK); }
M(SetRenderTarget) {
    HOSTFN; DEV;
    Surface *rt = ARG(2) ? com_get_kind(ARG(2), K_SURFACE) : NULL;
    Surface *ds = ARG(3) ? com_get_kind(ARG(3), K_SURFACE) : NULL;
    if (rt) {
        Surface *o = d->rt; addref(rt); PSET(rt, rt); if (o) d3d_release(&o->com);
        // D3D8: SetRenderTarget resets the viewport to the whole new target (the 4096x4096 pause capture relies on it)
        __typeof__(d->vp) v = {0, 0, rt->w, rt->h, 0.0f, 1.0f};
        prep_set(&d->vp, &v, sizeof v);
    }
    if (ds || ARG(3) == 0) { Surface *o = d->ds; addref(ds); PSET(ds, ds); if (o) d3d_release(&o->com); }
    prep_bind();
    STDRET(3, HR_OK);
}
M(GetRenderTarget) { HOSTFN; DEV; addref(d->rt); W32(ARG(2), d->rt ? d->rt->com.guest : 0); STDRET(2, HR_OK); }
M(GetDepthStencilSurface) {
    HOSTFN; DEV;
    addref(d->ds);
    W32(ARG(2), d->ds ? d->ds->com.guest : 0);
    STDRET(2, d->ds ? HR_OK : 0x8876086cu);
}
M(BeginScene) {
    HOSTFN; DEV;
    if (!d->in_scene) g_t_begin = SDL_GetPerformanceCounter();
    d->in_scene = 1;
    STDRET(1, HR_OK);
}
M(EndScene) { HOSTFN; DEV; d->in_scene = 0; STDRET(1, HR_OK); }
M(Clear) {
    HOSTFN; DEV;
    prep_clear(ARG(4), ARG(5), rt_f32(ARG(6)), ARG(7), (int)ARG(2), ARG(3));
    STDRET(7, HR_OK);
}
static float *xform(Device *d, uint32_t st) {
    if (st == 2) return d->view;
    if (st == 3) return d->proj;
    if (st >= 16 && st < 24) return d->texm[st - 16];
    if (st == 256) return d->world;
    return NULL;
}
M(SetTransform) { HOSTFN; DEV; float *m = xform(d, ARG(2)); if (m) prep_set(m, mb + ARG(3), 64); STDRET(3, HR_OK); }
M(GetTransform) { HOSTFN; DEV; float *m = xform(d, ARG(2)); if (m) memcpy(mb + ARG(3), m, 64); STDRET(3, HR_OK); }
M(MultiplyTransform) {
    HOSTFN; DEV;
    float *m = xform(d, ARG(2)), b[16], r[16];
    if (m) {
        memcpy(b, mb + ARG(3), 64);
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                r[i * 4 + j] = b[i * 4 + 0] * m[0 + j] + b[i * 4 + 1] * m[4 + j] + b[i * 4 + 2] * m[8 + j] + b[i * 4 + 3] * m[12 + j];
        prep_set(m, r, 64);
    }
    STDRET(3, HR_OK);
}
M(SetViewport) {
    HOSTFN; DEV;
    uint32_t p = ARG(2);
    __typeof__(d->vp) v = {R32(p), R32(p + 4), R32(p + 8), R32(p + 12), rt_f32(R32(p + 16)), rt_f32(R32(p + 20))};
    prep_set(&d->vp, &v, sizeof v);
    STDRET(2, HR_OK);
}
M(GetViewport) {
    HOSTFN; DEV;
    uint32_t p = ARG(2);
    W32(p, d->vp.x); W32(p + 4, d->vp.y); W32(p + 8, d->vp.w); W32(p + 12, d->vp.h);
    W32(p + 16, rt_u32f(d->vp.minz)); W32(p + 20, rt_u32f(d->vp.maxz));
    STDRET(2, HR_OK);
}
static uint8_t g_material[68], g_lights[8][104];
M(SetMaterial) { HOSTFN; memcpy(g_material, mb + ARG(2), 68); STDRET(2, HR_OK); }
M(GetMaterial) { HOSTFN; memcpy(mb + ARG(2), g_material, 68); STDRET(2, HR_OK); }
M(SetLight) { HOSTFN; if (ARG(2) < 8) memcpy(g_lights[ARG(2)], mb + ARG(3), 104); STDRET(3, HR_OK); }
M(GetLight) { HOSTFN; if (ARG(2) < 8) memcpy(mb + ARG(3), g_lights[ARG(2)], 104); STDRET(3, HR_OK); }
M(LightEnable) { STDRET(3, HR_OK); }
M(GetLightEnable) { HOSTFN; W32(ARG(3), 0); STDRET(3, HR_OK); }
M(SetClipPlane) { STDRET(3, HR_OK); }
M(GetClipPlane) { STDRET(3, HR_OK); }
M(SetRenderState) { HOSTFN; DEV; if (ARG(2) < 256) { uint32_t v = ARG(3); prep_set(&d->rs[ARG(2)], &v, 4); } STDRET(3, HR_OK); }
M(GetRenderState) { HOSTFN; DEV; W32(ARG(3), ARG(2) < 256 ? d->rs[ARG(2)] : 0); STDRET(3, HR_OK); }

// State blocks: full capture of the "simple" state (states, transforms, shaders).
typedef struct { uint32_t rs[256], tss[MAX_STAGES][32], vs, ps; float vsc[MAX_VSC][4]; } StateBlock;
static StateBlock *blocks[256];
static int recording;
static uint32_t sb_new(Device *d) {
    for (int i = 1; i < 256; i++)
        if (!blocks[i]) {
            blocks[i] = malloc(sizeof(StateBlock));
            memcpy(blocks[i]->rs, d->rs, sizeof d->rs);
            memcpy(blocks[i]->tss, d->tss, sizeof d->tss);
            memcpy(blocks[i]->vsc, d->vsc, sizeof d->vsc);
            blocks[i]->vs = d->vs; blocks[i]->ps = d->ps;
            return (uint32_t)i;
        }
    return 0;
}
M(BeginStateBlock) { recording = 1; d3d_log_once("BeginStateBlock"); STDRET(1, HR_OK); }
M(EndStateBlock) { HOSTFN; DEV; recording = 0; W32(ARG(2), sb_new(d)); STDRET(2, HR_OK); }
M(ApplyStateBlock) {
    HOSTFN; DEV;
    StateBlock *b = ARG(2) < 256 ? blocks[ARG(2)] : NULL;
    if (b) {
        prep_set(d->rs, b->rs, sizeof d->rs);
        prep_set(d->tss, b->tss, sizeof d->tss);
        prep_set(d->vsc, b->vsc, sizeof d->vsc);
        PSET(vs, b->vs); PSET(ps, b->ps);
    }
    STDRET(2, HR_OK);
}
M(CaptureStateBlock) {
    HOSTFN; DEV;
    StateBlock *b = ARG(2) < 256 ? blocks[ARG(2)] : NULL;
    if (b) {
        memcpy(b->rs, d->rs, sizeof d->rs);
        memcpy(b->tss, d->tss, sizeof d->tss);
        memcpy(b->vsc, d->vsc, sizeof d->vsc);
        b->vs = d->vs; b->ps = d->ps;
    }
    STDRET(2, HR_OK);
}
M(DeleteStateBlock) { HOSTFN; if (ARG(2) < 256) { free(blocks[ARG(2)]); blocks[ARG(2)] = NULL; } STDRET(2, HR_OK); }
M(CreateStateBlock) { HOSTFN; DEV; W32(ARG(3), sb_new(d)); STDRET(3, HR_OK); }
M(SetClipStatus) { STDRET(2, HR_OK); }
M(GetClipStatus) { HOSTFN; W32(ARG(2), 0); W32(ARG(2) + 4, 0); STDRET(2, HR_OK); }
M(GetTexture) {
    HOSTFN; DEV;
    Texture *t = ARG(2) < MAX_STAGES ? d->tex[ARG(2)] : NULL;
    addref(t);
    W32(ARG(3), t ? t->com.guest : 0);
    STDRET(3, HR_OK);
}
M(SetTexture) {
    HOSTFN; DEV;
    uint32_t s = ARG(2);
    if (s >= MAX_STAGES) STDRET(3, HR_OK);
    Texture *t = ARG(3) ? com_get_kind(ARG(3), K_TEXTURE) : NULL;
    if (ARG(3) && !t) d3d_log_once("SetTexture: unsupported texture type (cube/volume?)");
    Texture *o = d->tex[s];
    addref(t);
    PSET(tex[s], t);
    if (o) d3d_release(&o->com);
    STDRET(3, HR_OK);
}
M(GetTextureStageState) {
    HOSTFN; DEV;
    W32(ARG(4), ARG(2) < MAX_STAGES && ARG(3) < 32 ? d->tss[ARG(2)][ARG(3)] : 0);
    STDRET(4, HR_OK);
}
M(SetTextureStageState) {
    HOSTFN; DEV;
    if (ARG(2) < MAX_STAGES && ARG(3) < 32) { uint32_t v = ARG(4); prep_set(&d->tss[ARG(2)][ARG(3)], &v, 4); }
    STDRET(4, HR_OK);
}
M(ValidateDevice) { HOSTFN; W32(ARG(2), 1); STDRET(2, HR_OK); }
M(GetInfo) { STDRET(4, 1); } // S_FALSE
M(SetPaletteEntries) { d3d_log_once("SetPaletteEntries"); STDRET(3, HR_OK); }
M(GetPaletteEntries) { STDRET(3, HR_OK); }
M(SetCurrentTexturePalette) { STDRET(2, HR_OK); }
M(GetCurrentTexturePalette) { HOSTFN; W32(ARG(2), 0); STDRET(2, HR_OK); }

static uint32_t prim_verts(uint32_t type, uint32_t n) {
    switch (type) {
    case 1: return n;          // POINTLIST
    case 2: return n * 2;      // LINELIST
    case 3: return n + 1;      // LINESTRIP
    case 4: return n * 3;      // TRIANGLELIST
    case 5: case 6: return n + 2; // TRIANGLESTRIP / FAN
    default: return 0;
    }
}
M(DrawPrimitive) {
    HOSTFN; DEV;
    prep_draw(d, ARG(2), ARG(3), prim_verts(ARG(2), ARG(4)), ARG(4), NULL, 0, 0, NULL, 0);
    STDRET(4, HR_OK);
}
M(DrawIndexedPrimitive) {
    HOSTFN; DEV;
    uint32_t type = ARG(2), minidx = ARG(3), start = ARG(5), n = ARG(6);
    if (!d->ib) STDRET(6, D3DERR_INVALIDCALL);
    int i32 = d->ib->fmt_or_fvf == FMT_INDEX32;
    const uint8_t *idx = mb + d->ib->mem + start * (i32 ? 4 : 2);
    prep_draw(d, type, d->base_vertex, prim_verts(type, n), n, idx, i32, minidx, NULL, 0);
    STDRET(6, HR_OK);
}
M(DrawPrimitiveUP) {
    HOSTFN; DEV;
    prep_draw(d, ARG(2), 0, prim_verts(ARG(2), ARG(3)), ARG(3), NULL, 0, 0, mb + ARG(4), ARG(5));
    STDRET(5, HR_OK);
}
M(DrawIndexedPrimitiveUP) {
    HOSTFN; DEV;
    uint32_t type = ARG(2), minidx = ARG(3), n = ARG(5);
    prep_draw(d, type, 0, prim_verts(type, n), n, mb + ARG(6), ARG(7) == FMT_INDEX32, minidx, mb + ARG(8), ARG(9));
    STDRET(9, HR_OK);
}
M(ProcessVertices) { d3d_log_once("ProcessVertices"); STDRET(6, D3DERR_INVALIDCALL); }
M(CreateVertexShader) {
    HOSTFN;
    static unsigned s_n;
    uint32_t h = vs_create(ARG(2), ARG(3));
    if (++s_n <= 3 || s_n % 50 == 0) rt_log("[vs] CreateVertexShader #%u -> 0x%x\n", s_n, h);
    W32(ARG(4), h);
    STDRET(5, h ? HR_OK : D3DERR_INVALIDCALL);
}
M(SetVertexShader) { HOSTFN; DEV; PSET(vs, ARG(2)); STDRET(2, HR_OK); }
M(GetVertexShader) { HOSTFN; DEV; W32(ARG(2), d->vs); STDRET(2, HR_OK); }
M(DeleteVertexShader) { HOSTFN; prep_vsdel(ARG(2)); STDRET(2, HR_OK); }
M(SetVertexShaderConstant) {
    HOSTFN; DEV;
    uint32_t r = ARG(2), n = ARG(4);
    if (r + n > MAX_VSC) n = r < MAX_VSC ? MAX_VSC - r : 0;
    if (n) prep_set(d->vsc[r], mb + ARG(3), 16u * n);
    STDRET(4, HR_OK);
}

/* GOG v5 0x428e64..0x428e9d: upload twenty three-vector model matrices.
   No draw or state read occurs between those uploads. Assemble the resulting
   constant interval once, preserving the slots whose index is -1. Keep the
   original guest register/stack effects for the surrounding generated code. */
int d3d_model_palette(CPU *c) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *s = getenv("SH4_NATIVE_PALETTE");
        enabled = s && !strcmp(s, "1");
    }
    if (!enabled) return 0;
    uint8_t *const mb = g_mem;
    uint32_t guest = R32(0x6e7d94u);
    Device *d = g_dev;
    if (!d || d->com.guest != guest ||
        rt_lookup(R32(R32(guest) + 0x13cu)) != dev_SetVertexShaderConstant) return 0;

    uint32_t table = R32(c->esp + 0x10u), last = 0xffffffffu;
    float palette[60][4];
    memcpy(palette, d->vsc[34], sizeof palette);
    for (uint32_t i = 0; i < 20; ++i) {
        uint32_t index = R32(table + 4u * i);
        c->eax = index;
        if (index == 0xffffffffu) continue;
        uint32_t src = 0xf629c0u + (index << 6);
        memcpy(palette[3u * i], mb + src, 48);
        W32(c->esp - 4u, 3);
        W32(c->esp - 8u, src);
        W32(c->esp - 12u, 34u + 3u * i);
        W32(c->esp - 16u, guest);
        W32(c->esp - 20u, 0x428e8au);
        c->eax = HR_OK;
        c->ecx = guest;
        last = i;
    }
    if (last != 0xffffffffu) prep_set(d->vsc[34], palette, sizeof palette);
    c->edx = table + 80u;
    c->edi = 94;
    W32(c->esp + 0x10u, c->edx);
    return 1;
}
M(GetVertexShaderConstant) {
    HOSTFN; DEV;
    uint32_t r = ARG(2), n = ARG(4);
    if (r + n > MAX_VSC) n = r < MAX_VSC ? MAX_VSC - r : 0;
    memcpy(mb + ARG(3), d->vsc[r], 16u * n);
    STDRET(4, HR_OK);
}
M(GetVertexShaderDeclaration) { d3d_log_once("GetVertexShaderDeclaration"); STDRET(4, D3DERR_INVALIDCALL); }
M(GetVertexShaderFunction) { d3d_log_once("GetVertexShaderFunction"); STDRET(4, D3DERR_INVALIDCALL); }
M(SetStreamSource) {
    HOSTFN; DEV;
    uint32_t n = ARG(2);
    if (n >= 16) STDRET(4, D3DERR_INVALIDCALL);
    Buffer *b = ARG(3) ? com_get_kind(ARG(3), K_VB) : NULL;
    Buffer *o = d->stream[n].vb;
    addref(b);
    PSET(stream[n].vb, b);
    PSET(stream[n].stride, ARG(4));
    if (o) d3d_release(&o->com);
    STDRET(4, HR_OK);
}
M(GetStreamSource) {
    HOSTFN; DEV;
    uint32_t n = ARG(2) & 15;
    addref(d->stream[n].vb);
    W32(ARG(3), d->stream[n].vb ? d->stream[n].vb->com.guest : 0);
    W32(ARG(4), d->stream[n].stride);
    STDRET(4, HR_OK);
}
M(SetIndices) {
    HOSTFN; DEV;
    Buffer *b = ARG(2) ? com_get_kind(ARG(2), K_IB) : NULL;
    Buffer *o = d->ib;
    addref(b);
    PSET(ib, b);
    PSET(base_vertex, ARG(3));
    if (o) d3d_release(&o->com);
    STDRET(3, HR_OK);
}
M(GetIndices) {
    HOSTFN; DEV;
    addref(d->ib);
    W32(ARG(2), d->ib ? d->ib->com.guest : 0);
    W32(ARG(3), d->base_vertex);
    STDRET(3, HR_OK);
}
M(CreatePixelShader) {
    HOSTFN;
    uint32_t h = ps_create(ARG(2));
    W32(ARG(3), h);
    STDRET(3, h ? HR_OK : D3DERR_INVALIDCALL);
}
M(SetPixelShader) { HOSTFN; DEV; PSET(ps, ARG(2)); STDRET(2, HR_OK); }
M(GetPixelShader) { HOSTFN; DEV; W32(ARG(2), d->ps); STDRET(2, HR_OK); }
M(DeletePixelShader) { HOSTFN; prep_psdel(ARG(2)); STDRET(2, HR_OK); }
M(SetPixelShaderConstant) {
    HOSTFN; DEV;
    uint32_t r = ARG(2), n = ARG(4);
    if (r + n > 8) n = r < 8 ? 8 - r : 0;
    if (n) prep_set(d->psc[r], mb + ARG(3), 16u * n);
    STDRET(4, HR_OK);
}
M(GetPixelShaderConstant) {
    HOSTFN; DEV;
    uint32_t r = ARG(2), n = ARG(4);
    if (r + n > 8) n = r < 8 ? 8 - r : 0;
    memcpy(mb + ARG(3), d->psc[r], 16u * n);
    STDRET(4, HR_OK);
}
M(GetPixelShaderFunction) { STDRET(4, D3DERR_INVALIDCALL); }
M(DrawRectPatch) { d3d_log_once("DrawRectPatch"); STDRET(4, HR_OK); }
M(DrawTriPatch) { d3d_log_once("DrawTriPatch"); STDRET(4, HR_OK); }
M(DeletePatch) { STDRET(2, HR_OK); }

static const gfn dev_vt[] = {
    dev_QueryInterface, dev_AddRef, dev_Release, dev_TestCooperativeLevel, dev_GetAvailableTextureMem,
    dev_ResourceManagerDiscardBytes, dev_GetDirect3D, dev_GetDeviceCaps, dev_GetDisplayMode,
    dev_GetCreationParameters, dev_SetCursorProperties, dev_SetCursorPosition, dev_ShowCursor,
    dev_CreateAdditionalSwapChain, dev_Reset, dev_Present, dev_GetBackBuffer, dev_GetRasterStatus,
    dev_SetGammaRamp, dev_GetGammaRamp, dev_CreateTexture, dev_CreateVolumeTexture, dev_CreateCubeTexture,
    dev_CreateVertexBuffer, dev_CreateIndexBuffer, dev_CreateRenderTarget, dev_CreateDepthStencilSurface,
    dev_CreateImageSurface, dev_CopyRects, dev_UpdateTexture, dev_GetFrontBuffer, dev_SetRenderTarget,
    dev_GetRenderTarget, dev_GetDepthStencilSurface, dev_BeginScene, dev_EndScene, dev_Clear, dev_SetTransform,
    dev_GetTransform, dev_MultiplyTransform, dev_SetViewport, dev_GetViewport, dev_SetMaterial, dev_GetMaterial,
    dev_SetLight, dev_GetLight, dev_LightEnable, dev_GetLightEnable, dev_SetClipPlane, dev_GetClipPlane,
    dev_SetRenderState, dev_GetRenderState, dev_BeginStateBlock, dev_EndStateBlock, dev_ApplyStateBlock,
    dev_CaptureStateBlock, dev_DeleteStateBlock, dev_CreateStateBlock, dev_SetClipStatus, dev_GetClipStatus,
    dev_GetTexture, dev_SetTexture, dev_GetTextureStageState, dev_SetTextureStageState, dev_ValidateDevice,
    dev_GetInfo, dev_SetPaletteEntries, dev_GetPaletteEntries, dev_SetCurrentTexturePalette,
    dev_GetCurrentTexturePalette, dev_DrawPrimitive, dev_DrawIndexedPrimitive, dev_DrawPrimitiveUP,
    dev_DrawIndexedPrimitiveUP, dev_ProcessVertices, dev_CreateVertexShader, dev_SetVertexShader,
    dev_GetVertexShader, dev_DeleteVertexShader, dev_SetVertexShaderConstant, dev_GetVertexShaderConstant,
    dev_GetVertexShaderDeclaration, dev_GetVertexShaderFunction, dev_SetStreamSource, dev_GetStreamSource,
    dev_SetIndices, dev_GetIndices, dev_CreatePixelShader, dev_SetPixelShader, dev_GetPixelShader,
    dev_DeletePixelShader, dev_SetPixelShaderConstant, dev_GetPixelShaderConstant, dev_GetPixelShaderFunction,
    dev_DrawRectPatch, dev_DrawTriPatch, dev_DeletePatch};

static void default_states(Device *d, int autodepth) {
    static const uint32_t rs_def[][2] = {
        {8, 3}, {9, 2}, {14, 1}, {16, 1}, {19, 2}, {20, 1}, {22, 3}, {23, 4}, {25, 8}, {37, 0x3f800000},
        {38, 0x3f800000}, {53, 1}, {54, 1}, {55, 1}, {56, 8}, {58, 0xffffffff}, {59, 0xffffffff},
        {60, 0xffffffff}, {136, 1}, {137, 1}, {141, 1}, {142, 1}, {145, 1}, {146, 2}, {154, 0x3f800000},
        {161, 1}, {162, 0xffffffff}, {166, 0x42800000}, {168, 0xf}, {171, 1}};
    memset(d->rs, 0, sizeof d->rs);
    for (unsigned i = 0; i < sizeof rs_def / sizeof rs_def[0]; i++) d->rs[rs_def[i][0]] = rs_def[i][1];
    d->rs[7] = autodepth ? 1 : 0;
    for (int s = 0; s < MAX_STAGES; s++) {
        uint32_t *t = d->tss[s];
        memset(t, 0, 32 * 4);
        t[1] = s ? 1 : 4; t[2] = 2; t[3] = 1; t[4] = s ? 1 : 2; t[5] = 2; t[6] = 1;
        t[11] = (uint32_t)s; t[13] = 1; t[14] = 1; t[16] = 1; t[17] = 1; t[18] = 0; t[21] = 1;
        t[25] = 1; t[26] = 1; t[27] = 1; t[28] = 1;
    }
    for (int i = 0; i < 16; i++) d->world[i] = d->view[i] = d->proj[i] = (i % 5 == 0);
    for (int s = 0; s < MAX_STAGES; s++)
        for (int i = 0; i < 16; i++) d->texm[s][i] = (i % 5 == 0);
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < 256; i++) d->gamma[c][i] = (uint16_t)(i * 257);
}

uint32_t device_create(uint32_t pp) {
    HOSTFN;
    Device *d = calloc(1, sizeof *d);
    d->bb_w = R32(pp) ? R32(pp) : 640;
    d->bb_h = R32(pp + 4) ? R32(pp + 4) : 480;
    default_states(d, R32(pp + 32));
    d->vp.w = d->bb_w; d->vp.h = d->bb_h; d->vp.maxz = 1.0f;
    d->backbuffer = surf_create(d->bb_w, d->bb_h, FMT_X8R8G8B8, 0);
    d->backbuffer->is_backbuffer = 1;
    surf_wrap(d->backbuffer);
    if (R32(pp + 32)) {
        d->depth = surf_create(d->bb_w, d->bb_h, R32(pp + 36), 1);
        surf_wrap(d->depth);
    }
    d->rt = d->backbuffer; d->rt->com.refs++;
    d->ds = d->depth; if (d->ds) d->ds->com.refs++;
    g_dev = d;
    prep_init(d);
    prep_gfx_init();
    return com_wrap(&d->com, K_DEVICE, dev_vt, 97, "IDirect3DDevice8");
}
