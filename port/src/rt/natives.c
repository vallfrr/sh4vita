// Native replacements of game functions (cf. OVERRIDES in recomp/x86rec.py).
// Convention: same effects as the guest function, including on esp (RET).
#include "rt.h"
#include <stdlib.h>

// Observe archive users without changing pointers, PCM or allocation policy.
// Both entry points use ECX as the bank object and argument 1 as the sound id.
static void sound_archive_trace(CPU *c, unsigned entry) {
    extern unsigned g_frame;
    extern void rt_sound_archive_verify(CPU *,uint32_t,uint32_t);
    static int enabled = -1,verify;
    if (enabled < 0) { const char *v=getenv("SH4_SOUND_ARCHIVE_TRANSFER"); enabled = getenv("SH4_SOUND_ARCHIVE_TRACE") != NULL; verify = getenv("SH4_SOUND_ARCHIVE_VERIFY") != NULL || (v && !strcmp(v,"1")); }
    if (!enabled && !verify) return;
    HOSTFN;
    uint32_t obj=c->ecx;
    if (c->esp > g_mem_size-8 || obj > g_mem_size-0x1100u) return;
    uint32_t id=R32(c->esp+4);
    if (id >= 355) return;
    if (verify) rt_sound_archive_verify(c,obj,id);
    if (!enabled) return;
    uint32_t row=obj+12*id, base=R32(obj+0x10ec);
    rt_log("[sound-archive] frame=%u entry=%06x object=%08x id=%u base=%08x offset=%u bytes=%u opaque=%u\n",
           g_frame,entry,obj,id,base,R32(row),R32(row+4),R32(row+8));
}
extern void F_00409530_orig(CPU *c);
extern void F_00409a30_orig(CPU *c);
void nat_sound_request(CPU *c) { sound_archive_trace(c,0x409530); F_00409530_orig(c); }
void nat_sound_request3d(CPU *c) { sound_archive_trace(c,0x409a30); F_00409a30_orig(c); }

// 0x551f80: original code reserves and clears a 64 MiB, 64 KiB-aligned
// linear arena. Keep that path unless an experimental cap is requested.
extern void F_00551f80_orig(CPU *c);
void nat_arena_init(CPU *c) {
    const char *setting = getenv("SH4_ARENA_MB");
    unsigned mib = setting ? (unsigned)atoi(setting) : 56;   // 56: measured peak 47.5 MiB over 386 rooms; SH4_ARENA_MB=64 restores the original reservation
    if (mib < 32 || mib >= 64) { F_00551f80_orig(c); return; }
    HOSTFN;
    uint32_t bytes = mib << 20;
    uint32_t base = gheap_alloc_aligned(bytes, 0x10000);
    if (!base) rt_fatal("linear arena: %u MiB allocation failed", mib);
    memset(mb + base, 0, bytes);
    memset(mb + 0x011b6000, 0, 16);
    W32(0x011b6010, base);
    W32(0x011b6000, base);
    W32(0x011b6004, base);
    W32(0x011b6008, base + bytes);
    rt_log("[arena] experimental cap=%u MiB (original 64 MiB)\n", mib);
    c->eax = c->ecx = 0;
    c->edx = base + bytes;
    c->esp += 4;
}

// 0x552000: the original game's linear arena reserves 64 MiB at start-up.
// Record its actual high-water mark without changing allocation or reset behavior.
extern void F_00552000_orig(CPU *c);
void nat_arena_alloc(CPU *c) {
    HOSTFN;
    static int trace = -1;
    if (trace < 0) trace = getenv("SH4_ARENA_TRACE") != NULL;
    if (!trace) { F_00552000_orig(c); return; }
    uint32_t requested = ARG(1);
    F_00552000_orig(c);
    uint32_t base = R32(0x011b6000), cur = R32(0x011b6004), end = R32(0x011b6008);
    if (!base || end < base || end - base > (64u << 20) || cur < base || cur > end) return;
    static uint32_t peak, logged_mib;
    uint32_t used = cur - base;
    if (used > peak) peak = used;
    uint32_t mib = (peak + ((1u << 20) - 1)) >> 20;
    if (mib > logged_mib || (!c->eax && requested)) {
        logged_mib = mib;
        rt_log("[arena] peak=%u KB cap=%u KB request=%u result=%08x\n",
               (unsigned)((peak + 1023) >> 10), (unsigned)((end - base) >> 10), requested, c->eax);
    }
}

