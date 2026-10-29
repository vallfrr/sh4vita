// Emulated KERNEL32 (the 97 functions imported by SILENT HILL 4.exe).
#include "win32.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0777)
#endif

#define K(name) void imp_KERNEL32_##name(CPU *c)
#define FAKE_KERNEL32 0x7c800000u
#define FAKE_USER32   0x77d40000u

static uint32_t g_cmdline, g_env, g_envw;
static uint32_t next_tid = 0x100;

// ---------- processus / modules ----------
K(GetCurrentProcess) { STDRET(0, 0xffffffffu); }
K(SetPriorityClass) { STDRET(2, 1); }
K(TerminateProcess) { HOSTFN; rt_log("TerminateProcess(%u)\n", ARG(2)); exit((int)ARG(2)); }
K(ExitProcess) { HOSTFN; rt_log("ExitProcess(%u)\n", ARG(1)); exit((int)ARG(1)); }
K(GetVersion) { STDRET(0, 0x0a280105u); } // Windows XP 5.1.2600
K(GetVersionExA) {
    HOSTFN;
    uint32_t p = ARG(1), sz = R32(p);
    memset(mb + p + 4, 0, sz - 4);
    W32(p + 4, 5); W32(p + 8, 1); W32(p + 12, 2600); W32(p + 16, 2);
    STDRET(1, 1);
}
K(GetModuleHandleA) {
    HOSTFN;
    const char *n = GSTR(ARG(1));
    uint32_t r = !n ? 0x400000u : !SDL_strncasecmp(n, "kernel32", 8) ? FAKE_KERNEL32
               : !SDL_strncasecmp(n, "user32", 6) ? FAKE_USER32 : 0;
    STDRET(1, r);
}
K(GetModuleFileNameA) {
    HOSTFN;
    const char *s = GUEST_ROOT "SILENT HILL 4.exe";
    uint32_t n = ARG(3), len = (uint32_t)strlen(s);
    if (len >= n) len = n - 1;
    memcpy(mb + ARG(2), s, len);
    W8(ARG(2) + len, 0);
    STDRET(3, len);
}
K(LoadLibraryA) {
    HOSTFN;
    rt_log("LoadLibraryA(\"%s\") -> NULL\n", GSTR(ARG(1)));
    w32_set_last_error(c, 126);
    STDRET(1, 0);
}
K(FreeLibrary) { STDRET(1, 1); }
K(GetProcAddress) {
    HOSTFN;
    uint32_t n = ARG(2);
    rt_log("GetProcAddress(%08x, \"%s\") -> NULL\n", ARG(1), n > 0xffff ? GSTR(n) : "#ord");
    STDRET(2, 0);
}
K(GetCommandLineA) {
    if (!g_cmdline) g_cmdline = gstrdup("\"" GUEST_ROOT "SILENT HILL 4.exe\"");
    STDRET(0, g_cmdline);
}
K(GetStartupInfoA) {
    HOSTFN;
    memset(mb + ARG(1), 0, 68);
    W32(ARG(1), 68);
    STDRET(1, 0);
}
K(GetEnvironmentStrings) {
    if (!g_env) g_env = gheap_calloc(4);
    STDRET(0, g_env);
}
K(GetEnvironmentStringsW) {
    if (!g_envw) g_envw = gheap_calloc(4);
    STDRET(0, g_envw);
}
K(FreeEnvironmentStringsA) { STDRET(1, 1); }
K(FreeEnvironmentStringsW) { STDRET(1, 1); }
K(GetStdHandle) { STDRET(1, 0); }
K(SetStdHandle) { STDRET(2, 1); }
K(SetHandleCount) { HOSTFN; STDRET(1, ARG(1)); }
K(GetFileType) { HOSTFN; STDRET(1, h_type(ARG(1)) == H_FILE ? 1 : 0); }
K(SetUnhandledExceptionFilter) { STDRET(1, 0); }
K(UnhandledExceptionFilter) { STDRET(1, 0); }
K(RaiseException) {
    HOSTFN;
    rt_fatal("RaiseException code=%08x flags=%x nargs=%u (retour 0x%08x)", ARG(1), ARG(2), ARG(3), ARG(0));
}
K(RtlUnwind) { rt_fatal("RtlUnwind not supported"); }
K(IsBadReadPtr) { HOSTFN; STDRET(2, ARG(1) == 0); }
K(IsBadWritePtr) { HOSTFN; STDRET(2, ARG(1) == 0); }
K(IsBadCodePtr) { HOSTFN; STDRET(1, ARG(1) == 0); }
K(IsProcessorFeaturePresent) { STDRET(1, 0); }
K(GetSystemInfo) {
    HOSTFN;
    uint32_t p = ARG(1);
    memset(mb + p, 0, 36);
    W32(p + 4, 4096);          // dwPageSize
    W32(p + 8, 0x10000);       // lpMinimumApplicationAddress
    W32(p + 12, g_mem_size - 1);
    W32(p + 16, 1);            // dwActiveProcessorMask
    W32(p + 20, 1);            // dwNumberOfProcessors
    W32(p + 24, 586);
    W32(p + 28, 0x10000);      // dwAllocationGranularity
    W16(p + 32, 6);
    STDRET(1, 0);
}
K(GetVolumeInformationA) {
    HOSTFN;
    if (ARG(2) && ARG(3)) SDL_strlcpy(GSTR(ARG(2)), "SH4", ARG(3));
    if (ARG(4)) W32(ARG(4), 0x5348340a);
    if (ARG(5)) W32(ARG(5), 255);
    if (ARG(6)) W32(ARG(6), 0);
    if (ARG(7) && ARG(8)) SDL_strlcpy(GSTR(ARG(7)), "NTFS", ARG(8));
    STDRET(8, 1);
}
K(GetDriveTypeA) { STDRET(1, 3); } // DRIVE_FIXED

