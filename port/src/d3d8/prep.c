// D3D8 prep thread (see prep.h). The game thread writes records into an SPSC ring; the consumer replays them
// on the "shadow" device g_pdev and makes all the GL calls. Modes: 0 sync (immediate replay), 1 lazy (replay at presents and
// waits), 2 thread (dedicated thread, core 2 on Vita).
#include "prep.h"
#include "audit.h"
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_mutex.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
#endif

Device *g_pdev;
unsigned g_pframe;
int g_prep_mode;

enum { R_PAD, R_SET, R_BIND, R_DRAW, R_CLEAR, R_PRESENT, R_COPY, R_FREE, R_CALL, R_VSDEL, R_PSDEL };
enum { F_I32 = 1, F_IDX = 2, F_UP = 4, F_PTR = 8, F_IIDX = 16 };   // F_IDX: indexed; F_IIDX: indices in the record
#define FLUSH 2048u   // at most this many unpublished words

#define LD(p) __atomic_load_n(p, __ATOMIC_ACQUIRE)
#define PST(p, v) __atomic_store_n(p, v, __ATOMIC_RELEASE)

static uint32_t *ring;
static uint32_t rwords, rmask, maxrec;
static uint32_t whead, head_pub, ptail;   // game thread only
static volatile uint32_t head __attribute__((aligned(64)));   // published by the game
static volatile uint32_t tail __attribute__((aligned(64)));   // published by the consumer
static volatile int c_sleep __attribute__((aligned(64)));
static SDL_sem *sem;
static Device *pd;
static __thread int on_prep;
static uint32_t prev_present_pos;
static uint64_t st_rec, st_inl, st_lockw, st_lockw_t, st_fullw, st_ibytes, st_busy, st_idle;

static inline void putp(uint32_t *w, const void *p) { uint64_t v = (uint64_t)(uintptr_t)p; w[0] = (uint32_t)v; w[1] = (uint32_t)(v >> 32); }
static inline void *getp(const uint32_t *w) { return (void *)(uintptr_t)((uint64_t)w[0] | (uint64_t)w[1] << 32); }

