"""Exercise the actual guest options initializer with and without saved options.

Requires generated guest sources and the user's supported GOG executable.
No game bytes are stored in this test. Only file I/O and renderer/audio calls
are stubbed; the guest's default/load/menu-copy branches execute unchanged.
"""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include "rt.h"
#include "x86_funcs.h"
#include <assert.h>
#include <stdlib.h>

uint8_t *g_mem;
uint32_t g_mem_size = 32u << 20;
char g_game_dir[512];
static int load_saved;
static uint8_t saved_options[36];
static void ret(CPU *c) { c->esp += 4; }
void rt_log(const char *format, ...) { (void)format; }
uint32_t gheap_alloc(uint32_t size) { (void)size; assert(!"unrelated allocation"); return 0; }
uint32_t rt_guest_call(CPU *c, uint32_t address, int n, ...) {
    (void)c; (void)address; (void)n; assert(!"unrelated menu call"); return 0;
}
#define UNUSED_ORIG(name) void name(CPU *c) { (void)c; assert(!"unrelated options wrapper"); }
UNUSED_ORIG(F_00523e40_orig)
UNUSED_ORIG(F_00526810_orig)
UNUSED_ORIG(F_005254f0_orig)
UNUSED_ORIG(F_00523aa0_orig)
UNUSED_ORIG(F_0050c440_orig)
void F_004089d0(CPU *c) {
    uint8_t *mb = g_mem;
    W32(0x6550bc, load_saved);
    W32(0x1092248, 0);
    ret(c);
}
void F_00408920(CPU *c) { assert(load_saved); ret(c); }
void F_004fbee0(CPU *c) {
    uint8_t *mb = g_mem;
    uint32_t buffer = R32(c->esp + 4);
    memcpy(mb + buffer + 0x5c0, saved_options, sizeof saved_options);
    ret(c);
}
void F_004fbf50(CPU *c) { ret(c); }
void F_00408960(CPU *c) { ret(c); }
void F_0056c420(CPU *c) { ret(c); }
void F_0056f140(CPU *c) { ret(c); }
void F_00524320(CPU *c) { ret(c); }
void F_00575ed6(CPU *c) { ret(c); }

static uint32_t word(const uint8_t *p) { uint32_t n; memcpy(&n, p, 4); return n; }
static uint16_t half(const uint8_t *p) { uint16_t n; memcpy(&n, p, 2); return n; }
static void load_image(const char *name) {
    FILE *f = fopen(name, "rb"); assert(f);
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    uint8_t *data = malloc(n); assert(data);
    assert(fread(data, 1, n, f) == (size_t)n); fclose(f);
    free(g_mem); g_mem = calloc(1, g_mem_size); assert(g_mem);
    unsigned pe = word(data + 0x3c), opt = pe + 24;
    unsigned base = word(data + opt + 28), sections = opt + half(data + pe + 20);
    for (unsigned i = 0; i < half(data + pe + 6); ++i) {
        const uint8_t *s = data + sections + 40 * i;
        unsigned size = word(s + 16), raw = word(s + 20), address = base + word(s + 12);
        assert((size_t)raw + size <= (size_t)n && address + size <= g_mem_size);
        memcpy(g_mem + address, data + raw, size);
    }
    free(data);
    uint8_t *mb = g_mem;
    W32(0xed4f40, 1); // retain all stock options bytes in the no-save branch
}
static void language(const char *value) {
#ifdef _WIN32
    assert(_putenv_s("SH4_LANG", value ? value : "") == 0);
#else
    if (value) assert(setenv("SH4_LANG", value, 1) == 0);
    else assert(unsetenv("SH4_LANG") == 0);
#endif
}
static void initialize(void) {
    CPU c = { .esp = 0x300000 };
    F_00524470(&c);
    assert(c.esp == 0x300004);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    load_image(argv[1]); language("French"); load_saved = 0;
    initialize();
    assert(g_mem[0x1091ae1] == 1); // reproduce: original registry path leaves English

    const char *names[] = { "English", "French", "German", "Italian", "Spanish" };
    for (unsigned i = 0; i < 5; ++i) {
        load_image(argv[1]); language(names[i]); load_saved = 0;
        uint8_t expected[36]; memcpy(expected, g_mem + 0x5c5af4, 36); expected[0x11] = i + 1;
        rt_options_language_default();
        initialize();
        assert(!memcmp(g_mem + 0x1091ad0, expected, 36));
        assert(!memcmp(g_mem + 0x1091af4, expected, 36));
    }
    for (unsigned i = 1; i <= 5; ++i) {
        load_image(argv[1]); language("French"); load_saved = 1;
        memcpy(saved_options, g_mem + 0x5c5af4, 36);
        saved_options[0x11] = i; saved_options[0x18] ^= 1;
        rt_options_language_default(); initialize();
        assert(!memcmp(g_mem + 0x1091ad0, saved_options, 36));
        assert(!memcmp(g_mem + 0x1091af4, saved_options, 36));
        // A later options-menu choice remains adjustable through the original setter.
        uint8_t *mb = g_mem;
        memcpy(mb + 0x10000, saved_options, 36); W8(0x10011, 5);
        CPU c = { .esp = 0x300000 }; W32(c.esp + 4, 0x10000);
        F_00524630(&c);
        assert(c.esp == 0x300004 && R8(0x1091ae1) == 5);
    }
    const char *invalid[] = { NULL, "", "unknown", "Japanese" };
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        load_image(argv[1]); language(invalid[i]); load_saved = 0;
        rt_options_language_default(); initialize(); assert(g_mem[0x1091ae1] == 1);
    }
    free(g_mem);
    puts("Original guest initializer: old French bug reproduced; five defaults applied; saved/menu choices and all other options preserved.");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--exe', type=Path, required=True)
    args = parser.parse_args()
    assert hashlib.sha256(args.exe.read_bytes()).hexdigest() == 'cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0'
    source = (ROOT / 'port/gen/x86_f125.c').read_text(encoding='utf-8')
    bodies = []
    for address in ('00524470', '00524630'):
        start = source.index(f'void F_{address}(CPU *c) {{')
        following = re.search(r'\nvoid F_', source[start + 1:])
        assert following
        bodies.append(source[start:start + 1 + following.start()])
    with tempfile.TemporaryDirectory(prefix='sh4-language-') as tmp:
        cfile, exe = Path(tmp) / 'test.c', Path(tmp) / 'test.exe'
        cfile.write_text(HARNESS + '\n' + '\n'.join(bodies), encoding='utf-8')
        subprocess.run([args.cc, '-std=gnu11', '-O2', '-ffunction-sections', '-fdata-sections',
                        '-I' + str(ROOT / 'port/src/rt'), '-I' + str(ROOT / 'port/gen'),
                        str(cfile), str(ROOT / 'port/src/rt/options.c'), '-Wl,--gc-sections',
                        '-lm', '-o', str(exe)], check=True)
        subprocess.run([str(exe), str(args.exe.resolve())], check=True)


if __name__ == '__main__':
    main()
