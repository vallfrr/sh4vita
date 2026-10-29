// Cutscenes: the game builds a DirectShow graph (source -> MPEG-1 splitter -> codecs ->
// renderer). The DirectShow base classes and the render-to-texture code are game code, already
// recompiled; the host provides everything upstream: graph, filters, pins and decoder
// (pl_mpeg). Frames are delivered to the real guest renderer filter through the standard protocol
// (ReceiveConnection / GetAllocator / GetBuffer / Receive), audio goes to the host mixer.
#include "d3d8/d3d8.h"
#include "win32/win32.h"
#include <SDL.h>
#include <stdlib.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/sysmem.h>
#endif

#include <stdio.h>
// [movie] time spent in movie file reads (decode thread stats)
static double g_st_io;
static int g_movie_all_frames;
static unsigned s_movie_serial;
#define PLM_VIDEO_SKIP_B_ALLOWED() (!g_movie_all_frames)
static size_t movie_fread(void *p, size_t s, size_t n, FILE *f) {
    uint64_t t = SDL_GetPerformanceCounter();
    size_t r = fread(p, s, n, f);
    g_st_io += (double)(SDL_GetPerformanceCounter() - t) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    return r;
}
#define fread movie_fread
#define PL_MPEG_IMPLEMENTATION
#include "third_party/pl_mpeg/pl_mpeg.h"
#undef fread
#include "movie_stream_cache.h"

#define M(p, name) static void p##_##name(CPU *c)
enum { MK_GRAPH = 100, MK_CTL, MK_POS, MK_EV, MK_AUD, MK_SINK, MK_FILTER, MK_PIN, MK_ENUM, MK_ALLOC, MK_SAMP };
#define S_FALSE_ 1u
#define E_NOINTERFACE_ 0x80004002u
#define E_NOTIMPL_ 0x80004001u
#define VFW_E_TIMEOUT 0x80040227u
#define VFW_E_NOT_FOUND 0x80040216u

typedef struct Movie Movie;
typedef struct { ComObj com; Movie *m; int role; int dir; const char *id; uint32_t peer; } HObj;
typedef struct { ComObj com; Movie *m; uint32_t buf, size, len; int64_t t0, t1; int sync, busy, have_time; } Samp;
#define NSAMP 3
enum { F_SRC, F_SPLIT, F_ACODEC, F_VCODEC, F_DSR };

struct Movie {
    plm_t *plm;                                   // video only ("movie" thread, core 1)
    plm_demux_t *adm;                             // audio: second demuxer on the same file ("movie_audio" thread, core 2)
    plm_audio_t *aud;
    char path[600];
    uint32_t graph_g, ctl_g, pos_g, ev_g, aud_g, sink_g;
    HObj *graph, *filters[5], *pins[10];
    uint32_t renderer, rpin, mem_in, alloc;      // guest objects (alloc: host allocator)
    Samp *samp[NSAMP];
    uint32_t buf_size;
    SDL_Thread *thread, *athread, *othread;      // video decoding (core 1), audio and output (core 2)
    volatile int run, stop, ended, dec_done;
    // Output: decoding copies the YUV frame into yuv[otail % OQ]; the output thread converts and calls Receive.
    SDL_sem *osem;
#define OQ 5                                  // decoded frames waiting for the output thread (FIFO: a burst of decoded frames is not dropped)
    uint8_t *yuv[OQ];
    plm_frame_t of[OQ];
    unsigned ohead, otail;                        // frames read / written so far (slot = n % OQ)
    int obusy;
    int oq[OQ];                                 // queued slot IDs; never recycle the consumer's slot
    unsigned odropped;
    MovieStream *stream;
    MovieCursor cursor[2];
    int state;                                    // 0 stopped, 1 paused, 2 playing
    int w, h;
    SDL_mutex *lock;
    struct { uint32_t code, p1, p2; } ev[16];
    int nev;
    int vol;                                      // hundredths of a dB (0 = full, -10000 = mute)
    CPU *dec_cpu;
    double st_dec, st_conv, st_recv;              // [movie] stats per 2 s window (cumulative ms)
    int st_ndec, st_npush, st_nskip;
};
static Movie *g_mv;
int g_movie_w, g_movie_h;                         // size delivered to the game: identifies the movie texture (resource.c)

// ---------- audio: ring buffer consumed by the dsound.c mixer ----------
static float g_ring[48000 * 2 * 4];               // 4 s stereo
static volatile uint32_t g_ring_w, g_ring_r;
static volatile uint32_t g_movie_audio_played; // diagnostic clock, stereo frames actually consumed
static SDL_mutex *g_ring_lock;
static volatile int g_audio_gain_q8 = 256;

void movie_audio_push(const float *il, int frames, int rate) {
    if (!g_ring_lock) g_ring_lock = SDL_CreateMutex();
    SDL_LockMutex(g_ring_lock);
    const uint32_t cap = sizeof g_ring / sizeof g_ring[0];
    static double acc;
    double step = (double)rate / 48000.0, pos = acc;
    while (pos < frames) {
        uint32_t i = (uint32_t)pos;
        float t = (float)(pos - i);
        uint32_t j = i + 1 < (uint32_t)frames ? i + 1 : i;
        if (((g_ring_w + 2) % cap) == g_ring_r) break; // full: the surplus is dropped
        g_ring[g_ring_w] = il[2 * i] * (1 - t) + il[2 * j] * t;
        g_ring[(g_ring_w + 1) % cap] = il[2 * i + 1] * (1 - t) + il[2 * j + 1] * t;
        g_ring_w = (g_ring_w + 2) % cap;
        pos += step;
    }
    acc = pos - frames;
    SDL_UnlockMutex(g_ring_lock);
}
void movie_audio_flush(void) {
    if (!g_ring_lock) g_ring_lock = SDL_CreateMutex();
    SDL_LockMutex(g_ring_lock);
    g_ring_r = g_ring_w;
    g_movie_audio_played = 0;
    SDL_UnlockMutex(g_ring_lock);
}
// Called by the mixer (n stereo frames): adds into acc[2n].
void movie_audio_mix(float *acc, int n) {
    if (!g_ring_lock || g_ring_r == g_ring_w) return;
    const uint32_t cap = sizeof g_ring / sizeof g_ring[0];
    float g = g_audio_gain_q8 / 256.0f;
    SDL_LockMutex(g_ring_lock);
    uint32_t before = g_ring_r;
    for (int i = 0; i < n && g_ring_r != g_ring_w; i++) {
        acc[2 * i] += g_ring[g_ring_r] * g;
        acc[2 * i + 1] += g_ring[(g_ring_r + 1) % cap] * g;
        g_ring_r = (g_ring_r + 2) % cap;
    }
    g_movie_audio_played += ((g_ring_r + cap - before) % cap) / 2;
    SDL_UnlockMutex(g_ring_lock);
}

// ---------- calls into the guest code ----------
static uint32_t gcall(CPU *c, uint32_t obj, int slot, int nargs, ...) {
    HOSTFN;
    uint32_t args[10], fn = R32(R32(obj) + 4u * (uint32_t)slot);
    va_list ap;
    va_start(ap, nargs);
    for (int i = 0; i < nargs; i++) args[i] = va_arg(ap, uint32_t);
    va_end(ap);
    switch (nargs) {
    case 0: return rt_guest_call(c, fn, 1, obj);
    case 1: return rt_guest_call(c, fn, 2, obj, args[0]);
    case 2: return rt_guest_call(c, fn, 3, obj, args[0], args[1]);
    case 3: return rt_guest_call(c, fn, 4, obj, args[0], args[1], args[2]);
    default: return rt_guest_call(c, fn, 5, obj, args[0], args[1], args[2], args[3]);
    }
}
static void put_guid(CPU *c, uint32_t p, uint32_t d1, uint16_t d2, uint16_t d3, const uint8_t d4[8]) {
    HOSTFN;
    W32(p, d1); W16(p + 4, d2); W16(p + 6, d3); memcpy(mb + p + 8, d4, 8);
}