// ---------- erreurs / divers ----------
K(GetLastError) { HOSTFN; STDRET(0, R32(c->fs_base + 0x34)); }
K(SetLastError) { HOSTFN; w32_set_last_error(c, ARG(1)); STDRET(1, 0); }
K(MulDiv) {
    HOSTFN;
    int32_t a = (int32_t)ARG(1), b = (int32_t)ARG(2), d = (int32_t)ARG(3);
    if (!d) STDRET(3, -1);
    int64_t p = (int64_t)a * b;
    int64_t r = (p >= 0) == (d > 0) ? (p + (d > 0 ? d : -d) / 2) / d : (p - (d > 0 ? d : -d) / 2) / d;
    STDRET(3, (uint32_t)(int32_t)r);
}
uint64_t g_t_sleep, g_t_gl, g_t_swap, g_t_io, g_t_wait, g_io_bytes;   // diagnostics: main thread disk read / object wait
K(Sleep) {
    HOSTFN;
    static unsigned s_sleep_n;
    if (ARG(1) > 0 && (++s_sleep_n <= 3 || s_sleep_n % 500 == 0))
        rt_log("Sleep(%u) #%u\n", ARG(1), s_sleep_n);
    uint64_t t0 = SDL_GetPerformanceCounter();
    SDL_Delay(ARG(1));
    if (c == g_main_cpu) g_t_sleep += SDL_GetPerformanceCounter() - t0;
    STDRET(1, 0);
}
K(GetTickCount) {
    static unsigned s_tc;
    if (++s_tc <= 3 || s_tc % 50000 == 0) rt_log("GetTickCount #%u -> %u\n", s_tc, w32_tick_ms());
    STDRET(0, w32_tick_ms());
}
// SH4_HZ=60: the game sets its logic step to QPF/30 (0x414efa); advertising QPF*30/HZ makes its loop run at HZ.
K(QueryPerformanceFrequency) { HOSTFN; W64(ARG(1), SDL_GetPerformanceFrequency() * 30 / w32_hz()); STDRET(1, 1); }
// 0x414ffc: the main loop re-reads QPC nonstop until the next step (0xecb330); we sleep instead (core 0 freed).
K(QueryPerformanceCounter) {
    HOSTFN;
    if (R32(c->esp) == 0x415002) {
        uint64_t t0 = SDL_GetPerformanceCounter();
        w32_sleep_until(R64(0xecb330));
        g_t_sleep += SDL_GetPerformanceCounter() - t0;
    }
    W64(ARG(1), w32_qpc());
    STDRET(1, 1);
}
K(GetSystemTimeAsFileTime) {
    HOSTFN;
    uint64_t ft = ((uint64_t)time(NULL) + 11644473600ull) * 10000000ull + (SDL_GetTicks() % 1000) * 10000ull;
    W64(ARG(1), ft);
    STDRET(1, 0);
}
K(GetCurrentThreadId) { HOSTFN; STDRET(0, R32(c->fs_base + 0x24)); }
K(GetCurrentProcessId) { STDRET(0, 0x10); }
K(GetCurrentThread) { STDRET(0, 0xfffffffeu); }
K(GetThreadPriority) { STDRET(1, 0); }
K(SetThreadPriority) { STDRET(2, 1); }

// ---------- pages de code / locale ----------
K(GetACP) { STDRET(0, 1252); }
K(GetOEMCP) { STDRET(0, 437); }
K(GetCPInfo) {
    HOSTFN;
    uint32_t p = ARG(2);
    memset(mb + p, 0, 20);
    W32(p, 1);
    W8(p + 4, '?');
    STDRET(2, 1);
}
K(GetLocaleInfoA) {
    HOSTFN;
    const char *v = NULL;
    switch (ARG(2) & 0xffff) {
    case 0x1004: v = "1252"; break; // LOCALE_IDEFAULTANSICODEPAGE
    case 0x000b: v = "437"; break;  // LOCALE_IDEFAULTCODEPAGE
    case 0x0003: v = "FRA"; break;
    default: STDRET(4, 0);
    }
    uint32_t n = (uint32_t)strlen(v) + 1;
    if (ARG(4) == 0) STDRET(4, n);
    SDL_strlcpy(GSTR(ARG(3)), v, ARG(4));
    STDRET(4, n);
}
K(MultiByteToWideChar) {
    HOSTFN;
    uint32_t src = ARG(3), dst = ARG(5), dn = ARG(6);
    int32_t sn = (int32_t)ARG(4);
    uint32_t n = sn < 0 ? (uint32_t)strlen(GSTR(src)) + 1 : (uint32_t)sn;
    if (!dn) STDRET(6, n);
    if (n > dn) n = dn;
    for (uint32_t i = 0; i < n; i++) W16(dst + 2 * i, R8(src + i));
    STDRET(6, n);
}
K(WideCharToMultiByte) {
    HOSTFN;
    uint32_t src = ARG(3), dst = ARG(5), dn = ARG(6);
    int32_t sn = (int32_t)ARG(4);
    uint32_t n = (uint32_t)sn;
    if (sn < 0) { n = 0; while (R16(src + 2 * n)) n++; n++; }
    if (ARG(8)) W32(ARG(8), 0);
    if (!dn) STDRET(8, n);
    if (n > dn) n = dn;
    for (uint32_t i = 0; i < n; i++) { uint32_t w = R16(src + 2 * i); W8(dst + i, w < 256 ? w : '?'); }
    STDRET(8, n);
}
static uint16_t ctype1(uint32_t ch) {
    uint16_t f = 0;
    if (ch < 128) {
        if (isupper((int)ch)) f |= 0x001;
        if (islower((int)ch)) f |= 0x002;
        if (isdigit((int)ch)) f |= 0x004;
        if (isspace((int)ch)) f |= 0x008;
        if (ispunct((int)ch)) f |= 0x010;
        if (iscntrl((int)ch)) f |= 0x020;
        if (ch == ' ' || ch == '\t') f |= 0x040;
        if (isxdigit((int)ch)) f |= 0x080;
        if (isalpha((int)ch)) f |= 0x100;
    }
    return f;
}
K(GetStringTypeA) {
    HOSTFN;
    int32_t n = (int32_t)ARG(4);
    uint32_t src = ARG(3), out = ARG(5);
    if (n < 0) n = (int32_t)strlen(GSTR(src)) + 1;
    for (int32_t i = 0; i < n; i++) W16(out + 2 * i, ARG(2) == 1 ? ctype1(R8(src + i)) : 0);
    STDRET(5, 1);
}
K(GetStringTypeW) {
    HOSTFN;
    int32_t n = (int32_t)ARG(3);
    uint32_t src = ARG(2), out = ARG(4);
    if (n < 0) { n = 0; while (R16(src + 2 * n)) n++; n++; }
    for (int32_t i = 0; i < n; i++) W16(out + 2 * i, ARG(1) == 1 ? ctype1(R16(src + 2 * i)) : 0);
    STDRET(4, 1);
}
static uint32_t lcmap(uint32_t flags, uint32_t ch) {
    if (flags & 0x100) return ch < 128 ? (uint32_t)tolower((int)ch) : ch;
    if (flags & 0x200) return ch < 128 ? (uint32_t)toupper((int)ch) : ch;
    return ch;
}
K(LCMapStringA) {
    HOSTFN;
    uint32_t flags = ARG(2), src = ARG(3), dst = ARG(5), dn = ARG(6);
    int32_t n = (int32_t)ARG(4);
    if (n < 0) n = (int32_t)strlen(GSTR(src)) + 1;
    if (!dn) STDRET(6, n);
    if ((uint32_t)n > dn) STDRET(6, 0);
    for (int32_t i = 0; i < n; i++) W8(dst + i, lcmap(flags, R8(src + i)));
    STDRET(6, n);
}
K(LCMapStringW) {
    HOSTFN;
    uint32_t flags = ARG(2), src = ARG(3), dst = ARG(5), dn = ARG(6);
    int32_t n = (int32_t)ARG(4);
    if (n < 0) { n = 0; while (R16(src + 2 * n)) n++; n++; }
    if (!dn) STDRET(6, n);
    if ((uint32_t)n > dn) STDRET(6, 0);
    for (int32_t i = 0; i < n; i++) W16(dst + 2 * i, lcmap(flags, R16(src + 2 * i)));
    STDRET(6, n);
}

