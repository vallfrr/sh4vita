// Emulated DirectInput 8: keyboard (SDL) and gamepads (SDL GameController seen as one gamepad)
// DirectInput standard : stick gauche X/Y, stick droit Rx/Ry, croix = POV, 12 boutons).
#include "d3d8/d3d8.h"
#include <SDL.h>
#include <stdlib.h>
#include "audit.h"
#include "pad_mapping.h"
#include "pad_context.h"
#include "pad_sample.h"
#ifdef SH4_TEST_OPTIMIZATIONS
#include "pad_script.h"
#endif
#include "input_gate.h"
#ifdef __vita__
#include <psp2/touch.h>
#endif

typedef struct { ComObj com; } DInput;
int g_camera_pad_x, g_camera_pad_y, g_camera_pad_active;
unsigned g_camera_pad_frame;
typedef struct {
    ComObj com;
    int type;                  // 0 clavier, 1 souris, 2 manette
    int joy;                   // index SDL
    SDL_GameController *gc;
    uint32_t data_size;
    int32_t rmin[8], rmax[8];  // ranges per axis (offset/4: X Y Z Rx Ry Rz S0 S1)
    int32_t dead;              // zone morte (0..10000)
    unsigned neutral_generation;
    int await_neutral;
    Sh4PadSample sample;
} DIDev;

#define M(p, name) static void p##_##name(CPU *c)
static const uint8_t GUID_KBD[16] = {0x61, 0x2b, 0x1d, 0x6f, 0xa0, 0xd5, 0xcf, 0x11, 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0, 0};
static const uint8_t GUID_MOUSE[16] = {0x60, 0x2b, 0x1d, 0x6f, 0xa0, 0xd5, 0xcf, 0x11, 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0, 0};
static const uint8_t GUID_JOY[12] = {'s', 'h', '4', 'v', 'i', 't', 'a', 'J', 'O', 'Y', 0, 0}; // +4 bytes: index

// ---------- clavier : scancode SDL -> DIK ----------
static uint8_t dik_of[SDL_NUM_SCANCODES];
static void init_dik(void) {
    static int done;
    if (done) return;
    done = 1;
    const char *row1 = "QWERTYUIOP", *row2 = "ASDFGHJKL", *row3 = "ZXCVBNM";
    for (int i = 0; row1[i]; i++) dik_of[SDL_SCANCODE_A + row1[i] - 'A'] = (uint8_t)(0x10 + i);
    for (int i = 0; row2[i]; i++) dik_of[SDL_SCANCODE_A + row2[i] - 'A'] = (uint8_t)(0x1e + i);
    for (int i = 0; row3[i]; i++) dik_of[SDL_SCANCODE_A + row3[i] - 'A'] = (uint8_t)(0x2c + i);
    for (int i = 0; i < 9; i++) dik_of[SDL_SCANCODE_1 + i] = (uint8_t)(0x02 + i);
    dik_of[SDL_SCANCODE_0] = 0x0b;
    for (int i = 0; i < 10; i++) dik_of[SDL_SCANCODE_F1 + i] = (uint8_t)(0x3b + i);
    static const int pairs[][2] = {
        {SDL_SCANCODE_ESCAPE, 0x01}, {SDL_SCANCODE_MINUS, 0x0c}, {SDL_SCANCODE_EQUALS, 0x0d},
        {SDL_SCANCODE_BACKSPACE, 0x0e}, {SDL_SCANCODE_TAB, 0x0f}, {SDL_SCANCODE_LEFTBRACKET, 0x1a},
        {SDL_SCANCODE_RIGHTBRACKET, 0x1b}, {SDL_SCANCODE_RETURN, 0x1c}, {SDL_SCANCODE_LCTRL, 0x1d},
        {SDL_SCANCODE_SEMICOLON, 0x27}, {SDL_SCANCODE_APOSTROPHE, 0x28}, {SDL_SCANCODE_GRAVE, 0x29},
        {SDL_SCANCODE_LSHIFT, 0x2a}, {SDL_SCANCODE_BACKSLASH, 0x2b}, {SDL_SCANCODE_COMMA, 0x33},
        {SDL_SCANCODE_PERIOD, 0x34}, {SDL_SCANCODE_SLASH, 0x35}, {SDL_SCANCODE_RSHIFT, 0x36},
        {SDL_SCANCODE_KP_MULTIPLY, 0x37}, {SDL_SCANCODE_LALT, 0x38}, {SDL_SCANCODE_SPACE, 0x39},
        {SDL_SCANCODE_CAPSLOCK, 0x3a}, {SDL_SCANCODE_NUMLOCKCLEAR, 0x45}, {SDL_SCANCODE_SCROLLLOCK, 0x46},
        {SDL_SCANCODE_KP_7, 0x47}, {SDL_SCANCODE_KP_8, 0x48}, {SDL_SCANCODE_KP_9, 0x49},
        {SDL_SCANCODE_KP_MINUS, 0x4a}, {SDL_SCANCODE_KP_4, 0x4b}, {SDL_SCANCODE_KP_5, 0x4c},
        {SDL_SCANCODE_KP_6, 0x4d}, {SDL_SCANCODE_KP_PLUS, 0x4e}, {SDL_SCANCODE_KP_1, 0x4f},
        {SDL_SCANCODE_KP_2, 0x50}, {SDL_SCANCODE_KP_3, 0x51}, {SDL_SCANCODE_KP_0, 0x52},
        {SDL_SCANCODE_KP_PERIOD, 0x53}, {SDL_SCANCODE_F11, 0x57}, {SDL_SCANCODE_F12, 0x58},
        {SDL_SCANCODE_KP_ENTER, 0x9c}, {SDL_SCANCODE_RCTRL, 0x9d}, {SDL_SCANCODE_KP_DIVIDE, 0xb5},
        {SDL_SCANCODE_RALT, 0xb8}, {SDL_SCANCODE_HOME, 0xc7}, {SDL_SCANCODE_UP, 0xc8},
        {SDL_SCANCODE_PAGEUP, 0xc9}, {SDL_SCANCODE_LEFT, 0xcb}, {SDL_SCANCODE_RIGHT, 0xcd},
        {SDL_SCANCODE_END, 0xcf}, {SDL_SCANCODE_DOWN, 0xd0}, {SDL_SCANCODE_PAGEDOWN, 0xd1},
        {SDL_SCANCODE_INSERT, 0xd2}, {SDL_SCANCODE_DELETE, 0xd3}};
    for (unsigned i = 0; i < sizeof pairs / sizeof pairs[0]; i++) dik_of[pairs[i][0]] = (uint8_t)pairs[i][1];
}

