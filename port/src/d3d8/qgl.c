// GL command queue + render thread (see qgl.h).
#ifdef __vita__
#define QGL_IMPL
#include "qgl.h"
#include "rt.h"
#include "audit.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/cpu.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arm_neon.h>

// Optional diagnostic supplied by the isolated renderbuffer-storage vitaGL build.
extern int vglGetLastSceneError(void) __attribute__((weak));


// Ring: adjustable size (SH4_QRING_KB, power of 2); kept small it stays in the L2 cache instead of hammering DRAM.
static unsigned rw, rmask, maxrec;
#define FLUSH 2048           // words written before publishing (8 KB)
enum { OP_PAD, OP_ENABLE, OP_DISABLE, OP_TEXPARAM, OP_BINDFB, OP_BINDTEX, OP_VIEWPORT, OP_U4F, OP_FBTEX, OP_COLORMASK, OP_CLEAR,
       OP_ACTIVETEX, OP_VAP, OP_U4FV, OP_U1I, OP_U1F, OP_USEPROG, OP_ENVA, OP_DISVA, OP_DRAW, OP_CLEARCOL, OP_CLEARDEPTH,
       OP_CLEARSTENCIL, OP_DEPTHRANGE, OP_DEPTHFUNC, OP_DEPTHMASK, OP_STENCILMASK, OP_STENCILOP, OP_STENCILFUNC, OP_SCISSOR,
       OP_POLYOFF, OP_FRONTFACE, OP_CULLFACE, OP_BLENDFUNC, OP_BLENDEQ, OP_BINDBUF, OP_BUFDATA, OP_BUFSUB, OP_DELBUF, OP_DELTEX,
       OP_DELRB, OP_BINDRB, OP_RBSTORAGE, OP_FBRB, OP_SWAP, OP_TEX565 };

int q_on = -1;
static uint32_t *ring;
// Each core writes its own variable: one cache line (64 B) per shared variable, nothing else in it.
typedef struct { volatile unsigned v; char pad[60]; } __attribute__((aligned(64))) PadU;
static PadU q_head, q_tail, q_sleep;
#define head q_head.v      // published by the producer (words, increasing; index = value & rmask)
#define tail q_tail.v      // published by the consumer AFTER execution
#define c_sleep q_sleep.v
static unsigned whead, ptail;   // producer's local view
static volatile unsigned sync_req, fb_pending;
static SceUID c_sema;
static volatile unsigned swaps_enq, swaps_done;
static SceUID g_owner;
static unsigned inv_gen = 1;   // GL state shadow on the game side (see wrappers): changing the value invalidates it
// statistiques (µs)
static uint32_t st_sync, st_swapw, st_fullw, st_idle, st_swapx;
static unsigned st_nsync, st_recs, st_maxfill, st_big;
static struct { const char *n; unsigned c; } why_t[24];
static unsigned st_op[64], p_op[64];
static unsigned st_ip, st_cut, st_ovf, st_fbmax, st_large_skip, st_large_false;
static unsigned st_noip, st_off;
static uint32_t st_t1, st_t0, st_g1, st_g0, st_tp, st_nl, st_nv, st_nk;   // us: intermediate pass, real pass; GPU wait (SH4_GPUFIN)
static uint32_t audit_swap_time;
static int gpufin, qprof;   // SH4_QPROF=1: render thread time per operation (real pass), timers included
static uint32_t q_opt[64];

void q_set_gpufin(int on) { gpufin = on; }
void q_set_split(int n);
static inline uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
#define NOW() sceKernelGetProcessTimeWide()
void q_win(uint32_t *wall, uint32_t *idle) { *wall = NOW(); *idle = st_idle; }   // diagnostics: render thread active time

// ---------- producteur ----------
// Records are written into the ring without publishing anything; `flush` publishes `head` (and wakes the render thread) every
// FLUSH words, at every sync and at every present: little cache-line traffic between the two cores.
static void flush(void) {
    if (whead == head) return;
    __sync_synchronize();
    head = whead;
    __sync_synchronize();
    if (c_sleep && __sync_lock_test_and_set(&c_sleep, 0)) sceKernelSignalSema(c_sema, 1);
}
static volatile int prod_lock;   // several guest threads can issue GL calls: one producer at a time between rec_begin and rec_commit
static SceUID prod_owner;
static uint32_t *rec_begin(unsigned op, unsigned nw) {   // nw = words of the record, header included
    SceUID me = sceKernelGetThreadId();
    if (prod_owner != me) { while (__sync_lock_test_and_set(&prod_lock, 1)) sceKernelDelayThread(0); prod_owner = me; }
    for (;;) {
        unsigned h = whead, free = rw - (h - ptail), cont = rw - (h & rmask), need = nw > cont ? cont + nw : nw;
        if (free < need) {
            ptail = tail;
            free = rw - (h - ptail);
        }
        if (free >= need) {
            if (nw > cont) {   // padding up to the end of the ring
                ring[h & rmask] = (uint32_t)OP_PAD << 24 | cont;
                whead = h + cont;
                continue;
            }
            ring[h & rmask] = (uint32_t)op << 24 | nw;
            st_op[op & 63]++;
            unsigned fill = h - ptail + nw;
            if (fill > st_maxfill) st_maxfill = fill;
            return &ring[(h & rmask) + 1];
        }
        uint32_t t0 = NOW();
        flush();
        while (rw - (h - tail) < need) sceKernelDelayThread(50);
        st_fullw += NOW() - t0;
    }
}
static void rec_commit(unsigned nw) {
    whead += nw;
    prod_owner = 0; __sync_synchronize(); prod_lock = 0;
    st_recs++;
    if (whead - head >= FLUSH) flush();
}
static void emit(unsigned op, const uint32_t *a, unsigned n) {
    uint32_t *r = rec_begin(op, n + 1);
    for (unsigned i = 0; i < n; i++) r[i] = a[i];
    rec_commit(n + 1);
}
// header a[0..n) then `bytes` bytes copied; false if the record is too big (the caller issues the direct call)
static int emit_blob(unsigned op, const uint32_t *a, unsigned n, const void *data, unsigned bytes) {
    unsigned bw = (bytes + 3) / 4;
    if (1 + n + bw > maxrec) return 0;
    uint32_t *r = rec_begin(op, 1 + n + bw);
    for (unsigned i = 0; i < n; i++) r[i] = a[i];
    memcpy(r + n, data, bytes);
    rec_commit(1 + n + bw);
    return 1;
}

// diag: state printed by the watchdog when the game stops submitting GL records (hang analysis)
static volatile unsigned dbg_rop = 255, dbg_rt;
static const char *volatile dbg_why;
void q_sync_n(const char *why) {
    dbg_why = why;
    if (!g_owner) g_owner = sceKernelGetThreadId();
    else if (g_owner != sceKernelGetThreadId()) { static int w; if (!w++) rt_log("[q] GL call from another thread (%s)\n", why); }
    st_nsync++;
    inv_gen++;
    int i = 0;
    while (i < 23 && why_t[i].n && strcmp(why_t[i].n, why)) i++;
    if (!why_t[i].n) why_t[i].n = why;
    why_t[i].c++;
    flush();
    if (tail == whead && !fb_pending) return;
    uint32_t t0 = NOW();
    sync_req = 1;   // the render thread runs what it set aside (the frame will not be interpolated)
    __sync_synchronize();
    if (c_sleep && __sync_lock_test_and_set(&c_sleep, 0)) sceKernelSignalSema(c_sema, 1);
    while (tail != whead || fb_pending) __asm__ volatile("yield");
    st_sync += NOW() - t0;
    dbg_why = NULL;
}

