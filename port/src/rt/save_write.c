// Console save transport. The GOG's signature and MD5 routines remain the
// authority for the on-disk bytes; only the destructive file write is replaced.
#include "rt.h"
#include "../win32/win32.h"
#include "staged_file.h"
#include <errno.h>
#include <stdlib.h>

extern void F_00408030_orig(CPU *c);

void nat_savewrite(CPU *c) {
    static int enabled = -1;
    if (enabled < 0) { const char *v = getenv("SH4_SAFE_SAVE"); enabled = !v || strcmp(v, "0"); }
    if (!enabled) { F_00408030_orig(c); return; }
    HOSTFN;
    uint32_t object = c->ecx, data = ARG(1);
    if ((uint64_t)object + 4 > g_mem_size || (uint64_t)data + 0xa9810u > g_mem_size || c->esp < 1024) {
        rt_save_failed(c, 998);
        STDRET(1, 0);
    }
    // This function is used on the game's main thread with a closed file
    // object. Do not silently use a destructive fallback for unexpected state.
    if (c != g_main_cpu || (R32(object) && R32(object) != 0xffffffffu)) {
        rt_log("[save] refused unexpected writer context\n");
        rt_save_failed(c, 87);
        STDRET(1, 0);
    }
    CPU work = *c;
    work.esp -= 512;
    uint32_t path = work.esp, digest = path + 272;
    memset(mb + path, 0, 360);
    W32(data, 0x7ffff005u);
    work.ecx = object;
    rt_guest_call(&work, 0x407c10, 1, path); // original save directory/name builder
    if (!memchr(mb + path, 0, 260) || !R8(path)) {
        rt_save_failed(c, 87);
        STDRET(1, 0);
    }
    char host_path[600];
    w32_host_path(GSTR(path), host_path, sizeof host_path);
    // Original init/update/final sequence, including the signature in MD5.
    work.ecx = digest; rt_guest_call(&work, 0x415300, 0);
    work.ecx = digest; rt_guest_call(&work, 0x415bf0, 2, data, 0xa9800u);
    work.ecx = digest; rt_guest_call(&work, 0x415d30, 1, data + 0xa9800u);
    int ok;
#ifdef SH4_TEST_OPTIMIZATIONS
    // Diagnostic failure occurs before opening the stage: never fills the card
    // or damages an existing save. The production build ignores this variable.
    static int fault_loaded, fault_used;
    static unsigned fault_frame;
    if (!fault_loaded) {
        const char *v = getenv("SH4_TEST_SAVE_FAIL_AT");
        fault_frame = v ? (unsigned)strtoul(v, NULL, 10) : 0;
        fault_loaded = 1;
    }
    extern unsigned g_frame;
    if (fault_frame && !fault_used && g_frame >= fault_frame) {
        fault_used = 1; ok = 0;
        rt_log("[save-test] simulated full storage before stage creation\n");
        errno = ENOSPC;
    } else
#endif
    ok = rt_staged_file_write(host_path, mb + data, 0xa9810u);
    int error = errno;
    W32(object, 0);
    if (!ok) {
        rt_log("[save] staged write failed errno=%d; previous destination retained\n", error);
        rt_save_failed(c, error == ENOSPC ? 112 : error == EACCES ? 5 : 29);
    } else rt_log("[save] staged replacement complete (%u bytes)\n", 0xa9810u);
    STDRET(1, ok);
}