// 0x404a00: the game's MMX memory copy, cdecl (dst, src, n), no useful return value.
void nat_memcpy_mmx(CPU *c) {
    HOSTFN;
    uint32_t d = ARG(1), s = ARG(2);
    int32_t n = (int32_t)ARG(3);
    if (n > 0) {
        extern int g_sound_transfer_on;
        extern int rt_sound_archive_transfer(CPU *,uint32_t,uint32_t,uint32_t);
        if (!__atomic_load_n(&g_sound_transfer_on,__ATOMIC_RELAXED) || !rt_sound_archive_transfer(c,d,s,(uint32_t)n))
            memmove(mb + d, mb + s, (size_t)n);
    }
    c->esp += 4;
}

// 0x5771a6: _rand (MSVC CRT). __getptd does GetLastError/TlsGetValue/SetLastError (three host calls) for nothing:
// we read the TLS slot directly (guest TIB +0xE10); a thread's first call (empty slot) = the real __getptd.
void nat_rand(CPU *c) {
    HOSTFN;
    uint32_t p = R32(c->fs_base + 0xe10 + 4 * R32(0x6a33e0));
    if (!p) p = rt_guest_call(c, 0x57a2eb, 0);
    uint32_t h = R32(p + 0x14) * 0x343fdu + 0x269ec3u;
    W32(p + 0x14, h);
    c->eax = (h >> 16) & 0x7fffu;
    c->ecx = h;
    c->esp += 4;
}

// 0x50c2a0: opens a Yes/No dialog (cdecl bank, msg). "Do you want to change the options?" (msg 0x49) opens on No, so a quick
// Cross discarded every change (sh4.sav is only written on Yes): cursor on Yes (selection 0 = left button = the one that saves, DAT_0108cf90).
extern void F_0050c2a0_orig(CPU *c);
void nat_optdlg(CPU *c) {
    HOSTFN;
    uint32_t bank = ARG(1), msg = ARG(2);
    F_0050c2a0_orig(c);
    if (bank == 0 && msg == 0x49) W32(0x108cf90, 0);
}

// 0x433f00: light-halo grid (17x12 accumulation per light then ~770 sprites). SH4_GLOW: 0 = game, bit0 = no accumulation,
// bit1 = no sprites, 4 = nothing (cost experiments; the final console setting will come from sh4vita.json).
// Grid disabled by default on Vita (measured: -5 ms/frame in the hospital; 352 PC captures identical with/without). SH4_GLOW=0 restores it.
#ifdef __vita__
#define GLOW_DEFAULT 4
#else
#define GLOW_DEFAULT 0
#endif
extern void F_00433f00_orig(CPU *c);
void nat_lightglow(CPU *c) {
    static int mode = -1;
    if (mode < 0) { const char *e = getenv("SH4_GLOW"); mode = e ? atoi(e) : GLOW_DEFAULT; }
    if (!mode) { F_00433f00_orig(c); return; }
    HOSTFN;
    if (!(mode & 4) && rt_guest_call(c, 0x4f8db0, 0) == 0) {
        uint32_t n = rt_guest_call(c, 0x55dd80, 0);
        if (n) {
            rt_guest_call(c, 0x433300, 0);
            rt_guest_call(c, 0x433390, 0);
            if (!(mode & 1)) rt_guest_call(c, 0x433b70, 1, n);
            rt_guest_call(c, 0x433910, 0);
            if (!(mode & 2)) rt_guest_call(c, 0x433c70, 0);
        }
    }
    c->esp += 4;
}

// 0x554350: (wrap now a no-op, the cursor flag comes from ShowCursor).
extern void F_00554350_orig(CPU *c);
extern unsigned g_frame;
unsigned g_cursor_seen = 0;   // set by dev_ShowCursor (device.c)
void nat_curaxis(CPU *c) { F_00554350_orig(c); }

// The original axis handler sends a release only on a neutral sample. When a
// stick jumps straight from one side to the other, both directions stay pressed.
// Feed it a neutral transition first, preserving the real CPU and arguments.
extern void F_00406f50_orig(CPU *c);
void nat_axischange(CPU *c) {
    HOSTFN;
    uint32_t arg = c->esp + 4, previous_ptr = R32(c->esp + 8);
    int32_t current = (int32_t)R32(arg);
    int32_t previous = previous_ptr ? (int32_t)R32(previous_ptr) : 0;
    if ((previous < -300 && current > 300) || (previous > 300 && current < -300)) {
        CPU neutral = *c;
        W32(arg, 0);
        F_00406f50_orig(&neutral);
        W32(arg, (uint32_t)current);
    }
    F_00406f50_orig(c);
}