// ---------- host COM object factory ----------
static uint32_t mkobj(Movie *m, int kind, const gfn *vt, int n, const char *iface, HObj **out) {
    HObj *o = calloc(1, sizeof *o);
    o->m = m;
    uint32_t g = com_wrap(&o->com, kind, vt, n, iface);
    if (out) *out = o;
    return g;
}
#define SELF HObj *self = (HObj *)com_get(ARG(1)); (void)self
M(any, QI) { STDRET(3, E_NOINTERFACE_); }
M(any, AddRef) { HOSTFN; ComObj *o = com_get(ARG(1)); STDRET(1, o ? (uint32_t)++o->refs : 1); }
M(any, Release) { HOSTFN; ComObj *o = com_get(ARG(1)); STDRET(1, o && o->refs > 1 ? (uint32_t)--o->refs : 1); }
static void rpush(CPU *c, uint32_t code, uint32_t p1, uint32_t p2) {
    (void)c;
    Movie *m = g_mv;
    if (!m) return;
    SDL_LockMutex(m->lock);
    if (m->nev < 16) { m->ev[m->nev].code = code; m->ev[m->nev].p1 = p1; m->ev[m->nev].p2 = p2; m->nev++; }
    SDL_UnlockMutex(m->lock);
}

// ---------- host pins (IPin) and enumerator ----------
M(pin, QueryDirection) { HOSTFN; SELF; W32(ARG(2), (uint32_t)self->dir); STDRET(2, HR_OK); }
M(pin, QueryId) {
    HOSTFN; SELF;
    W32(ARG(2), 0);
    size_t n = strlen(self->id) + 1;
    uint32_t w = gheap_alloc((uint32_t)n * 2);
    if (!w) STDRET(2, 0x8007000eu); // E_OUTOFMEMORY
    for (size_t i = 0; i < n; i++) W16(w + 2 * i, (uint8_t)self->id[i]);
    W32(ARG(2), w);
    STDRET(2, HR_OK);
}
M(pin, QueryPinInfo) {
    HOSTFN; SELF;
    uint32_t p = ARG(2);
    memset(mb + p, 0, 8 + 128);
    W32(p, 0); W32(p + 4, (uint32_t)self->dir);
    for (size_t i = 0; self->id[i] && i < 60; i++) W16(p + 8 + 2 * i, (uint8_t)self->id[i]);
    STDRET(2, HR_OK);
}
M(pin, ConnectedTo) { HOSTFN; SELF; W32(ARG(2), self->peer); STDRET(2, self->peer ? HR_OK : 0x80040209u); }
M(pin, Disconnect) { HOSTFN; SELF; self->peer = 0; STDRET(1, HR_OK); }
M(pin, Ok1) { STDRET(2, HR_OK); }
M(pin, Ok2) { STDRET(3, HR_OK); }
M(pin, NotImpl2) { STDRET(2, E_NOTIMPL_); }
M(pin, NotImpl3) { STDRET(3, E_NOTIMPL_); }
M(pin, Ok0) { STDRET(1, HR_OK); }
M(pin, NewSeg) { STDRET(6, HR_OK); }
static const gfn pin_vt[] = {any_QI, any_AddRef, any_Release, pin_Ok2 /*Connect*/, pin_Ok2 /*ReceiveConnection*/,
    pin_Disconnect, pin_ConnectedTo, pin_NotImpl2 /*ConnectionMediaType*/, pin_QueryPinInfo, pin_QueryDirection,
    pin_QueryId, pin_Ok1 /*QueryAccept*/, pin_NotImpl2 /*EnumMediaTypes*/, pin_NotImpl3 /*QueryInternalConnections*/,
    pin_Ok0 /*EndOfStream*/, pin_Ok0, pin_Ok0, pin_NewSeg};

typedef struct { uint32_t pins[4]; int n, pos; } EnumState;
M(en, Next) {
    HOSTFN;
    HObj *self = (HObj *)com_get(ARG(1));
    EnumState *e = (EnumState *)(uintptr_t)self->peer;
    uint32_t want = ARG(2), out = ARG(3), got = 0;
    while (got < want && e->pos < e->n) {
        uint32_t p = e->pins[e->pos++];
        d3d_addref(com_get(p));
        W32(out + 4 * got, p);
        got++;
    }
    if (ARG(4)) W32(ARG(4), got);
    STDRET(4, got == want ? HR_OK : S_FALSE_);
}
M(en, Skip) { STDRET(2, HR_OK); }
M(en, Reset) { HOSTFN; HObj *self = (HObj *)com_get(ARG(1)); ((EnumState *)(uintptr_t)self->peer)->pos = 0; STDRET(1, HR_OK); }
M(en, Clone) { STDRET(2, E_NOTIMPL_); }
static const gfn en_vt[] = {any_QI, any_AddRef, any_Release, en_Next, en_Skip, en_Reset, en_Clone};

// ---------- host filters (IBaseFilter) ----------
M(flt, EnumPins) {
    HOSTFN; SELF;
    EnumState *e = calloc(1, sizeof *e);
    for (int i = 0; i < 10; i++)
        if (self->m->pins[i] && self->m->pins[i]->com.kind == MK_PIN && self->m->pins[i]->role == self->role)
            e->pins[e->n++] = self->m->pins[i]->com.guest;
    HObj *eo;
    uint32_t g = mkobj(self->m, MK_ENUM, en_vt, 7, "IEnumPins", &eo);
    eo->peer = (uint32_t)(uintptr_t)e;
    W32(ARG(2), g);
    STDRET(2, HR_OK);
}
M(flt, Ok1) { STDRET(1, HR_OK); }
M(flt, Ok2) { STDRET(2, HR_OK); }
M(flt, Ok3) { STDRET(3, HR_OK); }
M(flt, GetState) { HOSTFN; W32(ARG(3), 0); STDRET(3, HR_OK); }
M(flt, FindPin) { HOSTFN; W32(ARG(3), 0); STDRET(3, 0x80040209u); }
M(flt, NI) { STDRET(2, E_NOTIMPL_); }
M(flt, QueryFilterInfo) { HOSTFN; memset(mb + ARG(2), 0, 4 + 128 + 4); STDRET(2, HR_OK); }
M(flt, Join) { STDRET(3, HR_OK); }
static const gfn flt_vt[] = {any_QI, any_AddRef, any_Release, flt_NI /*GetClassID*/, flt_Ok1 /*Stop*/, flt_Ok1 /*Pause*/,
    flt_Ok3 /*Run (i64)*/, flt_GetState, flt_Ok2 /*SetSyncSource*/, flt_Ok2 /*GetSyncSource*/, flt_EnumPins,
    flt_FindPin, flt_QueryFilterInfo, flt_Join, flt_NI /*QueryVendorInfo*/};

static void make_filter(Movie *m, int which, int nin, int nout, const char *in_id, const char *out_ids[2]) {
    uint32_t g = mkobj(m, MK_FILTER, flt_vt, 15, "IBaseFilter", &m->filters[which]);
    m->filters[which]->role = which;
    (void)g;
    for (int i = 0; i < nin + nout; i++) {
        HObj *p;
        mkobj(m, MK_PIN, pin_vt, 18, "IPin", &p);
        p->role = which;
        p->dir = i < nin ? 0 : 1;
        p->id = i < nin ? in_id : out_ids[i - nin];
        for (int k = 0; k < 10; k++)
            if (!m->pins[k]) { m->pins[k] = p; break; }
    }
}