// ---------- IDirectInputDevice8A ----------
M(dev, QueryInterface) { STDRET(3, 0x80004002u); }
M(dev, AddRef) { HOSTFN; ComObj *o = com_get(ARG(1)); STDRET(1, o ? ++o->refs : 0); }
M(dev, Release) {
    HOSTFN;
    DIDev *d = com_get_kind(ARG(1), K_DIDEV);
    if (!d) STDRET(1, 0);
    int32_t r = --d->com.refs;
    if (r <= 0) { if (d->gc) SDL_GameControllerClose(d->gc); com_free(&d->com); free(d); r = 0; }
    STDRET(1, (uint32_t)r);
}
M(dev, GetCapabilities) {
    HOSTFN; THIS(DIDev, K_DIDEV);
    uint32_t p = ARG(2);
    W32(p + 4, 1);                                  // DIDC_ATTACHED
    W32(p + 8, self->type == 0 ? 0x413 : self->type == 1 ? 0x12 : 0x215);
    W32(p + 12, self->type == 2 ? 6 : 0);           // axes
    W32(p + 16, self->type == 2 ? 12 : 0);          // boutons
    W32(p + 20, self->type == 2 ? 1 : 0);           // POV
    STDRET(2, HR_OK);
}
// The game (0x4065e0) sets each axis range to -1000..1000 here; without this call it reads 0..65535 and thinks the stick is pushed (centre 32768 > threshold 300).
M(dev, EnumObjects) {
    HOSTFN; THIS(DIDev, K_DIDEV);
    uint32_t cb = ARG(2), ref = ARG(3);
    if (self->type != 2) STDRET(4, HR_OK);
    uint32_t inst = gheap_calloc(316);   // DIDEVICEOBJECTINSTANCEA
    if (!inst) STDRET(4, 0x8007000eu); // E_OUTOFMEMORY, no callback with a null guest object
    for (uint32_t a = 0; a < 6; a++) {
        memset(mb + inst, 0, 316);
        W32(inst, 316);
        W32(inst + 20, a * 4);           // dwOfs in DIJOYSTATE
        W32(inst + 24, 2 | a << 8);      // DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(a)
        if (rt_guest_call(c, cb, 2, inst, ref) == 0) break;
    }
    gheap_free(inst);
    STDRET(4, HR_OK);
}
M(dev, GetProperty) { STDRET(3, HR_OK); }
M(dev, SetProperty) {
    HOSTFN; THIS(DIDev, K_DIDEV);
    uint32_t prop = ARG(2), ph = ARG(3), how = R32(ph + 12), obj = R32(ph + 8);
    if (prop == 4 || prop == 5) { // DIPROP_RANGE / DIPROP_DEADZONE
        for (int a = 0; a < 8; a++) {
            if (how == 1 && obj != (uint32_t)a * 4) continue; // DIPH_BYOFFSET
            if (how == 2 && (obj >> 8 & 0xffff) != (uint32_t)a) continue; // DIPH_BYID (dwType from EnumObjects)
            if (prop == 4) { self->rmin[a] = (int32_t)R32(ph + 16); self->rmax[a] = (int32_t)R32(ph + 20); }
            else self->dead = (int32_t)R32(ph + 16);
        }
    }
    STDRET(3, HR_OK);
}
M(dev, Acquire) { STDRET(1, HR_OK); }
M(dev, Unacquire) { STDRET(1, HR_OK); }

