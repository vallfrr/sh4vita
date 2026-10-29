"""Exercise the actual Vita shader-cache functions with a simulated GL driver.

Run with a host C compiler, e.g. python shader_cache_test.py --cc J:/tools/mingw64/bin/gcc.exe.
No game data or GL context is required. The implementation is extracted from draw.c,
so malformed-file and failed-link tests cannot accidentally exercise a copied parser.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


STUBS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#endif
typedef unsigned GLuint;
typedef int GLint, GLsizei;
#define GL_CG_VERTEX_SHADER_EXT 1
#define GL_CG_FRAGMENT_SHADER_EXT 2
#define GL_PROGRAM_BINARY_LENGTH 3
#define GL_LINK_STATUS 4
static const char *g_game_dir = ".";
static unsigned binary_calls, link_calls, program_deletes, delete_calls;
static unsigned attached;
static unsigned fail_links;
static int bundle_disabled;
static const char *test_getenv(const char *name) {
    if (!strcmp(name, "SH4_BUNDLED_CACHE") && bundle_disabled) return "0";
    return NULL;
}
#define getenv test_getenv
#define SH4_SHADER_BUNDLE_ROOT "bundle"
static int link_ok = 1, binary_capacity = 8, vertex_size = 4, fragment_size = 4;
static GLuint glCreateShader(unsigned type) { return type; }
static void glShaderBinary(int n, const GLuint *shader, unsigned format, const void *data, int length) {
    assert(n == 1 && length > 0 && data && *shader); binary_calls++;
}
static void glAttachShader(GLuint p, GLuint s) { attached++; }
static void glDeleteProgram(GLuint p) { attached = 0; program_deletes++; }
static GLuint glCreateProgram(void) { return 77; }
static void glLinkProgram(GLuint p) { link_calls++; }
static void glDeleteShader(GLuint s) { delete_calls++; }
static void bind_attrs(GLuint p) {}
static void glGetProgramiv(GLuint p, unsigned key, GLint *out) {
    if (key == GL_LINK_STATUS && fail_links) { fail_links--; *out = 0; }
    else *out = key == GL_LINK_STATUS ? link_ok : binary_capacity;
}
static void vglGetShaderBinary(GLuint shader, int capacity, GLsizei *length, void *out) {
    int n = shader == 1 ? vertex_size : fragment_size;
    *length = n;
    if (n > 0 && n <= capacity) memset(out, shader, (size_t)n);
}
static void rt_log(const char *fmt, ...) {}
'''

TESTS = r'''
static int load_cache(GLuint p, const char *vs, const char *fs) {
    GLuint original = p;
    int ok = shc_load(&p, vs, fs);
    if (p != original) assert(!ok && p == 77);
    return ok;
}
static void write_cache(uint32_t vl, uint32_t fl, int extra) {
    FILE *f = shc_open("vs", "fs", "wb"); assert(f);
    unsigned char bytes[32] = {0}; unsigned at = 0;
    memcpy(bytes + at, &vl, 4); at += 4;
    /* Small payload is deliberate: malformed lengths must be rejected before driver calls. */
    unsigned available = vl <= 4 ? vl : 4;
    at += available;
    memcpy(bytes + at, &fl, 4); at += 8;
    if (extra) at++;
    assert(shc_write_file(f, bytes, at));
}
static void reject_without_driver(uint32_t vl, uint32_t fl, int extra) {
    unsigned calls = binary_calls;
    write_cache(vl, fl, extra);
    assert(!load_cache(3, "vs", "fs"));
    assert(binary_calls == calls);
}
int main(void) {
    assert(!load_cache(3, "vs", "fs"));
    write_cache(4, 4, 0);
    assert(load_cache(3, "vs", "fs"));
    assert(binary_calls == 2 && link_calls == 1 && delete_calls == 2);
    attached = 0; /* Simulates deleting the successfully linked program. */
    reject_without_driver(0, 4, 0);
    reject_without_driver(4, 0, 0);
    reject_without_driver(4, 4, 1);
    reject_without_driver(4, 5, 0);
    reject_without_driver(0xffffffffu, 4, 0);
    reject_without_driver(0x80000000u, 4, 0);
    FILE *f = shc_open("vs", "fs", "wb"); assert(f); fputc(0, f); fclose(f);
    assert(!load_cache(3, "vs", "fs"));
    write_cache(4, 4, 0);
    link_ok = 0;
    GLuint failed_program = 3;
    assert(!shc_load(&failed_program, "vs", "fs"));
    assert(failed_program == 77);
    assert(!attached && program_deletes == 1 && delete_calls == 4);
    /* A normal source compilation can now attach just two new shaders to the same program. */
    glAttachShader(3, 1); glAttachShader(3, 2); assert(attached == 2); attached = 0;
    link_ok = 1;
    shc_save(3, 1, 2, "save-vs", "save-fs");
    assert(load_cache(3, "save-vs", "save-fs")); attached = 0;
    vertex_size = -1;
    shc_save(3, 1, 2, "bad-vs", "bad-fs");
    assert(!shc_open("bad-vs", "bad-fs", "rb"));
    vertex_size = 9;
    shc_save(3, 1, 2, "large-vs", "large-fs");
    assert(!shc_open("large-vs", "large-fs", "rb"));
    vertex_size = 4; fragment_size = 5;
    shc_save(3, 1, 2, "large-fr-vs", "large-fr-fs");
    assert(!shc_open("large-fr-vs", "large-fr-fs", "rb"));
    mkdir("bundle", 0777); mkdir("bundle/" SH4_SHADER_CACHE_ABI, 0777);
    mkdir("bundle/" SH4_SHADER_CACHE_ABI "/programs", 0777);
    char bundled_path[256];
    snprintf(bundled_path, sizeof bundled_path, "bundle/" SH4_SHADER_CACHE_ABI "/programs/%016llx.bin",
             (unsigned long long)shc_key("vs", "fs"));
    uint32_t pair[] = {4, 1, 4, 2};
    f = fopen(bundled_path, "wb"); assert(shc_write_file(f, pair, sizeof pair));
    write_cache(0, 4, 0); /* Immutable bundle wins over an invalid writable file. */
    assert(load_cache(3, "vs", "fs")); attached = 0;
    f = fopen(bundled_path, "wb"); assert(f); fputc(0, f); fclose(f);
    write_cache(4, 4, 0); /* Invalid bundle falls back to a valid writable file. */
    assert(load_cache(3, "vs", "fs")); attached = 0;
    f = fopen(bundled_path, "wb"); assert(shc_write_file(f, pair, sizeof pair));
    fail_links = 1;
    GLuint retry_program = 3;
    assert(shc_load(&retry_program, "vs", "fs") && retry_program == 77 && !fail_links);
    attached = 0;
    bundle_disabled = 1;
    assert(!shc_open_bundle("vs", "fs"));
    bundle_disabled = 0;
    f = fopen(bundled_path, "r+b"); assert(f); fseek(f, sizeof(ShcHeader) + 5, SEEK_SET); fputc(0xff, f); fclose(f);
    unsigned corrupted_before = binary_calls;
    assert(!shc_load_file(&retry_program, shc_open_bundle("vs", "fs")) && corrupted_before == binary_calls);
    f = fopen(bundled_path, "wb"); assert(f); fseek(f, 2 * 1024 * 1024, SEEK_SET); fputc(0, f); fclose(f);
    unsigned before = binary_calls;
    assert(!shc_load_file(&retry_program, shc_open_bundle("vs", "fs")) && before == binary_calls);
    puts("Actual Vita shader cache: bundle preference, invalid/failed-link fallback, size bounds and writable cache checks passed");
    return 0;
}
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', default='gcc')
    args = ap.parse_args()
    source = (Path(__file__).resolve().parents[1] / 'src/d3d8/draw.c').read_text(encoding='utf-8')
    start = source.index('// On-disk program cache:')
    end = source.index('\n#endif', start)
    implementation = source[start:end]
    with tempfile.TemporaryDirectory(prefix='sh4-shader-cache-') as temp:
        folder = Path(temp)
        cfile, executable = folder / 'cache_test.c', folder / 'cache_test.exe'
        version = Path(__file__).resolve().parents[1] / 'src/d3d8/shader_cache_version.h'
        cfile.write_text(STUBS + '\n#include "' + version.as_posix() + '"\n' + implementation + TESTS, encoding='utf-8')
        subprocess.run([args.cc, '-std=gnu11', '-O2', '-Wall', str(cfile), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], cwd=folder, check=True)


if __name__ == '__main__':
    main()
