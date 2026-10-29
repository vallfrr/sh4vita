// Runtime core: guest memory, exe loading, dispatch, x87/CPUID helpers.
#include "rt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "log_rotate.h"
#include <SDL2/SDL_mutex.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_timer.h>

uint8_t *g_mem;
uint32_t g_mem_size;
char g_game_dir[512];
CPU *g_main_cpu;

extern const struct { uint32_t a; gfn f; } x86_functab[];
extern const unsigned x86_functab_n;
extern const struct { uint32_t slot; gfn f; const char *name; } x86_iat[];
extern const unsigned x86_iat_n;
extern const uint32_t x86_entry;

// ---------- journal / erreurs ----------
static FILE *g_logf;
static SDL_mutex *g_logmtx;
static int g_log_init_done;

// Log: rt_log only queues the text; on Vita a thread writes and fflushes (an fflush on ux0 blocks 10-100 ms: visible stutter).
static SDL_mutex *g_drainmtx;
static char *g_lq;
static size_t g_lqn, g_lqcap;
void rt_log_flush(void) {
    if (!g_logmtx) return;
    SDL_LockMutex(g_drainmtx);
    SDL_LockMutex(g_logmtx);
    char *b = g_lq;
    size_t n = g_lqn;
    g_lq = NULL; g_lqn = g_lqcap = 0;
    SDL_UnlockMutex(g_logmtx);
    if (b && g_logf) { fwrite(b, 1, n, g_logf); fflush(g_logf); }
    free(b);
    SDL_UnlockMutex(g_drainmtx);
}
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
static int log_thread(void *arg) {
    (void)arg;
    int cpu_registered=rt_cpu_profile_thread("log");
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_2);
    for (;;) { if (!cpu_registered) cpu_registered=rt_cpu_profile_thread("log"); SDL_Delay(100); rt_log_flush(); }
    return 0;
}
#endif
void rt_log(const char *fmt, ...) {
    va_list ap;
    #ifndef SH4_LOG_PATH
#define SH4_LOG_PATH "sh4log.txt"
#endif
    if (!g_logmtx) { g_drainmtx = SDL_CreateMutex(); g_logmtx = SDL_CreateMutex(); }
    if (!g_log_init_done) {
        g_log_init_done = 1;
        /* Preserve previous launches, including a log from a crashed run. */
        int rotated = sh4_log_rotate(SH4_LOG_PATH);
        g_logf = fopen(SH4_LOG_PATH, rotated ? "w" : "a");
        if (g_logf) atexit(rt_log_flush);
#ifdef __vita__
        if (g_logf) SDL_CreateThread(log_thread, "sh4log", NULL);
#endif
    }
    char tmp[512], *msg = tmp, *big = NULL;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof tmp && (big = malloc((size_t)n + 1))) {
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        msg = big;
    } else if ((size_t)n >= sizeof tmp) n = (int)sizeof tmp - 1;
#ifndef __vita__
    fputs(msg, stderr);
#endif
    if (!g_logf) { free(big); return; }
    SDL_LockMutex(g_logmtx);
    if (g_lqn + (size_t)n > g_lqcap) {
        g_lqcap = (g_lqn + (size_t)n) * 2 + 4096;
        g_lq = realloc(g_lq, g_lqcap);
    }
    if (g_lq) { memcpy(g_lq + g_lqn, msg, (size_t)n); g_lqn += (size_t)n; }
    SDL_UnlockMutex(g_logmtx);
    free(big);
#ifndef __vita__
    rt_log_flush();
#endif
}

void rt_platform_fatal(const char *msg);

_Noreturn void rt_fatal(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    rt_log("FATAL: %s\n", buf);
    rt_log_flush();
    rt_platform_fatal(buf);
    exit(1);
}