// ---------- memory allocator and samples (host side) ----------
#define MV Movie *m = ((HObj *)com_get(ARG(1)))->m
#define SAMP Samp *sp = (Samp *)com_get(ARG(1))
M(sm, AddRef) { HOSTFN; SAMP; STDRET(1, (uint32_t)++sp->com.refs); }
M(sm, Release) { HOSTFN; SAMP; int r = --sp->com.refs; if (r <= 0) { sp->com.refs = 0; sp->busy = 0; r = 0; } STDRET(1, (uint32_t)r); }
M(sm, GetPointer) { HOSTFN; SAMP; W32(ARG(2), sp->buf); STDRET(2, HR_OK); }
M(sm, GetSize) { HOSTFN; SAMP; STDRET(1, sp->size); }
M(sm, GetTime) {
    HOSTFN; SAMP;
    if (!sp->have_time) STDRET(3, 0x80040249u);
    W32(ARG(2), (uint32_t)sp->t0); W32(ARG(2) + 4, (uint32_t)((uint64_t)sp->t0 >> 32));
    W32(ARG(3), (uint32_t)sp->t1); W32(ARG(3) + 4, (uint32_t)((uint64_t)sp->t1 >> 32));
    STDRET(3, HR_OK);
}
M(sm, SetTime) {
    HOSTFN; SAMP;
    sp->t0 = (int64_t)(((uint64_t)R32(ARG(2) + 4) << 32) | R32(ARG(2)));
    sp->t1 = (int64_t)(((uint64_t)R32(ARG(3) + 4) << 32) | R32(ARG(3)));
    sp->have_time = 1;
    STDRET(3, HR_OK);
}
M(sm, IsSync) { HOSTFN; SAMP; STDRET(1, sp->sync ? HR_OK : S_FALSE_); }
M(sm, SetSync) { HOSTFN; SAMP; sp->sync = (int)ARG(2); STDRET(2, HR_OK); }
M(sm, False1) { STDRET(1, S_FALSE_); }
M(sm, Ok2) { STDRET(2, HR_OK); }
M(sm, GetLen) { HOSTFN; SAMP; STDRET(1, sp->len); }
M(sm, SetLen) { HOSTFN; SAMP; sp->len = ARG(2); STDRET(2, HR_OK); }
M(sm, GetMT) { HOSTFN; W32(ARG(2), 0); STDRET(2, S_FALSE_); }
M(sm, GetMediaTime) { STDRET(3, 0x80040251u); }
M(sm, Ok3) { STDRET(3, HR_OK); }
static const gfn sm_vt[] = {any_QI, sm_AddRef, sm_Release, sm_GetPointer, sm_GetSize, sm_GetTime, sm_SetTime,
    sm_IsSync, sm_SetSync, sm_False1 /*IsPreroll*/, sm_Ok2, sm_GetLen, sm_SetLen, sm_GetMT, sm_Ok2 /*SetMediaType*/,
    sm_False1 /*IsDiscontinuity*/, sm_Ok2, sm_GetMediaTime, sm_Ok3};

#include "movie_sample_reserve.h"
M(al, SetProperties) {
    HOSTFN; MV;
    uint32_t req = ARG(2), act = ARG(3);
    uint32_t sz = R32(req + 4);
    static uint32_t sbuf[NSAMP], scap[NSAMP];
    Samp *new_samples[NSAMP] = {0};
    for (int i = 0; i < NSAMP; ++i)
        if (m->samp[i] && m->samp[i]->busy) STDRET(3, HR_FAIL);
    for (int i = 0; i < NSAMP; ++i) if (!m->samp[i]) {
        new_samples[i] = calloc(1, sizeof(Samp));
        if (!new_samples[i]) {
            for (int j = 0; j < i; ++j) free(new_samples[j]);
            STDRET(3, 0x8007000eu);
        }
    }
    if (!movie_samples_reserve(sbuf, scap, NSAMP, sz)) {
        for (int i = 0; i < NSAMP; ++i) free(new_samples[i]);
        STDRET(3, 0x8007000eu);
    }
    m->buf_size = sz;
    for (int i = 0; i < NSAMP; i++) {
        if (!m->samp[i]) {
            m->samp[i] = new_samples[i];
            m->samp[i]->m = m;
            com_wrap(&m->samp[i]->com, MK_SAMP, sm_vt, 19, "IMediaSample");
        }
        m->samp[i]->buf = sbuf[i];
        m->samp[i]->size = sz;
    }
    if (act) { W32(act, NSAMP); W32(act + 4, sz); W32(act + 8, R32(req + 8)); W32(act + 12, 0); }
    STDRET(3, HR_OK);
}
M(al, GetProperties) { HOSTFN; MV; W32(ARG(2), NSAMP); W32(ARG(2) + 4, m->buf_size); W32(ARG(2) + 8, 1); W32(ARG(2) + 12, 0); STDRET(2, HR_OK); }
M(al, Ok1) { STDRET(1, HR_OK); }
M(al, GetBuffer) {
    HOSTFN; MV;
    for (int tries = 0; tries < 500; tries++) {
        for (int i = 0; i < NSAMP; i++)
            if (m->samp[i] && !m->samp[i]->busy) {
                Samp *sp = m->samp[i];
                sp->busy = 1; sp->com.refs = 1; sp->have_time = 0; sp->len = 0; sp->sync = 0;
                W32(ARG(2), sp->com.guest);
                STDRET(5, HR_OK);
            }
        SDL_Delay(2);
    }
    W32(ARG(2), 0);
    STDRET(5, HR_FAIL);
}
M(al, ReleaseBuffer) { HOSTFN; Samp *sp = (Samp *)com_get(ARG(2)); if (sp) sp->busy = 0; STDRET(2, HR_OK); }
static const gfn al_vt[] = {any_QI, any_AddRef, any_Release, al_SetProperties, al_GetProperties, al_Ok1 /*Commit*/,
    al_Ok1 /*Decommit*/, al_GetBuffer, al_ReleaseBuffer};

// ---------- decoding and delivery ----------
static uint32_t guest_tmp(void) {
    static uint32_t t;
    if (!t) t = gheap_calloc(256);
    return t;
}

#ifdef __ARM_NEON
#include <arm_neon.h>
// YUV 4:2:0 -> BGRA (or R5G6B5 if p565) in NEON (16 pixels x 2 rows per iteration), BT.601 in 6-bit fixed point (differs by a few
// units from plm_frame_to_bgra, cf. SH4_MOVIE_BENCH). Alpha = 255.
static inline __attribute__((always_inline)) void frame_conv(plm_frame_t *f, uint8_t *dst, int stride, const int p565) {
    int w = (int)f->width, h = (int)f->height, yw = (int)f->y.width, cw = (int)f->cb.width;
    const int16x8_t c128 = vdupq_n_s16(128), yoff = vdupq_n_s16(1192);
    const uint8x8_t k149 = vdup_n_u8(149);
    for (int j = 0; j < h; j += 2) {
        const uint8_t *cb = f->cb.data + (j >> 1) * cw, *cr = f->cr.data + (j >> 1) * cw;
        for (int i = 0; i < w; i += 16) {
            int16x8_t u = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(cb + i / 2))), c128);
            int16x8_t v = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(cr + i / 2))), c128);
            int16x8x2_t rr = vzipq_s16(vmulq_n_s16(v, 102), vmulq_n_s16(v, 102));
            int16x8_t g = vmlaq_n_s16(vmulq_n_s16(u, -25), v, -52), b = vmulq_n_s16(u, 129);
            int16x8x2_t gg = vzipq_s16(g, g), bb = vzipq_s16(b, b);
            for (int k = 0; k < 2; k++) {
                uint8x16_t yv = vld1q_u8(f->y.data + (j + k) * yw + i);
                uint8_t *d = dst + (j + k) * stride + i * (p565 ? 2 : 4);
                for (int hf = 0; hf < 2; hf++) {
                    uint8x8_t y8 = hf ? vget_high_u8(yv) : vget_low_u8(yv);
                    int16x8_t yy = vsubq_s16(vreinterpretq_s16_u16(vshrq_n_u16(vmull_u8(y8, k149), 1)), yoff);   // (Y-16)*74.5
                    uint8x8x4_t px;
                    px.val[0] = vqrshrun_n_s16(vqaddq_s16(yy, bb.val[hf]), 6);
                    px.val[1] = vqrshrun_n_s16(vqaddq_s16(yy, gg.val[hf]), 6);
                    px.val[2] = vqrshrun_n_s16(vqaddq_s16(yy, rr.val[hf]), 6);
                    if (p565) {   // R<<8, then insert G>>5 and B>>11 -> R5G6B5
                        uint16x8_t q = vsriq_n_u16(vshll_n_u8(px.val[2], 8), vshll_n_u8(px.val[1], 8), 5);
                        vst1q_u16((uint16_t *)(d + hf * 16), vsriq_n_u16(q, vshll_n_u8(px.val[0], 8), 11));
                    } else {
                        px.val[3] = vdup_n_u8(255);
                        vst4_u8(d + hf * 32, px);
                    }
                }
            }
        }
    }
}
static void frame_to_bgra(plm_frame_t *f, uint8_t *dst, int stride) {
    if (((int)f->width | (int)f->height) & 15) plm_frame_to_bgra(f, dst, stride);
    else frame_conv(f, dst, stride, 0);
}

