// Direct3D 8 emulated on OpenGL: guest COM objects, device state, resources.
#pragma once
#include "rt.h"
#include <SDL2/SDL_timer.h>
#include "gl.h"

// ---------- Guest COM: object = {guest vtable, host index} ----------
typedef struct ComObj {
    uint32_t guest;   // guest address of the object
    int32_t refs;
    int kind;
} ComObj;
enum { K_D3D, K_DEVICE, K_TEXTURE, K_SURFACE, K_VB, K_IB, K_CUBE, K_VOLTEX,
       K_DINPUT, K_DIDEV, K_DSOUND, K_DSBUF, K_DS3DL, K_DS3DB, K_DSNOTIFY };

uint32_t com_wrap(ComObj *o, int kind, const gfn *methods, int n, const char *iface);
ComObj *com_get(uint32_t guest);
void *com_get_kind(uint32_t guest, int kind);
void com_free(ComObj *o);
#define THIS(T, kind) T *self = com_get_kind(ARG(1), kind); (void)self
#define HR_OK 0u
#define HR_FAIL 0x80004005u
#define D3DERR_INVALIDCALL 0x8876086cu
#define D3DERR_NOTAVAILABLE 0x8876086au

// ---------- formats ----------
#define FMT_R8G8B8   20
#define FMT_A8R8G8B8 21
#define FMT_X8R8G8B8 22
#define FMT_R5G6B5   23
#define FMT_X1R5G5B5 24
#define FMT_A1R5G5B5 25
#define FMT_A4R4G4B4 26
#define FMT_A8       28
#define FMT_P8       41
#define FMT_L8       50
#define FMT_A8L8     51
#define FMT_D16      80
#define FMT_D24S8    75
#define FMT_D24X8    77
#define FMT_INDEX16  101
#define FMT_INDEX32  102
#define FMT_DXT1 0x31545844u
#define FMT_DXT2 0x32545844u
#define FMT_DXT3 0x33545844u
#define FMT_DXT4 0x34545844u
#define FMT_DXT5 0x35545844u

// ---------- ressources ----------
typedef struct Texture Texture;
typedef struct Surface {
    ComObj com;
    uint32_t fmt, w, h, usage, pool;
    Texture *tex;          // parent texture (NULL for a standalone surface)
    int level;
    uint32_t mem;          // guest copy (Lock), allocated on demand
    GLuint rb;             // renderbuffer (profondeur/stencil autonome)
    GLuint fbo_tex;        // GL texture of standalone render-target surfaces (back buffer...)
    int is_backbuffer, is_depth;
    struct Surface *pending;   // deferred GL -> system memory copy (see surf_flush_pending)
    uint32_t lock_flags;
} Surface;

struct Texture {
    ComObj com;
    uint32_t fmt, w, h, levels, usage, pool;
    GLuint gl;
    Surface **lv;          // surfaces de niveau
    int dirty;
    int movie;             // movie texture (single level, size of the current movie)
    unsigned up_frame;     // g_frame + 1 of the last upload
    int tfmt;              // Vita: GL format of the whole mip chain, fixed by level 0 (1 = 565, 2 = 4444, 3 = RGBA8): vitaGL mixes up levels of different formats
    int stg_alloc;         // movie (Vita): GL 565 storage exists (565 frames come from movie_565_take)
    uint32_t use;          // ring position (prep.c) of the last record that reads/writes this texture
};
extern int g_movie_w, g_movie_h;
uint16_t *movie_565_take(uint32_t w, uint32_t h, volatile int **busy);   // movie.c (Vita)

typedef struct Buffer {    // vertex or index buffer: data lives in guest memory
    ComObj com;
    uint32_t mem, size, usage, fmt_or_fvf, pool;
    GLuint gl;             // GPU copy (SH4_VBO); range [dlo, dhi) locked since the last upload
    uint32_t dlo, dhi, relocks;
    int dyn;               // relocked often: stays in client arrays
    uint32_t ver;          // unique content id (renewed on creation and on every Lock/Unlock): IBO cache key
    uint32_t use;          // ring position of the last draw that reads this static buffer (prep.c)
    // Native model UV bounds: owned by this resource, invalidated by every
    // potentially writing Lock/Unlock. No address-only cache survives reuse.
    uint32_t model_bounds_n, model_initial[4], model_extrema[4], model_read_ver;
    CPU *model_reader;
    struct ModelUvCache *model_uv;
} Buffer;

// ---------- device state ----------
#define MAX_STAGES 8
#define MAX_VSC 96
typedef struct VShader VShader;
typedef struct PShader PShader;

