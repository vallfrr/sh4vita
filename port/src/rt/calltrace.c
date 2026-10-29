// Guest code diagnostics (gcc -finstrument-functions on gen/):
//  -DSH4_TRACE_CALLS=ON: ring of the last entries, printed before a FATAL.
//  -DSH4_PROFILE=ON    : sampled profile (SH4_PROFILE=start,end as frame numbers): a thread reads the current guest function of the main thread every ms;
//                         top 60 written to the log at frame "end" (resolve with tools/resolve_prof.sh).
#if defined(SH4_TRACE_CALLS) || defined(SH4_PROFILE)
#include "rt.h"
#include <SDL.h>
#define NF __attribute__((no_instrument_function))
extern void F_00554100(CPU *c);
extern unsigned g_frame;
static uintptr_t main_lo, main_hi;   // main thread stack: TLS-free filter (SDL threads do not always have one)
#define IS_MAIN() ((uintptr_t)__builtin_frame_address(0) - main_lo < main_hi - main_lo)
#ifdef SH4_TRACE_CALLS
#define RING 128
static void *ring[RING];
static volatile unsigned ring_n;
NF void __cyg_profile_func_enter(void *fn, void *site) { (void)site; ring[ring_n++ % RING] = fn; }
NF void __cyg_profile_func_exit(void *fn, void *site) { (void)fn; (void)site; }
// Addresses relative to F_00554100: addr2line/nm give the name (nm sh4 | sort, then offset).
NF void rt_calltrace_dump(void) {
    rt_log("  calltrace ref F_00554100=%p, %u last entries (most recent last):\n", (void *)F_00554100, (unsigned)RING);
    for (unsigned i = ring_n > RING ? ring_n - RING : 0; i < ring_n; i++)
        rt_log("   T %+d\n", (int)((char *)ring[i % RING] - (char *)F_00554100));
}
NF void rt_prof_start(void) {}
NF void *rt_prof_host(void *f) { return f; }
#else
#define STK 512
#define HT 8192
static void *volatile stk[STK];
static volatile int depth;
static unsigned n_zero, n_over;
static volatile unsigned long long n_calls;   // guest calls of the main thread since the start
static void *volatile cur_host;
static unsigned p_begin, p_end;
#define HOT 16384
static struct { void *fn; unsigned n; } hot[HOT];   // entries per guest function in the window (SH4_HOTDUMP=file)
// f = host function called by the guest code (NULL = guest code); returns the previous value to restore after the call.
NF void *rt_prof_host(void *f) {
    void *pv = cur_host;
    if (IS_MAIN()) cur_host = f;
    return pv;
}
static struct { void *fn; unsigned n; } ht[HT];
// Call-context tree (SH4_PROFILE_TREE=1): one node per (parent, function), inclusive count; printed depth-first pruned at 1.2%.
#define TN 65536
#define TDEPTH 40
static struct { void *fn; int par; unsigned n; } tn[TN];
static int tn_n = 1;   // racine = indice TN (hors tableau)
static int tree_child(int par, void *fn) {
    unsigned h = (unsigned)(((uintptr_t)fn >> 2) * 2654435761u + (unsigned)par * 40503u) % TN;
    for (int k = 0; k < TN; k++, h = (h + 1) % TN) {
        if (!tn[h].fn) { if (tn_n >= TN - 1) return -1; tn[h].fn = fn; tn[h].par = par; tn_n++; return (int)h; }
        if (tn[h].fn == fn && tn[h].par == par) return (int)h;
    }
    return -1;
}
static void tree_print(int node, int depth, unsigned total, int *lines) {
    // children of node: scan (rare, offline) sorted by decreasing count
    for (;;) {
        int best = -1;
        for (int i = 0; i < TN; i++)
            if (tn[i].fn && tn[i].par == node && tn[i].n * 1000ull >= total * 12ull && tn[i].n != 0xffffffffu && (best < 0 || tn[i].n > tn[best].n)) best = i;
        if (best < 0 || *lines > 400) return;
        (*lines)++;
        rt_log("[tree] %5.1f%% %*sT %+d\n", 100.0 * tn[best].n / total, depth, "", (int)((char *)tn[best].fn - (char *)F_00554100));
        unsigned keep = tn[best].n;
        tree_print(best, depth + 1, total, lines);
        tn[best].n = 0xffffffffu;   // printed
        (void)keep;
    }
}
// Profile stack: pushes on entry; on exit, pops down to the last occurrence of fn (catches exits lost by longjmp).
NF void __cyg_profile_func_enter(void *fn, void *site) {
    (void)site;
    if (!IS_MAIN()) return;
    int d = depth;
    if (d < STK) stk[d] = fn;
    depth = d + 1;
    n_calls++;
    if (g_frame >= p_begin && g_frame < p_end) {
        unsigned h = ((uintptr_t)fn >> 2) * 2654435761u % HOT;
        while (hot[h].fn && hot[h].fn != fn) h = (h + 1) % HOT;
        hot[h].fn = fn;
        hot[h].n++;
    }
}
NF void __cyg_profile_func_exit(void *fn, void *site) {
    (void)site;
    if (!IS_MAIN()) return;
    int d = depth;
    if (d > STK) { depth = d - 1; return; }
    while (d > 0 && stk[d - 1] != fn) d--;
    depth = d > 0 ? d - 1 : 0;
}
NF void rt_calltrace_dump(void) {}
static NF int prof_thread(void *arg) {
    (void)arg;
    unsigned total = 0, tick = 0;
    int tree_on = getenv("SH4_PROFILE_TREE") != NULL;
    unsigned long long c0 = 0;
    unsigned f0 = 0;
    rt_log("[prof] thread demarre, fenetre %u..%u\n", p_begin, p_end);
    for (;;) {
        SDL_Delay(1);
        if (++tick % 5000 == 0) rt_log("[prof] tick %u frame %u depth %d samples %u\n", tick, g_frame, depth, total);
        if (g_frame < p_begin) continue;
        if (!c0) { c0 = n_calls; f0 = g_frame; }
        if (g_frame >= p_end) break;
        int d = depth;
        void *fn = cur_host ? cur_host : d > 0 && d <= STK ? stk[d - 1] : NULL;
        if (d <= 0) n_zero++; else if (d > STK) n_over++;
        unsigned h = ((uintptr_t)fn >> 2) * 2654435761u % HT;
        while (ht[h].fn && ht[h].fn != fn) h = (h + 1) % HT;
        ht[h].fn = fn;
        ht[h].n++;
        total++;
        if (tree_on && d > 0) {
            int node = TN, lim = d < STK ? d : STK;
            if (lim > TDEPTH) lim = TDEPTH;
            for (int i = 0; i < lim && node >= 0; i++) {
                node = tree_child(node, stk[i]);
                if (node >= 0) tn[node].n++;
            }
        }
    }
    const char *hd = getenv("SH4_HOTDUMP");
    if (hd) {
        FILE *f = fopen(hd, "w");
        if (f) {
            for (int i = 0; i < HOT; i++) if (hot[i].fn) fprintf(f, "%+d %u\n", (int)((char *)hot[i].fn - (char *)F_00554100), hot[i].n);
            fclose(f);
        }
    }
    if (tree_on) { int lines = 0; tree_print(TN, 0, total, &lines); }
    rt_log("[prof] %u samples over %u frames, ref F_00554100=%p\n", total, p_end - p_begin, (void *)F_00554100);
    rt_log("[prof] guest calls: %llu over %u frames = %llu per frame\n", n_calls - c0, g_frame - f0,
           (n_calls - c0) / (g_frame - f0 ? g_frame - f0 : 1));
    for (int k = 0; k < 60; k++) {
        int best = -1;
        for (int i = 0; i < HT; i++) if (ht[i].fn && (best < 0 || ht[i].n > ht[best].n)) best = i;
        if (best < 0 || !ht[best].n) break;
        rt_log("[prof] %5.1f%% %5u  T %+d\n", 100.0 * ht[best].n / total, ht[best].n, (int)((char *)ht[best].fn - (char *)F_00554100));
        ht[best].n = 0;
    }
    return 0;
}
NF void rt_prof_start(void) {
    main_hi = (uintptr_t)__builtin_frame_address(0) + 65536;
    main_lo = main_hi - 4u * 1024 * 1024;
    const char *e = getenv("SH4_PROFILE");
    if (!e) return;
    char *q;
    p_begin = (unsigned)strtoul(e, &q, 10);
    p_end = *q == ',' ? (unsigned)strtoul(q + 1, NULL, 10) : p_begin + 300;
    rt_log("[prof] rt_prof_start: stack %p..%p window %u..%u\n", (void *)main_lo, (void *)main_hi, p_begin, p_end);
    if (!SDL_CreateThread(prof_thread, "prof", NULL)) rt_log("[prof] SDL_CreateThread : %s\n", SDL_GetError());
}
#endif
#endif