#ifdef SH4_TRACE_CALLS
void rt_calltrace_dump(void);
#endif
static void dump(CPU *c) {
    rt_log("  eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x ftop=%u\n",
           c->eax, c->ecx, c->edx, c->ebx, c->esp, c->ebp, c->esi, c->edi, c->ftop);
    uint8_t *mb = g_mem;
    for (int i = 0; i < 8 && c->esp + 4 * i < g_mem_size; i++) rt_log("  [esp+%02x]=%08x\n", 4 * i, R32(c->esp + 4 * i));
#ifdef SH4_TRACE_CALLS
    rt_calltrace_dump();
#endif
    // Probable call chain: stack words that look like return addresses into the game code.
    for (int i = 8; i < 400 && c->esp + 4 * i < g_mem_size; i++) {
        uint32_t v = R32(c->esp + 4 * i);
        if (v >= 0x401000 && v < 0x5c0000 && (R8(v - 5) == 0xe8 || R8(v - 2) == 0xff || R8(v - 3) == 0xff || R8(v - 6) == 0xff)) rt_log("  ret? [esp+%03x]=%08x\n", 4 * i, v);
    }
}

_Noreturn void rt_unimpl(CPU *c, uint32_t pc, const char *what) {
    dump(c);
    rt_fatal("unhandled instruction '%s' at 0x%08x", what, pc);
}

_Noreturn void rt_bad_jump(CPU *c, uint32_t pc, uint32_t target) {
    dump(c);
    rt_fatal("jump to unknown address 0x%08x (from 0x%08x)", target, pc);
}

_Noreturn void rt_div0(CPU *c, uint32_t pc) {
    dump(c);
    rt_fatal("division by zero at 0x%08x", pc);
}

// ---------- host functions with synthetic address ----------
#define MAX_HOSTFN 4096
static gfn hostfn[MAX_HOSTFN];
static const char *hostfn_name[MAX_HOSTFN];
static unsigned hostfn_n;

uint32_t rt_host_addr(gfn f, const char *name) {
    for (unsigned i = 0; i < hostfn_n; i++)
        if (hostfn[i] == f) return G_HOSTFN_BASE + 16u * i;
    if (hostfn_n == MAX_HOSTFN) rt_fatal("too many host functions");
    hostfn[hostfn_n] = f;
    hostfn_name[hostfn_n] = name;
    return G_HOSTFN_BASE + 16u * hostfn_n++;
}

gfn rt_lookup(uint32_t t) {
    if (t >= G_HOSTFN_BASE) {
        uint32_t i = (t - G_HOSTFN_BASE) / 16u;
        return i < hostfn_n && (t & 15u) == 0 ? hostfn[i] : NULL;
    }
    unsigned lo = 0, hi = x86_functab_n;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (x86_functab[mid].a < t) lo = mid + 1; else hi = mid;
    }
    return lo < x86_functab_n && x86_functab[lo].a == t ? x86_functab[lo].f : NULL;
}

typedef struct JmpSlot { CPU *c; uint32_t ret; jmp_buf jb; struct JmpSlot *next; } JmpSlot;
static JmpSlot *g_jmps;

jmp_buf *rt_setjmp_slot(CPU *c, uint32_t ret) {
    // ponytail: a single slot per (thread, site); enough as long as a setjmp site is not recursive.
    for (JmpSlot *s = g_jmps; s; s = s->next)
        if (s->c == c && s->ret == ret) return &s->jb;
    JmpSlot *s = calloc(1, sizeof *s);
    s->c = c;
    s->ret = ret;
    s->next = g_jmps;
    g_jmps = s;
    return &s->jb;
}

#ifdef SH4_PROFILE
void *rt_prof_host(void *f);
#endif
// Direct-mapped cache in front of rt_lookup's binary search (9462 entries, ~13 unpredictable jumps): the main thread makes thousands
// of indirect calls per frame (D3D methods, Win32 API, virtuals). Single writer (main thread); other threads search.
#define LC_N 4096
static struct { uint32_t a; gfn f; } lc[LC_N];
void rt_call(CPU *c, uint32_t t) {
    gfn f;
    if (c == g_main_cpu) {
        __typeof__(lc[0]) *e = &lc[(t ^ (t >> 4) ^ (t >> 13)) & (LC_N - 1)];
        if (e->a == t) f = e->f;
        else if ((f = rt_lookup(t))) { e->a = t; e->f = f; }
    } else f = rt_lookup(t);
    if (!f) {
        for (JmpSlot *s = g_jmps; s; s = s->next)
            if (s->c == c && s->ret == t) longjmp(s->jb, 1);
        dump(c);
        rt_fatal("indirect call to 0x%08x: no known function", t);
    }
#ifdef SH4_PROFILE
    void *pv = rt_prof_host(t >= G_HOSTFN_BASE ? (void *)f : NULL);   // profile: attributes the time spent in a host function (D3D, Win32...) to that function
    f(c);
    rt_prof_host(pv);
#else
    f(c);
#endif
}

