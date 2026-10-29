// Host regression for the options extension: staging, bounds, discard and persistence.
// gcc -Isrc/rt test/options_test.c src/rt/options.c src/rt/staged_file.c -lm -o ../build/options_test
// Run in a disposable directory: options_test <directory> [expected-loaded-value]
#include "rt.h"
#include <assert.h>
#include <stdlib.h>

uint8_t *g_mem;
char g_game_dir[512];
static uint32_t bump = 0x1400000, buttons;
static unsigned sounds, last_row;
static CPU cpu;
unsigned rt_save_failure_serial(void) { return 0; }
uint32_t rt_save_error_code(void) { return 0; }
void rt_save_report_error(unsigned language, uint32_t error) { assert(!"unexpected save error"); }
void rt_fatal(const char *format, ...) { abort(); }

uint32_t gheap_alloc(uint32_t n) { uint32_t p = bump; bump += n; return p; }
void rt_log(const char *fmt, ...) { (void)fmt; }
uint32_t rt_guest_call(CPU *c, uint32_t fn, int n, ...) {
    (void)c; (void)n;
    if (fn == 0x4f85a0) return buttons;
    if (fn == 0x56cc40) { sounds++; return 0; }
    assert(fn == 0x4f84a0 || fn == 0x405490);
    return 0;
}
void F_00523e40_orig(CPU *c) { c->esp += 4; }
void F_00526810_orig(CPU *c) { c->eax = 1; c->esp += 4; }
void F_00523aa0_orig(CPU *c) { last_row = c->eax; c->eax = 0; c->esp += 4; }
void F_005254f0_orig(CPU *c) { c->esp += 4; }
void F_0050c440_orig(CPU *c) { assert(!"unexpected stock text lookup"); }

void nat_optbegin(CPU *c), nat_optsave(CPU *c), nat_camslider(CPU *c);
void nat_camvalue(CPU *c), nat_optanychanged(CPU *c), nat_optchanged(CPU *c), nat_opttext(CPU *c);
static void call(void (*f)(CPU *)) {
    cpu.esp = 0x2000;
    f(&cpu);
    assert(cpu.esp == 0x2004); // cdecl RET, caller owns the arguments
}
static void value(int n) { call(nat_camvalue); assert(cpu.eax == (unsigned)n); }
static void change(unsigned input, int count) {
    buttons = input;
    while (count--) call(nat_camslider);
    buttons = 0;
}

int main(int argc, char **argv) {
    assert(argc >= 2);
    snprintf(g_game_dir, sizeof g_game_dir, "%s", argv[1]);
    g_mem = calloc(1, 32u << 20);
    assert(g_mem);
    int initial = argc > 2 ? atoi(argv[2]) : 3;
    assert(fabsf(rt_camera_scale() - (0.4f + 0.2f * initial)) < 0.00001f);
    if (argc > 2) { puts("reload OK"); free(g_mem); return 0; }
    uint8_t *const mb = g_mem;
    call(nat_optbegin); value(3);
    call(nat_optanychanged); assert(!cpu.eax);
    W8(0x1091ad0 + 0x21, 1); // a byte outside all original per-row switch cases
    call(nat_optanychanged); assert(cpu.eax);
    W8(0x1091ad0 + 0x21, 0);
    change(4, 30); value(0); assert(sounds == 3);
    change(8, 30); value(10); assert(sounds == 13);
    assert(fabsf(rt_camera_scale() - 2.4f) < 0.00001f);
    call(nat_optanychanged); assert(cpu.eax);
    cpu.edx = 1; cpu.eax = 3; call(nat_optchanged); assert(cpu.eax);
    W32(0x1091a20 + 0x114, 1); call(nat_optsave); // No must not persist
    rt_camera_end(); value(3);
    assert(fabsf(rt_camera_scale() - 1.f) < 0.00001f);
    call(nat_optbegin); change(8, 3); value(6);
    W32(0x1091a20 + 0x114, 0); call(nat_optsave); // Yes persists
    rt_camera_end(); value(6);
    call(nat_optbegin); change(4, 1); value(5);
    rt_camera_end(); value(6); // leaving without acceptance rolls back
    cpu.edx = 0; cpu.eax = 0; call(nat_optchanged); assert(last_row == 1);
    cpu.edx = 1; cpu.eax = 2; call(nat_optchanged); assert(last_row == 5);
    cpu.edx = 2; cpu.eax = 6; call(nat_optchanged); assert(last_row == 7);
    for (unsigned lang = 1; lang <= 5; lang++) {
        W8(0x1091ad0 + 0x11, lang);
        W32(0x2004, 0); W32(0x2008, 0x7000);
        call(nat_opttext);
        uint32_t p = cpu.eax;
        assert(R16(p) == 0x8000);
        unsigned n = 2;
        while (R16(p + n) != 0xffff && n < 90) n++;
        assert(n < 90 && n > 8);
        if (lang == 2) assert(R8(p + 12) == 0x17); // é in Sensibilité (after the 2-byte header)
    }
    puts("options staging, bounds, Yes/No, row mapping and 5 labels OK");
    free(g_mem);
    return 0;
}
