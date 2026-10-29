// Opt-in observation and resume input guard. No suspend/audio/GPU policy change,
// no I/O, guest-memory edits or rendering calls from a power callback.
#include "rt.h"
#include "input_gate.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <stdlib.h>

typedef struct {
    uint32_t flags;
    uint64_t process_us, system_us;
} PowerEvent;
static PowerEvent events[8];
static unsigned produced, consumed, dropped;
static int initialized;
static SceUID callback = -1;
static int resume_guard;

static int power_event(int id, int count, int flags, void *arg) {
    (void)id; (void)count; (void)arg;
    if (produced - consumed >= 8) { ++dropped; return 0; }
    PowerEvent *event = &events[produced & 7u];
    event->flags = (uint32_t)flags;
    event->process_us = sceKernelGetProcessTimeWide();
    event->system_us = sceKernelGetSystemTimeWide();
    ++produced;
    return 0;
}

void rt_vita_lifecycle_poll(void) {
    if (!initialized) {
        initialized = 1;
        const char *trace = getenv("SH4_LIFECYCLE_TRACE");
        const char *guard = getenv("SH4_RESUME_GUARD");
        resume_guard = guard && !strcmp(guard, "1");
        if ((!trace || strcmp(trace, "1")) && !resume_guard) return;
        // The callback belongs to this main thread. Existing SDL_Delay uses
        // sceKernelDelayThreadCB; it dispatches callbacks without another thread.
        callback = sceKernelCreateCallback("sh4-power-trace", 0, power_event, NULL);
        int registered = callback < 0 ? callback : scePowerRegisterCallback(callback);
        rt_log("[lifecycle] callback=%08x registered=%08x (resume guard=%d)\n",
               (unsigned)callback, (unsigned)registered, resume_guard);
        if (registered < 0 && callback >= 0) { sceKernelDeleteCallback(callback); callback = -1; }
    }
    if (callback < 0) return;
    // Dispatch once per presented frame even when a busy game does not sleep.
    extern unsigned g_frame;
    static unsigned last_frame;
    static int sampled;
    if (!sampled || last_frame != g_frame) {
        sampled = 1;
        last_frame = g_frame;
        sceKernelCheckCallback();
    }
    while (consumed != produced) {
        PowerEvent event = events[consumed++ & 7u];
        // Main thread only, after callback dispatch. No file, audio or GPU
        // access from the kernel callback and no speculative resource rebuild.
        if (resume_guard && (event.flags & (SCE_POWER_CB_SYSTEM_RESUME | SCE_POWER_CB_APP_RESUME)))
            rt_input_neutralize();
        rt_log("[lifecycle] flags=%08x frame=%u process-us=%llu system-us=%llu\n",
               event.flags, g_frame, (unsigned long long)event.process_us,
               (unsigned long long)event.system_us);
    }
    if (dropped) { rt_log("[lifecycle] dropped=%u\n", dropped); dropped = 0; }
}
