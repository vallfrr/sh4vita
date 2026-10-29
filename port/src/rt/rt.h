// Host-side runtime API (Win32, D3D8, platform implementations).
#pragma once
#include "x86rt.h"
#include <stdarg.h>
#include <stdio.h>

extern uint32_t g_mem_size;
extern char g_game_dir[512];   // host-side game folder, no trailing separator
extern CPU *g_main_cpu;

// Guest address space layout (outside the image 0x400000..0x13e3000).
#define G_TIB_MAIN    0x00010000u
#define G_STACK_MAIN  0x00300000u   // top of the main thread stack (grows downward)
#define G_HEAP_BASE   0x01400000u
#define G_HOSTFN_BASE 0xF0000000u   // synthetic addresses of host functions

int rt_init(uint32_t mem_size);
int rt_init_mem(void *mem, uint32_t mem_size);   // memory provided by the platform (zeroed here)
void rt_text_check(void);
int rt_load_exe(const char *path);
void rt_fill_iat(void);
void rt_apply_res(void);
void rt_keep_res(void);
void rt_options_language_default(void);
float rt_camera_scale(void);
void rt_camera_end(void);
unsigned rt_save_failure_serial(void);
uint32_t rt_save_error_code(void);
void rt_save_failed(CPU *c, uint32_t error);
void rt_save_report_error(unsigned language, uint32_t error);
int rt_window_camera_active(void);
void rt_run_entry(void);

// Guest heap (guest addresses, 0 = failure)
void gheap_init(uint32_t base, uint32_t size);
uint32_t gheap_alloc(uint32_t size);
uint32_t gheap_calloc(uint32_t size);
uint32_t gheap_alloc_aligned(uint32_t size, uint32_t align);
void gheap_free(uint32_t a);
uint32_t gheap_realloc(uint32_t a, uint32_t size);
uint32_t gheap_size(uint32_t a);
uint32_t gstrdup(const char *s);

// Guest threads
CPU *rt_new_cpu(uint32_t stack_size);
// Main-thread cleanup only, after every host thread using this CPU has joined.
// The main CPU is static and is never released by this function.
void rt_free_cpu(CPU *cpu);
uint32_t rt_guest_call(CPU *c, uint32_t fn, int nargs, ...);

// Helpers for writing imported functions (convention: esp points to the return address)
#define HOSTFN uint8_t *const mb = g_mem; (void)mb
#define ARG(n) R32(c->esp + 4u * (n))
#define STDRET(nargs, v) do { c->eax = (uint32_t)(v); c->esp += 4u + 4u * (nargs); return; } while (0)
static inline char *GSTR(uint32_t a) { return a ? (char *)(g_mem + a) : NULL; }
static inline uint32_t GADDR(const void *p) { return (uint32_t)((const uint8_t *)p - g_mem); }

// Profiling (main thread): cumulative times in SDL_GetPerformanceCounter ticks.
extern uint64_t g_t_sleep, g_t_gl, g_t_swap;

void rt_log(const char *fmt, ...);
void rt_log_flush(void);
void rt_cpu_profile_init(void);
int rt_cpu_profile_thread(const char *label);
void rt_cpu_profile_report(double frames);
_Noreturn void rt_fatal(const char *fmt, ...);
void rt_trace_import(const char *name);