// ---------- consommateur ----------
typedef struct { GLuint idx; GLint size; GLenum type; GLboolean norm; GLsizei stride; uintptr_t ptr; } CA;

static void draw_exec(int kind, GLenum mode, GLint a, GLuint b, GLsizei count, GLenum type, const void *ind, GLuint restore,
                      const CA *ca, int nca) {
    if (nca && restore) glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (int i = 0; i < nca; i++) glVertexAttribPointer(ca[i].idx, ca[i].size, ca[i].type, ca[i].norm, ca[i].stride, (const void *)ca[i].ptr);
    if (nca && restore) glBindBuffer(GL_ARRAY_BUFFER, restore);
    if (kind == 0) glDrawArrays(mode, a, count);
    else glDrawRangeElements(mode, a, b, count, type, kind == 2 ? (const void *)0 : ind);   // kind 2: indices in the bound IBO (offset 0)
}

// OP_DRAW: kind (0 arrays, 1 client indices, 2 indices in the bound IBO) mode a b count type ibytes nca restore | nca x {idx size type norm stride off} | data (indices then groups)
static void exec_draw(const uint32_t *r) {
    unsigned nca = r[7];
    const uint32_t *d = r + 9 + nca * 6;
    CA ca[16];
    for (unsigned i = 0; i < nca; i++) {
        const uint32_t *p = r + 9 + i * 6;
        ca[i] = (CA){p[0], (GLint)p[1], p[2], (GLboolean)p[3], (GLsizei)p[4], (uintptr_t)d + (uintptr_t)(int32_t)p[5]};
    }
    draw_exec((int)r[0], r[1], (GLint)r[2], r[3], (GLsizei)r[4], r[5], d, r[8], ca, (int)nca);
}

static void exec(unsigned op, const uint32_t *a);
static void exec_inner(unsigned op, const uint32_t *a) {
    switch (op) {
    case OP_ENABLE: glEnable(a[0]); break;
    case OP_DISABLE: glDisable(a[0]); break;
    case OP_TEXPARAM: glTexParameteri(a[0], a[1], (GLint)a[2]); break;
    case OP_BINDFB: glBindFramebuffer(a[0], a[1]); break;
    case OP_BINDTEX: glBindTexture(a[0], a[1]); break;
    case OP_VIEWPORT: glViewport((GLint)a[0], (GLint)a[1], (GLsizei)a[2], (GLsizei)a[3]); break;
    case OP_U4F: glUniform4f((GLint)a[0], u2f(a[1]), u2f(a[2]), u2f(a[3]), u2f(a[4])); break;
    case OP_FBTEX: glFramebufferTexture2D(a[0], a[1], a[2], a[3], (GLint)a[4]); break;
    case OP_COLORMASK: glColorMask((GLboolean)a[0], (GLboolean)a[1], (GLboolean)a[2], (GLboolean)a[3]); break;
    case OP_CLEAR: glClear(a[0]); break;
    case OP_ACTIVETEX: glActiveTexture(a[0]); break;
    case OP_VAP: glVertexAttribPointer(a[0], (GLint)a[1], a[2], (GLboolean)a[3], (GLsizei)a[4], (const void *)(uintptr_t)a[5]); break;
    case OP_U4FV: glUniform4fv((GLint)a[0], (GLsizei)a[1], (const GLfloat *)(a + 2)); break;
    case OP_U1I: glUniform1i((GLint)a[0], (GLint)a[1]); break;
    case OP_U1F: glUniform1f((GLint)a[0], u2f(a[1])); break;
    case OP_USEPROG: glUseProgram(a[0]); break;
    case OP_ENVA: glEnableVertexAttribArray(a[0]); break;
    case OP_DISVA: glDisableVertexAttribArray(a[0]); break;
    case OP_DRAW: exec_draw(a); break;
    case OP_CLEARCOL: glClearColor(u2f(a[0]), u2f(a[1]), u2f(a[2]), u2f(a[3])); break;
    case OP_CLEARDEPTH: glClearDepth((GLdouble)u2f(a[0])); break;
    case OP_CLEARSTENCIL: glClearStencil((GLint)a[0]); break;
    case OP_DEPTHRANGE: glDepthRange((GLdouble)u2f(a[0]), (GLdouble)u2f(a[1])); break;
    case OP_DEPTHFUNC: glDepthFunc(a[0]); break;
    case OP_DEPTHMASK: glDepthMask((GLboolean)a[0]); break;
    case OP_STENCILMASK: glStencilMask(a[0]); break;
    case OP_STENCILOP: glStencilOp(a[0], a[1], a[2]); break;
    case OP_STENCILFUNC: glStencilFunc(a[0], (GLint)a[1], a[2]); break;
    case OP_SCISSOR: glScissor((GLint)a[0], (GLint)a[1], (GLsizei)a[2], (GLsizei)a[3]); break;
    case OP_POLYOFF: glPolygonOffset(u2f(a[0]), u2f(a[1])); break;
    case OP_FRONTFACE: glFrontFace(a[0]); break;
    case OP_CULLFACE: glCullFace(a[0]); break;
    case OP_BLENDFUNC: glBlendFunc(a[0], a[1]); break;
    case OP_BLENDEQ: glBlendEquation(a[0]); break;
    case OP_BINDBUF: glBindBuffer(a[0], a[1]); break;
    case OP_BUFDATA: glBufferData(a[0], (GLsizei)a[1], a[3] ? (const void *)(a + 4) : NULL, a[2]); break;
    case OP_BUFSUB: glBufferSubData(a[0], (GLintptr)a[1], (GLsizeiptr)a[2], a + 3); break;
    case OP_DELBUF: glDeleteBuffers((GLsizei)a[0], a + 1); break;
    case OP_DELTEX: glDeleteTextures((GLsizei)a[0], a + 1); break;
    case OP_DELRB: glDeleteRenderbuffers((GLsizei)a[0], a + 1); break;
    case OP_BINDRB: glBindRenderbuffer(a[0], a[1]); break;
    case OP_RBSTORAGE: glRenderbufferStorage(a[0], a[1], (GLsizei)a[2], (GLsizei)a[3]); break;
    case OP_FBRB: glFramebufferRenderbuffer(a[0], a[1], a[2], a[3]); break;
    case OP_TEX565:
        glTexSubImage2D(GL_TEXTURE_2D, (GLint)a[0], 0, 0, (GLsizei)a[1], (GLsizei)a[2], GL_RGB, GL_UNSIGNED_SHORT_5_6_5, (const void *)(uintptr_t)a[3]);
        *(volatile int *)(uintptr_t)a[4] = 0;
        break;
    case OP_SWAP: {
        uint32_t t0 = NOW();
        vglSwapBuffers(GL_FALSE);
        st_swapx += NOW() - t0;
        swaps_done++;
        if (g_audit_enabled) {
            uint32_t now = NOW();
            unsigned error = g_audit_checks ? glGetError() : 0;   // GL owner; no queue flush or GPU wait
            int gxm_checked = g_audit_checks && vglGetLastSceneError != NULL;
            int gxm_error = gxm_checked ? vglGetLastSceneError() : 0;
            if (g_audit_frames) rt_log("[audit-swap] frame=%u interval_us=%u gl_error=%u gxm_error_checked=%d gxm_error=%d\n", swaps_done,
                   audit_swap_time ? now - audit_swap_time : 0, error, gxm_checked, gxm_error);
            audit_swap_time = now;
            if (error) rt_log("[audit-error] frame=%u GL error 0x%x at real swap\n", swaps_done, error);
            if (gxm_error) rt_log("[audit-error] frame=%u GXM scene begin failed 0x%x\n", swaps_done, (unsigned)gxm_error);
        }
        break;
    }
    }
}