// ---------- consommateur ----------
static void dispatch(uint32_t k, uint32_t *r, uint32_t nw) {
    (void)nw;
    switch (k) {
    case R_PAD: break;
    case R_SET: {
        uint32_t off = r[1] >> 16, n = r[1] & 0xffff;
        memcpy((char *)pd + off, r + 2, n);
        if (off >= offsetof(Device, vsc) && off < offsetof(Device, vsc) + sizeof pd->vsc) pd->vsc_gen++;
        else if (off >= offsetof(Device, psc) && off < offsetof(Device, psc) + sizeof pd->psc) pd->psc_gen++;
        break;
    }
    case R_BIND: gfx_bind_target(pd); break;
    case R_DRAW: {
        uint32_t prim = r[1] & 0xff, fl = r[1] >> 8;
        uint32_t mask = r[8];
        const uint8_t *inl[16] = {0}, *indices = r[7] ? g_mem + r[7] : NULL;
        const uint32_t *ds = r + 9, *data = ds + 4 * (uint32_t)__builtin_popcount(mask);
        for (int bit = 16; mask; bit = bit == 16 ? 0 : bit + 1) {   // order: indices, then streams 0..15
            if (!(mask >> bit & 1)) continue;
            mask &= ~(1u << bit);
            uint32_t nbytes = ds[0], origin = ds[1];
            const uint8_t *src = getp(ds + 2);
            ds += 4;
            if (!src) { src = (const uint8_t *)data; data += (nbytes + 3) / 4; }
            if (bit == 16) indices = src; else inl[bit] = src - origin;
        }
        gfx_draw(pd, prim, r[2], r[3], r[4], (fl & F_IDX) ? indices : NULL, !!(fl & F_I32), r[5], (fl & F_UP) ? inl[0] : NULL, r[6], inl);
        break;
    }
    case R_CLEAR: {
        float z; memcpy(&z, &r[3], 4);
        gfx_clear(pd, r[1], r[2], z, r[4], (int)r[5], r[5] ? (const int32_t *)(r + 6) : NULL);
        break;
    }
    case R_PRESENT: gfx_present(pd); break;
    case R_COPY: {
        uint32_t n = r[5], hp = r[6];
        const int32_t *rects = (const int32_t *)(r + 7);
        gfx_copy_gl(getp(r + 1), getp(r + 3), n, n ? rects : NULL, hp ? (const int32_t *)(r + 7 + 4 * n) : NULL);
        break;
    }
    case R_FREE: res_free((int)r[1], getp(r + 2)); break;
    case R_CALL: ((void (*)(void *))(uintptr_t)getp(r + 1))(getp(r + 3)); break;
    case R_VSDEL: vs_delete(r[1]); break;
    case R_PSDEL: ps_delete(r[1]); break;
    }
}
static int consume_all(void) {
    uint32_t t = tail, h = LD(&head);
    int any = 0;
    for (;;) {
        if (t == h && (h = LD(&head)) == t) break;
        uint32_t *r = ring + (t & rmask), hd = r[0], nw = hd & 0xffffffu;
        dispatch(hd >> 24, r, nw);
        st_rec++;
        t += nw;
        PST(&tail, t);
        any = 1;
    }
    return any;
}
static void pump(void) {
    int was = on_prep;
    on_prep = 1;
    consume_all();
    on_prep = was;
}
static inline uint64_t now(void) { return SDL_GetPerformanceCounter(); }
static int prep_thread(void *arg) {
    (void)arg;
    rt_cpu_profile_thread("prepare");
    on_prep = 1;
#ifdef __vita__
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_2);
#endif
    for (;;) {
        uint64_t t0 = now();
        while (consume_all()) {}
        for (int i = 0; i < 1500 && tail == LD(&head); i++) __asm__ __volatile__("" ::: "memory");
        uint64_t t1 = now();
        st_busy += t1 - t0;
        if (tail != LD(&head)) continue;
        __atomic_store_n(&c_sleep, 1, __ATOMIC_SEQ_CST);
        if (tail != LD(&head)) {
            if (__atomic_exchange_n(&c_sleep, 0, __ATOMIC_SEQ_CST) == 0) SDL_SemWait(sem);   // the producer already took the wake-up
            continue;
        }
        SDL_SemWait(sem);
        st_idle += now() - t1;
    }
    return 0;
}

// ---------- producteur ----------
static void wait_progress(void) {
    if (g_prep_mode != 2) { pump(); return; }
#ifdef __vita__
    sceKernelDelayThread(50);
#else
    SDL_Delay(0);
#endif
}
static void prep_flush(void) {
    if (whead == head_pub) return;
    head_pub = whead;
    __atomic_store_n(&head, whead, __ATOMIC_SEQ_CST);
    if (g_prep_mode == 2) {
        if (__atomic_load_n(&c_sleep, __ATOMIC_SEQ_CST) && __atomic_exchange_n(&c_sleep, 0, __ATOMIC_SEQ_CST)) SDL_SemPost(sem);
    } else if (g_prep_mode == 0) pump();
}
static uint32_t *rec_begin(int kind, uint32_t nw) {
    uint32_t cont = rwords - (whead & rmask), need = nw > cont ? cont + nw : nw;
    if (rwords - (whead - ptail) < need) {
        st_fullw++;
        for (;;) {
            ptail = LD(&tail);
            if (rwords - (whead - ptail) >= need) break;
            prep_flush();
            wait_progress();
        }
    }
    if (nw > cont) { ring[whead & rmask] = (uint32_t)R_PAD << 24 | cont; whead += cont; }
    uint32_t *r = ring + (whead & rmask);
    r[0] = (uint32_t)kind << 24 | nw;
    return r;
}
static inline void rec_commit(uint32_t nw) {
    whead += nw;
    if (whead - head_pub >= FLUSH || g_prep_mode == 0) prep_flush();
}
static void wait_pos(uint32_t pos) {
    prep_flush();
    while ((int32_t)(LD(&tail) - pos) < 0) wait_progress();
}
static inline uint32_t cur_pos(void) { return whead ? whead : 1; }

