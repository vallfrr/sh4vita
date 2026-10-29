// Native tests for diagnostic gating, transition acknowledgement and malformed tours.
#include "audit.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <assert.h>
#include <stdlib.h>

uint8_t *g_mem;
char g_game_dir[512];
unsigned g_frame;
int g_movie_w, g_movie_h;
uint64_t g_heap_used, g_t_body, g_t_gl, g_t_io, g_t_wait, g_t_sleep, g_t_link, g_t_tex;
static unsigned calls, last_fn, timeouts, completed;

void rt_log(const char *fmt, ...) {
    if (strstr(fmt, "event=timeout")) timeouts++;
    if (strstr(fmt, "event=complete")) completed++;
}
void rt_log_flush(void) {}
_Noreturn void rt_fatal(const char *fmt, ...) { fprintf(stderr, "%s\n", fmt); exit(42); }
uint32_t gheap_calloc(uint32_t n) { assert(n == 16); memset(g_mem + 0x30000, 0, n); return 0x30000; }
void gheap_free(uint32_t p) { assert(p == 0x30000); }
uint32_t rt_guest_call(CPU *c, uint32_t fn, int nargs, ...) {
    (void)c; (void)nargs;
    uint8_t *const mb = g_mem;
    calls++; last_fn = fn;
    va_list ap; va_start(ap, nargs);
    if (fn == 0x4f72b0) { W32(0xfd5aa0, va_arg(ap, unsigned)); W32(0xfd5aa4, va_arg(ap, unsigned)); }
    else if (fn == 0x4f7220) { W32(0xfd5aa4, va_arg(ap, unsigned)); W32(0xfd5aa8, va_arg(ap, unsigned)); }
    else assert(fn == 0x52cee0);
    va_end(ap);
    return 0;
}
static void env(const char *key, const char *value) {
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}
static void tick(unsigned frame) {
    CPU cpu = {0}; g_frame = frame;
    rt_audit_input(&cpu, 0x1000, 256);
    rt_audit_frame(frame);
}
static void table(unsigned world) {
    uint8_t *const mb = g_mem;
    W32(0x62fffc + world * 4, 0x20000); W32(0x20000, 2);
    for (unsigned s = 1; s <= 2; s++) {
        uint32_t blob = 0x21000 + s * 0x100;
        W32(0x20000 + (s + 3) * 4, blob); W8(blob, 1); W8(blob + 4 + 12, 1);
    }
}

int main(int argc, char **argv) {
    assert(argc == 3);
    snprintf(g_game_dir, sizeof g_game_dir, "%s", argv[1]);
    const char *mode = argv[2];
    g_mem = calloc(1, 24u << 20); assert(g_mem);
    uint8_t *const mb = g_mem;
    env("SH4_AUDIT", strcmp(mode, "disabled") ? "1" : "0");
    env("SH4_AUDIT_START", "1"); env("SH4_AUDIT_SETTLE", "30"); env("SH4_AUDIT_DWELL", "60");
    env("SH4_AUDIT_TIMEOUT", !strcmp(mode, "timeout") ? "100" : "200");
    char path[600]; snprintf(path, sizeof path, "%s/tour.txt", argv[1]);
    FILE *f = fopen(path, "w"); assert(f);
    if (!strcmp(mode, "malformed")) fputs("2 1 1 junk\n", f);
    else if (!strcmp(mode, "background")) fputs("1 1 1 0 24\n", f);
    else fputs("2 1 1\n2 2 1\n", f);
    fclose(f); env("SH4_AUDIT_TOUR", path);
    table(1); table(2); W32(0xfd5a90, 1); W32(0xfd5a94, 1);
    W32(0x62fdf0, 111); W32(0x62fdf4, 222);
    rt_audit_init(); tick(1);
    if (!strcmp(mode, "disabled")) {
        assert(!g_audit_enabled && !calls); assert(R32(0x62fdf0) == 111); puts("disabled OK"); return 0;
    }
    if (!strcmp(mode, "timeout")) {
        tick(102); assert(timeouts == 1 && !completed && calls == 2); puts("timeout OK"); return 0;
    }
    char capture[600];
    if (!strcmp(mode, "background")) {
        assert(R32(0x62fdf0) == 24 && !R32(0x62fdf4));
        W32(0xfd5aa0, 0); W32(0xfd5aa4, 0);
        for (unsigned i = 2; i <= 97; i++) tick(i);
        assert(completed == 1 && R32(0x62fdf0) == 111 && R32(0x62fdf4) == 222);
        puts("selector restored OK"); return 0;
    }
    assert(last_fn == 0x4f72b0);
    W32(0xfd5a90, 2); W32(0xfd5a94, 1);
    if (!strcmp(mode, "late")) {
        W32(0xfd5aa0, 0); W32(0xfd5aa4, 0);
        for (unsigned i = 190; i <= 285; i++) tick(i);
        assert(completed == 1 && !timeouts && calls == 2);
        puts("late successful load gets a complete measurement interval OK"); return 0;
    }
    for (unsigned i = 2; i <= 30; i++) tick(i);
    assert(!rt_audit_capture(30, capture, sizeof capture)); // id matches but load request is still pending
    W32(0xfd5aa0, 0); W32(0xfd5aa4, 0);
    if (!strcmp(mode, "movie")) {
        g_movie_w = 640; g_movie_h = 480;
        for (unsigned i = 31; i <= 130; i++) tick(i);
        assert(!completed && !rt_audit_capture(130, capture, sizeof capture));
        g_movie_w = g_movie_h = 0;
        for (unsigned i = 131; i <= 161; i++) tick(i);
        assert(!rt_audit_capture(161, capture, sizeof capture));
        assert(rt_audit_capture(162, capture, sizeof capture));
        puts("movie excluded from arrival and measurement OK"); return 0;
    }
    for (unsigned i = 31; i <= 61; i++) tick(i);
    assert(!rt_audit_capture(61, capture, sizeof capture));
    assert(rt_audit_capture(62, capture, sizeof capture) && strstr(capture, "w2_s01"));
    assert(!rt_audit_capture(62, capture, sizeof capture));
    for (unsigned i = 62; i <= 126; i++) tick(i);
    assert(completed == 1 && last_fn == 0x4f7220 && calls == 2);
    puts("acknowledgement and same-world transition OK");
    free(g_mem); return 0;
}