// ---------- memory ----------
K(HeapCreate) { STDRET(3, 0x00a00000u); }
K(HeapDestroy) { STDRET(1, 1); }
// Tracking of live allocations per caller (SH4_TRACE_HEAP): address -> (size, caller) table.
#define LIVE_N (1u << 20)
static struct { uint32_t a, size, caller; } *live;
static void live_add(uint32_t a, uint32_t size, uint32_t caller) {
    if (!live) live = calloc(LIVE_N, sizeof *live);
    for (uint32_t i = (a >> 3) & (LIVE_N - 1);; i = (i + 1) & (LIVE_N - 1))
        if (!live[i].a || live[i].a == 1) { live[i].a = a; live[i].size = size; live[i].caller = caller; return; }
}
static void live_del(uint32_t a) {
    if (!live || !a) return;
    for (uint32_t i = (a >> 3) & (LIVE_N - 1); live[i].a; i = (i + 1) & (LIVE_N - 1))
        if (live[i].a == a) { live[i].a = 1; return; }
}
void heap_report(void) {
    if (!live) return;
    static struct { uint32_t caller; uint64_t bytes; uint32_t n; } top[4096];
    int nt = 0;
    for (uint32_t i = 0; i < LIVE_N; i++) {
        if (live[i].a <= 1) continue;
        int k;
        for (k = 0; k < nt && top[k].caller != live[i].caller; k++) {}
        if (k == nt && nt < 4096) { top[nt].caller = live[i].caller; top[nt].bytes = 0; top[nt].n = 0; nt++; }
        if (k < 4096) { top[k].bytes += live[i].size; top[k].n++; }
    }
    for (int r = 0; r < 12 && r < nt; r++) {
        int best = r;
        for (int k = r + 1; k < nt; k++) if (top[k].bytes > top[best].bytes) best = k;
        __typeof__(top[0]) t = top[r]; top[r] = top[best]; top[best] = t;
        rt_log("[heap] caller %08x: %llu KB in %u blocks\n", top[r].caller, (unsigned long long)(top[r].bytes >> 10), top[r].n);
    }
}
static void trace_big(CPU *c, const char *what, uint32_t size, uint32_t a) {
    HOSTFN;
    static int on = -1;
    if (on < 0) on = getenv("SH4_TRACE_HEAP") != NULL;
    if (on && size >= (64u << 10)) rt_log("[heap] %s %u KB -> %08x (caller %08x)\n", what, size >> 10, a, R32(c->esp));
    if (on && a) live_add(a, size, R32(c->esp));
}
K(HeapAlloc) {
    HOSTFN;
    uint32_t a = (ARG(2) & 8) ? gheap_calloc(ARG(3)) : gheap_alloc(ARG(3));
    trace_big(c, "HeapAlloc", ARG(3), a);
    STDRET(3, a);
}
K(HeapFree) { HOSTFN; live_del(ARG(3)); gheap_free(ARG(3)); STDRET(3, 1); }
K(HeapSize) { HOSTFN; STDRET(3, gheap_size(ARG(3))); }
K(HeapReAlloc) {
    HOSTFN;
    uint32_t flags = ARG(2), p = ARG(3), sz = ARG(4), old = gheap_size(p);
    if (flags & 0x10) STDRET(4, sz <= old ? p : 0); // HEAP_REALLOC_IN_PLACE_ONLY
    uint32_t r = gheap_realloc(p, sz);
    if (r && (flags & 8) && sz > old) memset(mb + r + old, 0, sz - old);
    if (r || (p && !sz)) live_del(p);
    if (r) trace_big(c, "HeapReAlloc", sz, r);
    STDRET(4, r);
}
K(VirtualAlloc) {
    HOSTFN;
    uint32_t addr = ARG(1), size = ARG(2);
    if (addr) STDRET(4, addr); // MEM_COMMIT in an already reserved region (memory present and zeroed)
    uint32_t a = gheap_alloc_aligned((size + 0xfff) & ~0xfffu, 0x10000);
    if (a) memset(mb + a, 0, size);
    trace_big(c, "VirtualAlloc", size, a);
    STDRET(4, a);
}
K(VirtualFree) {
    HOSTFN;
    if (ARG(3) & 0x8000) { live_del(ARG(1)); gheap_free(ARG(1)); } // MEM_RELEASE
    STDRET(3, 1);
}
K(VirtualQuery) {
    HOSTFN;
    uint32_t a = ARG(1) & ~0xfffu, p = ARG(2);
    W32(p, a); W32(p + 4, a); W32(p + 8, 4); W32(p + 12, 0x1000);
    W32(p + 16, 0x1000); W32(p + 20, 4); W32(p + 24, 0x20000);
    STDRET(3, 28);
}
K(VirtualProtect) { HOSTFN; if (ARG(4)) W32(ARG(4), 4); STDRET(4, 1); }