static int32_t axis(DIDev *d, int a, Sint16 v) {
    int32_t lo = d->rmin[a], hi = d->rmax[a];
    float f = v < 0 ? v / 32768.f : v / 32767.f;
    // Dead zone of at least 15%: Vita stick noise would otherwise count as an action (no attract-mode movie at the title).
    if (fabsf(f) * 10000.f < (float)(d->dead > 1500 ? d->dead : 1500)) f = 0;
    return lo + (int32_t)((f + 1.f) * 0.5f * (float)(hi - lo) + 0.5f);
}
// Legacy per-axis response: 10% threshold, curve and scale; axis() adds its own
// 15% threshold afterwards. This is not a radial dead zone or a single 10% filter.
static Sint16 shape(Sint16 v, float k) {
    float f = v < 0 ? v / 32768.f : v / 32767.f, a = fabsf(f);
    if (a < 0.10f) return 0;
    a = (a - 0.10f) / 0.90f;
    a = a * (0.35f + 0.65f * a) * k;
    if (a > 1.f) a = 1.f;
    return (Sint16)((f < 0 ? -a : a) * 32767.f);
}
// Each device independently waits for physical release after a system dialog.
// A neutral keyboard must not re-enable a still-held pad confirm button.
static int input_gate(DIDev *device, uint8_t *mb, uint32_t p, uint32_t n) {
    unsigned generation = __atomic_load_n(&g_input_neutral_generation, __ATOMIC_RELAXED);
    if (device->neutral_generation != generation) {
        device->neutral_generation = generation;
        device->await_neutral = 1;
    }
    if (!device->await_neutral) return 0;
    int busy = 0;
    if (device->type == 0) {
        int count = 0;
        const Uint8 *keys = SDL_GetKeyboardState(&count);
        for (int i = 0; keys && i < count; ++i) busy |= keys[i] != 0;
    } else if (device->type == 1) busy = SDL_GetMouseState(NULL, NULL) != 0;
    else if (device->type == 2) {
        g_camera_pad_x = g_camera_pad_y = g_camera_pad_active = 0;
        device->sample.valid = 0;
        if (device->gc) {
            for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i)
                busy |= SDL_GameControllerGetButton(device->gc, (SDL_GameControllerButton)i) != 0;
            for (int i = 0; i < 4; ++i) {
                int axis = SDL_GameControllerGetAxis(device->gc, (SDL_GameControllerAxis)i);
                busy |= axis < -6000 || axis > 6000;
            }
        }
        for (unsigned i = 0; i < 6 && i * 4 + 4 <= n; ++i)
            W32(p + i * 4, (int32_t)(((int64_t)device->rmin[i] + device->rmax[i]) / 2));
        for (unsigned offset = 32; offset + 4 <= n && offset < 48; offset += 4)
            W32(p + offset, 0xffffffffu); // zero would mean POV Up, not neutral
    }
    if (!busy) device->await_neutral = 0;
    return 1; // release sample stays neutral too; no stale scripted/physical press
}
M(dev, GetDeviceState) {
    HOSTFN; THIS(DIDev, K_DIDEV);
    uint32_t n = ARG(2), p = ARG(3);
    memset(mb + p, 0, n);
    SDL_PumpEvents();
#ifdef __vita__
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);   // nothing reads the SDL event queue: unread events (touch, sticks, sensors) pile up in the host heap
#endif
    if (input_gate(self, mb, p, n)) STDRET(3, HR_OK);
    if (self->type == 0) {
        int nk;
        const Uint8 *ks = SDL_GetKeyboardState(&nk);
        init_dik();
        for (int i = 0; i < nk && i < SDL_NUM_SCANCODES; i++)
            if (ks[i] && dik_of[i] && dik_of[i] < n) W8(p + dik_of[i], 0x80);
        // Automated test: SH4_INPUT="frame:DIK[:duration],..." (DIK in hexadecimal), e.g. "900:1c,960:c8:2"
        static const char *script;
        if (!script) script = getenv("SH4_INPUT") ? getenv("SH4_INPUT") : "";
        extern unsigned g_frame;
        { // Test-only: register the apartment window through the game's event machinery.
            static int init; static unsigned wanted; static int done;
            if (!init) { const char *v = getenv("SH4_WINDOW_FORCE"); wanted = v ? (unsigned)strtoul(v, NULL, 10) : 0; init = 1; }
            if (wanted && !done && g_frame >= wanted && R32(0xfd5a90) == 1 && R32(0xfd5a94) == 1) {
                uint32_t node = R32(0x13d8200);
                for (unsigned i = 0; node && i < 512 && node <= g_mem_size - 16; i++, node = R32(node + 12)) {
                    if (R32(node) != 0x69843c) continue;
                    uint32_t actor = R32(node + 4);
                    uint32_t kind = rt_guest_call(c, 0x52b230, 1, actor);
                    uint32_t type = rt_guest_call(c, 0x4d0d80, 1, kind);
                    uint32_t result = rt_guest_call(c, 0x4d0db0, 3, R32(node), node, type);
                    rt_log("[window-test] frame=%u node=%08x kind=%08x type=%08x result=%08x\n", g_frame, node, kind, type, result);
                    done = 1; break;
                }
            }
        }
        // Heavy-scene test: SH4_WARP="frame:type" calls the game's jump event (0x4e6e00) with destination `type`
        // (table 0x628110, 14 entries of 0x50 bytes: world, scene, position): 1/2 subway, 3/4 forest, 8 water prison, 9/10 building,
        // 0 hospital, 13 past. Not 7 (special case).
        static const char *warp;
        static const char *wnext;   // list "frame:type,frame:type,...": chains the jumps (test for leaks between worlds)
        if (!warp) { warp = getenv("SH4_WARP") ? getenv("SH4_WARP") : ""; wnext = warp; }
        unsigned wf, wt;
        if (wnext && sscanf(wnext, "%u:%u", &wf, &wt) == 2 && g_frame >= wf) {
            wnext = strchr(wnext, ','); if (wnext) wnext++;
            uint32_t obj = gheap_calloc(0x500);
            W16(obj + 0x414, wt);
            rt_log("[warp] frame %u: jump type %u\n", g_frame, wt);
            // Reproduce rendering bugs in an exact room without editing game files.
            // Optional scene/position override of this debug jump; restore the table after the game copies it.
            uint32_t table = 0x628110 + (wt <= 13 ? wt : 0) * 0x50;
            uint8_t saved[0x50]; memcpy(saved, mb + table, sizeof saved);
            const char *scene = getenv("SH4_WARP_SCENE"), *pos = getenv("SH4_WARP_POS");
            const char *target_world = getenv("SH4_WARP_WORLD"), *visit = getenv("SH4_WARP_VISIT");
            if (target_world) {
                char *end;
                unsigned long w = strtoul(target_world, &end, 10);
                if (!*target_world || *end || w < 1 || w > 11) rt_fatal("invalid SH4_WARP_WORLD");
                W32(table, (uint32_t)w);
            }
            if (visit) {
                if (strcmp(visit, "0") && strcmp(visit, "1")) rt_fatal("invalid SH4_WARP_VISIT");
                W8(0x13d812d, (R8(0x13d812d) & ~4u) | (unsigned)(visit[0] - '0') << 2);
            }
            if (scene) W32(table + 4, strtoul(scene, NULL, 0));
            float xyzr[4];
            if (pos && sscanf(pos, "%f,%f,%f,%f", &xyzr[0], &xyzr[1], &xyzr[2], &xyzr[3]) == 4) {
                for (unsigned a = 0; a < 3; a++) W32(table + 0x10 + a * 4, rt_u32f(xyzr[a]));
                W32(table + 0x20, rt_u32f(xyzr[3]));
            }
            rt_log("[warp] target world=%u scene=%u pos=%g,%g,%g rot=%g\n", R32(table), R32(table + 4),
                   rt_f32(R32(table + 0x10)), rt_f32(R32(table + 0x14)), rt_f32(R32(table + 0x18)), rt_f32(R32(table + 0x20)));
            rt_guest_call(c, 0x4e6e00, 1, obj);
            memcpy(mb + table, saved, sizeof saved);
            gheap_free(obj);
        }
#ifdef __vita__
        {   // live diagnostics: ctl.txt keys=hex,hex (held) and data/sh4/key.txt "hex ms [hex ms]" (timed pulses, consumed)
            extern unsigned g_ctl_keys[8], g_ctl_nkeys, g_frame;
            static unsigned pk[4], pu[4];
            unsigned now = SDL_GetTicks();
            static unsigned lastpoll;
            static int live = -1; if (live < 0) live = getenv("SH4_LIVE") != NULL;   // remote-play tooling (tools/live.ps1): off unless SH4_LIVE=1
            if (live && now - lastpoll >= 40) {
                lastpoll = now;
                FILE *kf = fopen("ux0:data/sh4/key.txt", "r");
                if (kf) {
                    char kb[80] = {0}; size_t kn = fread(kb, 1, sizeof kb - 1, kf); kb[kn] = 0; fclose(kf); remove("ux0:data/sh4/key.txt");
                    char *c = kb;
                    for (int i = 0; i < 4 && *c; i++) { char *e; pk[i] = (unsigned)strtoul(c, &e, 16); if (e == c) break; c = e; pu[i] = now + (unsigned)strtoul(c, &e, 10); c = e; }
                }
            }
            for (unsigned i = 0; i < g_ctl_nkeys; i++) if (g_ctl_keys[i] < n) W8(p + g_ctl_keys[i], 0x80);
            for (int i = 0; i < 4; i++) if (pk[i] && (int)(pu[i] - now) > 0 && pk[i] < n) W8(p + pk[i], 0x80);
        }
#endif
        for (const char *s = script; *s;) {
            unsigned f = 0, k = 0, dur = 3;
            int used = 0;
            if (sscanf(s, "%u:%x%n", &f, &k, &used) < 2) break;
            s += used;
            if (*s == ':') { s++; dur = (unsigned)strtoul(s, (char **)&s, 10); }
            if (g_frame >= f && g_frame < f + dur && k < n) W8(p + k, 0x80);
            while (*s && *s != ',') s++;
            if (*s == ',') s++;
        }
        rt_audit_input(c, p, n);
    } else if (self->type == 2 && self->gc) {
        SDL_GameController *g = self->gc;
        extern unsigned g_frame;
        // The game chooses its player mode in 0x4fa3c0 (0xfd5ba4): apartment 302 and
        // tunnels (worlds 1/10) are first person; home (6) and spiral stairs (11) are third person too.
        // First person: left stick = forward/back + side step, right stick = turn + look up/down.
        // Third person: left stick = forward/back + turn (the game's tank controls), right stick = camera (CameraLeft/Right = lRz, CameraUp/Down = lZ).
        const uint32_t *gw = (const uint32_t *)mb;
        uint32_t world = gw[0xfd5a90 / 4];
        int tps = gw[0xfd5ba4 / 4] == 1;
        {
            static int lastw = -1, lastt = -1;
            if ((int)world != lastw || tps != lastt) { lastw = (int)world; lastt = tps; rt_log("[pad] world %u -> %s controls\n", world, tps ? "third person" : "first person"); }
        }
        // On-screen cursor (phone dial, keypads...): everything on the left stick, nothing on the right one. The game never calls ShowCursor on the
        // console and its pointer position (0x5ff27c/0x5ff280) is rewritten during the pad poll itself, so the mode comes from the list of clickable
        // elements the game fills while a pointer screen is shown (0xfd1d50, zero otherwise and in the apartment window view, which shares the
        // interaction state 0x10a1c34 == 7 with the phone) plus six state flags. Found by diffing guest memory dumps of normal play, the phone,
        // the window view and the Start menu (tools/dump_diff.py).
        int cur;
        {
            int flags = (gw[0x6e8ea0 / 4] == 0) + (gw[0xfd1aa4 / 4] == 0) + (gw[0x12f6dc4 / 4] == 0) +
                        (gw[0xfd5c28 / 4] == 1) + (gw[0x10a0f44 / 4] == 1) + (gw[0x10a1144 / 4] == 1);
            int on = gw[0xfd1d50 / 4] != 0 && flags >= 2;
            static Sh4PadContext context;
            cur = sh4_pad_cursor(&context, g_frame, on);
            static int last = -1;
            if (cur != last) { last = cur; rt_log("[cursor] mode %d frame %u list=%08x flags=%d/6\n", cur, g_frame, gw[0xfd1d50 / 4], flags); }
        }
        // Options menu: the left stick acts as the d-pad (up/down = row, left/right = value), the right stick does nothing and L/R = previous/next page
        // (MoveLeft/MoveRight, buttons 6/7: the game's page change). ponytail: only while the menu is open, L/R stay buttons 4/5 elsewhere
        extern int g_options_active;
        int menu = g_options_active;
        // Title screen (world 0) and pause menu: the right stick must not move the menu (in first person it was wired to the horizontal axis).
        // Pause = the two input-active flags 0x5ff2d8 / 0xec7664 are both zero (they stay 1 in normal play, phone and window view; found with dump_diff.py).
        int ui = world == 0 || (gw[0x5ff2d8 / 4] == 0 && gw[0xec7664 / 4] == 0);
        int stickmode = cur || menu || ui;   // sticks do not drive the character
        const Sh4PadSample *physical = sh4_pad_sample(&self->sample, g, g_frame);
        unsigned raw_buttons = physical->buttons;
#ifdef SH4_TEST_OPTIMIZATIONS
        static const char *script_buttons;
        if (!script_buttons) script_buttons = getenv("SH4_PAD_RAW") ? getenv("SH4_PAD_RAW") : "";
        raw_buttons = sh4_pad_script_buttons(script_buttons, g_frame, raw_buttons,
                                             (1u << SDL_CONTROLLER_BUTTON_MAX) - 1u);
#endif
        Sint16 lsx = physical->axes[0], lsy = physical->axes[1];
        Sint16 rsx = physical->axes[2], rsy = physical->axes[3];
#ifdef __vita__
        { extern int g_ctl_pad[4], g_ctl_pad_on; if (g_ctl_pad_on) { lsx = (Sint16)g_ctl_pad[0]; lsy = (Sint16)g_ctl_pad[1]; rsx = (Sint16)g_ctl_pad[2]; rsy = (Sint16)g_ctl_pad[3]; } }   // ctl.txt pad=: test injection
#endif
        {   // Deterministic frame-level controller injection for diagnostics only.
            static const char *script_pad;
            if (!script_pad) script_pad = getenv("SH4_PAD_SCRIPT") ? getenv("SH4_PAD_SCRIPT") : "";
            for (const char *p = script_pad; *p;) {
                unsigned start, duration; int x, y, rx, ry, used = 0;
                if (sscanf(p, "%u:%u:%d:%d:%d:%d%n", &start, &duration, &x, &y, &rx, &ry, &used) != 6) break;
                if (g_frame >= start && g_frame - start < duration) { lsx = (Sint16)x; lsy = (Sint16)y; rsx = (Sint16)rx; rsy = (Sint16)ry; }
                p += used; if (*p == ',') ++p; else break;
            }
        }
        {   // The tour's keyboard keys cannot exercise a controller-selected game.
            unsigned motion=rt_audit_motion();
            if (motion&1u) { lsx=24000;lsy=-18000; } // forward circle, bounded to the tour interval
            if (motion&2u) rsx=16000;
        }
        g_camera_pad_x = rsx; g_camera_pad_y = rsy;
        g_camera_pad_active = !cur && !menu && !ui;
        g_camera_pad_frame = g_frame;
        // Response curve (gentle around the centre, full speed at the edge) and per-axis scale: the game turns about twice as fast when looking
        // up/down as when turning, which made first person unmanageable; look up/down is scaled down to match.
        #define SHAPE(v, k) shape((v), (k))
        float camera = rt_camera_scale();
        if (menu) { W32(p + 0, axis(self, 0, 0)); W32(p + 4, axis(self, 1, 0)); }
        else if (cur) { W32(p + 0, axis(self, 0, SHAPE(lsx, 1.0f))); W32(p + 4, axis(self, 1, SHAPE(lsy, 1.0f))); }
        else if (tps || ui) { W32(p + 0, axis(self, 0, SHAPE(lsx, 1.0f))); W32(p + 4, axis(self, 1, SHAPE(lsy, 1.0f))); }
        else { W32(p + 0, axis(self, 0, SHAPE(rsx, 1.0f))); W32(p + 4, axis(self, 1, SHAPE(lsy, 1.0f))); }
        W32(p + 8, axis(self, 2, (tps && !stickmode) ? SHAPE(rsy, 0.9f * camera) : 0));    // lZ: camera up/down (third person)
        // Apartment observation mode can use a different camera callback than
        // 0x54bae0. Its player state remains 7 while movement is blocked.
        int observing = R32(0x10a1914) && R32(0x10a1c34) == 7;
        int window = world == 1 && !tps && !stickmode &&
            (rt_window_camera_active() || observing);
        W32(p + 12, axis(self, 3, window ? SHAPE(rsx, camera) : 0)); // lRx: exterior-window horizontal look
        W32(p + 16, axis(self, 4, (!tps && !stickmode) ? SHAPE(rsy, 0.55f) : 0)); // lRy: look up/down (first person)
        W32(p + 20, axis(self, 5, (tps && !stickmode) ? SHAPE(rsx, 0.9f * camera) : 0));   // lRz: camera left/right (third person)
        static int audit_pad = -1;
        if (audit_pad < 0) { const char *v = getenv("SH4_AUDIT_PAD"); audit_pad = v ? (atoi(v) == 2 ? 2 : 1) : 0; }
        if (audit_pad && (audit_pad == 2 || g_frame % 30 == 0)) {
            static unsigned logged = ~0u;
            if (logged != g_frame) {
                logged = g_frame;
                rt_log("[audit-pad] frame=%u world=%u scene=%u game_mode=%u tps=%d cursor=%d menu=%d window=%d sensitivity=%.3f raw=%d,%d,%d,%d axes=%d,%d,%d,%d,%d,%d\n",
                       g_frame, world, R32(0xfd5a94), R32(0xfd5ba4), tps, cur, menu, window, (double)camera, lsx, lsy, rsx, rsy,
                       (int32_t)R32(p), (int32_t)R32(p+4), (int32_t)R32(p+8), (int32_t)R32(p+12), (int32_t)R32(p+16), (int32_t)R32(p+20));
                if (audit_pad == 2) rt_log("[audit-pad-guest] frame=%u axes=%.5g,%.5g,%.5g,%.5g\n", g_frame,
                    (double)rt_f32(R32(0x1236c54)), (double)rt_f32(R32(0x1236c58)),
                    (double)rt_f32(R32(0x1236c5c)), (double)rt_f32(R32(0x1236c60)));
                if (audit_pad == 2) {
                    uint32_t player = R32(0xfd5ba0);
                    if (player) rt_log("[audit-pad-player] frame=%u rotation=%.6f,%.6f,%.6f\n", g_frame,
                        (double)rt_f32(R32(player + 0x80)), (double)rt_f32(R32(player + 0x84)), (double)rt_f32(R32(player + 0x88)));
                }
            }
        }
        int up = (raw_buttons >> SDL_CONTROLLER_BUTTON_DPAD_UP) & 1u;
        int dn = (raw_buttons >> SDL_CONTROLLER_BUTTON_DPAD_DOWN) & 1u;
        int lf = (raw_buttons >> SDL_CONTROLLER_BUTTON_DPAD_LEFT) & 1u;
        int rt = (raw_buttons >> SDL_CONTROLLER_BUTTON_DPAD_RIGHT) & 1u;
        int32_t pov = -1;
        if (up) pov = rt ? 4500 : lf ? 31500 : 0;
        else if (dn) pov = rt ? 13500 : lf ? 22500 : 18000;
        else if (rt) pov = 9000;
        else if (lf) pov = 27000;
        if (menu) {   // left stick = d-pad
            up |= lsy < -16000; dn |= lsy > 16000; lf |= lsx < -16000; rt |= lsx > 16000;
            pov = -1;
            if (up) pov = rt ? 4500 : lf ? 31500 : 0;
            else if (dn) pov = rt ? 13500 : lf ? 22500 : 18000;
            else if (rt) pov = 9000;
            else if (lf) pov = 27000;
        }
        W32(p + 32, (uint32_t)pov);
        W32(p + 36, 0xffffffffu); W32(p + 40, 0xffffffffu); W32(p + 44, 0xffffffffu);
        // Buttons per the game's built-in pad defaults (FUN_00405c00, used when there is no input.ini): 0 map, 1 cancel/run, 2 confirm/action,
        // 3 items, 4 rear camera, 5 aim, 6/7 side steps, 8 pause/skip, 9 item info, 10 dodge, 11 album.
        // Vita layout: cross confirm, circle cancel/run, triangle items, square map. No rear touch pad: holding the console touches it (it made Henry side-step on its own).
        static int combos = -1;
        if (combos < 0) {
            const char *v = getenv("SH4_PAD_COMBOS");
#ifdef __vita__
            combos = !v || !strcmp(v, "1");
#else
            combos = v && !strcmp(v, "1");
#endif
        }
        static Sh4PadButtons button_state;
        int button_context = menu ? SH4_PAD_OPTIONS : cur ? SH4_PAD_POINTER : SH4_PAD_GAMEPLAY;
        unsigned mapped = sh4_pad_buttons(&button_state, raw_buttons, button_context, combos);
        for (int i = 0; i < 12 && 48u + (uint32_t)i < n; i++) {
            if (mapped & (1u << i)) W8(p + 48 + (uint32_t)i, 0x80);
        }
        if (!menu && !cur && !tps && !ui) {   // left stick sideways = side step (first person only) (MoveLeft/MoveRight are buttons 6/7); nothing on the cursor screens
            if (lsx < -12000 && 54u < n) W8(p + 54, 0x80);
            if (lsx > 12000 && 55u < n) W8(p + 55, 0x80);
        }
        {   // automated test: SH4_PADIN="frame:button[:duration],..." (DI button index in decimal) presses a pad button
            static const char *ps; extern unsigned g_frame;
            if (!ps) ps = getenv("SH4_PADIN") ? getenv("SH4_PADIN") : "";
            for (const char *s = ps; *s;) {
                unsigned f = 0, k = 0, dur = 3; int used = 0;
                if (sscanf(s, "%u:%u%n", &f, &k, &used) < 2) break;
                s += used;
                if (*s == ':') { s++; dur = (unsigned)strtoul(s, (char **)&s, 10); }
                if (g_frame >= f && g_frame < f + dur && k < 12 && 48u + k < n) W8(p + 48 + k, 0x80);
                while (*s && *s != ',') s++;
                if (*s == ',') s++;
            }
        }
        {   // diagnostic: logs every change of the pad's buttons (SDL controller button / raw joystick button -> DI index)
            static unsigned prev_c, prev_j;
            unsigned cur_c = raw_buttons, cur_j = 0;
            SDL_Joystick *js = SDL_GameControllerGetJoystick(g);
            for (int i = 0; i < 16; i++) if (js && SDL_JoystickGetButton(js, i)) cur_j |= 1u << i;
            if (cur_c != prev_c || cur_j != prev_j) {
                extern unsigned g_frame;
                rt_log("[pad] frame %u sdl-ctrl %06x raw-joy %04x -> DI buttons:", g_frame, cur_c, cur_j);
                for (int i = 0; i < 12; i++) if (mapped & (1u << i)) rt_log(" %d", i);
                rt_log("\n");
                prev_c = cur_c; prev_j = cur_j;
            }
        }
    }
    STDRET(3, HR_OK);
}
M(dev, GetDeviceData) { HOSTFN; if (ARG(4)) W32(ARG(4), 0); STDRET(5, HR_OK); }
M(dev, SetDataFormat) { HOSTFN; THIS(DIDev, K_DIDEV); self->data_size = R32(ARG(2) + 12); STDRET(2, HR_OK); }
M(dev, SetEventNotification) { STDRET(2, HR_OK); }
M(dev, SetCooperativeLevel) { STDRET(3, HR_OK); }
M(dev, GetObjectInfo) { STDRET(4, 0x80070002u); }
M(dev, GetDeviceInfo) { STDRET(2, HR_OK); }
M(dev, RunControlPanel) { STDRET(3, HR_OK); }
M(dev, Initialize) { STDRET(4, HR_OK); }
M(dev, CreateEffect) { HOSTFN; W32(ARG(3), 0); STDRET(5, 0x80004001u); }
M(dev, EnumEffects) { STDRET(4, HR_OK); }
M(dev, GetEffectInfo) { STDRET(3, 0x80004001u); }
M(dev, GetForceFeedbackState) { HOSTFN; W32(ARG(2), 0); STDRET(2, HR_OK); }
M(dev, SendForceFeedbackCommand) { STDRET(2, HR_OK); }
M(dev, EnumCreatedEffectObjects) { STDRET(4, HR_OK); }
M(dev, Escape) { STDRET(2, 0x80004001u); }
M(dev, Poll) { STDRET(1, HR_OK); }
M(dev, SendDeviceData) { STDRET(5, HR_OK); }
M(dev, EnumEffectsInFile) { STDRET(5, HR_OK); }
M(dev, WriteEffectToFile) { STDRET(5, HR_OK); }
M(dev, BuildActionMap) { STDRET(4, 0x80004001u); }
M(dev, SetActionMap) { STDRET(4, 0x80004001u); }
M(dev, GetImageInfo) { STDRET(2, 0x80004001u); }
static const gfn dev_vt[] = {dev_QueryInterface, dev_AddRef, dev_Release, dev_GetCapabilities, dev_EnumObjects,
    dev_GetProperty, dev_SetProperty, dev_Acquire, dev_Unacquire, dev_GetDeviceState, dev_GetDeviceData,
    dev_SetDataFormat, dev_SetEventNotification, dev_SetCooperativeLevel, dev_GetObjectInfo, dev_GetDeviceInfo,
    dev_RunControlPanel, dev_Initialize, dev_CreateEffect, dev_EnumEffects, dev_GetEffectInfo,
    dev_GetForceFeedbackState, dev_SendForceFeedbackCommand, dev_EnumCreatedEffectObjects, dev_Escape, dev_Poll,
    dev_SendDeviceData, dev_EnumEffectsInFile, dev_WriteEffectToFile, dev_BuildActionMap, dev_SetActionMap,
    dev_GetImageInfo};