void prep_wait_all(void) { wait_pos(whead); }
void prep_use(uint32_t *use) { *use = cur_pos(); }
void prep_wait_use(uint32_t use) {
    if (!use || (uint32_t)(whead - use) >= rwords) return;   // never used, or older than the ring (so already replayed)
    if ((int32_t)(LD(&tail) - use) >= 0) return;
    uint64_t t0 = now();
    wait_pos(use);
    st_lockw++;
    st_lockw_t += now() - t0;
}

void prep_init(Device *game) {
    const char *m = getenv("SH4_PREP");
#ifdef __vita__
    g_prep_mode = 2;
#else
    g_prep_mode = 0;
#endif
    if (m) g_prep_mode = (!strcmp(m, "sync") || !strcmp(m, "0")) ? 0 : (!strcmp(m, "lazy") || !strcmp(m, "1")) ? 1 : 2;
    uint32_t kb = getenv("SH4_PREP_KB") ? (uint32_t)atoi(getenv("SH4_PREP_KB")) : 1024;
    rwords = 1024;
    while (rwords * 4 < kb * 1024) rwords <<= 1;
    rmask = rwords - 1;
    maxrec = rwords / 4;
    ring = calloc(rwords, 4);
    if (!ring) rt_fatal("prep thread: %u KB ring not allocated", rwords / 256);
    head = tail = whead = head_pub = ptail = 4;
    pd = calloc(1, sizeof *pd);
    *pd = *game;
    g_pdev = pd;
    sem = SDL_CreateSemaphore(0);
    if (g_prep_mode == 2) SDL_DetachThread(SDL_CreateThread(prep_thread, "sh4prep", NULL));
    rt_log("[prep] mode %s, ring %u KB, max record %u KB\n", g_prep_mode == 0 ? "sync" : g_prep_mode == 1 ? "lazy" : "thread", rwords / 256, maxrec / 256);
}

// ---------- encodeurs ----------
int prep_set(void *field, const void *src, size_t n) {
    static int scalar = -1;
    if (scalar < 0) { const char *v = getenv("SH4_PREP_SCALAR"); scalar = v && !strcmp(v, "1"); }
    if (scalar && n == 4) {
        uint32_t old, value;
        memcpy(&old, field, 4); memcpy(&value, src, 4);
        if (old == value) return 0;
        memcpy(field, &value, 4);
        uint32_t *r = rec_begin(R_SET, 3);
        r[1] = ((uint32_t)((char *)field - (char *)g_dev) << 16) | 4u;
        r[2] = value;
        rec_commit(3);
        return 1;
    }
    if (!memcmp(field, src, n)) return 0;
    memcpy(field, src, n);
    uint32_t w = (uint32_t)(n + 3) / 4, nw = 2 + w;
    uint32_t *r = rec_begin(R_SET, nw);
    r[1] = ((uint32_t)((char *)field - (char *)g_dev) << 16) | (uint32_t)n;
    r[nw - 1] = 0;
    memcpy(r + 2, src, n);
    rec_commit(nw);
    return 1;
}
void prep_bind(void) { rec_begin(R_BIND, 1); rec_commit(1); }

