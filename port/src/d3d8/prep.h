// D3D8 prep thread: the game thread only writes compact records into a ring (state SETs, DRAW, CLEAR,
// PRESENT...); the consumer replays them on a "shadow" device (g_pdev) and makes all the GL calls (draw.c, resource.c).
// SH4_PREP=sync (each record replayed immediately: reference), lazy (replayed at present / waits),
// thread (dedicated thread; Vita default, core 2). SH4_PREP_KB = ring size (default 1024).
#pragma once
#include "d3d8.h"

extern Device *g_pdev;       // state as seen by the consumer
extern unsigned g_pframe;    // presents replayed (g_frame = presents issued by the game)
extern int g_prep_mode;      // 0 sync, 1 lazy, 2 thread

void prep_init(Device *game);                                    // shadow copy + start; called when the device is complete
void prep_gfx_init(void);                                        // gfx_init(g_pdev) on the consumer (the GL context is born there)
int prep_set(void *field, const void *src, size_t n);            // g_dev field: copy + record if changed (returns 1)
#define PSET(f, v) prep_set(&d->f, &(__typeof__(d->f)){(v)}, sizeof d->f)
void prep_bind(void);                                            // gfx_bind_target(pd)
void prep_draw(Device *d, uint32_t prim, uint32_t first, uint32_t nverts, uint32_t nprims, const uint8_t *indices,
               int index32, uint32_t min_index, const uint8_t *up, uint32_t up_stride);
void prep_clear(uint32_t flags, uint32_t color, float z, uint32_t stencil, int nrects, uint32_t rects_guest);
void prep_present(void);
void prep_copy(Surface *src, Surface *dst, uint32_t n, const int32_t *rects, const int32_t *points);   // n = 0: the whole source
void prep_free(int kind, void *p);                               // frees on the GL / guest-heap side once the in-flight draws have been replayed
void prep_call(void (*fn)(void *), void *arg);                   // runs fn on the consumer and waits for it to finish
void prep_vsdel(uint32_t h);
void prep_psdel(uint32_t h);
void prep_use(uint32_t *use);                                    // marks the resource "used by the records issued so far"
void prep_wait_use(uint32_t use);                                // before writing a resource's memory
void prep_wait_all(void);
void prep_report(double frames);                                 // SH4_STATS
void prep_gfree(uint32_t wrapper, uint32_t mem);                 // consumer: deferred guest free
void prep_gdrain(void);                                          // game thread: runs the deferred frees

// on the draw.c / resource.c side
int gfx_use_vbo(void);
uint32_t gfx_stream_mask(const Device *d, uint32_t *fvf_stride);   // streams read by the current VS (+ stride of an FVF)
int surf_is_gl(const Surface *s);
void gfx_copy_gl(Surface *src, Surface *dst, uint32_t n, const int32_t *rects, const int32_t *points);
void res_free(int kind, void *p);
