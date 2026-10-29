// Pure D3D8 -> GLSL translator, also built by sh4prep without SDL or GL.
#pragma once
#include <stdint.h>
#ifndef MAX_VSC
#define MAX_VSC 96
#endif
char *vs_translate(const uint32_t *tokens, int *error);
char *vs_translate_ex(const uint32_t *tokens, uint32_t bgra, int *error);
char *ps_translate(const uint32_t *tokens, int *error);
int vs_constant_count(void);
void shader_translate_free(void *source);