// ---------- TLS (slots in the guest TIB, +0xE10) ----------
static uint32_t tls_used;
K(TlsAlloc) {
    for (uint32_t i = 0; i < 32; i++)
        if (!(tls_used & (1u << i))) { tls_used |= 1u << i; STDRET(0, i); }
    STDRET(0, 0xffffffffu);
}
K(TlsFree) { HOSTFN; tls_used &= ~(1u << (ARG(1) & 31)); STDRET(1, 1); }
K(TlsSetValue) { HOSTFN; W32(c->fs_base + 0xe10 + 4 * ARG(1), ARG(2)); STDRET(2, 1); }
K(TlsGetValue) {
    HOSTFN;
    uint32_t v = R32(c->fs_base + 0xe10 + 4 * ARG(1));
    w32_set_last_error(c, 0);
    STDRET(1, v);
}

// ---------- synchronisation ----------
// CRITICAL_SECTION: the host mutex index (+1) is stored in LockSemaphore (+16).
#define MAX_CS 512
static SDL_mutex *cs_tab[MAX_CS];
static SDL_mutex *cs_of(CPU *c, uint32_t p) {
    HOSTFN;
    uint32_t i = R32(p + 16);
    if (i == 0 || i > MAX_CS || !cs_tab[i - 1]) rt_fatal("critical section not initialised 0x%08x", p);
    return cs_tab[i - 1];
}
K(InitializeCriticalSection) {
    HOSTFN;
    uint32_t p = ARG(1);
    memset(mb + p, 0, 24);
    for (int i = 0; i < MAX_CS; i++)
        if (!cs_tab[i]) { cs_tab[i] = SDL_CreateMutex(); W32(p + 16, i + 1); STDRET(1, 0); }
    rt_fatal("too many critical sections");
}
K(DeleteCriticalSection) {
    HOSTFN;
    uint32_t i = R32(ARG(1) + 16);
    if (i && i <= MAX_CS && cs_tab[i - 1]) { SDL_DestroyMutex(cs_tab[i - 1]); cs_tab[i - 1] = NULL; }
    W32(ARG(1) + 16, 0);
    STDRET(1, 0);
}
K(EnterCriticalSection) { HOSTFN; SDL_LockMutex(cs_of(c, ARG(1))); STDRET(1, 0); }
K(LeaveCriticalSection) { HOSTFN; SDL_UnlockMutex(cs_of(c, ARG(1))); STDRET(1, 0); }
K(InterlockedIncrement) { HOSTFN; STDRET(1, __atomic_add_fetch((uint32_t *)(mb + ARG(1)), 1, __ATOMIC_SEQ_CST)); }
K(InterlockedDecrement) { HOSTFN; STDRET(1, __atomic_sub_fetch((uint32_t *)(mb + ARG(1)), 1, __ATOMIC_SEQ_CST)); }

K(CreateEventA) {
    HOSTFN;
    Event *e = calloc(1, sizeof *e);
    e->manual = ARG(2) != 0;
    e->state = ARG(3) != 0;
    w32_set_last_error(c, 0);
    STDRET(4, h_new(H_EVENT, e));
}
void w32_event_set(uint32_t h, int v);
static void ev_set(uint32_t h, int v) { w32_event_set(h, v); }
void w32_event_set(uint32_t h, int v) {
    Event *e = h_get(h, H_EVENT);
    if (!e) return;
    SDL_LockMutex(g_sync);
    e->state = v;
    SDL_CondBroadcast(g_sync_cv);
    SDL_UnlockMutex(g_sync);
}
K(SetEvent) { HOSTFN; ev_set(ARG(1), 1); STDRET(1, 1); }
K(ResetEvent) { HOSTFN; ev_set(ARG(1), 0); STDRET(1, 1); }
K(CreateMutexA) { w32_set_last_error(c, 0); STDRET(3, h_new(H_MUTEX, NULL)); }
K(WaitForMultipleObjects) {
    HOSTFN;
    uint32_t n = ARG(1), hs[64];
    for (uint32_t i = 0; i < n && i < 64; i++) hs[i] = R32(ARG(2) + 4 * i);
    uint64_t t0 = SDL_GetPerformanceCounter();
    uint32_t r = w32_wait(n, hs, ARG(3) != 0, ARG(4));
    if (c == g_main_cpu) g_t_wait += SDL_GetPerformanceCounter() - t0;
    STDRET(4, r);
}

static int thread_main(void *p) {
    rt_cpu_profile_thread("guest");
    Thread *t = p;
    HOSTFN;
    W32(t->c->fs_base + 0x24, t->tid);
    rt_guest_call(t->c, t->start, 1, t->param);
    SDL_LockMutex(g_sync);
    t->done = 1;
    SDL_CondBroadcast(g_sync_cv);
    SDL_UnlockMutex(g_sync);
    return 0;
}
K(CreateThread) {
    HOSTFN;
    Thread *t = calloc(1, sizeof *t);
    uint32_t stack = ARG(2) ? ARG(2) : (1u << 20);
    t->c = rt_new_cpu(stack < 0x40000 ? 0x40000 : stack);
    t->start = ARG(3);
    t->param = ARG(4);
    t->tid = next_tid++;
    if (ARG(5) & 4) rt_log("CreateThread: CREATE_SUSPENDED ignored\n");
    if (ARG(6)) W32(ARG(6), t->tid);
    uint32_t h = h_new(H_THREAD, t);
    rt_log("CreateThread start=%08x param=%08x tid=%u\n", t->start, t->param, t->tid);
    t->t = SDL_CreateThread(thread_main, "guest", t);
    STDRET(6, h);
}

