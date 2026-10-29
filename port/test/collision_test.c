#include "rt.h"
#include <assert.h>
#include <stdlib.h>

uint8_t *g_mem;
unsigned g_frame;
static int valid;
static unsigned original_calls;
void rt_log(const char *fmt, ...) { (void)fmt; }
void F_00561a20_orig(CPU *c) {
    uint8_t *const mb = g_mem;
    original_calls++;
    W32(0x12f6d70, valid ? 0x20000 : 0);
    W32(0x12f6d74, valid ? 0x20100 : 0);
    if (valid) W32(0x12f6d78, 0x20200);
    c->esp += 4;  // cdecl RET
}
void nat_collision_set(CPU *c);
int main(void) {
    g_mem = calloc(1, 24u << 20); assert(g_mem);
    uint8_t *const mb = g_mem;
    CPU c = {.esp=0x1000};
    valid = 1; nat_collision_set(&c);
    assert(c.esp == 0x1004 && R32(0x12f6d78) == 0x20200);
    // A new room without collision must not reuse the old BSP after its owner was released.
    valid = 0; nat_collision_set(&c);
    assert(c.esp == 0x1008 && !R32(0x12f6d70) && !R32(0x12f6d74) && !R32(0x12f6d78));
    nat_collision_set(&c); assert(!R32(0x12f6d78));
    valid = 1; nat_collision_set(&c); assert(R32(0x12f6d78) == 0x20200 && original_calls == 4);
    puts("collision lifetime and valid BSP preservation OK");
    free(g_mem); return 0;
}
