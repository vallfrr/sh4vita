// Diagnostic only (-DSH4_ALLOC_PROF=ON): live host-heap bytes per allocation site (return address of malloc/calloc/realloc),
// reported through rt_log every 60 s. Resolve the addresses offline against sh4.velf. Not part of any normal build.
#ifdef SH4_ALLOC_PROF
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "rt.h"

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);

#define TAB (1u << 17)
#define SITES 2048u
typedef struct { void *p; uint32_t size, caller; } Ent;
typedef struct { uint32_t caller; int32_t count; int64_t live; uint64_t total; } Site;
static Ent tab[TAB];
static Site sites[SITES];
static volatile int lock;
static uint32_t dropped;

static void lk(void) { while (__sync_lock_test_and_set(&lock, 1)) { } }
static void ul(void) { __sync_lock_release(&lock); }
static uint32_t hp(void *p) { uint32_t x = (uint32_t)p >> 3; return (x ^ (x >> 15)) * 2654435761u >> 15 & (TAB - 1); }
static Site *site(uint32_t c) {
    uint32_t i = (c >> 1) * 2654435761u >> 21 & (SITES - 1);
    for (uint32_t n = 0; n < SITES; n++, i = (i + 1) & (SITES - 1)) {
        if (sites[i].caller == c) return &sites[i];
        if (!sites[i].caller) { sites[i].caller = c; return &sites[i]; }
    }
    return NULL;
}
static void add(void *p, size_t size, uint32_t caller) {
    if (!p) return;
    lk();
    uint32_t i = hp(p);
    for (uint32_t n = 0; n < TAB && tab[i].p; n++) i = (i + 1) & (TAB - 1);
    if (tab[i].p) { dropped++; ul(); return; }
    tab[i] = (Ent){p, (uint32_t)size, caller};
    Site *s = site(caller);
    if (s) { s->count++; s->live += size; s->total += size; }
    ul();
}
static void del(void *p) {
    if (!p) return;
    lk();
    uint32_t i = hp(p);
    for (uint32_t n = 0; n < TAB && tab[i].p && tab[i].p != p; n++) i = (i + 1) & (TAB - 1);
    if (tab[i].p == p) {
        Site *s = site(tab[i].caller);
        if (s) { s->count--; s->live -= tab[i].size; }
        uint32_t j = i;
        for (;;) {
            j = (j + 1) & (TAB - 1);
            if (!tab[j].p) break;
            uint32_t k = hp(tab[j].p);
            if ((j > i && (k <= i || k > j)) || (j < i && k <= i && k > j)) { tab[i] = tab[j]; i = j; }
        }
        tab[i].p = NULL;
    }
    ul();
}
void *__wrap_malloc(size_t n) { void *p = __real_malloc(n); add(p, n, (uint32_t)__builtin_return_address(0)); return p; }
void *__wrap_calloc(size_t a, size_t b) { void *p = __real_calloc(a, b); add(p, a * b, (uint32_t)__builtin_return_address(0)); return p; }
void *__wrap_realloc(void *o, size_t n) {
    void *p = __real_realloc(o, n);
    if (p || !n) { del(o); add(p, n, (uint32_t)__builtin_return_address(0)); }
    return p;
}
void __wrap_free(void *p) { del(p); __real_free(p); }

extern void gheap_report(void);
void allocprof_report(void) {
    Site top[24] = {0};
    int64_t live = 0; int32_t cnt = 0;
    lk();
    for (uint32_t i = 0; i < SITES; i++) {
        if (!sites[i].caller) continue;
        live += sites[i].live; cnt += sites[i].count;
        for (int k = 0; k < 24; k++) if (sites[i].live > top[k].live) {
            for (int m = 23; m > k; m--) top[m] = top[m - 1];
            top[k] = sites[i]; break;
        }
    }
    ul();
    rt_log("[allocprof] self=%08x live=%lld KB in %d blocks, dropped %u\n", (unsigned)(uintptr_t)allocprof_report, (long long)(live >> 10), cnt, dropped);
    for (int k = 0; k < 24 && top[k].live > 0; k++)
        rt_log("[allocprof] %08x live=%lld KB n=%d total=%llu KB\n", top[k].caller, (long long)(top[k].live >> 10), top[k].count,
               (unsigned long long)(top[k].total >> 10));
    gheap_report();
}
#endif
