// Common infrastructure of the emulated Win32 APIs.
#pragma once
#include "rt.h"
#include <SDL.h>

enum { H_FREE, H_FILE, H_EVENT, H_MUTEX, H_THREAD, H_REGKEY, H_TIMER };

uint32_t h_new(int type, void *obj);
void *h_get(uint32_t h, int type);
int h_type(uint32_t h);
void h_close(uint32_t h);

// Waitable objects: all protected by a global lock and a common signal.
extern SDL_mutex *g_sync;
extern SDL_cond *g_sync_cv;
typedef struct { int manual, state; } Event;
typedef struct { SDL_Thread *t; CPU *c; uint32_t start, param, tid; int done; } Thread;
int w32_signaled(uint32_t h);        // under g_sync
void w32_consume(uint32_t h);        // under g_sync: resets an auto event
uint32_t w32_wait(uint32_t n, const uint32_t *hs, int all, uint32_t timeout_ms);

void w32_set_last_error(CPU *c, uint32_t e);
void w32_event_set(uint32_t h, int v);   // SetEvent/ResetEvent from any thread
void w32_host_path(const char *gpath, char *out, size_t n);
uint32_t w32_tick_ms(void);
uint64_t w32_qpc(void);
void w32_sleep_until(uint64_t t);
unsigned w32_hz(void);

#define GUEST_ROOT "C:\\SH4\\"
#define WAIT_TIMEOUT_ 0x102u