uint32_t rt_guest_call(CPU *c, uint32_t fn, int nargs, ...) {
    HOSTFN;
    uint32_t saved = c->esp, args[16];
    va_list ap;
    va_start(ap, nargs);
    for (int i = 0; i < nargs; i++) args[i] = va_arg(ap, uint32_t);
    va_end(ap);
    for (int i = nargs - 1; i >= 0; i--) { c->esp -= 4; W32(c->esp, args[i]); }
    c->esp -= 4;
    W32(c->esp, 0xFFFFFFF0u);
    rt_call(c, fn);
    c->esp = saved;
    return c->eax;
}

// ---------- memory and loading ----------
int rt_init_mem(void *mem, uint32_t mem_size) {
    g_mem = mem;
    memset(g_mem, 0, mem_size);
    g_mem_size = mem_size;
    gheap_init(G_HEAP_BASE, mem_size - G_HEAP_BASE);
    return 0;
}

int rt_init(uint32_t mem_size) {
    void *m = malloc(mem_size);
    return m ? rt_init_mem(m, mem_size) : -1;
}

int rt_load_exe(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(n);
    if (!d || fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); return -2; }
    fclose(f);
    uint32_t pe = *(uint32_t *)(d + 0x3c);
    uint16_t nsec = *(uint16_t *)(d + pe + 6), optsz = *(uint16_t *)(d + pe + 20);
    uint32_t base = *(uint32_t *)(d + pe + 24 + 28), hdrsz = *(uint32_t *)(d + pe + 24 + 60);
    if (base != 0x400000) { free(d); return -3; }
    memcpy(g_mem + base, d, hdrsz);
    uint8_t *sec = d + pe + 24 + optsz;
    for (unsigned i = 0; i < nsec; i++, sec += 40) {
        uint32_t vsz = *(uint32_t *)(sec + 8), va = *(uint32_t *)(sec + 12);
        uint32_t rsz = *(uint32_t *)(sec + 16), raw = *(uint32_t *)(sec + 20);
        memcpy(g_mem + base + va, d + raw, rsz < vsz || !vsz ? rsz : vsz);
    }
    free(d);
    rt_options_language_default();
    return 0;
}