// Movie (Vita): the frame is also produced as R5G6B5 (top-down) in one of 3 rotating buffers, converted here (core 2); the main thread
// only has to hand it to the render thread (resource.c, tex_upload). busy: 1 = being written or waiting for upload (reset to 0 by the renderer).
#define N565 6
static uint16_t *s_m565[N565];
static volatile int s_m565_busy[N565];
static int s_m565_q[N565];                    // FIFO of complete buffers in display order (q[n % N565]); a buffer is free when not busy and not queued
static unsigned s_m565_qh, s_m565_qt, s_m565_w, s_m565_h;
static unsigned s_m565_dropped;               // converted frames dropped because the display was too far behind
static SDL_mutex *s_m565_mx;
static double s_m565_ms;
static unsigned s_m565_n;
static double s_m565_pts[N565];
static uint64_t s_m565_ready[N565];
static unsigned s_m565_serial;
static void m565_begin(void) {
    if (!s_m565_mx) return;
    SDL_LockMutex(s_m565_mx);
    s_m565_qh = s_m565_qt; // discard only queued frames, never buffers owned by the renderer
    s_m565_dropped = 0;
    SDL_UnlockMutex(s_m565_mx);
}
static int m565_queued(int k) { for (unsigned n = s_m565_qh; n != s_m565_qt; n++) if (s_m565_q[n % N565] == k) return 1; return 0; }
static void m565_put(Movie *m, plm_frame_t *f) {
    if (((int)f->width | (int)f->height) & 15) return;   // ponytail: sizes not a multiple of 16: fall back to the main thread conversion
    if (!s_m565_mx) s_m565_mx = SDL_CreateMutex();
    if (!s_m565_w) { s_m565_w = (unsigned)m->w; s_m565_h = (unsigned)m->h; }
    if (s_m565_w != (unsigned)m->w || s_m565_h != (unsigned)m->h) return;   // ponytail: buffers sized for the first movie (all 640x480)
    int k = 0;
    SDL_LockMutex(s_m565_mx);
    while (k < N565 && (s_m565_busy[k] || m565_queued(k))) k++;
    if (k < N565) s_m565_busy[k] = 1;
    else s_m565_dropped++;
    SDL_UnlockMutex(s_m565_mx);
    if (k == N565) return;   // the renderer did not return the buffers: frame skipped
    if (!s_m565[k] && !(s_m565[k] = malloc((size_t)m->w * m->h * 2))) { s_m565_busy[k] = 0; return; }
    uint64_t t0 = SDL_GetPerformanceCounter();
    s_m565_pts[k] = f->time;
    frame_conv(f, (uint8_t *)s_m565[k], m->w * 2, 1);
    s_m565_ms += (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    SDL_LockMutex(s_m565_mx);
    s_m565_ready[k] = SDL_GetPerformanceCounter();
    s_m565_q[s_m565_qt % N565] = k; s_m565_qt++;
    s_m565_busy[k] = 0;
    SDL_UnlockMutex(s_m565_mx);
    if (!(++s_m565_n % 75)) { rt_log("[movie] 565 conversion (movie thread): %.1f ms\n", s_m565_ms / 75); s_m565_ms = 0; }
}
// Main thread: next 565 frame in display order not yet uploaded (NULL: nothing new). One frame per call, so every decoded frame is shown;
// if more than 3 are waiting the oldest are dropped (latency stays under ~100 ms). The buffer stays reserved until *busy drops to 0.
uint16_t *movie_565_take(uint32_t w, uint32_t h, volatile int **busy) {
    uint16_t *p = NULL;
    if (!s_m565_mx) return NULL;
    SDL_LockMutex(s_m565_mx);
    if (s_m565_qh != s_m565_qt && w == s_m565_w && h == s_m565_h) {
        while (s_m565_qt - s_m565_qh > 3) { s_m565_qh++; s_m565_dropped++; }
        int k = s_m565_q[s_m565_qh % N565]; s_m565_qh++;
        s_m565_busy[k] = 1;
        {   // diagnostics: order and regularity of what is shown (windows of 120 presents)
            extern unsigned g_pframe;
            static double last_pts = -1; static unsigned win0, ntake, nback, maxgap, lastf;
            if (s_m565_serial != s_movie_serial) {
                last_pts = -1; win0 = g_pframe; ntake = nback = maxgap = lastf = 0;
            }
            double pts = s_m565_pts[k];
            if (pts < last_pts - 1e-6) nback++;
            last_pts = pts; ntake++;
            if (lastf && g_pframe - lastf > maxgap) maxgap = g_pframe - lastf;
            lastf = g_pframe;
            if (g_pframe - win0 >= 120) { rt_log("[movie] display: %u new frames shown in %u presents, %u dropped, %u out of order, longest gap %u presents\n", ntake, g_pframe - win0, s_m565_dropped, nback, maxgap); win0 = g_pframe; ntake = nback = maxgap = 0; s_m565_dropped = 0; }
        }
        *busy = &s_m565_busy[k];
        p = s_m565[k];
        // Low-rate wall-clock diagnostics; Present counts alone hide long gaps.
        static uint64_t begin, previous, max_gap;
        static unsigned shown, gaps50, gaps100;
        static double max_queue_ms;
        uint64_t now = SDL_GetPerformanceCounter(), hz = SDL_GetPerformanceFrequency();
        int first = s_m565_serial != s_movie_serial;
        if (first) {
            s_m565_serial = s_movie_serial;
            begin = previous = now; max_gap = 0; shown = gaps50 = gaps100 = 0; max_queue_ms = 0;
        }
        uint64_t gap = now - previous; previous = now;
        if (gap > max_gap) max_gap = gap;
        if (gap * 1000 > hz * 50) gaps50++;
        if (gap * 1000 > hz * 100) gaps100++;
        double age = (double)(now - s_m565_ready[k]) * 1000.0 / (double)hz;
        if (age > max_queue_ms) max_queue_ms = age;
        if (!first) shown++;
        if (now - begin >= 2 * hz) {
            rt_log("[movie-flow] shown=%.2f/s max_gap_ms=%.2f gaps50=%u gaps100=%u queue_max_ms=%.2f pts=%.3f audio_minus_video_ms=%.2f\n",
                   shown * (double)hz / (double)(now - begin), max_gap * 1000.0 / (double)hz,
                   gaps50, gaps100, max_queue_ms, s_m565_pts[k],
                   g_movie_audio_played / 48.0 - s_m565_pts[k] * 1000.0);
            begin = now; shown = gaps50 = gaps100 = 0; max_gap = 0; max_queue_ms = 0;
        }
    }
    SDL_UnlockMutex(s_m565_mx);
    return p;
}
#else
#define frame_to_bgra plm_frame_to_bgra
#define m565_put(m, f) ((void)0)
#define m565_begin() ((void)0)
#endif

static void push_frame(Movie *m, CPU *c, plm_frame_t *f) {
    HOSTFN;
    Samp *sp = NULL;
    for (int i = 0; i < NSAMP && !sp; i++)
        if (m->samp[i] && !m->samp[i]->busy) sp = m->samp[i];
    if (!sp || !m->mem_in) {
        static int nlog; if (nlog++ < 5) rt_log("[movie] push skipped: sp=%p mem_in=%08x busy=%d%d%d\n", (void *)sp, m->mem_in,
            m->samp[0] ? m->samp[0]->busy : -1, m->samp[1] ? m->samp[1]->busy : -1, m->samp[2] ? m->samp[2]->busy : -1);
        return;                                                 // the renderer did not return its samples: we skip the frame
    }
    sp->busy = 1; sp->com.refs = 1;
    uint32_t pitch = (uint32_t)m->w * 4, size = pitch * (uint32_t)m->h;
    uint64_t t0 = SDL_GetPerformanceCounter();
    frame_to_bgra(f, mb + sp->buf + (size - pitch), -(int)pitch);   // DIB bas-haut
    m565_put(m, f);
    uint64_t t1 = SDL_GetPerformanceCounter();
    sp->len = size;
    sp->t0 = (int64_t)(f->time * 1e7);
    sp->t1 = sp->t0 + 333667;
    sp->have_time = 1;
    sp->sync = 1;
    uint32_t rhr = gcall(c, m->mem_in, 6, 1, sp->com.guest);    // IMemInputPin::Receive
    uint64_t t2 = SDL_GetPerformanceCounter();
    double ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
    m->st_conv += (double)(t1 - t0) * ms; m->st_recv += (double)(t2 - t1) * ms; m->st_npush++;
    if ((int32_t)rhr < 0) rt_log("[movie] Receive -> %08x\n", rhr);
    sp->com.refs = 0;
    sp->busy = 0;
}

static plm_frame_t *g_pending;
// Video decode time: gap between two plm_decode callbacks (g_cb_t reset at the start of each plm_decode).
// Audio: measured in its own thread.
static uint64_t g_cb_t;
static double g_st_vid, g_st_aud;
static double cb_ms(void) { uint64_t n = SDL_GetPerformanceCounter(); double d = (double)(n - g_cb_t) * 1000.0 / (double)SDL_GetPerformanceFrequency(); g_cb_t = n; return d; }
static int g_handed;
static void hand_frame(Movie *m, plm_frame_t *f);
static void on_video(plm_t *p, plm_frame_t *f, void *u) {
    g_st_vid += cb_ms();
    Movie *m = u;
    if (p->video_decoder->skipped) { m->st_nskip++; return; }   // B frame skipped (late)
    m->st_ndec++;
    if (m->yuv[0]) { hand_frame(m, f); g_handed = 1; }   // every decoded frame goes to the output FIFO, in display order
    else g_pending = f;
}

// SH4_MOVIE_BENCH=s: at startup, decodes s seconds of m_00 alone (nothing else running): raw cost of pl_mpeg.
void movie_bench(const char *dir, int secs) {
    char p[300];
    SDL_snprintf(p, sizeof p, "%s/movie/m_00.pac", dir);
    plm_t *plm = plm_create_with_filename(p);
    uint8_t *rgb = malloc(640 * 480 * 4);
    if (!plm || !rgb) { rt_log("[bench] %s: cannot open\n", p); return; }
    double ms = 1000.0 / (double)SDL_GetPerformanceFrequency(), dec = 0, conv = 0, aud = 0;
    plm_set_audio_enabled(plm, 0);
    int n = 0, na = 0;
    uint32_t hash = 2166136261u;
    for (; n < secs * 30; n++) {
        uint64_t t0 = SDL_GetPerformanceCounter();
        plm_frame_t *f = plm_decode_video(plm);
        uint64_t t1 = SDL_GetPerformanceCounter();
        if (!f) break;
        frame_to_bgra(f, rgb, (int)f->width * 4);
        dec += (double)(t1 - t0) * ms; conv += (double)(SDL_GetPerformanceCounter() - t1) * ms;
        // FNV-1a hash of the YUV planes: must equal that of test/plm_vbench.c (plain C) for the same number of frames
        plm_plane_t *pl[3] = { &f->y, &f->cb, &f->cr };
        for (int k = 0; k < 3; k++)
            for (size_t i = 0, e = (size_t)pl[k]->width * pl[k]->height; i < e; i++) hash = (hash ^ pl[k]->data[i]) * 16777619u;
        if (n % 30 == 0) rt_log("[bench] frame %d: hash %08x\n", n, (unsigned)hash);
        if (n == 90) {   // check: max difference against the reference conversion
            uint8_t *ref = malloc(640 * 480 * 4);
            int dmax = 0;
            if (ref) {
                plm_frame_to_bgra(f, ref, (int)f->width * 4);
                for (int i = 0; i < 640 * 480 * 4; i++) { int d = abs(ref[i] - rgb[i]); if ((i & 3) != 3 && d > dmax) dmax = d; }
                free(ref);
            }
            rt_log("[bench] max conversion error = %d\n", dmax);
        }
    }
    plm_destroy(plm);
    plm = plm_create_with_filename(p);
    if (plm) {
        plm_set_video_enabled(plm, 0);
        uint64_t t0 = SDL_GetPerformanceCounter();
        plm_samples_t *a;
        for (; (a = plm_decode_audio(plm)) && a->time < secs; na++) {}
        aud = (double)(SDL_GetPerformanceCounter() - t0) * ms;
    }
    rt_log("[bench] %d frames: decode %.1f ms/frame, BGRA conversion %.1f ms/frame; audio: %.0f ms for %d frames; hash %08x\n",
           n, n ? dec / n : 0, n ? conv / n : 0, aud, na, (unsigned)hash);
    free(rgb);
    if (plm) plm_destroy(plm);
}

// SH4_MOVIE_BENCH=s[,wait,core,core...]: with a wait, the bench runs during the game in a pinned thread
// successively on each given core (0-2): measures the contention suffered by movie decoding.
static char g_bench_dir[300];
static int g_bench_arg[8], g_bench_n;
static int bench_thread(void *u) {
    (void)u;
    SDL_Delay((uint32_t)g_bench_arg[1] * 1000);
    for (int i = 2; i < g_bench_n; i++) {
#ifdef __vita__
        sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0 << g_bench_arg[i]);
#endif
        rt_log("[bench] core %d\n", g_bench_arg[i]);
        movie_bench(g_bench_dir, g_bench_arg[0]);
    }
    return 0;
}
void movie_bench_start(const char *dir, const char *spec) {
    SDL_strlcpy(g_bench_dir, dir, sizeof g_bench_dir);
    for (g_bench_n = 0; g_bench_n < 8 && *spec; g_bench_n++) {
        g_bench_arg[g_bench_n] = atoi(spec);
        spec += strcspn(spec, ",");
        if (*spec) spec++;
    }
    if (g_bench_n < 3) movie_bench(dir, g_bench_arg[0]);
    else SDL_CreateThread(bench_thread, "bench", NULL);
}
// newlib fills a FILE in BUFSIZ blocks (1 KB): unbuffered stream = one read per pl_mpeg refill (<= 128 KB).
static plm_buffer_t *movie_file(const char *host) {
    FILE *f = fopen(host, "rb");
    if (!f) return NULL;
    setvbuf(f, NULL, _IONBF, 0);
    return plm_buffer_create_with_file(f, 1);
}
static void movie_cached_load(plm_buffer_t *buffer, void *user) {
    if (buffer->discard_read_bytes) plm_buffer_discard_read_bytes(buffer);
    size_t n = movie_stream_read(user, buffer->bytes + buffer->length, buffer->capacity - buffer->length);
    buffer->length += n;
    if (!n) buffer->has_ended = TRUE;
}
static void movie_cached_seek(plm_buffer_t *buffer, size_t pos, void *user) {
    (void)buffer; ((MovieCursor *)user)->pos = pos;
}
static size_t movie_cached_tell(plm_buffer_t *buffer, void *user) {
    (void)buffer; return ((MovieCursor *)user)->pos;
}
static plm_buffer_t *movie_input(Movie *m, const char *host, int index) {
    if (!m->stream) return movie_file(host);
    m->cursor[index].source = m->stream; m->cursor[index].pos = 0;
    return plm_buffer_create_with_callbacks(movie_cached_load, movie_cached_seek,
                                          movie_cached_tell, m->stream->size, &m->cursor[index]);
}
// Audio buffer refill: demuxes up to the next audio packet (video packets are skipped).
static void apkt_load(plm_buffer_t *ab, void *u) {
    plm_packet_t *p;
    while ((p = plm_demux_decode(u)))
        if (p->type == PLM_DEMUX_PACKET_AUDIO_1) { plm_buffer_write(ab, p->data, p->length); return; }
    plm_buffer_signal_end(ab);
}
// Movie audio on core 2, ~0.25 s ahead of the mixer: video decoding keeps core 1 to itself.
static int audio_thread(void *arg) {
    Movie *m = arg;
#ifdef __vita__
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_2);
#endif
    const uint32_t cap = sizeof g_ring / sizeof g_ring[0];
    double ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
    while (!m->stop && !m->ended) {
        plm_samples_t *s = NULL;
        if (m->state == 2 && (g_ring_w - g_ring_r + cap) % cap < 48000 * 2 / 4) {
            uint64_t t = SDL_GetPerformanceCounter();
            s = plm_audio_decode(m->aud);
            g_st_aud += (double)(SDL_GetPerformanceCounter() - t) * ms;
            if (s) movie_audio_push(s->interleaved, (int)s->count, plm_audio_get_samplerate(m->aud));
        }
        if (!s) SDL_Delay(5);
    }
    return 0;
}
// Caller must join every decoder/output/audio thread first. Queued graphics
// uploads own their separate RGB565 staging buffers, not these YUV slots.
static void movie_free_output(Movie *m) {
    for (int i = 0; i < OQ; i++) { free(m->yuv[i]); m->yuv[i] = NULL; }
    rt_free_cpu(m->dec_cpu);
    m->dec_cpu = NULL;
    m->ohead = m->otail = 0;
    m->obusy = -1;
}
static void movie_free_decoders(Movie *m) {
    if (m->plm) plm_destroy(m->plm);
    if (m->aud) plm_audio_destroy(m->aud);
    if (m->adm) plm_demux_destroy(m->adm);
    m->plm = NULL; m->aud = NULL; m->adm = NULL;
    movie_stream_close(m->stream); m->stream = NULL;
    movie_free_output(m);
}
// Decode -> output: YUV copy (~0.5 MB) into the next FIFO slot; if the output thread is too far behind, the oldest waiting frame is dropped.
static void hand_frame(Movie *m, plm_frame_t *f) {
    SDL_LockMutex(m->lock);
    while (m->otail - m->ohead >= OQ - 2) { m->ohead++; m->odropped++; }
    if (m->ohead == m->otail) m->ohead = m->otail = 0;
    int w;
    for (w = 0; w < OQ; w++) {
        if (w == m->obusy) continue;
        unsigned n = m->ohead;
        while (n != m->otail && m->oq[n % OQ] != w) n++;
        if (n == m->otail) break;
    }
    if (w == OQ) { m->odropped++; SDL_UnlockMutex(m->lock); return; }
    SDL_UnlockMutex(m->lock);
    size_t ys = (size_t)f->y.width * f->y.height, cs = (size_t)f->cb.width * f->cb.height;
    plm_frame_t *o = &m->of[w];
    *o = *f;
    o->y.data = m->yuv[w]; o->cb.data = o->y.data + ys; o->cr.data = o->cb.data + cs;
    memcpy(o->y.data, f->y.data, ys); memcpy(o->cb.data, f->cb.data, cs); memcpy(o->cr.data, f->cr.data, cs);
    SDL_LockMutex(m->lock);
    m->oq[m->otail % OQ] = w;
    m->otail++;
    SDL_UnlockMutex(m->lock);
    SDL_SemPost(m->osem);
}
// BGRA conversion + guest Receive on core 2 (core 1 only decodes), then EndOfStream at the end of the stream.
static int out_thread(void *arg) {
    Movie *m = arg;
    rt_log("[movie] output thread started\n");
#ifdef __vita__
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_2);
#endif
    while (!m->stop) {
        SDL_SemWaitTimeout(m->osem, 50);
        int done = m->dec_done;
        SDL_LockMutex(m->lock);
        int r = -1;
        if (m->ohead != m->otail) { r = m->oq[m->ohead % OQ]; m->ohead++; }
        m->obusy = r;
        SDL_UnlockMutex(m->lock);
        if (r >= 0) push_frame(m, m->dec_cpu, &m->of[r]);
        SDL_LockMutex(m->lock);
        m->obusy = -1;
        SDL_UnlockMutex(m->lock);
        if (r < 0 && done) {
            if (!m->stop && m->rpin) {
                rt_log("[movie] fin de flux\n");
                gcall(m->dec_cpu, m->rpin, 14, 0); // EndOfStream -> the renderer notifies EC_COMPLETE to the graph
            }
            m->ended = 1;
            break;
        }
    }
    return 0;
}
static int decode_thread(void *arg) {
    Movie *m = arg;
#ifdef __vita__
    const char *choice = getenv("SH4_MOVIE_CORE");
    int core = choice ? atoi(choice) : 0;
    if (core < 0 || core > 2) core = 0;
    int aff = sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0 << core);
    const char *low = getenv("SH4_MOVIE_LOW_PRIORITY");
    int priority = (!low || strcmp(low, "0")) ? SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW) : 0;
    rt_log("[movie] decode thread on core %d -> %08x, low priority -> %08x\n", core, aff, priority);
