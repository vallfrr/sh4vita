#pragma once
#include "x86rt.h"
#include <string.h>

extern int g_sound_read_headers;
int rt_sound_archive_scalar(CPU *c,uint32_t src,void *out,uint32_t bytes);

/* These accessors are used only inside the three audited sound functions.
   Ordinary guest fields keep their direct load; bank reads use the reader
   only during the explicit header-routing experiment. */
#define SOUND_ACCESS(bits,type) \
static inline type rt_sound_r##bits(const uint8_t *mb,CPU *c,uint32_t a) { \
    type value; \
    if (__atomic_load_n(&g_sound_read_headers,__ATOMIC_RELAXED) && \
        rt_sound_archive_scalar(c,a,&value,sizeof value)) return value; \
    memcpy(&value,mb+a,sizeof value);return value; \
}
SOUND_ACCESS(8,uint8_t)
SOUND_ACCESS(16,uint16_t)
SOUND_ACCESS(32,uint32_t)
#undef SOUND_ACCESS
