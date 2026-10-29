// Guest heap: TLSF over the zone [G_HEAP_BASE, end of g_mem), guest addresses.
#include "rt.h"
#include "third_party/tlsf/tlsf.h"
#include <SDL.h>
#include <stdlib.h>

static tlsf_t g_tlsf;
static SDL_mutex *g_heap_lock;

typedef struct HeapFreeStats {
    size_t total, largest;
    unsigned blocks;
} HeapFreeStats;
static void count_free_block(void *ptr, size_t bytes, int used, void *arg) {
    (void)ptr;
    if (used) return;
    HeapFreeStats *stats = arg;
    stats->total += bytes;
    if (bytes > stats->largest) stats->largest = bytes;
    stats->blocks++;
}
static void log_heap_block(void *ptr, size_t bytes, int used, void *arg) {
    (void)arg;
    if (!used || bytes >= 65536)
        rt_log("[heap-map] address=%08x size=%llu used=%d\n", GADDR(ptr),
               (unsigned long long)bytes, used);
}

void gheap_init(uint32_t base, uint32_t size) {
    g_tlsf = tlsf_create_with_pool(g_mem + base, size);
    g_heap_lock = SDL_CreateMutex();
}

static uint32_t to_g(void *p) { return p ? GADDR(p) : 0; }
uint64_t g_heap_used, g_heap_peak;
uint64_t gheap_used_snapshot(void) {
    if (!g_heap_lock) return 0;
    SDL_LockMutex(g_heap_lock);
    uint64_t used = g_heap_used;
    SDL_UnlockMutex(g_heap_lock);
    return used;
}
static void track(void *p, int sign) {
    if (!p) return;
    size_t n = tlsf_block_size(p);
    if (sign > 0) { g_heap_used += n; if (g_heap_used > g_heap_peak) g_heap_peak = g_heap_used; }
    else g_heap_used -= n;
}

static void host_trace(uint32_t size, void *ra) {
    static int on = -1;
    if (on < 0) on = getenv("SH4_TRACE_HEAP") != NULL;
    if (on && size >= (1u << 20)) rt_log("[heap-host] %u KB from %p\n", size >> 10, ra);
}

uint32_t gheap_alloc(uint32_t size) {
    host_trace(size, __builtin_return_address(0));
    HeapFreeStats free_stats = {0};
    SDL_LockMutex(g_heap_lock);
    void *p = tlsf_malloc(g_tlsf, size ? size : 1);
    track(p, 1);
    if (!p) {
        tlsf_walk_pool(tlsf_get_pool(g_tlsf), count_free_block, &free_stats);
        /* Diagnostic only: capture the first fragmented pool on demand. */
        static int map_dumped;
        if (!map_dumped && getenv("SH4_HEAP_MAP")) {
            map_dumped = 1;
            tlsf_walk_pool(tlsf_get_pool(g_tlsf), log_heap_block, NULL);
        }
    }
    SDL_UnlockMutex(g_heap_lock);
    if (!p) rt_log("[heap] allocation of %u bytes failed: used %llu MB, free %llu KB, largest block %llu KB in %u blocks\n",
                   size, (unsigned long long)(g_heap_used >> 20),
                   (unsigned long long)(free_stats.total >> 10),
                   (unsigned long long)(free_stats.largest >> 10), free_stats.blocks);
    return to_g(p);
}

uint32_t gheap_calloc(uint32_t size) {
    host_trace(size | 1u, __builtin_return_address(0)); // |1: marks the call via calloc in the trace
    uint32_t a = gheap_alloc(size);
    if (a) memset(g_mem + a, 0, size);
    return a;
}

uint32_t gheap_alloc_aligned(uint32_t size, uint32_t align) {
    host_trace(size, __builtin_return_address(0));
    SDL_LockMutex(g_heap_lock);
    void *p = tlsf_memalign(g_tlsf, align, size ? size : 1);
    track(p, 1);
    SDL_UnlockMutex(g_heap_lock);
    return to_g(p);
}

void gheap_free(uint32_t a) {
    if (!a) return;
    SDL_LockMutex(g_heap_lock);
    track(g_mem + a, -1);
    tlsf_free(g_tlsf, g_mem + a);
    SDL_UnlockMutex(g_heap_lock);
}

uint32_t gheap_realloc(uint32_t a, uint32_t size) {
    SDL_LockMutex(g_heap_lock);
    if (a) track(g_mem + a, -1);
    void *p = tlsf_realloc(g_tlsf, a ? g_mem + a : NULL, size);
    /* TLSF frees the original block and returns NULL for size zero. */
    track(p ? p : (a && size ? g_mem + a : NULL), 1);
    SDL_UnlockMutex(g_heap_lock);
    return to_g(p);
}

uint32_t gheap_size(uint32_t a) {
    return a ? (uint32_t)tlsf_block_size(g_mem + a) : 0;
}

uint32_t gstrdup(const char *s) {
    uint32_t n = (uint32_t)strlen(s) + 1, a = gheap_alloc(n);
    memcpy(g_mem + a, s, n);
    return a;
}