// SH4_RES=i: game resolution preset (cf. 0x4148e0: 0=640x480, 1=1024x768 by default, 2=1366x768...).
// SH4_RES=WxH: free size (e.g. 960x544); the game sets its field of view from the backbuffer size (native 16:9),
// the menu option stays on the 16:9 preset (2) so that 0x4148e0 does not reapply it.
// Called at CreateWindowExA: WinMain has just reset 1024x768 (0x414150) and the D3D device does not exist yet.
static int res_index(void) {
    const char *e = getenv("SH4_RES");
    if (!e) return -1;
    return strchr(e, 'x') ? 2 : atoi(e) % 6;
}
void rt_apply_res(void) {
    HOSTFN;
    const char *res = getenv("SH4_RES");
    if (!res) return;
    static const uint16_t wh[6][2] = {{640, 480}, {1024, 768}, {1366, 768}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
    unsigned i = (unsigned)res_index(), w = wh[i][0], h = wh[i][1];
    if (strchr(res, 'x')) sscanf(res, "%ux%u", &w, &h);
    W32(0x5d524c, w); W32(0x5d5250, h);
    W32(0x610dd4, i); W32(0x610dd8, i);   // chosen option / applied option
}

// With SH4_RES, the "Resolution" line of the options menu (byte 0x1091af0 + cancel copy 0x1091b14) shows
// the resolution actually rendered; the PC option makes no sense on console, it is kept frozen. Called every frame.
void rt_keep_res(void) {
    static int r = -2, done;
    uint8_t *const mb = g_mem;
    if (r == -2) r = res_index();
    if (!done) {
        // "Console" options menu: from the page table (0x654cc0 / 0x654d88 / 0x654e50, 0x1c bytes per row,
        // row counts at 0x655034 and 0x655040). Handlers, labels and help travel with each row.
        static const struct { uint32_t tab; uint8_t n, k[8]; } pg[] = {
            {0x654cc0, 6, {1, 2, 3, 4, 5, 6}},    // without Resolution
            {0x654d88, 5, {0, 1, 5, 2, 6}},       // buttons, walk/run, inversion, sensitivity, exit
            {0x654e50, 7, {0, 1, 2, 3, 4, 5, 7}}, // "Others" page without Realtime Shadow
        };
        for (unsigned p = 0; p < 3; p++) {
            uint8_t old[8 * 0x1c];
            memcpy(old, mb + pg[p].tab, sizeof old);
            for (unsigned i = 0; i < pg[p].n; i++) memcpy(mb + pg[p].tab + i * 0x1c, old + pg[p].k[i] * 0x1c, 0x1c);
            W32(0x655034 + 4 * p, pg[p].n); W32(0x655040 + 4 * p, pg[p].n);
        }
        extern void rt_controls_patch_labels(void);
        rt_controls_patch_labels();
        uint32_t slider = 0x654d88 + 3 * 0x1c;
        W32(slider + 0xc, 0x654c58); W32(slider + 0x10, 11);
        W32(slider + 0x14, 0x7000); // custom label; nat_optrow selects the volume gauge renderer
        done = 1; rt_log("[menu] options console: %u/%u/%u lignes\n", R32(0x655034), R32(0x655038), R32(0x65503c));
    }
    {   // TEMP diagnostics: where does the options text live? (SH4_STRSCAN=text)
        static const char *pat; static unsigned n;
        if (!pat) pat = getenv("SH4_STRSCAN") ? getenv("SH4_STRSCAN") : "";
        if (*pat && ++n % 120 == 0 && n < 120 * 40) {
            size_t pl = strlen(pat); unsigned hits = 0;
            for (uint32_t a = 0; a + pl < g_mem_size && hits < 6; a++)
                if (!memcmp(mb + a, pat, pl)) {
                    rt_log("[strscan] '%s' at %08x (prev bytes %02x %02x %02x %02x, next %02x %02x)\n", pat, a, mb[a-4], mb[a-3], mb[a-2], mb[a-1], mb[a+pl], mb[a+pl+1]);
                    hits++;
                    unsigned refs = 0;
                    for (uint32_t b = 0; b + 4 <= g_mem_size && refs < 4; b += 4) if (R32(b) == a) { rt_log("[strscan]   pointer at %08x\n", b); refs++; }
                }
        }
    }
    // Hidden controls retain the pad defaults, including when loading an older save.
    for (unsigned copy = 0; copy < 2; copy++) {
        uint32_t opt = 0x1091ad0 + copy * 0x24;
        W8(opt + 8, 2); W8(opt + 0xb, 1); W8(opt + 0x17, 0); // defaults at 0x5c5af4
    }
    if (r < 0) return;
    W8(0x1091af0, r); W8(0x1091b14, r);
}

void rt_fill_iat(void) {
    HOSTFN;
    for (unsigned i = 0; i < x86_iat_n; i++) W32(x86_iat[i].slot, rt_host_addr(x86_iat[i].f, x86_iat[i].name));
}

// BSS - outside the newlib heap and the guest memory, no risk of overlap.
static CPU g_main_cpu_storage;
// Keep allocation ownership outside CPU: generated-code and oracle CPU layouts
// must remain unchanged, and guest code may modify the TIB's stack fields.
typedef struct {
    CPU cpu;
    uint32_t tib, stack;
} OwnedCPU;

CPU *rt_new_cpu(uint32_t stack_size) {
    HOSTFN;
    if (stack_size < 16 || stack_size > g_mem_size) rt_fatal("CPU context: invalid stack size %u", stack_size);
    static int main_used;
    CPU *c = (!main_used++) ? (memset(&g_main_cpu_storage, 0, sizeof g_main_cpu_storage), &g_main_cpu_storage)
                            : calloc(1, sizeof(OwnedCPU));
    if (!c) rt_fatal("CPU context: host allocation failed");
    uint32_t tib = gheap_calloc(0x1000);
    if (!tib) {
        if (c != &g_main_cpu_storage) free(c);
        rt_fatal("CPU context: TIB allocation failed");
    }
    uint32_t stack = gheap_alloc_aligned(stack_size, 16);
    if (!stack) {
        gheap_free(tib);
        if (c != &g_main_cpu_storage) free(c);
        rt_fatal("CPU context: stack allocation failed (%u bytes)", stack_size);
    }
    if (c != &g_main_cpu_storage) {
        OwnedCPU *owned = (OwnedCPU *)c;
        owned->tib = tib;
        owned->stack = stack;
    }
    c->fs_base = tib;
    c->esp = stack + stack_size - 16;
    c->fcw = 0x27f;
    W32(tib + 0x00, 0xffffffffu);        // empty SEH chain
    W32(tib + 0x04, stack + stack_size); // StackBase
    W32(tib + 0x08, stack);              // StackLimit
    W32(tib + 0x18, tib);                // Self
    rt_log("[cpu] CPU @%p g_mem @%p (guest 0x%08x - 0x%08x)\n",
           (void *)c, (void *)g_mem, 0u, g_mem_size - 1);
    return c;
}

void rt_free_cpu(CPU *c) {
    if (!c || c == &g_main_cpu_storage) return;
    OwnedCPU *owned = (OwnedCPU *)c;
    gheap_free(owned->stack);
    gheap_free(owned->tib);
    free(owned);
}

#if defined(SH4_TRACE_CALLS) || defined(SH4_PROFILE)
void rt_prof_start(void);
#else
#define rt_prof_start()
#endif
void rt_run_entry(void) {
    extern void rt_audit_init(void);
    rt_audit_init();
    CPU *c = g_main_cpu = rt_new_cpu(4u << 20);
    rt_log("entry 0x%08x, stack 0x%08x, TIB 0x%08x\n", x86_entry, c->esp, c->fs_base);
    rt_prof_start();
    rt_guest_call(c, x86_entry, 0);
    rt_log("the entry point returned (eax=%08x)\n", c->eax);
}

// ---------- CPUID / EFLAGS ----------
void rt_cpuid(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    // Pentium III without advertised MMX/SSE: the game and the CRT stay on the x87 paths.
    switch (*a) {
    case 0: *a = 1; *b = 0x756e6547; *d = 0x49656e69; *c = 0x6c65746e; break; // "GenuineIntel"
    case 1: *a = 0x673; *b = 0; *c = 0; *d = (1u << 0) | (1u << 4) | (1u << 8); break; // FPU TSC CX8
    case 0x80000000u: *a = 0x80000000u; *b = *c = *d = 0; break;
    default: *a = *b = *c = *d = 0; break;
    }
}

uint32_t rt_flags_pack(CPU *c, uint32_t cf, uint32_t pf, uint32_t zf, uint32_t sf, uint32_t of) {
    return cf | 2u | (pf << 2) | (zf << 6) | (sf << 7) | (1u << 9) | (c->df << 10) | (of << 11) | c->eflags_ext;
}

// ---------- x87 ----------
double rt_fround(CPU *c, double v) {
    switch ((c->fcw >> 10) & 3) {
    case 0: return nearbyint(v);
    case 1: return floor(v);
    case 2: return ceil(v);
    default: return trunc(v);
    }
}

int32_t rt_fist32(CPU *c, double v) {
    double r = rt_fround(c, v);
    return r >= -2147483648.0 && r <= 2147483647.0 ? (int32_t)r : INT32_MIN;
}

int16_t rt_fist16(CPU *c, double v) {
    double r = rt_fround(c, v);
    return r >= -32768.0 && r <= 32767.0 ? (int16_t)r : INT16_MIN;
}

int64_t rt_fist64(CPU *c, double v) {
    double r = rt_fround(c, v);
    return r >= -9223372036854775808.0 && r < 9223372036854775808.0 ? (int64_t)r : INT64_MIN;
}

double rt_f80_load(const uint8_t *p) {
    uint64_t m;
    uint16_t se;
    memcpy(&m, p, 8);
    memcpy(&se, p + 8, 2);
    int e = se & 0x7fff;
    double v;
    // Formats that the x87 (387+) rejects (unnormal, pseudo-infinity, pseudo-NaN) -> NaN, like the hardware.
    if (e != 0 && !(m >> 63)) v = NAN;
    else if (e == 0x7fff) v = (m << 1) ? NAN : INFINITY;
    else v = ldexp((double)m, e - 16383 - 63);
    return (se & 0x8000) ? -v : v;
}

void rt_f80_store(uint8_t *p, double v) {
    uint64_t m = 0;
    uint16_t se = signbit(v) ? 0x8000 : 0;
    if (isnan(v)) { se |= 0x7fff; m = 0xC000000000000000ull; }
    else if (isinf(v)) { se |= 0x7fff; m = 0x8000000000000000ull; }
    else if (v != 0) {
        int e;
        double f = frexp(fabs(v), &e);
        m = (uint64_t)ldexp(f, 64);
        se |= (uint16_t)(e - 1 + 16383);
    }
    memcpy(p, &m, 8);
    memcpy(p + 8, &se, 2);
}

void rt_fsave(CPU *c, uint32_t a) {
    HOSTFN;
    memset(mb + a, 0, 108);
    W32(a, c->fcw);
    W32(a + 4, rt_fnstsw(c));
    for (int i = 0; i < 8; i++) rt_f80_store(mb + a + 28 + 10 * i, ST(i));
    c->fsw = 0; c->ftop = 0; c->fcw = 0x37f;
}

void rt_frstor(CPU *c, uint32_t a) {
    HOSTFN;
    c->fcw = R32(a) & 0xffff;
    uint32_t sw = R32(a + 4);
    c->ftop = (sw >> 11) & 7;
    c->fsw = sw & ~0x3800u;
    for (int i = 0; i < 8; i++) ST(i) = rt_f80_load(mb + a + 28 + 10 * i);
}

// Diagnostics (SH4_TEXTCHECK=1): .text is read-only for the game; any page modified after loading
// betrays a faulty guest pointer (memset/write outside a buffer). Called every frame.
void rt_text_check(void) {
    uint8_t *const mb = g_mem;
    static uint32_t *snap;
    static int on = -1;
    if (on < 0) on = getenv("SH4_TEXTCHECK") != NULL;
    if (!on) return;
    const uint32_t lo = 0x401000, np = 0x1b5;   // 0x1b5 pages de 4 Ko
    if (!snap) {
        snap = malloc(np * 4);
        for (uint32_t p = 0; p < np; p++) { uint32_t h = 0; for (uint32_t i = 0; i < 1024; i++) h = h * 31 + R32(lo + p * 4096 + i * 4); snap[p] = h; }
        return;
    }
    for (uint32_t p = 0; p < np; p++) {
        uint32_t h = 0;
        for (uint32_t i = 0; i < 1024; i++) h = h * 31 + R32(lo + p * 4096 + i * 4);
        if (h == snap[p]) continue;
        snap[p] = h;
        // Detail: compares with the file (.text section: byte 0x200 for 0x401000, but the image keeps the load-time values).
        char path[600];
        snprintf(path, sizeof path, "%s/SILENT HILL 4.exe", g_game_dir);
        FILE *f = fopen(path, "rb");
        uint32_t nd = 0, first = 0, shown = 0;
        for (uint32_t i = 0; f && i < 1024; i++) {
            uint32_t v = 0, w = R32(lo + p * 4096 + i * 4);
            fseek(f, 0x200 + p * 4096 + i * 4, SEEK_SET);
            if (fread(&v, 4, 1, f) != 1) break;
            if (v != w) { if (!nd++) first = i * 4; if (shown++ < 3) rt_log("[textcheck]   +%03x : file %08x -> memory %08x\n", i * 4, v, w); }
        }
        if (f) fclose(f);
        rt_log("[textcheck] page 0x%08x: %u words changed, first at +%03x\n", lo + p * 4096, nd, first);
    }
}