// ---------- IDirectInput8A ----------
M(di, QueryInterface) { STDRET(3, 0x80004002u); }
M(di, AddRef) { HOSTFN; ComObj *o = com_get(ARG(1)); STDRET(1, o ? ++o->refs : 0); }
M(di, Release) { HOSTFN; ComObj *o = com_get(ARG(1)); STDRET(1, o && o->refs > 1 ? --o->refs : 0); }
M(di, CreateDevice) {
    HOSTFN;
    const uint8_t *g = mb + ARG(2);
    W32(ARG(3), 0);
    DIDev *d = calloc(1, sizeof *d);
    if (!d) STDRET(4, 0x8007000eu);
    for (int a = 0; a < 8; a++) d->rmax[a] = 65535;
    if (!memcmp(g, GUID_KBD, 16)) d->type = 0;
    else if (!memcmp(g, GUID_MOUSE, 16)) d->type = 1;
    else if (!memcmp(g, GUID_JOY, 10)) {
        d->type = 2;
        d->joy = (int)R32(ARG(2) + 12);
        d->gc = SDL_GameControllerOpen(d->joy);
    } else {
        rt_log("[dinput] CreateDevice: unknown GUID %08x\n", R32(ARG(2)));
        free(d);
        W32(ARG(3), 0);
        STDRET(4, 0x80040154u);
    }
    rt_log("[dinput] CreateDevice type %d\n", d->type);
    W32(ARG(3), com_wrap(&d->com, K_DIDEV, dev_vt, 32, "IDirectInputDevice8A"));
    STDRET(4, HR_OK);
}
M(di, EnumDevices) {
    HOSTFN;
    uint32_t type = ARG(2), cb = ARG(3), ref = ARG(4);
    int want_pad = type == 0 || type == 4 || (type & 0xff) == 0x14 || (type & 0xff) == 0x15;
    if (!want_pad || getenv("SH4_NOPAD")) STDRET(5, HR_OK);
    uint32_t inst = gheap_calloc(580);
    if (!inst) STDRET(5, 0x8007000eu);
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i)) continue;
        memset(mb + inst, 0, 580);
        W32(inst, 580);
        memcpy(mb + inst + 4, GUID_JOY, 12);
        W32(inst + 16, (uint32_t)i);
        memcpy(mb + inst + 20, GUID_JOY, 12);
        W32(inst + 36, 0x215); // DI8DEVTYPE_GAMEPAD standard
        SDL_strlcpy(GSTR(inst + 40), SDL_GameControllerNameForIndex(i), 260);
        SDL_strlcpy(GSTR(inst + 300), SDL_GameControllerNameForIndex(i), 260);
        rt_log("[dinput] manette %d : %s\n", i, SDL_GameControllerNameForIndex(i));
        if (rt_guest_call(c, cb, 2, inst, ref) == 0) break; // DIENUM_STOP
    }
    gheap_free(inst);
    STDRET(5, HR_OK);
}
M(di, GetDeviceStatus) { STDRET(2, HR_OK); }
M(di, RunControlPanel) { STDRET(3, HR_OK); }
M(di, Initialize) { STDRET(3, HR_OK); }
M(di, FindDevice) { STDRET(4, 0x80040154u); }
M(di, EnumDevicesBySemantics) { STDRET(6, HR_OK); }
M(di, ConfigureDevices) { STDRET(5, 0x80004001u); }
static const gfn di_vt[] = {di_QueryInterface, di_AddRef, di_Release, di_CreateDevice, di_EnumDevices,
    di_GetDeviceStatus, di_RunControlPanel, di_Initialize, di_FindDevice, di_EnumDevicesBySemantics,
    di_ConfigureDevices};

void imp_DINPUT8_DirectInput8Create(CPU *c) {
    HOSTFN;
    W32(ARG(4), 0);
    DInput *d = calloc(1, sizeof *d);
    if (!d) STDRET(5, 0x8007000eu);
    W32(ARG(4), com_wrap(&d->com, K_DINPUT, di_vt, 11, "IDirectInput8A"));
    rt_log("[dinput] DirectInput8Create (version %x)\n", ARG(2));
    STDRET(5, HR_OK);
}