// ---------- files ----------
// Read-ahead: the game re-reads streams (music/ambience) in 350-1200 KB blocks from the main thread, ~50 ms of Vita disk
// each. The next block (learned: pos -> pos',n'; otherwise sequential, the game re-reads 2 KB before the end of the previous one) is read by a
// thread while the game runs. Only after a 2nd large read on the same handle (file loads only have one).
#define PF_MIN 262144u
#define PF_MAX 1310720u
typedef struct {
    FILE *f; char path[600]; int ro, pfreuse;
    uint8_t *pf; uint32_t pfcap, pfwant; long pfpos; SDL_Thread *th;
    SDL_atomic_t pfgot, pfdone, pfabort;
    struct { long key, pos; uint32_t n; } nx[8]; unsigned nxi;
    long lastpos; uint32_t lastn;
} File;
static uint32_t s_pf_mem;
static uint32_t file_error(int error, uint32_t fallback);
static uint32_t file_status(uint32_t error);
K(CreateFileA) {
    HOSTFN;
    char hp[600];
    const char *gp = GSTR(ARG(1));
    uint32_t access = ARG(2), disp = ARG(5);
    w32_host_path(gp, hp, sizeof hp);
    const char *mode = (access & 0x40000000u) ? (disp == 3 || disp == 4 ? "r+b" : "w+b") : "rb";
    // Reserve metadata before opening a possibly destructive CREATE_ALWAYS
    // handle. Host OOM must neither dereference NULL nor truncate a save/config.
    File *fl = calloc(1, sizeof *fl);
    if (!fl) { w32_set_last_error(c, 8); STDRET(7, 0xffffffffu); }
    errno = 0;
    FILE *f = fopen(hp, mode);
    if (!f && errno == ENOENT && (access & 0x40000000u) && disp == 4) f = fopen(hp, "w+b"); // OPEN_ALWAYS
    if (!f) {
        uint32_t error = file_error(errno, 2);
        free(fl);
        rt_log("CreateFileA(\"%s\") failed\n", gp);
        w32_set_last_error(c, error);
        STDRET(7, 0xffffffffu);
    }
    fl->f = f;
    fl->ro = !(access & 0x40000000u);
    snprintf(fl->path, sizeof fl->path, "%s", hp);
    // Only immutable game archives may retain a completed stream block.
    // Saves, configuration and writable handles keep the existing policy.
    const char *reuse=getenv("SH4_PREFETCH_REUSE"),*ext=strrchr(hp,'.');
    fl->pfreuse=fl->ro && reuse && !strcmp(reuse,"1") && ext &&
        (!SDL_strcasecmp(ext,".sh4") || !SDL_strcasecmp(ext,".pac"));
    w32_set_last_error(c, 0);
    uint32_t h = h_new(H_FILE, fl);
    rt_log("CreateFileA(\"%s\") -> %x\n", gp, h);
    STDRET(7, h);
}
static File *file_of(uint32_t h) { return h_get(h, H_FILE); }
#ifndef O_BINARY
#define O_BINARY 0
#endif
static int pf_run(void *arg) {
    rt_cpu_profile_thread("prefetch");
    File *fl = arg;
#ifdef __vita__
    // This short-lived SDL thread is created for every stream block. A tester
    // dump reached _open_r here with a null newlib reentrancy pointer. Kernel
    // I/O avoids that path and uses the same ux0 file path.
    SceUID fd = sceIoOpen(fl->path, SCE_O_RDONLY, 0);
    if (fd >= 0 && sceIoLseek(fd, (SceOff)fl->pfpos, SCE_SEEK_SET) == (SceOff)fl->pfpos) {
        uint32_t got = 0;
        while (got < fl->pfwant && !SDL_AtomicGet(&fl->pfabort)) {
            SceSSize r = sceIoRead(fd, fl->pf + got, fl->pfwant - got < 131072 ? fl->pfwant - got : 131072);
            if (r <= 0) break;
            got += (uint32_t)r;
            SDL_AtomicSet(&fl->pfgot, (int)got);
        }
    }
    if (fd >= 0) sceIoClose(fd);
#else
    int fd = open(fl->path, O_RDONLY | O_BINARY);
    if (fd >= 0 && lseek(fd, fl->pfpos, SEEK_SET) == fl->pfpos) {
        uint32_t got = 0;
        while (got < fl->pfwant && !SDL_AtomicGet(&fl->pfabort)) {
            int r = read(fd, fl->pf + got, fl->pfwant - got < 131072 ? fl->pfwant - got : 131072);
            if (r <= 0) break;
            got += (uint32_t)r;
            SDL_AtomicSet(&fl->pfgot, (int)got);
        }
    }
    if (fd >= 0) close(fd);
#endif
    SDL_AtomicSet(&fl->pfdone, 1);
    return 0;
}
static void pf_stop(File *fl) {
    if (!fl->th) return;
    SDL_AtomicSet(&fl->pfabort, 1);
    SDL_WaitThread(fl->th, NULL);
    fl->th = NULL;
}
// All advisory stream buffers share the original two-block ceiling. Reserve
// before allocation so readers on different handles cannot exceed it together.
static int pf_reserve(uint32_t n) {
    uint32_t used=__atomic_load_n(&s_pf_mem,__ATOMIC_RELAXED);
    for (;;) {
        if(used>2*PF_MAX || n>2*PF_MAX-used)return 0;
        if(__atomic_compare_exchange_n(&s_pf_mem,&used,used+n,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED))return 1;
    }
}
static void pf_release(uint32_t n) { __atomic_fetch_sub(&s_pf_mem,n,__ATOMIC_RELAXED); }
// Bounded BGM head cache, opt-in. Only the game CPU reads or replaces this
// immutable copy; other readers continue through the normal prefetch path.
// Ownership ends with the File handle. Closing a handle concurrently with its
// ReadFile remains outside the lifetime contract of the file boundary itself.
static struct { File *owner; uint8_t *bytes; uint32_t capacity,valid; long pos; } pf_head;
static void pf_head_forget(File *fl) {
    if(pf_head.owner!=fl)return;
    free(pf_head.bytes);pf_release(pf_head.capacity);
    memset(&pf_head,0,sizeof pf_head);
}
static int pf_head_take(File *fl,long pos,uint8_t *dst,uint32_t n) {
    if(pf_head.owner!=fl || pos<pf_head.pos || (uint64_t)(pos-pf_head.pos)+n>pf_head.valid)return 0;
    memcpy(dst,pf_head.bytes+(pos-pf_head.pos),n);return 1;
}
static void pf_head_pressure(File *fl,uint32_t n) {
    if(!pf_head.owner || n<PF_MIN)return;
    uint32_t want=n>PF_MAX-4096u?PF_MAX:n+4096u;
    uint32_t used=__atomic_load_n(&s_pf_mem,__ATOMIC_RELAXED);
    if(want>fl->pfcap && (used>2*PF_MAX || want-fl->pfcap>2*PF_MAX-used))pf_head_forget(pf_head.owner);
}
static void pf_head_remember(File *fl,long pos,const uint8_t *src,uint32_t n) {
    if(!fl->ro || !fl->pfreuse || pos<0 || n<(1u<<20) || n>PF_MAX)return;
    static int enabled=-1;
    if(enabled<0){const char *e=getenv("SH4_AUDIO_HEAD_CACHE");enabled=e&&!strcmp(e,"1");}
    if(!enabled)return;
    const char *name=strrchr(fl->path,'/');if(!name)name=strrchr(fl->path,'\\');name=name?name+1:fl->path;
    if(SDL_strcasecmp(name,"bgm.sh4"))return;
    // Keep a prefix, not another full streaming buffer. One MiB contains the
    // observed loop restart and leaves room for both existing audio jobs.
    if(n>(1u<<20))n=1u<<20;
    if(pf_head.owner==fl && pf_head.pos==pos && pf_head.valid==n)return;
    if(pf_head.owner && pf_head.owner!=fl)pf_head_forget(pf_head.owner);
    if(pf_head.capacity<n){
        uint32_t extra=n-pf_head.capacity;
        if(!pf_reserve(extra))return;
        uint8_t *bytes=realloc(pf_head.bytes,n);
        if(!bytes){pf_release(extra);return;}
        pf_head.bytes=bytes;pf_head.capacity=n;
    }
    memcpy(pf_head.bytes,src,n);pf_head.owner=fl;pf_head.pos=pos;pf_head.valid=n;
}
static void pf_start(File *fl, long pos, uint32_t n) {
    if (n > PF_MAX) n = PF_MAX;
    // A cyclic stream can ask for the exact block already held or being read.
    // Keep that job/buffer instead of reopening and rereading the same bytes.
    // An incomplete finished job must still be retried by the original path.
    if(fl->pfreuse && fl->th && fl->pfpos==pos && fl->pfwant==n &&
       (!SDL_AtomicGet(&fl->pfdone) || (uint32_t)SDL_AtomicGet(&fl->pfgot)>=n)) return;
    pf_stop(fl);
    if (fl->pfcap < n) {
        uint32_t extra=n-fl->pfcap;
        if (!pf_reserve(extra)) return;
        uint8_t *b = realloc(fl->pf, n);
        if (!b) { pf_release(extra); return; }
        fl->pf = b; fl->pfcap = n;
    }
    fl->pfpos = pos; fl->pfwant = n;
    SDL_AtomicSet(&fl->pfgot, 0); SDL_AtomicSet(&fl->pfdone, 0); SDL_AtomicSet(&fl->pfabort, 0);
    fl->th = SDL_CreateThread(pf_run, "sh4pf", fl);
}
// true if [pos,pos+n) is served from the read-ahead (waits for the thread's progress if needed)
static int pf_take(File *fl, long pos, uint8_t *dst, uint32_t n) {
    if (!fl->th || pos < fl->pfpos || (uint64_t)(pos - fl->pfpos) + n > fl->pfwant) return 0;
    uint32_t need = (uint32_t)(pos - fl->pfpos) + n;
    for (;;) {
        int d = SDL_AtomicGet(&fl->pfdone);
        if ((uint32_t)SDL_AtomicGet(&fl->pfgot) >= need) break;
        if (d) return 0;
        SDL_Delay(1);
    }
    SDL_memcpy(dst, fl->pf + (pos - fl->pfpos), n);
    return 1;
}
static void pf_plan(File *fl, long pos, uint32_t n) {
    // Short tails must also become nodes in the learned stream. Otherwise the
    // preceding full block learns a jump straight to the loop restart, causing
    // the next tail to miss even though the first tail used a prefetched range.
    int small = 0;
    if (fl->ro && fl->pfreuse && n >= 65536 && n < PF_MIN) {
        const char *e = getenv("SH4_PREFETCH_SMALL"); small = e && !strcmp(e,"1");
    }
    if (!fl->ro || (!small && n < PF_MIN) || n > PF_MAX) return;
    int stream = fl->lastn != 0;
    if (stream) {
        unsigned i = fl->nxi++ & 7;
        fl->nx[i].key = fl->lastpos; fl->nx[i].pos = pos; fl->nx[i].n = n;
    }
    fl->lastpos = pos; fl->lastn = n;
    if (!stream || small) return; // Learn the tail, but do not launch an unneeded job after it.
    long ppos = pos + (long)n - 4096; uint32_t pn = n;
    int learned = 0;
    for (int i = 0; i < 8; i++) if (fl->nx[i].n && fl->nx[i].key == pos) { ppos = fl->nx[i].pos; pn = fl->nx[i].n; learned = 1; }
    static int guard = -1;
    if (guard < 0) {
        const char *e = getenv("SH4_PREFETCH_GUARD");
#ifdef __vita__
        guard = !e || !strcmp(e, "1");
#else
        guard = e && !strcmp(e, "1");
#endif
    }
    // A sequential block can overlap by 0, 2 or 4 KiB. The old fallback
    // predicts a 4 KiB overlap but no tail slack, missing a full-sized read
    // starting 2 KiB later. Learned exact ranges keep their original sizes.
    if (guard && fl->pfreuse && !learned) pn += PF_MAX - pn < 4096u ? PF_MAX - pn : 4096u;
    pf_start(fl, ppos, pn);
}
// fread goes through the stdio buffer (blocks of a few KB: ~3 MB/s on ux0 -> 150-230 ms frames); read() gives sceIoRead the whole size at once.
static uint32_t read_direct(FILE *fp, uint8_t *dst, uint32_t n, int *error) {
    *error = 0;
    errno = 0;
#ifdef __vita__
    long pos = ftell(fp);
    if (pos < 0 || fseek(fp, pos, SEEK_SET)) {
        *error = errno ? errno : EIO;
        return 0;
    } // drops the stdio buffer before direct descriptor access
    uint32_t got = 0;
    while (got < n) {
        errno = 0;
        int r = read(fileno(fp), dst + got, n - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            *error = errno ? errno : EIO;
            break;
        }
        if (!r) break; // EOF is not a storage error
        got += (uint32_t)r;
    }
    errno = 0;
    if (fseek(fp, pos + (long)got, SEEK_SET) && !*error)
        *error = errno ? errno : EIO;
    return got;
#else
    uint32_t got = (uint32_t)fread(dst, 1, n, fp);
    if (ferror(fp)) *error = errno ? errno : EIO;
    return got;
#endif
}
K(ReadFile) {
    HOSTFN;
    File *f = file_of(ARG(1));
    uint32_t buf = ARG(2), n = ARG(3), outn = ARG(4), ov = ARG(5);
    uint32_t error = 0, got = 0;
    int io_error = 0;
    if (outn) W32(outn, 0);
    if (!f) error = 6;
    else if ((uint64_t)buf + n > g_mem_size) error = 998;
    else if (ov) {
        uint32_t low = R32(ov + 8), high = R32(ov + 12);
        if (high || low > 0x7fffffffu) error = 87;
        else { errno = 0; if (fseek(f->f, (long)low, SEEK_SET)) error = file_error(errno, 25); }
    }
    long pos0 = -1;
    if (!error) {
        errno = 0; pos0 = ftell(f->f);
        if (pos0 < 0) error = file_error(errno, 25);
    }
    uint64_t t_io = SDL_GetPerformanceCounter();
    int pfhit = 0, headhit = 0;
    if (!error) {
        clearerr(f->f);
        errno = 0;
        if(c==g_main_cpu)pf_head_pressure(f,n);
        int use_pf = n >= PF_MIN;
        if (!use_pf && n >= 65536 && f->ro && f->pfreuse) {
            static int small = -1;
            if (small < 0) { const char *e = getenv("SH4_PREFETCH_SMALL"); small = e && !strcmp(e,"1"); }
            use_pf = small;
        }
        // A short audio tail can already lie in the full-sized pending job.
        // Accepting that exact range needs no new allocation or prediction;
        // pf_take still rejects incomplete jobs and partial EOF as before.
        if (use_pf && ((c==g_main_cpu && (headhit=pf_head_take(f,pos0,mb+buf,n))) || pf_take(f, pos0, mb + buf, n))) {
            got = n; pfhit = 1;
            errno = 0;
            if (fseek(f->f, pos0 + (long)n, SEEK_SET)) io_error = errno ? errno : EIO;
        } else if (n >= 16384) got = read_direct(f->f, mb + buf, n, &io_error);
        else {
            errno = 0;
            got = (uint32_t)fread(mb + buf, 1, n, f->f);
            if (ferror(f->f)) io_error = errno ? errno : EIO;
        }
        if (io_error) error = file_error(io_error, 1117); // ERROR_IO_DEVICE
        if (!error && got == n) {
            pf_plan(f, pos0, n);
            if(c==g_main_cpu)pf_head_remember(f,pos0,mb+buf,n);
        }
        // Preserve the GOG resource reader's sector padding contract at EOF.
        if (got < n) SDL_memset(mb + buf + got, 0, n - got);
    }
    uint64_t d_io = SDL_GetPerformanceCounter() - t_io;
    if (c == g_main_cpu) { g_t_io += d_io; g_io_bytes += got; }
    static int io_tail_profile = -1;
    if (io_tail_profile < 0) { const char *e = getenv("SH4_IO_TAIL_PROFILE"); io_tail_profile = e && !strcmp(e,"1"); }
    if (got >= 262144 || (io_tail_profile && got >= 65536)) {
        static unsigned s_big;
        extern unsigned g_frame;
        if (++s_big <= 400) rt_log("[io] frame %u h=%x pos=%ld n=%u KB=%u %.1f ms (%.1f MB/s) thread=%d ahead=%d caller=%08x head=%d\n", g_frame, ARG(1), pos0, n, got >> 10,
                                   d_io * 1000.0 / SDL_GetPerformanceFrequency(), got / 1048576.0 / (d_io / (double)SDL_GetPerformanceFrequency() + 1e-9),
                                   c == g_main_cpu, pfhit, R32(c->esp), headhit);
    }
    {
        static unsigned s_rf;
        if (++s_rf <= 20 || s_rf % 100 == 0)
            rt_log("ReadFile #%u h=%x n=%u -> %u\n", s_rf, ARG(1), n, got);
    }
    if (outn) W32(outn, got);
    if (ov) {
        W32(ov, error ? file_status(error) : 0);
        W32(ov + 4, got);
        if (R32(ov + 16)) ev_set(R32(ov + 16), 1);
    }
    if (error) {
        rt_log("[io-error] ReadFile h=%x pos=%ld requested=%u got=%u error=%u errno=%d\n",
               ARG(1), pos0, n, got, error, io_error);
        w32_set_last_error(c, error);
    }
    STDRET(5, error ? 0 : 1);
}
// Translate storage failures at the compatibility boundary. Capture errno
// immediately: logging, cleanup and event signalling may change it.
static uint32_t file_error(int error, uint32_t fallback) {
    switch (error) {
    case ENOENT: return 2;
    case ENOTDIR: return 3;
    case EEXIST: return 80;
    case EROFS: return 19;
    case EMFILE: case ENFILE: return 4;
    case ENOSPC: return 112; // ERROR_DISK_FULL
    case EACCES: case EPERM: return 5;
    case EBADF: return 6;
    case ENOMEM: return 8;
    case EINVAL: return 87;
    default: return fallback;
    }
}
static uint32_t file_status(uint32_t error) {
    switch (error) {
    case 112: return 0xc000007fu; // STATUS_DISK_FULL
    case 5: return 0xc0000022u;
    case 6: return 0xc0000008u;
    case 8: return 0xc0000017u;
    case 87: return 0xc000000du;
    default: return 0xc0000185u; // STATUS_IO_DEVICE_ERROR
    }
}
K(WriteFile) {
    HOSTFN;
    File *f = file_of(ARG(1));
    uint32_t buf = ARG(2), n = ARG(3), outn = ARG(4), ov = ARG(5);
    uint32_t put = 0, error = 0;
    if (outn) W32(outn, 0);
    if (!f) error = 6;
    else if (f->ro) error = 5;
    else if ((uint64_t)buf + n > g_mem_size) error = 998; // ERROR_NOACCESS
    else if (ov) {
        uint32_t low = R32(ov + 8), high = R32(ov + 12);
        // The current FILE backend has 32-bit signed offsets. Reject rather
        // than wrap an unsupported offset; preserve Win32's append sentinel.
        int append = low == 0xffffffffu && high == 0xffffffffu;
        if (!append && (high || low > 0x7fffffffu)) error = 87;
        else {
            errno = 0;
            if (fseek(f->f, append ? 0 : (long)low, append ? SEEK_END : SEEK_SET))
                error = file_error(errno, 25); // ERROR_SEEK
        }
    }
    if (!error && n) {
        errno = 0;
        put = (uint32_t)fwrite(mb + buf, 1, n, f->f);
        if (put != n) error = file_error(errno, 29); // ERROR_WRITE_FAULT
    }
    if (outn) W32(outn, put);
    if (ov) { W32(ov, error ? file_status(error) : 0); W32(ov + 4, put); if (R32(ov + 16)) ev_set(R32(ov + 16), 1); }
    if (error) {
        rt_log("[file] WriteFile h=%x requested=%u written=%u error=%u\n", ARG(1), n, put, error);
        w32_set_last_error(c, error);
    }
    STDRET(5, error == 0);
}
K(GetOverlappedResult) {
    HOSTFN;
    if (!file_of(ARG(1))) { w32_set_last_error(c, 6); STDRET(4, 0); }
    if (!ARG(2)) { w32_set_last_error(c, 87); STDRET(4, 0); }
    if (ARG(3)) W32(ARG(3), R32(ARG(2) + 4));
    uint32_t status = R32(ARG(2));
    if (status) {
        uint32_t error = status == 0x103u ? 996 : // STATUS_PENDING / ERROR_IO_INCOMPLETE
            status == 0xc000007fu ? 112 : status == 0xc0000022u ? 5 :
            status == 0xc0000008u ? 6 : status == 0xc0000017u ? 8 :
            status == 0xc000000du ? 87 : 1117;
        w32_set_last_error(c, error);
        STDRET(4, 0);
    }
    STDRET(4, 1);
}
K(SetFilePointer) {
    HOSTFN;
    File *f = file_of(ARG(1));
    if (!f) STDRET(4, 0xffffffffu);
    int64_t off = (int32_t)ARG(2);
    if (ARG(3)) off = (int64_t)(((uint64_t)R32(ARG(3)) << 32) | ARG(2));
    fseek(f->f, (long)off, ARG(4) == 0 ? SEEK_SET : ARG(4) == 1 ? SEEK_CUR : SEEK_END);
    long pos = ftell(f->f);
    if (ARG(3)) W32(ARG(3), 0);
    STDRET(4, (uint32_t)pos);
}
K(GetFileSize) {
    HOSTFN;
    File *f = file_of(ARG(1));
    if (!f) STDRET(2, 0xffffffffu);
    long cur = ftell(f->f);
    fseek(f->f, 0, SEEK_END);
    long n = ftell(f->f);
    fseek(f->f, cur, SEEK_SET);
    rt_log("GetFileSize h=%x -> %ld\n", ARG(1), n);
    if (ARG(2)) W32(ARG(2), 0);
    STDRET(2, (uint32_t)n);
}
K(FlushFileBuffers) {
    HOSTFN;
    File *f = file_of(ARG(1));
    if (!f) { w32_set_last_error(c, 6); STDRET(1, 0); }
    if (f->ro) { w32_set_last_error(c, 5); STDRET(1, 0); }
    errno = 0;
    if (fflush(f->f)) {
        uint32_t error = file_error(errno, 29);
        rt_log("[file] FlushFileBuffers h=%x error=%u\n", ARG(1), error);
        w32_set_last_error(c, error);
        STDRET(1, 0);
    }
    // This drains stdio only; physical-media synchronisation is a separate
    // platform contract, not a promise of power-loss durability.
    STDRET(1, 1);
}
K(CloseHandle) {
    HOSTFN;
    uint32_t h = ARG(1);
    File *f = file_of(h);
    if (h_type(h) == H_FREE) { w32_set_last_error(c, 6); STDRET(1, 0); }
    uint32_t error = 0;
    if (f) {
        pf_stop(f); pf_head_forget(f); pf_release(f->pfcap); free(f->pf);
        errno = 0;
        if (fclose(f->f)) error = file_error(errno, 29);
        free(f);
    }
    h_close(h);
    if (error) {
        rt_log("[file] CloseHandle h=%x error=%u\n", h, error);
        w32_set_last_error(c, error);
    }
    STDRET(1, error == 0);
}
K(CreateDirectoryA) {
    HOSTFN;
    char hp[600];
    w32_host_path(GSTR(ARG(1)), hp, sizeof hp);
    int r = MKDIR(hp);
    if (r) w32_set_last_error(c, 183);
    STDRET(2, r == 0);
}
K(GetFileAttributesA) {
    HOSTFN;
    char hp[600];
    struct stat st;
    w32_host_path(GSTR(ARG(1)), hp, sizeof hp);
    if (stat(hp, &st)) { w32_set_last_error(c, 2); STDRET(1, 0xffffffffu); }
    STDRET(1, S_ISDIR(st.st_mode) ? 0x10 : 0x80);
}
K(SetFileAttributesA) { STDRET(2, 1); }