typedef struct { uint32_t nbytes, origin, copy; const uint8_t *src; } Part;
void prep_draw(Device *d, uint32_t prim, uint32_t first, uint32_t nverts, uint32_t nprims, const uint8_t *indices, int index32,
               uint32_t min_index, const uint8_t *up, uint32_t up_stride) {
    if (!nverts) return;
    uint32_t fvfs = 0, smask = up ? 1u : gfx_stream_mask(d, &fvfs);
    if (g_audit_checks) {
        uint32_t hi = nverts - 1;
        uint64_t ibytes = (uint64_t)nverts * (index32 ? 4 : 2);
        if (indices) {
            uintptr_t addr = (uintptr_t)indices, base = (uintptr_t)g_mem;
            if (addr < base || addr - base > g_mem_size || ibytes > g_mem_size - (addr - base))
                rt_fatal("[audit-error] frame=%u index range outside guest memory", g_frame);
            if (!up && d->ib && (addr < base + d->ib->mem || addr - base - d->ib->mem + ibytes > d->ib->size))
                rt_fatal("[audit-error] frame=%u index range outside index buffer", g_frame);
            hi = 0;
            for (uint32_t i = 0; i < nverts; i++) {
                uint32_t v = index32 ? ((const uint32_t *)indices)[i] : ((const uint16_t *)indices)[i];
                if (v > hi) hi = v;
            }
        }
        if (up) {
            uintptr_t addr = (uintptr_t)up, base = (uintptr_t)g_mem;
            uint64_t bytes = ((uint64_t)first + hi + 1) * up_stride;
            if (addr < base || addr - base > g_mem_size || bytes > g_mem_size - (addr - base))
                rt_fatal("[audit-error] frame=%u user vertex range outside guest memory", g_frame);
        } else for (uint32_t m = smask; m; m &= m - 1) {
            unsigned s = __builtin_ctz(m);
            Buffer *vb = d->stream[s].vb;
            uint32_t stride = d->stream[s].stride;
            if (!stride && s == 0) stride = fvfs;
            if (!vb) rt_fatal("[audit-error] frame=%u missing vertex stream %u", g_frame, s);
            if (stride && ((uint64_t)first + hi + 1) * stride > vb->size)
                rt_fatal("[audit-error] frame=%u vertex range outside buffer stream=%u first=%u max=%u stride=%u size=%u", g_frame, s, first, hi, stride, vb->size);
        }
    }
    int inl_streams = up || !gfx_use_vbo();
    for (uint32_t m = smask; m && !inl_streams; m &= m - 1) {
        Buffer *vb = d->stream[__builtin_ctz(m)].vb;
        if (vb && vb->dyn) inl_streams = 1;
    }
    int inl_idx = indices && (up || (d->ib && d->ib->dyn));
    uint32_t lo = 0, hi = 0, isz = index32 ? 4 : 2;
    if (indices && inl_streams) {   // index bounds: the consumer rebases on lo like idx_rewrite
        lo = 0xffffffffu;
        if (index32) for (uint32_t i = 0; i < nverts; i++) { uint32_t v = ((const uint32_t *)indices)[i]; if (v < lo) lo = v; if (v > hi) hi = v; }
        else for (uint32_t i = 0; i < nverts; i++) { uint32_t v = ((const uint16_t *)indices)[i]; if (v < lo) lo = v; if (v > hi) hi = v; }
    }
    Part parts[17];
    int np = 0;
    uint32_t mask = 0, fl = (index32 ? F_I32 : 0) | (indices ? F_IDX : 0) | (up ? F_UP : 0);
    if (inl_idx) {
        parts[np++] = (Part){nverts * isz, 0, nverts * isz, indices};
        mask |= 1u << 16;
        fl |= F_IIDX;
    }
    if (inl_streams) {
        if (up) {
            uint32_t origin = indices ? lo * up_stride : 0, nb = indices ? (hi - lo + 1) * up_stride : nverts * up_stride;
            parts[np++] = (Part){nb, origin, nb, up + origin};
            mask |= 1;
        } else
            for (uint32_t m = smask; m; m &= m - 1) {
                int s = __builtin_ctz(m);
                Buffer *vb = d->stream[s].vb;
                if (!vb) continue;
                uint32_t stride = d->stream[s].stride;
                if (!stride && s == 0) stride = fvfs;
                uint32_t origin = (first + lo) * stride, nb = stride ? (indices ? hi - lo + 1 : nverts) * stride : 64;
                uint32_t avail = origin < vb->size ? vb->size - origin : 0;
                parts[np++] = (Part){nb, origin, nb < avail ? nb : avail, g_mem + vb->mem + origin};
                mask |= 1u << s;
            }
    }
    uint32_t tw = 9 + 4 * (uint32_t)np, dw = 0;
    for (int i = 0; i < np; i++) dw += (parts[i].nbytes + 3) / 4;
    if (tw + dw > maxrec) fl |= F_PTR;
    else tw += dw;
    uint32_t *r = rec_begin(R_DRAW, tw);
    r[1] = prim | fl << 8; r[2] = first; r[3] = nverts; r[4] = nprims; r[5] = min_index; r[6] = up_stride;
    r[7] = indices && !inl_idx ? (uint32_t)(indices - g_mem) : 0;
    r[8] = mask;
    uint32_t *dp = r + 9, *dd = dp + 4 * np;
    for (int i = 0; i < np; i++, dp += 4) {
        dp[0] = parts[i].nbytes; dp[1] = parts[i].origin;
        if (fl & F_PTR) putp(dp + 2, parts[i].src);
        else {
            dp[2] = dp[3] = 0;
            uint32_t w = (parts[i].nbytes + 3) / 4;
            memcpy(dd, parts[i].src, parts[i].copy);
            memset((char *)dd + parts[i].copy, 0, w * 4 - parts[i].copy);
            dd += w;
            st_ibytes += parts[i].nbytes;
        }
    }
    rec_commit(tw);
    uint32_t pos = cur_pos();
    if (!inl_streams) for (uint32_t m = smask; m; m &= m - 1) { Buffer *vb = d->stream[__builtin_ctz(m)].vb; if (vb) vb->use = pos; }
    if (indices && !inl_idx && d->ib) d->ib->use = pos;
    for (int s = 0; s < MAX_STAGES; s++) if (d->tex[s]) d->tex[s]->use = pos;
    if (d->rt && d->rt->tex) d->rt->tex->use = pos;
    if (d->ds && d->ds->tex) d->ds->tex->use = pos;
    if (fl & F_PTR) { st_inl++; wait_pos(pos); }   // the data is in guest memory: wait until it has been read
}