// ---------- frame interpolation (60 fps) ----------
// The game computes 30 frames/s. The render thread sets aside the records of a frame until its present, then plays it
// twice: first with the uniforms (projection matrices, bones, lights...) halfway between the previous frame and
// this one, presented; then as is, presented. Each uniform upload is paired with that of the previous frame by
// (program, slot, size, signature of the next draw, rank). Values that jump (plane cut, scrolling that
// wraps): kept as they are; too many jumps or too few pairings: no intermediate frame.
// A sync (direct GL call) or a frame that is too big makes the frame play directly, without interpolation.
static int ip_on, f_direct, prev_ok, fcur;
static int skip_large_enabled, skip_large_active;
static unsigned frame_words;
static unsigned ipshot, nframe;   // SH4_IPSHOT=N: captures ip<N>_a (real N-1), _b (intermediate), _c (real N)
void plat_shot_raw(const char *path);
static void ip_shot(char k) { char s[64]; snprintf(s, sizeof s, "ux0:data/sh4/shot_ip%u_%c.tga", ipshot, k); plat_shot_raw(s); }
static uint32_t *fb[2];
static unsigned fb_cap, fb_n;
#define UK_N 4096                      // keys per frame (power of 2)
#define U_MAX 2048                     // interpolable uniform uploads per frame
typedef struct { uint32_t key; unsigned gen; const float *v; unsigned n; } UK;
static UK uk[2][UK_N];
static unsigned uk_gen[2], uk_seq, uk_nu;
static const float *rep[U_MAX];        // interpolated values of upload #u (NULL: as is)

