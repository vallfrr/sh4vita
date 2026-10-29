// Handles Win32, attentes et chemins.
#include "win32.h"
#include <ctype.h>

#define MAX_H 1024
static struct { int type; void *obj; } htab[MAX_H];
static SDL_mutex *hlock;
SDL_mutex *g_sync;
SDL_cond *g_sync_cv;

static void lazy_init(void) {
    if (!hlock) {
        hlock = SDL_CreateMutex();
        g_sync = SDL_CreateMutex();
        g_sync_cv = SDL_CreateCond();
    }
}

uint32_t h_new(int type, void *obj) {
    lazy_init();
    SDL_LockMutex(hlock);
    for (int i = 1; i < MAX_H; i++)
        if (htab[i].type == H_FREE) {
            htab[i].type = type;
            htab[i].obj = obj;
            SDL_UnlockMutex(hlock);
            return 0x1000u + 4u * i;
        }
    SDL_UnlockMutex(hlock);
    rt_fatal("handle table full");
}

static int idx(uint32_t h) {
    int i = (int)((h - 0x1000u) / 4u);
    return h >= 0x1000u && (h & 3) == 0 && i < MAX_H ? i : 0;
}

int h_type(uint32_t h) { return htab[idx(h)].type; }

void *h_get(uint32_t h, int type) {
    int i = idx(h);
    return i && htab[i].type == type ? htab[i].obj : NULL;
}

void h_close(uint32_t h) {
    int i = idx(h);
    if (!i) return;
    SDL_LockMutex(hlock);
    htab[i].type = H_FREE;
    htab[i].obj = NULL;
    SDL_UnlockMutex(hlock);
}

int w32_signaled(uint32_t h) {
    switch (h_type(h)) {
    case H_EVENT: return ((Event *)h_get(h, H_EVENT))->state;
    case H_THREAD: return ((Thread *)h_get(h, H_THREAD))->done;
    case H_MUTEX: return 1;
    default: return 1;
    }
}

void w32_consume(uint32_t h) {
    Event *e = h_get(h, H_EVENT);
    if (e && !e->manual) e->state = 0;
}

uint32_t w32_wait(uint32_t n, const uint32_t *hs, int all, uint32_t timeout_ms) {
    lazy_init();
    static unsigned s_wait_n;
    unsigned wait_id = ++s_wait_n;
    if (wait_id <= 5 || wait_id % 200 == 0)
        rt_log("w32_wait #%u n=%u all=%d to=%u h0=%x\n", wait_id, n, all, timeout_ms, n > 0 ? hs[0] : 0);
    uint32_t start = SDL_GetTicks();
    SDL_LockMutex(g_sync);
    for (;;) {
        uint32_t ok = 0, first = 0xffffffffu;
        for (uint32_t i = 0; i < n; i++)
            if (w32_signaled(hs[i])) { ok++; if (first == 0xffffffffu) first = i; }
        if (all ? ok == n : ok > 0) {
            if (all) for (uint32_t i = 0; i < n; i++) w32_consume(hs[i]);
            else w32_consume(hs[first]);
            SDL_UnlockMutex(g_sync);
            if (wait_id <= 5 || wait_id % 200 == 0) rt_log("w32_wait #%u -> %u\n", wait_id, all ? 0 : first);
            return all ? 0 : first;
        }
        uint32_t el = SDL_GetTicks() - start;
        if (timeout_ms != 0xffffffffu && el >= timeout_ms) break;
        SDL_CondWaitTimeout(g_sync_cv, g_sync, timeout_ms == 0xffffffffu ? 50 : (timeout_ms - el < 50 ? timeout_ms - el : 50));
    }
    SDL_UnlockMutex(g_sync);
    if (wait_id <= 5 || wait_id % 200 == 0) rt_log("w32_wait #%u -> TIMEOUT\n", wait_id);
    return WAIT_TIMEOUT_;
}

void w32_set_last_error(CPU *c, uint32_t e) {
    HOSTFN;
    W32(c->fs_base + 0x34, e);
}

// SH4_VTIME=1 (PC profiling): virtual 30 Hz clock + 10 us per read, so the game never sleeps.
extern unsigned g_frame;
static int vtime(void) { static int v = -1; if (v < 0) v = getenv("SH4_VTIME") != NULL; return v; }
unsigned w32_hz(void) {   // no game logic (30 originally); SH4_HZ=60 to test a 60 Hz loop
    static unsigned h;
    if (!h) { const char *e = getenv("SH4_HZ"); h = e && atoi(e) >= 15 && atoi(e) <= 120 ? (unsigned)atoi(e) : 30; }
    return h;
}
uint64_t w32_qpc(void) {
    static uint64_t calls;
    if (!vtime()) return SDL_GetPerformanceCounter();
    return ((uint64_t)g_frame * 1000000 / w32_hz() + ++calls * 10) * SDL_GetPerformanceFrequency() / 1000000;
}
// Sleeps until QPC instant t to within ~1 ms (the caller finishes with a busy wait); nothing with the virtual clock.
void w32_sleep_until(uint64_t t) {
    if (vtime()) return;
    uint64_t now = SDL_GetPerformanceCounter(), f = SDL_GetPerformanceFrequency();
    if (t <= now || t - now >= f) return;
    uint32_t ms = (uint32_t)((t - now) * 1000 / f);
    if (ms >= 2) SDL_Delay(ms - 1);
}
uint32_t w32_tick_ms(void) { return vtime() ? (uint32_t)(w32_qpc() * 1000 / SDL_GetPerformanceFrequency()) : SDL_GetTicks(); }

void w32_host_path(const char *g, char *out, size_t n) {
    char tmp[512];
    size_t k = 0;
    for (; *g && k < sizeof tmp - 1; g++) tmp[k++] = *g == '\\' ? '/' : *g;
    tmp[k] = 0;
    const char *p = tmp;
    if (!SDL_strncasecmp(p, "C:/SH4/", 7)) p += 7;
    while (p[0] == '.' && p[1] == '/') p += 2;
    if (p[0] && p[1] == ':') { SDL_snprintf(out, n, "%s", p); return; }
    SDL_snprintf(out, n, "%s/%s", g_game_dir, p);
}