void prep_clear(uint32_t flags, uint32_t color, float z, uint32_t stencil, int nrects, uint32_t rects_guest) {
    HOSTFN;
    uint32_t n = rects_guest && nrects > 0 ? (uint32_t)nrects : 0;
    if (n > maxrec / 8) n = maxrec / 8;
    uint32_t nw = 6 + 4 * n;
    uint32_t *r = rec_begin(R_CLEAR, nw);
    r[1] = flags; r[2] = color; memcpy(&r[3], &z, 4); r[4] = stencil; r[5] = n;
    for (uint32_t i = 0; i < 4 * n; i++) r[6 + i] = R32(rects_guest + 4 * i);
    rec_commit(nw);
    uint32_t pos = cur_pos();
    if (g_dev->rt && g_dev->rt->tex) g_dev->rt->tex->use = pos;
    if (g_dev->ds && g_dev->ds->tex) g_dev->ds->tex->use = pos;
}

void prep_present(void) {
    rec_begin(R_PRESENT, 1);
    rec_commit(1);
    prep_flush();
    if (g_prep_mode == 1) pump();
    uint32_t pos = cur_pos();
    if (prev_present_pos) wait_pos(prev_present_pos);   // at most one frame ahead of the consumer
    prev_present_pos = pos;
}

void prep_copy(Surface *src, Surface *dst, uint32_t n, const int32_t *rects, const int32_t *points) {
    uint32_t nw = 7 + 4 * n + (points ? 2 * n : 0);
    uint32_t *r = rec_begin(R_COPY, nw);
    putp(r + 1, src); putp(r + 3, dst);
    r[5] = n; r[6] = points != NULL;
    if (n) memcpy(r + 7, rects, 16u * n);
    if (points) memcpy(r + 7 + 4 * n, points, 8u * n);
    rec_commit(nw);
    uint32_t pos = cur_pos();
    if (dst->tex) dst->tex->use = pos;
    if (src->tex) src->tex->use = pos;
}

