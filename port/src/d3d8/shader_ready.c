#include "shader_ready.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ReadyHeader {
    char magic[8];
    uint64_t bank, config;
    uint32_t count, reserved;
    uint64_t checksum;
} ReadyHeader;
static uint64_t checksum(const void *data, size_t bytes) {
    const unsigned char *p = data;
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < bytes; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}
void shader_ready_add(ShaderReady *list, const char *path) {
    if (strlen(path) >= SHADER_READY_PATH) { list->overflow = 1; return; }
    for (unsigned i = 0; i < list->count; i++) if (!strcmp(list->paths[i], path)) return;
    if (list->count == SHADER_READY_MAX) { list->overflow = 1; return; }
    strcpy(list->paths[list->count++], path);
}
int shader_ready_check(const char *path, uint64_t bank, uint64_t config, int (*validate)(const char *)) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    ReadyHeader header;
    int valid = fread(&header, 1, sizeof header, f) == sizeof header &&
        !memcmp(header.magic, "SH4PW1\0\0", 8) && header.bank == bank && header.config == config &&
        !header.reserved && header.count && header.count <= SHADER_READY_MAX;
    size_t bytes = valid ? header.count * SHADER_READY_PATH : 0;
    char (*paths)[SHADER_READY_PATH] = bytes ? malloc(bytes) : NULL;
    valid = paths && fread(paths, 1, bytes, f) == bytes && fgetc(f) == EOF && checksum(paths, bytes) == header.checksum;
    fclose(f);
    if (valid) for (unsigned i = 0; i < header.count; i++) {
        if (!memchr(paths[i], 0, SHADER_READY_PATH) || !validate(paths[i])) { valid = 0; break; }
    }
    free(paths);
    return valid;
}
int shader_ready_save(const char *path, uint64_t bank, uint64_t config, const ShaderReady *list) {
    if (!list->count || list->overflow) return 0;
    size_t bytes = list->count * SHADER_READY_PATH;
    ReadyHeader header = {{'S','H','4','P','W','1',0,0}, bank, config, list->count, 0, checksum(list->paths, bytes)};
    char temporary[512];
    int length = snprintf(temporary, sizeof temporary, "%s.new", path);
    if (length < 0 || (size_t)length >= sizeof temporary) return 0;
    FILE *f = fopen(temporary, "wb");
    if (!f) return 0;
    int ok = fwrite(&header, 1, sizeof header, f) == sizeof header && fwrite(list->paths, 1, bytes, f) == bytes;
    if (fclose(f)) ok = 0;
    if (ok) { remove(path); ok = rename(temporary, path) == 0; }
    if (!ok) remove(temporary);
    return ok;
}
