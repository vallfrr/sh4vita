#include "rt.h"
#include <SDL.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
uint8_t *g_mem;
extern uint64_t g_heap_used;
void rt_log(const char *fmt, ...) { (void)fmt; }
int main(void) {
    g_mem = calloc(1, 4u << 20);
    if (!g_mem || SDL_Init(0)) return 2;
    gheap_init(4096, (4u << 20) - 4096);
    uint32_t a = gheap_alloc(1024);
    uint64_t used = g_heap_used;
    if (!a || !used) return 3;
    memset(g_mem + a, 0x5a, 1024);
    if (gheap_realloc(a, 8u << 20) || g_heap_used != used) return 4;
    for (unsigned i = 0; i < 1024; ++i)
        if (g_mem[a + i] != 0x5a) return 8;
    if (gheap_realloc(a, 0) || g_heap_used != 0) {
        printf("after free-by-realloc: used=%llu\n", (unsigned long long)g_heap_used);
        return 5;
    }
    uint32_t b = gheap_alloc(1024);
    if (!b || !g_heap_used) return 6;
    gheap_free(b);
    if (g_heap_used) return 7;
    puts("allocation, failed resize, free-by-resize and reuse passed");
    return 0;
}