static inline uint32_t mix(uint32_t h, uint32_t v) { h ^= v; h *= 0x9e3779b1u; return h ^ h >> 15; }
static UK *uk_find(int t, uint32_t key) {
    for (unsigned i = key & (UK_N - 1);; i = (i + 1) & (UK_N - 1)) {
        UK *e = &uk[t][i];
        if (e->gen != uk_gen[t] || e->key == key) return e;
    }
}
static void go_direct(void);
// Halfway between p (previous frame) and c, per vec4, written in place into p (each upload of the previous frame is paired
// only once and its buffer is only rewritten after the present); NULL if nothing moves.
// unsafe-math: otherwise GCC emulates vcgtq_f32 with scalar compares + vmrs (~270 cycles/vec4 instead of ~30).
__attribute__((optimize("unsafe-math-optimizations")))
static const float *lerp_u(const float *c, float *p, unsigned n, unsigned *nchg, unsigned *nrej) {
    // Branch-free NEON (a float compare + jump costs ~20 cycles on A9): max deviation and max amplitude of the vec4,
    // jump if deviation > 0.35*amplitude + 1e-4 (keep c), otherwise average. Counters: lanes set to 1 within vectors.
    uint32x4_t vchg = vdupq_n_u32(0), vrej = vchg, vmov = vchg, one = vdupq_n_u32(1);
    for (unsigned i = 0; i < n * 4; i += 4) {
        __builtin_prefetch(c + i + 32);   // A9 has no hardware prefetch: limited by memory latency
        __builtin_prefetch(p + i + 32);
        float32x4_t x = vld1q_f32(c + i), y = vld1q_f32(p + i);
        float32x4_t d = vabdq_f32(x, y), m = vmaxq_f32(vabsq_f32(x), vabsq_f32(y));
        float32x2_t d2 = vpmax_f32(vget_low_f32(d), vget_high_f32(d)), m2 = vpmax_f32(vget_low_f32(m), vget_high_f32(m));
        d2 = vpmax_f32(d2, d2);
        m2 = vpmax_f32(m2, m2);
        float32x4_t dm = vcombine_f32(d2, d2), mm = vcombine_f32(m2, m2);
        uint32x4_t chg = vcgtq_f32(dm, vdupq_n_f32(0)), rej = vcgtq_f32(dm, vmlaq_n_f32(vdupq_n_f32(1e-4f), mm, 0.35f));
        vst1q_f32(p + i, vbslq_f32(rej, x, vmulq_n_f32(vaddq_f32(x, y), 0.5f)));
        vchg = vaddq_u32(vchg, vandq_u32(chg, one));
        vrej = vaddq_u32(vrej, vandq_u32(rej, one));
        vmov = vorrq_u32(vmov, vbicq_u32(chg, rej));
    }
    *nchg += vgetq_lane_u32(vchg, 0);
    *nrej += vgetq_lane_u32(vrej, 0);
    return vgetq_lane_u32(vmov, 0) ? p : NULL;
}
// Scan of the set-aside frame: keys of its uniform uploads, interpolated values. Incremental: the render thread scans what
// arrived while it waits for the game (otherwise ~6 ms in the hospital on the present's critical path).
static struct { unsigned off, np, nu, nins, nmatch, nchg, nrej, nmov; uint32_t prog, abuf, ebuf, a0, a0buf; struct { unsigned off, prog, ord; } pend[256]; } S;
static int ip_want = 1;   // adaptive interpolation (see frame_end)
static void scan_reset(void) { memset(&S, 0, sizeof S); uk_gen[fcur] = ++uk_seq; }
static void ip_scan(int last) {
    if (!ip_want) return;
    int cur = fcur, prv = cur ^ 1;
    const uint32_t *f = fb[cur];
    uint32_t t0 = NOW();
    for (;;) {
        int end = S.off == fb_n;
        if (end && !last) break;
        unsigned op = end ? OP_DRAW : f[S.off] >> 24, nw = end ? 1 : f[S.off] & 0xffffff;
        const uint32_t *a = f + S.off + 1;
        if (op == OP_USEPROG) S.prog = a[0];
        else if (op == OP_BINDBUF) { if (a[0] == GL_ARRAY_BUFFER) S.abuf = a[1]; else if (a[0] == GL_ELEMENT_ARRAY_BUFFER) S.ebuf = a[1]; }
        else if (op == OP_VAP && a[0] == 0) { S.a0 = a[5]; S.a0buf = S.abuf; }
        else if (op == OP_U4FV || op == OP_U4F) {
            if (S.nu < U_MAX) rep[S.nu] = NULL;
            st_nk++;
            if (S.np < 256) { S.pend[S.np].off = S.off; S.pend[S.np].prog = S.prog; S.pend[S.np++].ord = S.nu; }   // ponytail: beyond 256 per draw, not paired
            S.nu++;
        } else if (op == OP_DRAW) {   // pending uploads apply to this draw
            uint32_t sig = 0;
            if (!end) for (int i = 0; i < 9; i++) sig = mix(sig, a[i]);
            sig = mix(mix(mix(sig, S.a0), S.a0buf), S.ebuf);
            for (unsigned k = 0; k < S.np; k++) {
                const uint32_t *u = f + S.pend[k].off + 1;
                unsigned uop = f[S.pend[k].off] >> 24, n = uop == OP_U4FV ? u[1] : 1;
                const float *v = (const float *)(uop == OP_U4FV ? u + 2 : u + 1);
                uint32_t base = mix(mix(mix(sig, S.pend[k].prog), u[0]), n), key = base;
                UK *e = uk_find(cur, key);
                for (unsigned occ = 1; e->gen == uk_gen[cur] && occ < 64; occ++) e = uk_find(cur, key = mix(base, occ));
                unsigned ord = S.pend[k].ord;
                if (e->gen == uk_gen[cur] || ++S.nins > UK_N / 2) continue;
                *e = (UK){key, uk_gen[cur], v, n};
                if (!prev_ok || ord >= U_MAX) continue;
                UK *p = uk_find(prv, key);
                if (p->gen != uk_gen[prv] || p->n != n) continue;
                S.nmatch++;
                if ((rep[ord] = lerp_u(v, (float *)p->v, n, &S.nchg, &S.nrej))) S.nmov++;
                st_nl++; st_nv += n;
            }
            S.np = 0;
        }
        S.off += nw;
        if (end) break;
    }
    st_tp += NOW() - t0;
}
// End of the scan. True if the intermediate frame is worth it.
static int ip_prepare(void) {
    ip_scan(1);
    uk_nu = S.nu;
    if (!ip_want || !prev_ok || !S.nmov || S.nmatch * 2 < S.nu) return 0;
    if (S.nrej * 3 > S.nchg) { st_cut++; return 0; }   // coupure de plan
    return 1;
}
// Plays the set-aside frame; interp: interpolated uniforms, no movie upload or object deletion.
// SH4_QPROF: time of draws and clears only (timers cost ~1.6 us, too much to time everything); draws by index size
static uint32_t q_db_t[5], q_db_n[5], q_fb_t[16], q_fb_n[16];
static void exec_q(unsigned op, const uint32_t *a) {
    static uint32_t curfb, opn;
    if (qprof) { if (op == OP_BINDFB) curfb = a[1]; if (op == OP_SWAP) opn = 0; opn++; }
    if (!qprof || (op != OP_DRAW && op != OP_CLEAR)) { exec_inner(op, a); return; }
    uint32_t t0 = NOW();
    exec_inner(op, a);
    uint32_t d = NOW() - t0;
    q_opt[op & 63] += d;
    q_fb_t[curfb & 15] += d; q_fb_n[curfb & 15]++;
    if (op == OP_DRAW) { unsigned n = a[4], b = n <= 150 ? 0 : n <= 600 ? 1 : n <= 3000 ? 2 : n <= 20000 ? 3 : 4; q_db_t[b] += d; q_db_n[b]++; }
    if (d > 1500) rt_log("[qslow] op code %u (no %u, masque %x, fb %u) : %u us\n", op, opn, op == OP_CLEAR ? a[0] : 0, curfb, d);
}
// SH4_SPLIT=N (default 0: 64 dropped late draws (actors) after each flush, 0 = off): ends the GXM scene every N draws (glFlush) so the GPU starts before the
// end of recording; heavy hospital room: render thread 16.4 ms -> 8.3 ms per pass (N = 60-90; 40: 10.5; 20: 15.3)
static unsigned split_n, split_env = 0;
void q_set_split(int n) { split_n = n < 0 ? split_env : (unsigned)n; }   // n < 0: value of SH4_SPLIT (default 0)
static void exec(unsigned op, const uint32_t *a) {
    static unsigned nd;
    exec_q(op, a);
    if (!split_n) return;
    if (op == OP_BINDFB || op == OP_SWAP) nd = 0;
    else if (op == OP_DRAW && ++nd >= split_n) { nd = 0; glFlush(); }
}
static void play(int interp) {
    const uint32_t *f = fb[fcur];
    unsigned u = 0;
    for (unsigned off = 0, nw; off < fb_n; off += nw) {
        unsigned op = f[off] >> 24;
        const uint32_t *a = f + off + 1;
        nw = f[off] & 0xffffff;
        if (interp) {
            if (op == OP_U4FV || op == OP_U4F) {
                const float *r = u < U_MAX && u < uk_nu ? rep[u] : NULL;
                u++;
                if (r) { glUniform4fv((GLint)a[0], op == OP_U4FV ? (GLsizei)a[1] : 1, r); continue; }
            } else if (op == OP_TEX565 || op == OP_DELBUF || op == OP_DELTEX || op == OP_DELRB) continue;
        }
        exec(op, a);
    }
}
static void go_direct(void) {
    play(0);
    fb_n = 0;
    f_direct = 1;
    prev_ok = 0;
    scan_reset();
}
static void frame_end(void) {
    if (fb_n * 4 > st_fbmax) st_fbmax = fb_n * 4;
    uint32_t t0 = NOW(), t1;
    int ip = ip_prepare();
    nframe++;
    if (ip) {
        play(1);
        if (nframe == ipshot) ip_shot('b');
        t1 = NOW();
        vglSwapBuffers(GL_FALSE);
        st_swapx += NOW() - t1;
        st_ip++;
        if (gpufin) { t1 = NOW(); glFinish(); st_g1 += NOW() - t1; }
    }
    t1 = NOW();
    st_t1 += t1 - t0;
    uint32_t audit_ip_us = t1 - t0;
    play(0);
    if (ipshot && nframe + 1 >= ipshot && nframe <= ipshot) ip_shot(nframe == ipshot ? 'c' : 'a');
    exec(OP_SWAP, NULL);
    if (gpufin) { t0 = NOW(); glFinish(); st_g0 += NOW() - t0; }
    uint32_t treal = NOW() - t1;
    st_t0 += treal;
    if (g_audit_enabled && g_audit_frames) rt_log("[audit-pass] frame=%u real_us=%u intermediate=%d pair_and_ip_us=%u\n",
                              swaps_done, treal, ip, audit_ip_us);
    // Adaptive: if the game waited for the render thread (full queue, present in flight) more than 1 ms for 3 interpolated frames in a row,
    // the two passes do not fit in 33 ms: real frame only. Resumes when the render thread's idle time exceeds the cost
    // of one pass + pairing + 2 ms, for 30 frames in a row. The frame after a resume is not interpolated (no keys).
    static uint32_t idle0, wait0, tp0, tscan, tavg;   // tavg: smoothed pairing (an isolated spike must not block the resume)
    static unsigned bad, good, need = 30, ontime;   // need: healthy frames required before resuming (doubles on every cut, avoids flip-flopping)
    uint32_t idle = st_idle - idle0, wait = st_fullw + st_swapw - wait0;
    idle0 = st_idle;
    wait0 = st_fullw + st_swapw;
    prev_ok = ip_want;
    if (ip_want) {
        tscan = st_tp >= tp0 ? st_tp - tp0 : 0;   // st_tp is reset at every stats window
        tavg = (tavg * 7 + tscan) / 8;
        bad = ip && wait > 3500 ? bad + 1 : 0;
        if (ip && ++ontime >= 600) need = 30;
        if (bad >= 3) { ip_want = 0; bad = 0; ontime = 0; need = need < 480 ? need * 2 : 480; st_off++; }
    } else {
        st_noip++;
        tavg -= tavg / 8;
        good = idle > treal * 3 / 4 + tavg + 2000 ? good + 1 : (good > 4 ? good - 4 : 0);   // a late frame must not cancel everything
        if (good >= need) { ip_want = 1; good = 0; }
    }
    tp0 = st_tp;
    fcur ^= 1;
    fb_n = 0;
    scan_reset();
}