#endif
    uint64_t last = SDL_GetPerformanceCounter(), hz = SDL_GetPerformanceFrequency(), st0 = last;
    while (!m->stop && !plm_has_ended(m->plm)) {
        uint64_t now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / (double)hz;
        last = now;
        if (dt > 0.25) dt = 0.25;
        int got = 0;
        if (m->state == 2) {
            g_pending = NULL; g_handed = 0;
            uint64_t d0 = SDL_GetPerformanceCounter();
            g_cb_t = d0;
            plm_decode(m->plm, dt);
            m->st_dec += (double)(SDL_GetPerformanceCounter() - d0) * 1000.0 / (double)hz;
            got = g_handed;
            if (g_pending) { got = 1; push_frame(m, m->dec_cpu, g_pending); }   // no memory for the output buffers: the last frame only
        }
        if (now - st0 > 2 * hz) {
            double sec = (double)(now - st0) / (double)hz;
            rt_log("[movie] t=%.1f s: %.1f frames decoded/s (%.1f B skipped), %.1f delivered/s; per second: decode %.0f ms (video %.0f, audio %.0f, file read %.0f), conversion %.0f ms, Receive %.0f ms\n",
                   plm_get_time(m->plm), m->st_ndec / sec, m->st_nskip / sec, m->st_npush / sec, m->st_dec / sec, g_st_vid / sec, g_st_aud / sec, g_st_io / sec, m->st_conv / sec, m->st_recv / sec);
#ifdef __vita__
            // real CPU time of the thread (runClocks, us) versus elapsed time: tells preemption from raw slowness
            static uint64_t run0; static uint32_t pre0, ipre0;
            SceKernelThreadInfo ti = {.size = sizeof ti};
            if (!sceKernelGetThreadInfo(sceKernelGetThreadId(), &ti)) {
                rt_log("[movie] thread: %.0f ms CPU/s, preemptions %u (interrupts %u), core %d, priority %x\n", (double)(ti.runClocks - run0) / 1000.0 / sec,
                       ti.threadPreemptCount - pre0, ti.intrPreemptCount - ipre0, ti.currentCpuId, ti.currentPriority);
                run0 = ti.runClocks; pre0 = ti.threadPreemptCount; ipre0 = ti.intrPreemptCount;
            }
#endif
            m->st_dec = m->st_conv = m->st_recv = 0; g_st_vid = g_st_aud = g_st_io = 0; m->st_ndec = m->st_npush = m->st_nskip = 0; st0 = now;
        }
        if (!got) SDL_Delay(4);
    }
    m->dec_done = 1;
    SDL_SemPost(m->osem);
    return 0;
}

