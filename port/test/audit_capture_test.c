// Exercise the real capture handoff with a producer and a render consumer.
#include <assert.h>
#include <stdarg.h>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "../src/rt/audit.c"

uint8_t *g_mem;
uint32_t g_mem_size = 0x02000000;
char g_game_dir[512] = "test";
unsigned g_frame;
int g_movie_w, g_movie_h;

void rt_log(const char *format, ...) { (void)format; }
void rt_log_flush(void) {}
_Noreturn void rt_fatal(const char *format, ...) { (void)format; abort(); }
uint32_t rt_guest_call(CPU *c, uint32_t address, int nargs, ...) { abort(); }
uint32_t gheap_calloc(uint32_t bytes) { abort(); }
void gheap_free(uint32_t address) { abort(); }

enum { REQUESTS = 100000 };
static int publish(void *unused) {
    (void)unused;
    for (unsigned i = 1; i <= REQUESTS; i++) {
        while (__atomic_load_n(&capture_frame, __ATOMIC_ACQUIRE)) SDL_CPUPauseInstruction();
        capture_index = i;
        capture_world = i % 11 + 1;
        capture_scene = i % 37;
        capture_angle = i % 3;
        __atomic_store_n(&capture_frame, 1, __ATOMIC_RELEASE);
    }
    return 0;
}

int main(void) {
    assert(SDL_Init(SDL_INIT_TIMER) == 0);
    g_mem = calloc(1, 0x02000000);
    assert(g_mem);
    uint8_t *const mb = g_mem;
    W32(0xfd5a90, 7); W32(0xfd5a94, 9);
#ifdef _WIN32
    _putenv_s("SH4_AUDIT_SHOTS", "100");
    _putenv_s("SH4_AUDIT_HEAL", "0");
#else
    setenv("SH4_AUDIT_SHOTS", "100", 1);
    setenv("SH4_AUDIT_HEAL", "0", 1);
#endif
    g_audit_enabled = 1;
    g_frame = 100;
    CPU cpu = {0};
    char path[600];
    rt_audit_input(&cpu, 0, 0);
    // The already-issued present must never consume a newly published request.
    assert(!rt_audit_capture(100, path, sizeof path));
    assert(rt_audit_capture(101, path, sizeof path));
    assert(!strcmp(path, "test/audit_001_w7_s09_a0.bmp"));
    SDL_Thread *producer = SDL_CreateThread(publish, "capture-test", NULL);
    assert(producer);
    for (unsigned i = 1; i <= REQUESTS; i++) {
        while (!rt_audit_capture(1, path, sizeof path)) SDL_CPUPauseInstruction();
        unsigned index, world, scene, angle;
        assert(sscanf(path, "test/audit_%u_w%u_s%u_a%u.bmp", &index, &world, &scene, &angle) == 4);
        assert(index == i && world == i % 11 + 1 && scene == i % 37 && angle == i % 3);
    }
    SDL_WaitThread(producer, NULL);
    assert(!rt_audit_capture(1, path, sizeof path));
    free(g_mem);
    SDL_Quit();
    puts("Actual audit handoff: upcoming present selected; 100000 concurrent requests preserve all metadata.");
    return 0;
}
