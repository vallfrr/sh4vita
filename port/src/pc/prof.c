// PC sampling profiler: SH4_PROF=start,end (frame numbers). A thread reads the game thread's RIP
// every ~0.5 ms and writes build/pc/prof.txt (address histogram) at frame "end"; resolve with tools/prof_report.py.
#include "rt.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

extern unsigned g_frame;
static HANDLE g_target;
static unsigned g_begin, g_end;
static uint64_t *g_samples;
static size_t g_n;
#define MAXS (4u << 20)

static DWORD WINAPI sampler(LPVOID p) {
    (void)p;
    for (;;) {
        Sleep(1);
        if (g_frame < g_begin) continue;
        if (g_frame >= g_end || g_n >= MAXS) break;
        CONTEXT cx;
        cx.ContextFlags = CONTEXT_CONTROL;
        if (SuspendThread(g_target) == (DWORD)-1) continue;
        if (GetThreadContext(g_target, &cx)) g_samples[g_n++] = cx.Rip;
        ResumeThread(g_target);
    }
    FILE *f = fopen("prof.txt", "w");
    if (f) { for (size_t i = 0; i < g_n; i++) fprintf(f, "%llx\n", (unsigned long long)g_samples[i]); fclose(f); }
    rt_log("[prof] %zu samples written to prof.txt\n", g_n);
    return 0;
}

void prof_start(void) {
    const char *e = getenv("SH4_PROF");
    if (!e) return;
    g_begin = (unsigned)strtoul(e, (char **)&e, 10);
    g_end = *e == ',' ? (unsigned)strtoul(e + 1, NULL, 10) : g_begin + 300;
    g_samples = malloc(MAXS * sizeof *g_samples);
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_target, 0, FALSE, DUPLICATE_SAME_ACCESS);
    CreateThread(NULL, 0, sampler, NULL, 0, NULL);
}
