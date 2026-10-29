// Guest COM objects: guest memory holds {vtable, index}, the vtable points to synthetic
// addresses of host functions (rt_host_addr).
#include "d3d8.h"
#include <stdlib.h>

#define MAX_OBJ 65536
static ComObj *objs[MAX_OBJ];
static uint32_t next_free = 1;
#define MAX_VTBL 128
static struct { const gfn *m; uint32_t vtbl; } vtbls[MAX_VTBL];
static int nvtbl;

static uint32_t vtable_for(const gfn *m, int n, const char *iface) {
    HOSTFN;
    for (int i = 0; i < nvtbl; i++)
        if (vtbls[i].m == m) return vtbls[i].vtbl;
    if (nvtbl == MAX_VTBL) rt_fatal("too many COM method tables");
    uint32_t v = gheap_alloc(4u * n);
    if (!v) rt_fatal("COM vtable: guest memory exhausted (%u bytes, %s)", 4u * n, iface);
    for (int i = 0; i < n; i++) W32(v + 4u * i, rt_host_addr(m[i], iface));
    vtbls[nvtbl].m = m;
    vtbls[nvtbl++].vtbl = v;
    return v;
}

uint32_t com_wrap(ComObj *o, int kind, const gfn *methods, int n, const char *iface) {
    HOSTFN;
    uint32_t id = 0;
    for (uint32_t i = 0; i < MAX_OBJ; i++) {
        uint32_t k = (next_free + i) % MAX_OBJ;
        if (k && !objs[k]) { id = k; break; }
    }
    if (!id) rt_fatal("too many D3D objects");
    next_free = id + 1;
    o->kind = kind;
    o->refs = 1;
    o->guest = gheap_alloc(16);
    if (!o->guest) rt_fatal("COM object: guest memory exhausted (16 bytes, %s)", iface);
    W32(o->guest, vtable_for(methods, n, iface));
    W32(o->guest + 4, id);
    W32(o->guest + 8, 0x44334438u); // marqueur "D3D8"
    objs[id] = o;
    return o->guest;
}

ComObj *com_get(uint32_t g) {
    HOSTFN;
    if (!g || g + 12 > g_mem_size || R32(g + 8) != 0x44334438u) return NULL;
    uint32_t id = R32(g + 4);
    return id < MAX_OBJ ? objs[id] : NULL;
}

void *com_get_kind(uint32_t g, int kind) {
    ComObj *o = com_get(g);
    return o && o->kind == kind ? o : NULL;
}

void com_free(ComObj *o) {
    HOSTFN;
    uint32_t id = R32(o->guest + 4);
    W32(o->guest + 8, 0);
    if (id < MAX_OBJ) objs[id] = NULL;
    gheap_free(o->guest);
}

void d3d_log_once(const char *what) {
    static const char *seen[512];
    static int n;
    for (int i = 0; i < n; i++)
        if (seen[i] == what) return;
    if (n < 512) seen[n++] = what;
    rt_log("[d3d8] %s\n", what);
}

// Frees an object by its guest address (game thread): the host struct was already freed by the consumer (res_free).
void com_free_guest(uint32_t g) {
    HOSTFN;
    uint32_t id = R32(g + 4);
    W32(g + 8, 0);
    if (id < MAX_OBJ) objs[id] = NULL;
    gheap_free(g);
}