void q_dump_stall(void) {
    static unsigned last;
    if (st_recs != last) { last = st_recs; return; }
    rt_log("[stall] tail=%u whead=%u head=%u fb_pending=%d sync_req=%d c_sleep=%d fb_n=%u f_direct=%d render op=%u at %u, game in sync '%s' recs=%u\n",
           tail, whead, head, (int)fb_pending, (int)sync_req, (int)c_sleep, (unsigned)fb_n, (int)f_direct, dbg_rop, dbg_rt,
           dbg_why ? dbg_why : "-", st_recs);
}
static int render_thread(SceSize len, void *arg) {
    rt_cpu_profile_thread("render");
    (void)len; (void)arg;
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_1);
    unsigned t = 0;   // owner of `tail`
    for (;;) {
        unsigned h = head;
        if (t == h) {
            if (sync_req) {
                sync_req = 0;
                __sync_synchronize();
                if (fb_n) go_direct();
                __sync_synchronize();
                fb_pending = 0;
                continue;
            }
            if (ip_want && S.off < fb_n) { ip_scan(0); continue; }
            uint32_t t0 = NOW();
            dbg_rop = 255;
            c_sleep = 1;
            __sync_synchronize();
            if (t == head && !sync_req) sceKernelWaitSema(c_sema, 1, NULL);
            c_sleep = 0;
            st_idle += NOW() - t0;
            continue;
        }
        __sync_synchronize();
        while (t != h) {   // whole batch published, without re-reading `head`
            uint32_t hd = ring[t & rmask];
            unsigned nw = hd & 0xffffff, op = hd >> 24;
            dbg_rop = op; dbg_rt = t;
            if (!nw) {   // corrupt ring (should not happen): log and resync on the published head instead of looping forever
                rt_log("[q] zero-size record at %u (hd %08x) head %u whead %u: %08x %08x %08x\n", t, hd, h, whead, ring[(t + 1) & rmask], ring[(t + 2) & rmask], ring[(t + 3) & rmask]);
                t = h;
                break;
            }
            // When the previous frame exceeded the replay buffer, the current
            // frame is sent directly. Count its records so a smaller frame can
            // resume normal capture on the next present.
            if (skip_large_enabled && ip_on && op != OP_PAD && op != OP_SWAP) {
                if (frame_words <= UINT32_MAX - nw) frame_words += nw;
                else frame_words = UINT32_MAX;
            }
            int was_direct = f_direct;
            if (op == OP_PAD) ;
            else if (!ip_on || f_direct) {
                exec(op, &ring[(t & rmask) + 1]);
                if (op == OP_SWAP) f_direct = prev_ok = 0;
            } else if (op == OP_SWAP) {
                frame_end();
                __sync_synchronize();
                fb_pending = 0;
            } else if (fb_n + nw <= fb_cap) {   // set aside until the present
                memcpy(fb[fcur] + fb_n, &ring[t & rmask], nw * 4);
                fb_n += nw;
                fb_pending = 1;
            } else {   // frame too big: played as is
                st_ovf++;
                go_direct();
                exec(op, &ring[(t & rmask) + 1]);
                __sync_synchronize();
                fb_pending = 0;
            }
            if (skip_large_enabled && ip_on && op == OP_SWAP) {
                int large = frame_words > fb_cap;
                if (skip_large_active) {
                    st_large_skip++;
                    if (!large) st_large_false++;
                }
                frame_words = 0;
                skip_large_active = large;
                if (large) { f_direct = 1; prev_ok = 0; }
                else if (was_direct) scan_reset();
            }
            __sync_synchronize();
            t += nw;
            tail = t;
        }
    }
    return 0;
}

static void q_start(void) {
    const char *e = getenv("SH4_NORTHREAD");
    if (e && *e == '1') { q_on = 0; rt_log("[q] GL queue disabled (SH4_NORTHREAD)\n"); return; }
    e = getenv("SH4_INTERP");   // 60 frames/s by interpolation (default); 0: 30
    int ipw = !e || *e != '0';
    // With interpolation, the ring must absorb a whole frame (hospital ~830 KB) during the render thread's two passes.
    e = getenv("SH4_QRING_KB");
    unsigned kb = e ? (unsigned)atoi(e) : ipw ? 1024 : 512;
    if (kb < 64 || (kb & (kb - 1))) kb = 512;
    rw = kb * 256;
    rmask = rw - 1;
    maxrec = rw / 2 < 262144 ? rw / 2 : 262144;
    ring = malloc((size_t)rw * 4);
    c_sema = sceKernelCreateSema("sh4q", 0, 0, 0x10000, NULL);
    if (ipw) {
        e = getenv("SH4_QFRAME_KB");
        fb_cap = (e ? (unsigned)atoi(e) : 1536) * 256;
        fb[0] = malloc(fb_cap * 4);
        fb[1] = malloc(fb_cap * 4);
        ip_on = fb[0] && fb[1];
        e = getenv("SH4_INTERP_SKIP_LARGE");
        skip_large_enabled = e && !strcmp(e, "1");
        scan_reset();
        gpufin = getenv("SH4_GPUFIN") != NULL;
        if (getenv("SH4_IPSHOT")) ipshot = (unsigned)atoi(getenv("SH4_IPSHOT"));
    }
    qprof = getenv("SH4_QPROF") != NULL;
    if (getenv("SH4_SPLIT")) split_env = (unsigned)atoi(getenv("SH4_SPLIT"));
    split_n = split_env;
    SceUID th = ring && c_sema >= 0 ? sceKernelCreateThread("sh4gl", render_thread, sceKernelGetThreadCurrentPriority(), 0x40000, 0, 0, NULL) : -1;
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) { q_on = 0; rt_log("[q] cannot start the render thread (%08x)\n", (unsigned)th); return; }
    g_owner = sceKernelGetThreadId();
    q_on = 1;
    rt_log("[q] GL queue active: %u KB ring, render thread on core 1, interpolation %s\n", kb, ip_on ? "yes" : "no");
}

#define QENTER(...) if (q_on != 1) { if (q_on < 0) q_start(); if (q_on != 1) { __VA_ARGS__; return; } }

void q_tex565(GLint level, GLsizei w, GLsizei h, const void *px, volatile int *busy) {
    QENTER(glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, w, h, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, px); *busy = 0);
    uint32_t a[] = {(uint32_t)level, (uint32_t)w, (uint32_t)h, (uint32_t)(uintptr_t)px, (uint32_t)(uintptr_t)busy};
    emit(OP_TEX565, a, 5);
}

void q_swap(void) {
    QENTER(vglSwapBuffers(GL_FALSE));
    uint32_t t0 = NOW();
    while (swaps_enq != swaps_done) sceKernelDelayThread(100);   // at most one present in flight
    st_swapw += NOW() - t0;
    inv_gen++;
    emit(OP_SWAP, NULL, 0);
    swaps_enq++;
    flush();
}

// ---------- enveloppes ----------
static struct { uint8_t en, vbo, norm, kn; unsigned g; GLuint buf; GLint size; GLenum type; GLsizei stride; const void *ptr; } at[16];
static GLuint arr_buf, ele_buf;

