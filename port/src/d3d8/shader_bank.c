#include "shader_bank.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t u64(const unsigned char *p) { return u32(p) | (uint64_t)u32(p + 4) << 32; }
static uint64_t hash(const unsigned char *p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}
void shader_bank_free(ShaderBank *bank) {
    free(bank->entries);
    free(bank->storage);
    memset(bank, 0, sizeof *bank);
}
int shader_bank_load(const char *path, ShaderBank *bank) {
    static const unsigned char exe_sha[32] = {
        0xcf,0x70,0x81,0xa6,0xc7,0x8d,0xbe,0x51,0xf6,0xdc,0x2e,0x1c,0x9b,0xd9,0x78,0xd8,
        0x48,0x6f,0x1d,0x7b,0x59,0xd7,0xa3,0xb8,0x8e,0x02,0x7c,0x9b,0x39,0xf2,0x1d,0xd0
    };
    memset(bank, 0, sizeof *bank);
    FILE *f = fopen(path, "rb");
    if (!f) return -1; // optional file absent: normal runtime remains available
    int result = -2;
    if (fseek(f, 0, SEEK_END)) goto done;
    long length = ftell(f);
    if (length < 64 || length > 8 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) goto done;
    unsigned char *data = malloc((size_t)length);
    bank->storage = data;
    if (!data || fread(data, 1, (size_t)length, f) != (size_t)length) goto done;
    if (memcmp(data, "SH4VSB1\0", 8) || u32(data + 8) != 0x00010001 || u32(data + 12) != 210 ||
        u32(data + 16) != (size_t)length - 64 || u32(data + 20) || memcmp(data + 24, exe_sha, 32) ||
        u64(data + 56) != hash(data + 64, (size_t)length - 64)) goto done;
    bank->count = u32(data + 12);
    bank->checksum = u64(data + 56);
    bank->entries = calloc(bank->count, sizeof *bank->entries);
    if (!bank->entries) goto done;
    size_t at = 64;
    uint32_t previous = 0;
    for (unsigned i = 0; i < bank->count; i++) {
        if ((size_t)length - at < 24) goto done;
        ShaderBankEntry *e = &bank->entries[i];
        e->func = u32(data + at); e->decl = u32(data + at + 4); e->bgra = u32(data + at + 8);
        e->stride = u32(data + at + 12); e->nc = u32(data + at + 16);
        uint32_t n = u32(data + at + 20);
        at += 24;
        if (e->func <= previous || e->func < 0x400000 || e->func > 0x1400000 ||
            e->decl < 0x400000 || e->decl > 0x1400000 || e->bgra >> 16 ||
            e->stride > 1024 || e->stride % 4 || e->nc > 96 || n < 2 || n > 131072 || n > (size_t)length - at) goto done;
        previous = e->func;
        e->source = (const char *)data + at;
        if (data[at + n - 1] || memchr(data + at, 0, n - 1)) goto done;
        at += n;
        while (at % 4) { if (at >= (size_t)length || data[at++]) goto done; }
    }
    if (at != (size_t)length) goto done;
    result = 0;
done:
    fclose(f);
    if (result) shader_bank_free(bank);
    return result;
}