// ---------- IMediaControl / IMediaPosition / IMediaEvent / IBasicAudio / IMediaEventSink ----------
M(ctl, Run) {
    HOSTFN; MV;
    if (m->state != 2) {
        if (m->renderer) gcall(c, m->renderer, 6, 2, 0u, 0u); // Run(tStart=0)
        m->state = 2;
        if (!m->thread && !m->ended) {
            m->dec_cpu = rt_new_cpu(1u << 20);
            size_t fs = (size_t)((m->w + 15) & ~15) * ((m->h + 15) & ~15) * 3 / 2;
            int ok = 1;
            for (int i = 0; i < OQ; i++) { if (!m->yuv[i]) m->yuv[i] = malloc(fs); if (!m->yuv[i]) ok = 0; }
            if (!ok) { for (int i = 0; i < OQ; i++) { free(m->yuv[i]); m->yuv[i] = NULL; } rt_log("[movie] output buffers: not enough memory\n"); }
            if (!m->osem) m->osem = SDL_CreateSemaphore(0);
            m->ohead = m->otail = 0; m->obusy = -1; m->dec_done = 0;
            m->odropped = 0; s_movie_serial++; m565_begin();
            g_st_vid = g_st_aud = g_st_io = 0;
            m->thread = SDL_CreateThread(decode_thread, "movie", m);
            m->othread = SDL_CreateThread(out_thread, "movie_out", m);
            if (m->aud) m->athread = SDL_CreateThread(audio_thread, "movie_audio", m);
            if (!m->thread || !m->othread || (m->aud && !m->athread)) rt_log("[movie] thread creation failed : dec=%p out=%p aud=%p (%s)\n", (void *)m->thread, (void *)m->othread, (void *)m->athread, SDL_GetError());
#ifdef __vita__
            if (!m->othread) { SceKernelFreeMemorySizeInfo fi = {.size = sizeof fi}; sceKernelGetFreeMemorySize(&fi); rt_log("[movie] free user %d KB\n", fi.size_user >> 10); }
#endif
        }
    }
    STDRET(1, HR_OK);
}
M(ctl, Pause) { HOSTFN; MV; m->state = 1; if (m->renderer) gcall(c, m->renderer, 5, 0); STDRET(1, HR_OK); }
M(ctl, Stop) {
    HOSTFN; MV;
    m->stop = 1;
    if (m->thread) { SDL_WaitThread(m->thread, NULL); m->thread = NULL; }
    if (m->othread) { SDL_WaitThread(m->othread, NULL); m->othread = NULL; }
    if (m->athread) { SDL_WaitThread(m->athread, NULL); m->athread = NULL; }
    if (m->state && m->renderer) gcall(c, m->renderer, 4, 0);  // Stop
    m->state = 0;
    g_movie_w = g_movie_h = 0;
    movie_audio_flush();
    if (m->stream) rt_log("[movie-source] requests=%llu disk_reads=%llu disk_kb=%llu cache_hit_kb=%llu io_ms=%.1f output_dropped=%u\n",
                         (unsigned long long)m->stream->requests, (unsigned long long)m->stream->reads,
                         (unsigned long long)(m->stream->read_bytes / 1024),
                         (unsigned long long)(m->stream->hit_bytes / 1024),
                         m->stream->io_ticks * 1000.0 / SDL_GetPerformanceFrequency(), m->odropped);
    // The output/EOF callbacks have ended. Their private x86 CPU owns a 1 MiB
    // guest stack and a TIB; keeping it after Stop leaks one set per movie.
    movie_free_output(m);
    STDRET(1, HR_OK);
}
M(ctl, GetState) { HOSTFN; MV; W32(ARG(3), (uint32_t)m->state); STDRET(3, HR_OK); }
M(ctl, NI) { STDRET(2, E_NOTIMPL_); }
M(ctl, Disp) { STDRET(2, E_NOTIMPL_); }
static const gfn ctl_vt[] = {any_QI, any_AddRef, any_Release, ctl_Disp, ctl_Disp, ctl_Disp, ctl_Disp, ctl_Run,
    ctl_Pause, ctl_Stop, ctl_GetState, ctl_NI, ctl_NI, ctl_NI, ctl_NI, ctl_Pause};