#define QI(NAME, OP, PROTO, CALL, ...) \
    void q_##NAME PROTO { QENTER(NAME CALL); uint32_t a[] = {__VA_ARGS__}; emit(OP, a, sizeof a / 4); }
// GL state shadow on the game side: a call that changes nothing is not recorded. Anything that may change the state behind our back
// (target change, clear, direct call after a flush, object deletion, present) invalidates the shadow (inv_gen).
#define QX(NAME, OP, PROTO, CALL, ...) \
    void q_##NAME PROTO { QENTER(NAME CALL); inv_gen++; uint32_t a[] = {__VA_ARGS__}; emit(OP, a, sizeof a / 4); }
#define QD(NAME, OP, PROTO, CALL, ...) \
    void q_##NAME PROTO { QENTER(NAME CALL); static uint32_t q_s[5]; static unsigned q_g; uint32_t a[] = {__VA_ARGS__}; \
        if (q_g == inv_gen && !memcmp(q_s, a, sizeof a)) return; \
        q_g = inv_gen; memcpy(q_s, a, sizeof a); emit(OP, a, sizeof a / 4); }

static struct { uint32_t cap; unsigned g; uint8_t on; } ce[64];
static int cap_same(GLenum c, int on) {
    unsigned i = (c ^ c >> 7) & 63;
    if (ce[i].cap == c && ce[i].g == inv_gen && ce[i].on == on) return 1;
    ce[i].cap = c; ce[i].g = inv_gen; ce[i].on = (uint8_t)on;
    return 0;
}
void q_glEnable(GLenum c) { QENTER(glEnable(c)); if (cap_same(c, 1)) return; emit(OP_ENABLE, &c, 1); }
void q_glDisable(GLenum c) { QENTER(glDisable(c)); if (cap_same(c, 0)) return; emit(OP_DISABLE, &c, 1); }

static GLenum cur_unit; static int unit_known;            // active texture unit (only changes through glActiveTexture)
static uint32_t bt[8]; static unsigned bt_g[8];              // bound 2D texture per unit
void q_glActiveTexture(GLenum t) {
    QENTER(glActiveTexture(t));
    if (unit_known && cur_unit == t) return;
    unit_known = 1; cur_unit = t;
    emit(OP_ACTIVETEX, &t, 1);
}
void q_glBindTexture(GLenum t, GLuint n) {
    QENTER(glBindTexture(t, n));
    unsigned u = cur_unit - GL_TEXTURE0;
    if (t == GL_TEXTURE_2D && unit_known && u < 8) {
        if (bt_g[u] == inv_gen && bt[u] == n) return;
        bt_g[u] = inv_gen; bt[u] = n;
    }
    uint32_t a[] = {t, n};
    emit(OP_BINDTEX, a, 2);
}
static struct { uint32_t key, val; unsigned g; } tp[4096];
void q_glTexParameteri(GLenum t, GLenum n, GLint v) {
    QENTER(glTexParameteri(t, n, v));
    unsigned u = cur_unit - GL_TEXTURE0, pi = n - 0x2800;   // MAG, MIN, WRAP_S, WRAP_T
    if (t == GL_TEXTURE_2D && pi < 4 && unit_known && u < 8 && bt_g[u] == inv_gen) {
        uint32_t key = bt[u] * 4 + pi, i = (key * 2654435761u) >> 20;
        if (tp[i].key == key && tp[i].g == inv_gen && tp[i].val == (uint32_t)v) return;
        tp[i].key = key; tp[i].g = inv_gen; tp[i].val = (uint32_t)v;
    }
    uint32_t a[] = {t, n, (uint32_t)v};
    emit(OP_TEXPARAM, a, 3);
}

static GLuint cur_prog;
void q_glUseProgram(GLuint p) { QENTER(glUseProgram(p)); cur_prog = p; emit(OP_USEPROG, &p, 1); }
static struct { uint32_t program; GLint location; uint32_t w[4]; unsigned g; } un[1024];
static int uni_same(GLint l, const uint32_t *w, unsigned n) {   // true if the current program already has this value
    if (!cur_prog) return 0;
    uint32_t key = cur_prog << 12 ^ (uint32_t)l, i = (key * 2654435761u) >> 22;
    // The XOR key selects a bucket; it cannot identify the pair by itself.
    if (un[i].program == cur_prog && un[i].location == l && un[i].g == inv_gen && !memcmp(un[i].w, w, n * 4)) return 1;
    un[i].program = cur_prog; un[i].location = l; un[i].g = inv_gen; memcpy(un[i].w, w, n * 4);
    return 0;
}
void q_glUniform4f(GLint l, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    QENTER(glUniform4f(l, x, y, z, w));
    uint32_t a[] = {(uint32_t)l, f2u(x), f2u(y), f2u(z), f2u(w)};
    if (uni_same(l, a + 1, 4)) return;
    emit(OP_U4F, a, 5);
}
void q_glUniform1f(GLint l, GLfloat v) {
    QENTER(glUniform1f(l, v));
    uint32_t a[] = {(uint32_t)l, f2u(v)};
    if (uni_same(l, a + 1, 1)) return;
    emit(OP_U1F, a, 2);
}
void q_glUniform1i(GLint l, GLint v) {
    QENTER(glUniform1i(l, v));
    uint32_t a[] = {(uint32_t)l, (uint32_t)v};
    if (uni_same(l, a + 1, 1)) return;
    emit(OP_U1I, a, 2);
}