typedef struct Device {
    ComObj com;
    uint32_t rs[256];
    uint32_t tss[MAX_STAGES][32];
    float world[16], view[16], proj[16], texm[MAX_STAGES][16];
    struct { uint32_t x, y, w, h; float minz, maxz; } vp;
    Texture *tex[MAX_STAGES];
    struct { Buffer *vb; uint32_t stride; } stream[16];
    Buffer *ib;
    uint32_t base_vertex;
    uint32_t vs;           // VS handle (>= 0x10000 or odd = shader) or FVF
    uint32_t ps;
    float vsc[MAX_VSC][4];
    float psc[8][4];
    uint32_t vsc_gen, psc_gen;   // incremented on every real change (avoids resending identical uniforms)
    Surface *rt, *ds, *backbuffer, *depth;
    GLuint fbo;            // FBO for render targets other than the back buffer
    int in_scene;
    uint32_t bb_w, bb_h;
    uint16_t gamma[3][256];
    Surface *ui_display;   // optional native-resolution menu surface for final blit
} Device;

extern Device *g_dev;

// ---------- shaders ----------
struct VShader {
    uint32_t handle;
    uint32_t decl[64];     // copied declaration (tokens)
    int ndecl;
    char *glsl;            // NULL if fixed-function VS (FVF + declaration)
    int is_fixed;
    int nc;                // constant registers read (partial uniform upload)
    uint32_t smask1;       // streams read (mask | 0x10000), 0 = not computed yet (gfx_stream_mask)
    uint64_t source_hash;  // audit: catch program-cache keys aliasing a freed/reallocated shader
};
struct PShader {
    uint32_t handle;
    char *glsl_body;       // GLSL fragment body
    int ntex;
    uint64_t source_hash;
};

uint32_t vs_create(uint32_t decl_addr, uint32_t func_addr);
VShader *vs_get(uint32_t handle);
void vs_delete(uint32_t handle);
uint32_t ps_create(uint32_t func_addr);
PShader *ps_get(uint32_t handle);
void ps_delete(uint32_t handle);
char *vs_translate(const uint32_t *tok, int *err);
char *ps_translate(const uint32_t *tok, int *err);

// ---------- rendu ----------
void gfx_init(Device *d);
// inl[s]: data of stream s copied into the record (already offset: inl[s] + (first+lo)*stride = start byte); NULL = guest/GL buffer
void gfx_draw(Device *d, uint32_t prim, uint32_t first_vertex, uint32_t nverts, uint32_t nprims,
              const void *indices, int index32, uint32_t min_index, const uint8_t *up_data, uint32_t up_stride,
              const uint8_t *const *inl);
void gfx_clear(Device *d, uint32_t flags, uint32_t color, float z, uint32_t stencil, int nrects, const int32_t *rects);   // rects: 4 int32 per rectangle (host)
void gfx_present(Device *d);
void gfx_bind_target(Device *d);
void tex_upload(Texture *t, int level);
GLuint tex_gl(Texture *t);
unsigned rt_shift(const Surface *s);   // Vita: render targets > 1024 reduced by a power of 2 (VRAM)

// conversions
uint32_t fmt_bpp(uint32_t fmt);        // bits per pixel (0 = compressed)
int fmt_is_dxt(uint32_t fmt);
uint32_t fmt_level_size(uint32_t fmt, uint32_t w, uint32_t h);
uint32_t fmt_pitch(uint32_t fmt, uint32_t w);
void surf_flush_pending(Surface *s);

Texture *tex_create(uint32_t w, uint32_t h, uint32_t levels, uint32_t usage, uint32_t fmt, uint32_t pool);
Surface *surf_create(uint32_t w, uint32_t h, uint32_t fmt, int depth);
uint32_t surf_wrap(Surface *s);
Buffer *buf_create(uint32_t size, uint32_t usage, uint32_t fmt, uint32_t pool, int is_index);
extern unsigned g_frame;  // frames presented by the game (game side); g_pframe (prep.h) = consumer side
void com_free_guest(uint32_t g);
void d3d_log_once(const char *what);
void d3d_addref(void *o);
void d3d_release(ComObj *o);

// Diagnostics: logs any span longer than 100 ms (GPU stall, readback, texture upload...).
typedef struct { uint64_t t0; const char *what; uint32_t a, b; } SlowGuard;
static inline void slow_end(SlowGuard *g) {
    unsigned ms = (unsigned)((SDL_GetPerformanceCounter() - g->t0) * 1000 / SDL_GetPerformanceFrequency());
    if (ms > 100) rt_log("[slow] %s %u ms (%u, %u)\n", g->what, ms, g->a, g->b);
}
#define SLOW(what, a, b) SlowGuard _sg __attribute__((cleanup(slow_end))) = {SDL_GetPerformanceCounter(), what, (uint32_t)(a), (uint32_t)(b)}