// Deferred guest frees: the guest heap and the COM object table are not shared, the game thread drains them.
typedef struct { uint32_t wrapper, mem; } GFree;
static GFree *gf, *gd;
static int gfree_n, gfcap, gdcap;
static SDL_mutex *gfmtx;
void com_free_guest(uint32_t g);
void prep_gfree(uint32_t wrapper, uint32_t mem) {
    if (!gfmtx) gfmtx = SDL_CreateMutex();
    SDL_LockMutex(gfmtx);
    if (gfree_n == gfcap) { gfcap = gfcap ? gfcap * 2 : 256; gf = realloc(gf, (size_t)gfcap * sizeof *gf); }
    gf[gfree_n++] = (GFree){wrapper, mem};
    SDL_UnlockMutex(gfmtx);
}
void prep_gdrain(void) {
    if (!__atomic_load_n(&gfree_n, __ATOMIC_ACQUIRE)) return;
    SDL_LockMutex(gfmtx);
    int n = gfree_n;
    if (n > gdcap) { gdcap = n * 2; gd = realloc(gd, (size_t)gdcap * sizeof *gd); }
    memcpy(gd, gf, (size_t)n * sizeof *gd);
    gfree_n = 0;
    SDL_UnlockMutex(gfmtx);
    for (int i = 0; i < n; i++) {
        if (gd[i].mem) gheap_free(gd[i].mem);
        if (gd[i].wrapper) com_free_guest(gd[i].wrapper);
    }
}

void prep_free(int kind, void *p) {
    uint32_t *r = rec_begin(R_FREE, 4);
    r[1] = (uint32_t)kind; putp(r + 2, p);
    rec_commit(4);
    prep_gdrain();
}
void prep_call(void (*fn)(void *), void *arg) {
    if (on_prep) { fn(arg); return; }
    uint32_t *r = rec_begin(R_CALL, 5);
    putp(r + 1, (void *)(uintptr_t)fn); putp(r + 3, arg);
    rec_commit(5);
    wait_pos(cur_pos());
}
void prep_vsdel(uint32_t h) { uint32_t *r = rec_begin(R_VSDEL, 2); r[1] = h; rec_commit(2); }
void prep_psdel(uint32_t h) { uint32_t *r = rec_begin(R_PSDEL, 2); r[1] = h; rec_commit(2); }

static void gfx_init_cb(void *a) { (void)a; gfx_init(g_pdev); }
void prep_gfx_init(void) { prep_call(gfx_init_cb, NULL); }

void prep_report(double frames) {
    double k = 1000.0 / (double)SDL_GetPerformanceFrequency() / frames;
    rt_log("[prep] recs/frame %.0f; inline vertices %.0f KB/frame; big draws %llu; ring full %llu; Lock waits %llu (%.2f ms/frame); consumer busy %.2f ms, idle %.2f ms/frame\n",
           (double)st_rec / frames, (double)st_ibytes / 1024.0 / frames, (unsigned long long)st_inl, (unsigned long long)st_fullw,
           (unsigned long long)st_lockw, (double)st_lockw_t * k, (double)st_busy * k, (double)st_idle * k);
    st_rec = st_ibytes = st_inl = st_fullw = st_lockw = st_lockw_t = st_busy = st_idle = 0;
}