M(pos, Duration) { HOSTFN; MV; W64(ARG(2), rt_u64f(m->plm ? plm_get_duration(m->plm) : 0)); STDRET(2, HR_OK); }
// ponytail: audio only follows a rewind to the start outside playback (the only expected case); a real seek would need plm_demux_seek.
M(pos, PutCur) {
    HOSTFN; MV;
    double t = rt_f64(((uint64_t)ARG(3) << 32) | ARG(2));
    rt_log("[movie] PutCurrentPosition(%.2f)\n", t);
    if (m->plm) plm_seek(m->plm, t, 0);
    if (m->aud && !m->athread && t == 0) { plm_demux_rewind(m->adm); plm_audio_rewind(m->aud); movie_audio_flush(); }
    STDRET(3, HR_OK);
}
M(pos, GetCur) { HOSTFN; MV; W64(ARG(2), rt_u64f(m->plm ? plm_get_time(m->plm) : 0)); STDRET(2, HR_OK); }
M(pos, Gen2) { STDRET(2, HR_OK); }
M(pos, Gen3) { STDRET(3, HR_OK); }
static const gfn pos_vt[] = {any_QI, any_AddRef, any_Release, ctl_Disp, ctl_Disp, ctl_Disp, ctl_Disp, pos_Duration,
    pos_PutCur, pos_GetCur, pos_Gen2, pos_Gen3, pos_Gen2, pos_Gen3, pos_Gen3, pos_Gen2, pos_Gen2, pos_Gen2};

M(ev, GetHandle) { HOSTFN; W32(ARG(2), 0); STDRET(2, HR_OK); }
M(ev, GetEvent) {
    HOSTFN; MV;
    uint32_t hr = VFW_E_TIMEOUT;
    SDL_LockMutex(m->lock);
    if (m->nev) {
        W32(ARG(2), m->ev[0].code); W32(ARG(3), m->ev[0].p1); W32(ARG(4), m->ev[0].p2);
        memmove(&m->ev[0], &m->ev[1], (size_t)(--m->nev) * sizeof m->ev[0]);
        hr = HR_OK;
    }
    SDL_UnlockMutex(m->lock);
    STDRET(5, hr);
}
M(ev, WaitComp) { HOSTFN; W32(ARG(3), 0); STDRET(3, VFW_E_TIMEOUT); }
M(ev, Free) { STDRET(4, HR_OK); }
static const gfn ev_vt[] = {any_QI, any_AddRef, any_Release, ctl_Disp, ctl_Disp, ctl_Disp, ctl_Disp, ev_GetHandle,
    ev_GetEvent, ev_WaitComp, pos_Gen2, pos_Gen2, ev_Free};

M(au, PutVol) {
    HOSTFN; MV;
    m->vol = (int32_t)ARG(2);
    g_audio_gain_q8 = m->vol <= -10000 ? 0 : (int)(256.0f * powf(10.0f, (float)m->vol / 2000.0f));
    STDRET(2, HR_OK);
}
M(au, GetVol) { HOSTFN; MV; W32(ARG(2), (uint32_t)m->vol); STDRET(2, HR_OK); }
static const gfn au_vt[] = {any_QI, any_AddRef, any_Release, ctl_Disp, ctl_Disp, ctl_Disp, ctl_Disp, au_PutVol,
    au_GetVol, pos_Gen2, au_GetVol};

M(sink, Notify) { HOSTFN; rt_log("[movie] event %u (%08x, %08x)\n", ARG(2), ARG(3), ARG(4)); rpush(c, ARG(2), ARG(3), ARG(4)); STDRET(4, HR_OK); }
static const gfn sink_vt[] = {any_QI, any_AddRef, any_Release, sink_Notify};

// ---------- IGraphBuilder ----------
static uint32_t guid_d1(CPU *c, uint32_t p) { HOSTFN; return R32(p); }