// ---------- resources (.rsrc of the loaded image) ----------
static uint32_t rsrc_root(void) {
    HOSTFN;
    uint32_t pe = 0x400000 + R32(0x40003c);
    return 0x400000 + R32(pe + 24 + 96 + 8 * 2);
}
static int rsrc_match(uint32_t root, uint32_t ent, uint32_t key, int wide) {
    HOSTFN;
    uint32_t id = R32(ent);
    if (key < 0x10000) return !(id & 0x80000000u) && id == key;
    if (!(id & 0x80000000u)) {
        const char *s = wide ? NULL : GSTR(key);
        return s && s[0] == '#' && (uint32_t)atoi(s + 1) == id;
    }
    uint32_t str = root + (id & 0x7fffffffu), len = R16(str);
    for (uint32_t i = 0; i < len; i++) {
        uint32_t a = R16(str + 2 + 2 * i), b = wide ? R16(key + 2 * i) : R8(key + i);
        if (toupper((int)(a & 0xff)) != toupper((int)(b & 0xff))) return 0;
    }
    return (wide ? R16(key + 2 * len) : R8(key + len)) == 0;
}
static uint32_t rsrc_find(uint32_t name, uint32_t type, int wide) {
    HOSTFN;
    uint32_t root = rsrc_root(), dir = root, keys[2] = {type, name};
    for (int lvl = 0; lvl < 3; lvl++) {
        uint32_t n = R16(dir + 12) + R16(dir + 14), ent = dir + 16, found = 0;
        for (uint32_t i = 0; i < n; i++, ent += 8)
            if (lvl == 2 || rsrc_match(root, ent, keys[lvl], wide)) { found = ent; break; }
        if (!found) return 0;
        uint32_t off = R32(found + 4);
        if (lvl == 2) return root + off;
        dir = root + (off & 0x7fffffffu);
    }
    return 0;
}
K(FindResourceA) { HOSTFN; STDRET(3, rsrc_find(ARG(2), ARG(3), 0)); }
K(FindResourceW) { HOSTFN; STDRET(3, rsrc_find(ARG(2), ARG(3), 1)); }
K(LoadResource) { HOSTFN; STDRET(2, ARG(2)); }
K(LockResource) { HOSTFN; STDRET(1, ARG(1) ? 0x400000 + R32(ARG(1)) : 0); }
K(SizeofResource) { HOSTFN; STDRET(2, ARG(2) ? R32(ARG(2) + 4) : 0); }
