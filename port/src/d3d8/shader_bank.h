// Bank generated from the user's executable by sh4prep. No GL dependency.
#pragma once
#include <stdint.h>

typedef struct ShaderBankEntry {
    uint32_t func, decl, bgra, stride, nc;
    const char *source;
} ShaderBankEntry;
typedef struct ShaderBank {
    void *storage;
    unsigned count;
    uint64_t checksum;
    ShaderBankEntry *entries;
} ShaderBank;
int shader_bank_load(const char *path, ShaderBank *bank);
void shader_bank_free(ShaderBank *bank);