M(gr, QI) {
    HOSTFN; MV;
    uint32_t d1 = guid_d1(c, ARG(2)), out = ARG(3), r = 0;
    switch (d1) {
    case 0x56a868b1: r = m->ctl_g; break;
    case 0x56a868b2: r = m->pos_g; break;
    case 0x56a868b6: case 0x56a868c0: r = m->ev_g; break;
    case 0x56a868b3: r = m->aud_g; break;
    case 0x56a868a2: r = m->sink_g; break;
    case 0x56a868a9: case 0x56a8689f: case 0: r = m->graph_g; break;
    default: rt_log("[movie] graph: unsupported QueryInterface IID %08x\n", d1); break;
    }
    W32(out, r);
    STDRET(3, r ? HR_OK : E_NOINTERFACE_);
}
M(gr, AddFilter) {
    HOSTFN; MV;
    uint32_t f = ARG(2);
    if (!f) STDRET(3, HR_OK);
    if (!com_get(f)) { // guest filter (the renderer): it joins the graph
        m->renderer = f;
        gcall(c, f, 13, 2, m->graph_g, ARG(3));
        rt_log("[movie] guest filter %08x added to the graph\n", f);
    }
    STDRET(3, HR_OK);
}
M(gr, Connect) {
    HOSTFN; MV;
    uint32_t out = ARG(2), in = ARG(3);
    HObj *po = (HObj *)com_get(out), *pi = (HObj *)com_get(in);
    if (po && pi) { po->peer = in; pi->peer = out; STDRET(3, HR_OK); }
    if (!po || pi) STDRET(3, 0x80040217u);
    // host output pin (video codec) -> guest input pin (renderer)
    uint32_t tmp = guest_tmp();
    uint8_t g_vid[8] = {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}, g_rgb[8] = {0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70}, /* RGB32 */
            g_vi[8] = {0xBF, 0x01, 0x00, 0xAA, 0x00, 0x55, 0x59, 0x5A};
    uint32_t mt = tmp, fmt = tmp + 80, w = (uint32_t)m->w, h = (uint32_t)m->h;
    memset(mb + tmp, 0, 256);
    put_guid(c, mt, 0x73646976, 0x0000, 0x0010, g_vid);
    put_guid(c, mt + 16, 0xe436eb7e, 0x524f, 0x11ce, g_rgb); // MEDIASUBTYPE_RGB32: the only one the game's renderer accepts
    W32(mt + 32, 1); W32(mt + 40, w * h * 4);
    put_guid(c, mt + 44, 0x05589f80, 0xc356, 0x11ce, g_vi);
    W32(mt + 64, 88); W32(mt + 68, fmt);
    W32(fmt + 16, w); W32(fmt + 20, h); W32(fmt + 24, w); W32(fmt + 28, h);        // rcSource/rcTarget
    W32(fmt + 40, 333667); W32(fmt + 44, 0);                                          // AvgTimePerFrame
    W32(fmt + 48, 40); W32(fmt + 52, w); W32(fmt + 56, h);                            // BITMAPINFOHEADER
    W16(fmt + 60, 1); W16(fmt + 62, 32); W32(fmt + 68, w * h * 4);
    uint32_t hr = gcall(c, in, 4, 2, out, mt); // IPin::ReceiveConnection
    rt_log("[movie] ReceiveConnection -> %08x\n", hr);
    if ((int32_t)hr < 0) STDRET(3, hr);
    m->rpin = in;
    uint32_t pm = tmp + 160;
    W32(pm, 0);
    uint8_t g_mi[8] = {0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70};
    put_guid(c, tmp + 176, 0x56a8689d, 0x0ad4, 0x11ce, g_mi);
    hr = gcall(c, in, 0, 2, tmp + 176, pm);   // QueryInterface(IID_IMemInputPin)
    m->mem_in = R32(pm);
    if ((int32_t)hr < 0 || !m->mem_in) STDRET(3, hr ? hr : E_NOINTERFACE_);
    W32(pm, 0);
    hr = gcall(c, m->mem_in, 3, 1, pm);       // GetAllocator (CoCreateInstance(CLSID_MemoryAllocator) -> host)
    m->alloc = R32(pm);
    rt_log("[movie] GetAllocator -> %08x (%08x)\n", hr, m->alloc);
    if ((int32_t)hr < 0 || !m->alloc) STDRET(3, hr ? hr : HR_FAIL);
    uint32_t req = tmp + 192;
    W32(req, 2); W32(req + 4, w * h * 4); W32(req + 8, 1); W32(req + 12, 0);
    hr = gcall(c, m->alloc, 3, 2, req, req + 16);  // SetProperties
    rt_log("[movie] SetProperties -> %08x\n", hr);
    hr = gcall(c, m->mem_in, 4, 2, m->alloc, 0u);  // NotifyAllocator
    rt_log("[movie] NotifyAllocator -> %08x\n", hr);
    po->peer = in;
    STDRET(3, hr);
}
M(gr, AddSourceFilter) {
    HOSTFN; MV;
    char path[600], host[700];
    uint32_t w = ARG(2);
    size_t i = 0;
    for (; i < sizeof path - 1 && R16(w + 2 * i); i++) path[i] = (char)R16(w + 2 * i);
    path[i] = 0;
    w32_host_path(path, host, sizeof host);
    const char *cache = getenv("SH4_MOVIE_CACHE"), *all = getenv("SH4_MOVIE_ALL_FRAMES");
    int use_cache = cache && !strcmp(cache, "1");
#ifdef __vita__
    if (!cache) use_cache = 1;
#endif
    if (use_cache) m->stream = movie_stream_open(host);
    // Allocation failure retains the old streaming/catch-up path. An explicit
    // flag can still force either policy for paired laboratory comparisons.
    g_movie_all_frames = all ? !strcmp(all, "1") : m->stream != NULL;
    rt_log("[movie-mode] shared_cache=%d all_frames=%d\n", m->stream != NULL, g_movie_all_frames);
    plm_buffer_t *fb = movie_input(m, host, 0);
    m->plm = fb ? plm_create_with_buffer(fb, TRUE) : NULL;
    rt_log("[movie] source \"%s\" -> %s\n", host, m->plm ? "ok" : "FAILED");
    if (!m->plm) { W32(ARG(4), 0); STDRET(4, VFW_E_NOT_FOUND); }
    plm_set_loop(m->plm, 0);
    plm_set_audio_enabled(m->plm, 0);
    plm_set_video_decode_callback(m->plm, on_video, m);
    if (plm_get_num_audio_streams(m->plm) && (fb = movie_input(m, host, 1))) {
        m->adm = plm_demux_create(fb, TRUE);
        plm_buffer_t *ab = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
        plm_buffer_set_load_callback(ab, apkt_load, m->adm);
        m->aud = plm_audio_create_with_buffer(ab, TRUE);
    }
    m->w = plm_get_width(m->plm);
    m->h = plm_get_height(m->plm);
    g_movie_w = m->w; g_movie_h = m->h;
    rt_log("[movie] %dx%d, %.2f i/s, %.1f s, audio=%d\n", m->w, m->h, plm_get_framerate(m->plm), plm_get_duration(m->plm),
           plm_get_num_audio_streams(m->plm));
    W32(ARG(4), m->filters[F_SRC]->com.guest);
    STDRET(4, HR_OK);
}
M(gr, Ok2) { STDRET(2, HR_OK); }
M(gr, Ok3) { STDRET(3, HR_OK); }
M(gr, NI3) { STDRET(3, E_NOTIMPL_); }
static const gfn gr_vt[] = {gr_QI, any_AddRef, any_Release, gr_AddFilter, gr_Ok2 /*RemoveFilter*/, gr_NI3 /*EnumFilters*/,
    gr_NI3 /*FindFilterByName*/, gr_Ok3 /*ConnectDirect*/, gr_Ok2 /*Reconnect*/, gr_Ok2 /*Disconnect*/,
    gr_Ok2 /*SetDefaultSyncSource*/, gr_Connect, gr_Ok3 /*Render*/, gr_NI3 /*RenderFile*/, gr_AddSourceFilter,
    gr_Ok2 /*SetLogFile*/, gr_Ok2 /*Abort*/, gr_Ok2 /*ShouldOperationContinue*/};

// ---------- entry points for CoCreateInstance ----------
static Movie *movie_new(void) {
    Movie *m = calloc(1, sizeof *m);
    m->lock = SDL_CreateMutex();
    m->vol = -10000;
    m->graph_g = mkobj(m, MK_GRAPH, gr_vt, 18, "IGraphBuilder", &m->graph);
    m->ctl_g = mkobj(m, MK_CTL, ctl_vt, 16, "IMediaControl", NULL);
    m->pos_g = mkobj(m, MK_POS, pos_vt, 18, "IMediaPosition", NULL);
    m->ev_g = mkobj(m, MK_EV, ev_vt, 13, "IMediaEvent", NULL);
    m->aud_g = mkobj(m, MK_AUD, au_vt, 11, "IBasicAudio", NULL);
    m->sink_g = mkobj(m, MK_SINK, sink_vt, 4, "IMediaEventSink", NULL);
    const char *none[2] = {0, 0}, *vs[2] = {"Audio", "Video"}, *one[2] = {"Output", 0};
    make_filter(m, F_SRC, 0, 1, NULL, one);
    make_filter(m, F_SPLIT, 1, 2, "Input", vs);
    make_filter(m, F_ACODEC, 1, 1, "Input", one);
    make_filter(m, F_VCODEC, 1, 1, "Input", one);
    make_filter(m, F_DSR, 1, 0, "Input", none);
    return m;
}

// Returns 1 if the CLSID is handled (object written to *out), 0 otherwise.
int movie_cocreate(CPU *c, uint32_t clsid, uint32_t riid, uint32_t out, uint32_t *hr) {
    HOSTFN;
    (void)riid;
    uint32_t d1 = R32(clsid);
    int which = d1 == 0x336475d0 ? F_SPLIT : d1 == 0x4a2286e0 ? F_ACODEC : d1 == 0xfeb50740 ? F_VCODEC : d1 == 0x79376820 ? F_DSR : -1;
    if (d1 == 0xe436ebb3) { // CLSID_FilterGraph : nouveau film
        if (g_mv) {                                             // movie finished without Stop: without a join, dead threads keep their stack and creating the next one fails
            g_mv->stop = 1;
            if (g_mv->thread) { SDL_WaitThread(g_mv->thread, NULL); g_mv->thread = NULL; }
            if (g_mv->othread) { SDL_WaitThread(g_mv->othread, NULL); g_mv->othread = NULL; }
            if (g_mv->athread) { SDL_WaitThread(g_mv->athread, NULL); g_mv->athread = NULL; }
            movie_free_decoders(g_mv);
        }
        // ponytail: the rest of the previous movie (COM objects, ~1 KB) leaks
        g_mv = movie_new();
        W32(out, g_mv->graph_g);
        *hr = HR_OK;
        return 1;
    }
    if (d1 == 0x1e651cc0 && g_mv) { // CLSID_MemoryAllocator
        if (!g_mv->alloc) g_mv->alloc = mkobj(g_mv, MK_ALLOC, al_vt, 9, "IMemAllocator", NULL);
        W32(out, g_mv->alloc);
        *hr = HR_OK;
        return 1;
    }
    if (which < 0 || !g_mv) return 0;
    W32(out, g_mv->filters[which]->com.guest);
    *hr = HR_OK;
    return 1;
}