QX(glBindFramebuffer, OP_BINDFB, (GLenum t, GLuint f), (t, f), t, f)
QD(glViewport, OP_VIEWPORT, (GLint x, GLint y, GLsizei w, GLsizei h), (x, y, w, h), (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h)
QX(glFramebufferTexture2D, OP_FBTEX, (GLenum t, GLenum at_, GLenum tt, GLuint tex, GLint lv), (t, at_, tt, tex, lv), t, at_, tt, tex, (uint32_t)lv)
QD(glColorMask, OP_COLORMASK, (GLboolean r, GLboolean g, GLboolean b, GLboolean a_), (r, g, b, a_), r, g, b, a_)
QX(glClear, OP_CLEAR, (GLbitfield m), (m), m)
QI(glClearColor, OP_CLEARCOL, (GLfloat r, GLfloat g, GLfloat b, GLfloat a_), (r, g, b, a_), f2u(r), f2u(g), f2u(b), f2u(a_))
QI(glClearDepth, OP_CLEARDEPTH, (GLdouble d), (d), f2u((float)d))
QI(glClearStencil, OP_CLEARSTENCIL, (GLint s), (s), (uint32_t)s)
QD(glDepthRange, OP_DEPTHRANGE, (GLdouble n, GLdouble f), (n, f), f2u((float)n), f2u((float)f))
QD(glDepthFunc, OP_DEPTHFUNC, (GLenum f), (f), f)
QD(glDepthMask, OP_DEPTHMASK, (GLboolean f), (f), f)
QD(glStencilMask, OP_STENCILMASK, (GLuint m), (m), m)
QD(glStencilOp, OP_STENCILOP, (GLenum a_, GLenum b, GLenum c), (a_, b, c), a_, b, c)
QD(glStencilFunc, OP_STENCILFUNC, (GLenum f, GLint r, GLuint m), (f, r, m), f, (uint32_t)r, m)
QD(glScissor, OP_SCISSOR, (GLint x, GLint y, GLsizei w, GLsizei h), (x, y, w, h), (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h)
QD(glPolygonOffset, OP_POLYOFF, (GLfloat f, GLfloat u), (f, u), f2u(f), f2u(u))
QD(glFrontFace, OP_FRONTFACE, (GLenum m), (m), m)
QD(glCullFace, OP_CULLFACE, (GLenum m), (m), m)
QD(glBlendFunc, OP_BLENDFUNC, (GLenum s, GLenum d), (s, d), s, d)
QD(glBlendEquation, OP_BLENDEQ, (GLenum m), (m), m)
QX(glBindRenderbuffer, OP_BINDRB, (GLenum t, GLuint r), (t, r), t, r)
QI(glRenderbufferStorage, OP_RBSTORAGE, (GLenum t, GLenum f, GLsizei w, GLsizei h), (t, f, w, h), t, f, (uint32_t)w, (uint32_t)h)
QX(glFramebufferRenderbuffer, OP_FBRB, (GLenum t, GLenum a_, GLenum rt, GLuint r), (t, a_, rt, r), t, a_, rt, r)

void q_glBindBuffer(GLenum t, GLuint b) {
    QENTER(glBindBuffer(t, b));
    if (t == GL_ARRAY_BUFFER) arr_buf = b;
    else if (t == GL_ELEMENT_ARRAY_BUFFER) ele_buf = b;
    uint32_t a[] = {t, b};
    emit(OP_BINDBUF, a, 2);
}
void q_glEnableVertexAttribArray(GLuint i) {
    QENTER(glEnableVertexAttribArray(i));
    if (i < 16) at[i].en = 1;
    emit(OP_ENVA, &i, 1);
}
void q_glDisableVertexAttribArray(GLuint i) {
    QENTER(glDisableVertexAttribArray(i));
    if (i < 16) at[i].en = 0;
    emit(OP_DISVA, &i, 1);
}
// Client pointer: its contents are only copied at draw time (its size depends on the vertex range); with a bound VBO it is an offset.
void q_glVertexAttribPointer(GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void *p) {
    QENTER(glVertexAttribPointer(i, size, type, norm, stride, p));
    if (i < 16) {
        if (arr_buf && at[i].kn && at[i].g == inv_gen && at[i].buf == arr_buf && at[i].size == size && at[i].type == type &&
            at[i].norm == norm && at[i].stride == stride && at[i].ptr == p) return;   // already set as is
        at[i].vbo = arr_buf != 0; at[i].size = size; at[i].type = type; at[i].norm = norm; at[i].stride = stride; at[i].ptr = p;
        at[i].kn = 1; at[i].g = inv_gen; at[i].buf = arr_buf;
    }
    if (arr_buf || i >= 16) {
        uint32_t a[] = {i, (uint32_t)size, type, norm, (uint32_t)stride, (uint32_t)(uintptr_t)p};
        emit(OP_VAP, a, 6);
    }
}
void q_glUniform4fv(GLint l, GLsizei n, const GLfloat *v) {
    QENTER(glUniform4fv(l, n, v));
    uint32_t a[] = {(uint32_t)l, (uint32_t)n};
    if (!emit_blob(OP_U4FV, a, 2, v, (unsigned)n * 16)) { q_sync_n("glUniform4fv big"); glUniform4fv(l, n, v); }
}
void q_glBufferData(GLenum t, GLsizei size, const GLvoid *data, GLenum usage) {
    QENTER(glBufferData(t, size, data, usage));
    uint32_t a[] = {t, (uint32_t)size, usage, data != NULL};
    if (!data) emit(OP_BUFDATA, a, 4);
    else if (!emit_blob(OP_BUFDATA, a, 4, data, (unsigned)size)) { st_big++; q_sync_n("glBufferData big"); glBufferData(t, size, data, usage); }
}
void q_glBufferSubData(GLenum t, GLintptr off, GLsizeiptr size, const void *data) {
    QENTER(glBufferSubData(t, off, size, data));
    uint32_t a[] = {t, (uint32_t)off, (uint32_t)size};
    if (!emit_blob(OP_BUFSUB, a, 3, data, (unsigned)size)) { st_big++; q_sync_n("glBufferSubData big"); glBufferSubData(t, off, size, data); }
}
#define QDEL(NAME, OP) \
    void q_##NAME(GLsizei n, const GLuint *ids) { \
        QENTER(NAME(n, ids)); \
        inv_gen++; \
        uint32_t a[] = {(uint32_t)n}; \
        if (!emit_blob(OP, a, 1, ids, (unsigned)n * 4)) { q_sync_n(#NAME); NAME(n, ids); } \
    }
QDEL(glDeleteBuffers, OP_DELBUF)
QDEL(glDeleteTextures, OP_DELTEX)
QDEL(glDeleteRenderbuffers, OP_DELRB)

// ---------- draw: copy of client arrays (vertices) and indices into the ring ----------
static unsigned type_bytes(GLenum t) {
    switch (t) { case GL_FLOAT: case GL_FIXED: case GL_UNSIGNED_INT: case GL_INT: return 4; case GL_SHORT: case GL_UNSIGNED_SHORT: return 2; default: return 1; }
}
static void q_draw(int kind, GLenum mode, GLint a, GLuint b, GLsizei count, GLenum type, const void *ind) {
    uint32_t v0 = kind == 0 ? (uint32_t)a : a, v1 = kind == 0 ? (uint32_t)a + (uint32_t)count - 1 : b;
    unsigned ibytes = kind == 1 ? (unsigned)count * type_bytes(type) : 0;
    struct { uintptr_t min, end; unsigned stride, off, bytes; } g[16];
    CA ca[16];
    uint8_t gi[16];
    int ng = 0, nca = 0;
    for (int i = 0; i < 16; i++) {
        if (!at[i].en || at[i].vbo || !at[i].ptr) continue;
        unsigned eb = (unsigned)at[i].size * type_bytes(at[i].type), st = at[i].stride ? (unsigned)at[i].stride : eb;
        uintptr_t p = (uintptr_t)at[i].ptr;
        int j = 0;
        for (; j < ng; j++) if (g[j].stride == st && p + st > g[j].min && p < g[j].min + st) break;
        if (j == ng) { g[ng].min = p; g[ng].end = p + eb; g[ng].stride = st; ng++; }
        else { if (p < g[j].min) g[j].min = p; if (p + eb > g[j].end) g[j].end = p + eb; }
        ca[nca] = (CA){(GLuint)i, at[i].size, at[i].type, at[i].norm, at[i].stride, p};
        gi[nca++] = (uint8_t)j;
    }
    if (kind == 0 && count <= 0) return;
    unsigned dw = (ibytes + 3) / 4, nw;
    for (int j = 0; j < ng; j++) {   // useful range of a group: [min + v0*stride, end + v1*stride), original alignment kept
        unsigned pad = (unsigned)(g[j].min + (uintptr_t)v0 * g[j].stride) & 3;
        g[j].off = dw * 4 + pad;
        g[j].bytes = (v1 - v0) * g[j].stride + (unsigned)(g[j].end - g[j].min);
        dw += (pad + g[j].bytes + 3) / 4;
    }
    nw = 1 + 9 + (unsigned)nca * 6 + dw;
    if (nw > maxrec) {   // too big: queue flushed, original pointers
        st_big++;
        { static int lg; if (lg++ < 6) rt_log("[q] big draw: %u KB (mode %u, %d indices, v %u..%u, %d client arrays)\n", nw * 4 / 1024, mode, count, v0, v1, nca); }
        q_sync_n("big draw");
        for (int i = 0; i < nca; i++) ca[i].ptr = (uintptr_t)at[ca[i].idx].ptr;
        draw_exec(kind, mode, a, b, count, type, ind, arr_buf, ca, nca);
        return;
    }
    uint32_t *r = rec_begin(OP_DRAW, nw);
    r[0] = (uint32_t)kind; r[1] = mode; r[2] = (uint32_t)a; r[3] = b; r[4] = (uint32_t)count; r[5] = type; r[6] = ibytes;
    r[7] = (uint32_t)nca; r[8] = arr_buf;
    uint8_t *data = (uint8_t *)(r + 9 + nca * 6);
    if (ibytes) memcpy(data, ind, ibytes);
    for (int j = 0; j < ng; j++)
        memcpy(data + g[j].off, (const void *)(g[j].min + (uintptr_t)v0 * g[j].stride), g[j].bytes);
    for (int i = 0; i < nca; i++) {
        uint32_t *p = r + 9 + i * 6;
        int j = gi[i];
        uintptr_t gdata = g[j].off;   // offset of the group's data relative to the start of the data
        p[0] = ca[i].idx; p[1] = (uint32_t)ca[i].size; p[2] = ca[i].type; p[3] = ca[i].norm; p[4] = (uint32_t)ca[i].stride;
        p[5] = (uint32_t)(gdata + (ca[i].ptr - g[j].min) - (uintptr_t)v0 * g[j].stride);
    }
    rec_commit(nw);
}
void q_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    QENTER(glDrawArrays(mode, first, count));
    q_draw(0, mode, first, 0, count, 0, NULL);
}
void q_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *ind) {
    QENTER(glDrawRangeElements(mode, start, end, count, type, ind));
    q_draw(ele_buf ? 2 : 1, mode, (GLint)start, end, count, type, ind);
}

void q_report(double frames) {
    rt_cpu_profile_report(frames);
    static uint32_t last_t;
    static uint32_t p_sync, p_swapw, p_fullw, p_idle, p_swapx;
    static unsigned p_nsync, p_recs, p_big;
    uint32_t now = NOW();
    if (q_on == 1 && last_t && frames > 0) {
        double wall = (double)(now - last_t) / 1000.0 / frames, k = 1.0 / 1000.0 / frames;
        char why[200] = "";
        for (int i = 0; i < 24 && why_t[i].n; i++) {
            size_t l = strlen(why);
            if (why_t[i].c && l < sizeof why - 40) snprintf(why + l, sizeof why - l, " %s:%.1f", why_t[i].n + 2, why_t[i].c / frames);
            why_t[i].c = 0;
        }
        rt_log("[q] render active %.2f ms/frame (swap %.2f) | game waits: sync %.2f swap %.2f full %.2f ms | %.0f recs %.1f sync/frame, max queue %u KB, big %u |%s\n",
               wall - (st_idle - p_idle) * k, (st_swapx - p_swapx) * k, (st_sync - p_sync) * k, (st_swapw - p_swapw) * k,
               (st_fullw - p_fullw) * k, (st_recs - p_recs) / frames, (st_nsync - p_nsync) / frames, st_maxfill * 4 / 1024, st_big - p_big, why);
        static const char *opn[] = {"pad","enable","disable","texparam","bindfb","bindtex","viewport","u4f","fbtex","colormask","clear","activetex","vap","u4fv","u1i","u1f","useprog","enva","disva","draw","clearcol","cleardepth","clearstencil","depthrange","depthfunc","depthmask","stencilmask","stencilop","stencilfunc","scissor","polyoff","frontface","cullface","blendfunc","blendeq","bindbuf","bufdata","bufsub","delbuf","deltex","delrb","bindrb","rbstorage","fbrb","swap"};
        char ops[300] = "";
        for (int i = 1; i < 45; i++) { double v = (st_op[i] - p_op[i]) / frames; size_t l = strlen(ops); if (v >= 0.5 && l < sizeof ops - 30) snprintf(ops + l, sizeof ops - l, " %s:%.0f", opn[i], v); }
        rt_log("[q] recs/frame:%s\n", ops);
        if (ip_on) rt_log("[q] interpolation: %u intermediate frames, %u cuts, %u too big, %u without (%u stops), max frame %u KB; intermediate pass %.2f ms, pairing %.2f ms (%.0f uploads, %.0f paired, %.0f vec4), real %.2f ms (GPU %.2f / %.2f)\n", st_ip, st_cut, st_ovf, st_noip, st_off, st_fbmax / 1024, st_t1 * k, st_tp * k, st_nk * k * 1000, st_nl * k * 1000, st_nv * k * 1000, st_t0 * k, st_g1 * k, st_g0 * k);
        if (skip_large_enabled) rt_log("[q] large-frame prediction: %u direct frames, %u smaller than cap\n", st_large_skip, st_large_false);
        if (qprof) {
            char ot[300] = "";
            for (int i = 1; i < 45; i++) { size_t l = strlen(ot); if (q_opt[i] * k >= 0.05 && l < sizeof ot - 30) snprintf(ot + l, sizeof ot - l, " %s:%.2f", opn[i], q_opt[i] * k); }
            rt_log("[q] ms per operation (real pass, with timers):%s\n", ot);
            memset(q_opt, 0, sizeof q_opt);
            rt_log("[q] draws by index count (<=150 / <=600 / <=3000 / <=20000 / more): %.0f x %.1f us, %.0f x %.1f us, %.0f x %.1f us, %.0f x %.1f us, %.0f x %.1f us\n",
                   q_db_n[0] / frames, q_db_n[0] ? (double)q_db_t[0] / q_db_n[0] : 0, q_db_n[1] / frames, q_db_n[1] ? (double)q_db_t[1] / q_db_n[1] : 0,
                   q_db_n[2] / frames, q_db_n[2] ? (double)q_db_t[2] / q_db_n[2] : 0, q_db_n[3] / frames, q_db_n[3] ? (double)q_db_t[3] / q_db_n[3] : 0,
                   q_db_n[4] / frames, q_db_n[4] ? (double)q_db_t[4] / q_db_n[4] : 0);
            memset(q_db_t, 0, sizeof q_db_t); memset(q_db_n, 0, sizeof q_db_n);
            { char fs[300] = ""; for (int i = 0; i < 16; i++) if (q_fb_n[i]) { size_t l = strlen(fs); snprintf(fs + l, sizeof fs - l, " fb%d:%.1f/%.2fms", i, q_fb_n[i] / frames, q_fb_t[i] * k); } rt_log("[q] per target (ops/frame, ms):%s\n", fs); memset(q_fb_t, 0, sizeof q_fb_t); memset(q_fb_n, 0, sizeof q_fb_n); }
        }
        st_ip = st_cut = st_ovf = st_fbmax = st_noip = st_off = st_large_skip = st_large_false = 0;
        st_t1 = st_t0 = st_g1 = st_g0 = st_tp = st_nl = st_nv = st_nk = 0;
        st_maxfill = 0;
    }
    last_t = now;
    p_sync = st_sync; p_swapw = st_swapw; p_fullw = st_fullw; p_idle = st_idle; p_swapx = st_swapx;
    p_nsync = st_nsync; p_recs = st_recs; p_big = st_big;
    memcpy(p_op, st_op, sizeof p_op);
}
#endif