extern void F_0052d710_orig(CPU *c);
extern int g_camera_pad_x, g_camera_pad_y, g_camera_pad_active;
extern unsigned g_camera_pad_frame;
void nat_camera_probe(CPU *c) {
    static int trace = -1;
    if (trace < 0) trace = getenv("SH4_CAMERA_TRACE") != NULL;
    if (trace) {
        HOSTFN;
        uint32_t arg = ARG(1), player = R32(0xfd5ba0);
        if (getenv("SH4_CAMERA_SNAP") && (g_frame == 1590 || g_frame == 1610 || g_frame == 1620)) {
            static unsigned snapped;
            unsigned bit = g_frame == 1590 ? 1u : g_frame == 1610 ? 2u : 4u;
            if (!(snapped & bit)) {
                char path[600]; snprintf(path, sizeof path, "%s/camera-snap-%u.bin", g_game_dir, g_frame);
                FILE *f = fopen(path, "wb"); if (f) { fwrite(mb + 0x1090000u, 1, 0x20000u, f); fclose(f); snapped |= bit; }
            }
        }
        if (arg && player) rt_log("[camera-probe] frame=%u mode=%u arg=%08x values=%.6f,%.6f,%.6f player_state=%u yaw=%.6f\n",
            g_frame, R32(0xfd5ba4), arg, (double)rt_f32(R32(arg)), (double)rt_f32(R32(arg+4)),
            (double)rt_f32(R32(arg+8)), R32(player+0x410), (double)rt_f32(R32(player+0x84)));
        if (arg && g_frame >= 1598 && g_frame <= 1740 && g_frame % 10 == 0) {
            for (unsigned i = 0; i < 0x60; i += 4)
                rt_log("[camera-probe-word] frame=%u off=%02x value=%08x\n", g_frame, i, R32(arg+i));
        }
    }
    HOSTFN;
    uint32_t arg = ARG(1), previous = arg ? R32(arg + 8) : 0;
    float turn = rt_f32(previous);
    int pad_turn = g_camera_pad_active && g_frame - g_camera_pad_frame <= 2 &&
        R32(0xfd5ba4) == 0 && !rt_window_camera_active() &&
        ((g_camera_pad_x > 12000 && turn > 0) || (g_camera_pad_x < -12000 && turn < 0));
    if (arg && pad_turn) W32(arg + 8, rt_u32f(turn * rt_camera_scale()));
    F_0052d710_orig(c);
    if (arg && pad_turn) W32(arg + 8, previous);
}

extern void F_0052e0b0_orig(CPU *c);
void nat_camera_input(CPU *c) {
    HOSTFN;
    uint32_t caller = R32(c->esp);
    F_0052e0b0_orig(c);
    if (caller == 0x52dbe6 && c->eax == 0x10a1890 && R32(0xfd5ba4) == 0 &&
        g_camera_pad_active && g_frame - g_camera_pad_frame <= 2 && !rt_window_camera_active()) {
        float scale = rt_camera_scale();
        if (g_camera_pad_y > 12000 || g_camera_pad_y < -12000) {
            float look = rt_f32(R32(c->eax + 0x10));
            // The consumer squares this input, so sqrt preserves a linear speed slider.
            W32(c->eax + 0x10, rt_u32f(look * sqrtf(0.5f * scale)));
        }
        if (getenv("SH4_CAMERA_TRACE") && (g_camera_pad_y > 12000 || g_camera_pad_y < -12000 || g_camera_pad_x > 12000 || g_camera_pad_x < -12000))
            rt_log("[camera-scale] frame=%u mode=0 scale=%.3f y=%.6f x=%.6f\n", g_frame,
                (double)scale, (double)rt_f32(R32(c->eax + 0x10)), (double)rt_f32(R32(c->eax + 0x14)));
    }
}

// Exterior-window views read the second analog pair directly (0x54bae0),
// unlike first-person walking. Keep the extra horizontal axis scoped to that view.
extern void F_0054bae0_orig(CPU *c);
extern void F_0054cc10_orig(CPU *c);
static unsigned window_camera_seen;
static int window_camera_open;
int rt_window_camera_active(void) {
    return window_camera_open && g_frame - window_camera_seen <= 2;
}
void nat_window_camera(CPU *c) {
    window_camera_seen = g_frame;
    window_camera_open = 1;
    F_0054bae0_orig(c);
}
void nat_window_close(CPU *c) {
    window_camera_open = 0;
    F_0054cc10_orig(c);
}

// 0x526be0: options menu, once per frame while it is open. Up/down (pad bits 0x1000/0x4000) switch the page, left/right change the hovered value:
// dinput.c maps L/R to up/down while this is flagged.
extern void F_00526be0_orig(CPU *c);
int g_options_active = 0;
void nat_optframe(CPU *c) {
    if (!g_options_active) rt_log("[pad-context] options enter\n");
    g_options_active = 1;
    F_00526be0_orig(c);
    extern void rt_controls_page_badges(void);
    extern void console_ui_release(void);
    if (!c->eax) rt_controls_page_badges();
    if (c->eax) {
        console_ui_release();
        g_options_active = 0; rt_camera_end();
        rt_log("[pad-context] options leave result=%u\n", c->eax);
    }
}
