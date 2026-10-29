#pragma once
#include <stdint.h>
#define SHADER_READY_MAX 512
#define SHADER_READY_PATH 256
typedef struct ShaderReady {
    unsigned count;
    int overflow;
    char paths[SHADER_READY_MAX][SHADER_READY_PATH];
} ShaderReady;
void shader_ready_add(ShaderReady *list, const char *path);
int shader_ready_check(const char *path, uint64_t bank, uint64_t config, int (*validate)(const char *));
int shader_ready_save(const char *path, uint64_t bank, uint64_t config, const ShaderReady *list);
