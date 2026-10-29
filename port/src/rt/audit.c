#include "audit.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

int g_audit_enabled;
int g_audit_frames;
int g_audit_checks;
static FILE *tour;
static unsigned start = 3700, settle = 120, dwell = 240, timeout = 1800;
static int no_tour_shots, end_only_shots;
static unsigned index_, world, scene, entry, requested, arrived, age, last_input = ~0u;
static unsigned visit, bg;
static uint32_t selector_table;
static uint8_t selector_saved[8];
static unsigned done, capture_frame, capture_world, capture_scene, capture_index, capture_angle;
static unsigned capture_until;
static unsigned request_ticks;
static uint64_t previous;
static int active;
extern int g_movie_w, g_movie_h;
static SDL_atomic_t heartbeat_frame, heartbeat_ticks;

void rt_audit_request_capture(unsigned index, unsigned angle) {
    extern unsigned g_frame;
    uint8_t *const mb=g_mem;
    if (!g_audit_enabled || __atomic_load_n(&capture_frame,__ATOMIC_ACQUIRE)) return;
    capture_world=R32(0xfd5a90); capture_scene=R32(0xfd5a94);
    capture_index=index; capture_angle=angle; capture_until=g_frame+3;
    __atomic_store_n(&capture_frame,g_frame+1,__ATOMIC_RELEASE);
}

static void heal_player(void) {
    uint8_t *const mb = g_mem;
    float maximum = rt_f32(R32(0x10a0f4c));
    if (isfinite(maximum) && maximum > 0 && maximum <= 100000)
        W32(0x10a0f28, R32(0x10a0f4c));
}

static unsigned setting(const char *name, unsigned def, unsigned min, unsigned max) {
    const char *s = getenv(name);
    if (!s) return def;
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (!*s || *end || v < min || v > max) rt_fatal("invalid %s", name);
    return (unsigned)v;
}

void rt_audit_init(void) {
    const char *enabled = getenv("SH4_AUDIT"), *path = getenv("SH4_AUDIT_TOUR");
    g_audit_enabled = enabled && strcmp(enabled, "0");
    const char *frames = getenv("SH4_AUDIT_LOG");
    g_audit_frames = !frames || strcmp(frames, "0");
    const char *checks = getenv("SH4_AUDIT_VALIDATE");
    g_audit_checks = g_audit_enabled && (!checks || strcmp(checks, "0"));
    if (!g_audit_enabled) return;
    const char *no_shots = getenv("SH4_AUDIT_NO_SHOTS");
    no_tour_shots = no_shots && !strcmp(no_shots, "1");
    const char *end_shot = getenv("SH4_AUDIT_END_SHOT");
    end_only_shots = end_shot && !strcmp(end_shot, "1");
    start = setting("SH4_AUDIT_START", start, 1, 1000000);
    settle = setting("SH4_AUDIT_SETTLE", settle, 30, 10000);
    dwell = setting("SH4_AUDIT_DWELL", dwell, 60, 30000);
    timeout = setting("SH4_AUDIT_TIMEOUT", timeout, settle + dwell, 100000);
    if (path) {
        if (getenv("SH4_AUDIT_SHOTS")) rt_fatal("standalone play shots require no room tour");
        tour = fopen(path, "r");
        if (!tour) rt_fatal("audit: cannot read tour %s", path);
    }
    rt_log("[audit-config] schema=1 start=%u settle=%u dwell=%u timeout=%u tour=%s move=%d\n",
           start, settle, dwell, timeout, path ? path : "none", getenv("SH4_AUDIT_MOVE") != NULL);
}

static int next_room(CPU *c, unsigned frame) {
    char line[128];
    uint8_t *const mb = g_mem;
    if (selector_table) { memcpy(mb + selector_table, selector_saved, 8); selector_table = 0; }
    while (fgets(line, sizeof line, tour)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        unsigned values[5] = {0};
        char *p = line;
        int fields = 0;
        for (;;) {
            while (isspace((unsigned char)*p)) p++;
            if (!*p) break;
            if (!isdigit((unsigned char)*p) || fields == 5) rt_fatal("audit: invalid tour line %s", line);
            char *end;
            unsigned long value = strtoul(p, &end, 10);
            if (value > UINT_MAX || (*end && !isspace((unsigned char)*end))) rt_fatal("audit: invalid tour line %s", line);
            values[fields++] = (unsigned)value; p = end;
        }
        world = values[0]; scene = values[1]; entry = values[2]; visit = values[3]; bg = values[4];
        if (fields < 3 || world < 1 || world > 11 || scene > 255 || entry > 255 || visit > 1)
            rt_fatal("audit: invalid tour line %s", line);
        // Validate against the live table before requesting a transition.
        uint32_t table = R32(0x62fffc + world * 4);
        unsigned maximum = world == 1 ? 7 : world == 9 ? 2 : world == 10 ? 11 : world == 11 ? 7 : table ? R32(table) : 0;
        if (scene < 1 || scene > maximum || (world == 8 && scene == 22)) rt_fatal("audit: room absent/reserved");
        uint32_t blob = table && scene <= R32(table) ? R32(table + (scene + 3) * 4) : 0;
        int found = !entry;
        if (blob) for (unsigned i = 0; i < R8(blob); i++)
            if (R8(blob + 4 + i * 0x38 + 0xc) == entry) found = 1;
        if (!found) rt_fatal("audit: entrance absent from room");
        if (bg && world != 1 && !(world == 10 && scene == 1 && (bg == 1 || bg == 12)))
            rt_fatal("audit: background override only supports apartment/tunnel variants");
        if (world == 1 && bg) {
            static const uint32_t selectors[] = {0x62fdf0, 0x62fe40, 0x62fe68, 0x62fea0, 0x62feb8, 0x62fec8, 0x62fee0};
            if (bg > 30) rt_fatal("audit: invalid apartment background");
            selector_table = selectors[scene - 1];
            memcpy(selector_saved, mb + selector_table, 8);
            W32(selector_table, bg); W32(selector_table + 4, 0); // diagnostic selector, restored before next room
        }
        unsigned old_visit = (R8(0x13d812d) >> 2) & 1;
        W8(0x13d812d, (R8(0x13d812d) & ~4u) | (visit << 2));
        if (world == 10 && scene == 1 && bg)
            W8(0x13d8181, (R8(0x13d8181) & ~2u) | (bg == 1 ? 2 : 0));
        ++index_;
        if (getenv("SH4_AUDIT_HEAL")) heal_player();
        requested = frame; request_ticks = SDL_GetTicks(); arrived = age = 0; active = 1;
        rt_log("[audit] event=request index=%u frame=%u world=%u scene=%u entry=%u visit=%u target_bg=%u ticks=%u\n",
               index_, frame, world, scene, entry, visit, bg, SDL_GetTicks());
        // Same request as the game's jump event. 4f0620 resolves the door's spawn on arrival.
        if (R32(0xfd5a90) == world && old_visit == visit)
            rt_guest_call(c, 0x4f7220, 2, scene, entry);
        else {
            rt_guest_call(c, 0x4f72b0, 2, world, scene);
            W32(0xfd5aa8, entry);
        }
        if (!entry) {
            uint32_t pos = gheap_calloc(16), rotation = 0;
            if (!pos) rt_fatal("audit: spawn allocation failed");
            W32(pos + 12, rt_u32f(1));
            const char *source = "origin-unverified";
            for (unsigned i = 0; i < 14; i++) {
                uint32_t warp = 0x628110 + i * 0x50;
                if (R32(warp) != world || R32(warp + 4) != scene) continue;
                memcpy(mb + pos, mb + warp + 0x10, 12); rotation = R32(warp + 0x20);
                source = "game-jump-table"; break;
            }
            rt_guest_call(c, 0x52cee0, 2, pos, rotation);
            rt_log("[audit] event=spawn index=%u frame=%u world=%u scene=%u source=%s\n", index_, frame, world, scene, source);
            gheap_free(pos);
        }
        return 1;
    }
    fclose(tour); tour = NULL; done = 1;
    rt_log("[audit] event=finished index=%u frame=%u world=%u scene=%u\n", index_, frame, world, scene);
    rt_log_flush();
    // The external runner stops the application after collecting the evidence.
    return 0;
}

void rt_audit_input(CPU *c, uint32_t keys, uint32_t size) {
    extern unsigned g_frame;
    if (!g_audit_enabled) return;
    uint8_t *const mb = g_mem;
    const char *heal = getenv("SH4_AUDIT_HEAL");
    if (heal && !strcmp(heal, "always") && g_frame >= start) heal_player();
    if (!tour) {
        static const char *shots;
        static unsigned shot_index;
        if (!shots) shots = getenv("SH4_AUDIT_SHOTS") ? getenv("SH4_AUDIT_SHOTS") : "";
        if (*shots && !__atomic_load_n(&capture_frame, __ATOMIC_ACQUIRE)) {
            char *end;
            unsigned long wanted = strtoul(shots, &end, 10);
            if (end == shots || !wanted || wanted > 1000000 || (*end && *end != ','))
                rt_fatal("invalid SH4_AUDIT_SHOTS");
            if (g_frame >= wanted) {
                capture_index = ++shot_index; capture_world = R32(0xfd5a90); capture_scene = R32(0xfd5a94); capture_angle = 0;
                capture_until = g_frame + 3;
                rt_log("[audit-play-shot] requested=%lu frame=%u index=%u world=%u scene=%u\n",
                       wanted, g_frame, capture_index, capture_world, capture_scene);
                // Input runs after g_frame presents have already been issued.
                // That present may also have finished on the render consumer;
                // always target the upcoming one, as the room-tour path does.
                __atomic_store_n(&capture_frame, g_frame + 1, __ATOMIC_RELEASE);
                shots = *end ? end + 1 : end;
            }
        }
    }
    if (done || !tour || g_frame < start || last_input == g_frame) return;
    last_input = g_frame;
    if (!active) { next_room(c, g_frame); return; }
    int match = R32(0xfd5a90) == world && R32(0xfd5a94) == scene &&
                !R32(0xfd5aa0) && !R32(0xfd5aa4) && !(g_movie_w && g_movie_h);
    // Observe a sustained arrival; the id alone can change before loading is complete.
    if (!match) arrived = age = 0;
    else {
        if (!arrived) {
            arrived = g_frame;
            rt_log("[audit] event=arrive index=%u frame=%u world=%u scene=%u load_ms=%u\n",
                   index_, g_frame, world, scene, SDL_GetTicks() - request_ticks);
        }
        age = g_frame - arrived;
        if (!no_tour_shots && ((!end_only_shots && age == settle) || age == settle + dwell - 1)) {
            capture_world = world; capture_scene = scene; capture_index = index_;
            capture_angle = age == settle ? 0 : 1;
            capture_until = g_frame + 3;
            __atomic_store_n(&capture_frame, g_frame + 1, __ATOMIC_RELEASE);
        }
        if (getenv("SH4_AUDIT_MOVE") && age > settle + 30 && age < settle + dwell - 30 && size > 0x20)
            W8(keys + 0x20, 0x80);   // TurnRight; some rooms can still transition during this exploratory route
        if (getenv("SH4_AUDIT_SWEEP") && age > settle + 30 && age < settle + dwell - 30 && size > 0x26)
            W8(keys + 0x26, 0x80);   // CameraRight: fixed spawn, no walking across a room boundary
        if (age >= settle + dwell + 5) {
            rt_log("[audit] event=complete index=%u frame=%u world=%u scene=%u age=%u\n", index_, g_frame, world, scene, age);
            next_room(c, g_frame);
        }
    }
    // A slow successful load still gets the full settled measurement interval.
    // If identity/loading changes again, arrived resets and the original deadline applies.
    if (active && !arrived && g_frame - requested > timeout) {
        rt_log("[audit] event=timeout index=%u frame=%u world=%u scene=%u actual=%u:%u\n",
               index_, g_frame, world, scene, R32(0xfd5a90), R32(0xfd5a94));
        next_room(c, g_frame);
    }
}

unsigned rt_audit_motion(void) {
    extern unsigned g_frame;
    if (!g_audit_enabled || !active || !arrived || done ||
        age <= settle+30 || age >= settle+dwell-30) return 0;
    unsigned flags=(getenv("SH4_AUDIT_MOVE") ? 1u : 0u) |
                   (getenv("SH4_AUDIT_SWEEP") ? 2u : 0u);
    if (flags) {
        static unsigned last;
        if (g_frame-last>=120) {
            last=g_frame;
            uint8_t *const mb=g_mem;uint32_t player=R32(0xfd5ba0);
            if (player && player<=g_mem_size-0x90)
                rt_log("[audit-motion] frame=%u flags=%u world=%u scene=%u pos=%.3f,%.3f,%.3f yaw=%.6f\n",
                    g_frame,flags,R32(0xfd5a90),R32(0xfd5a94),rt_f32(R32(player+0x40)),
                    rt_f32(R32(player+0x44)),rt_f32(R32(player+0x48)),rt_f32(R32(player+0x84)));
        }
    }
    return flags;
}

void rt_audit_frame(unsigned frame) {
    if (!g_audit_enabled) return;
    uint8_t *const mb = g_mem;
    uint64_t now = SDL_GetPerformanceCounter();
    double factor = 1000.0 / SDL_GetPerformanceFrequency();
    double ms = previous ? (now - previous) * factor : 0;
    previous = now;
    SDL_AtomicSet(&heartbeat_frame, (int)frame);
    SDL_AtomicSet(&heartbeat_ticks, (int)SDL_GetTicks());
    if (!g_audit_frames) return;   // performance-only run: keep tour/captures/watchdog, use existing [perf] windows
    static int play_trace = -1;
    if (play_trace < 0) play_trace = getenv("SH4_AUDIT_PLAY") != NULL;
    if (play_trace && frame % 30 == 0) {
        uint32_t player = R32(0xfd5ba0);
        if (!player || player > g_mem_size - 0x90) player = 0;
        rt_log("[audit-play] frame=%u world=%u scene=%u bg=%u player=%08x pos=%.3f,%.3f,%.3f rot=%.6f,%.6f,%.6f movie=%d control_blocked=%u script=%u player_state=%u visit=%u action_id=%u\n",
               frame, R32(0xfd5a90), R32(0xfd5a94), R32(0xfd5ab0), player,
               player ? rt_f32(R32(player + 0x40)) : 0, player ? rt_f32(R32(player + 0x44)) : 0,
               player ? rt_f32(R32(player + 0x48)) : 0, player ? rt_f32(R32(player + 0x80)) : 0,
               player ? rt_f32(R32(player + 0x84)) : 0, player ? rt_f32(R32(player + 0x88)) : 0,
               !!(g_movie_w && g_movie_h), R32(0x10a1914), R32(0xfd5c28), R32(0x10a1c34),
               (R8(0x13d812d) >> 2) & 1, R16(0x108ec84));
        uint32_t action = R32(0xfd1a40);
        for (unsigned i = 0; i < 8 && action && action <= g_mem_size - 28; i++, action = R32(action + 24)) {
            uint32_t point = R32(action + 12);
            if (point > g_mem_size - 28) point = 0;
            rt_log("[audit-action] frame=%u node=%08x subtype=%u descriptor=%08x kind=%u point=%08x pos=%.3f,%.3f,%.3f\n",
                   frame, action, R32(action + 4), R32(action + 8), R32(action + 16), point,
                   point ? rt_f32(R32(point + 16)) : 0, point ? rt_f32(R32(point + 20)) : 0,
                   point ? rt_f32(R32(point + 24)) : 0);
        }
    }
    extern uint64_t g_heap_used;
    extern uint64_t g_t_body, g_t_gl, g_t_io, g_t_wait, g_t_sleep, g_t_link, g_t_tex;
    uint64_t counters[] = {g_t_body, g_t_gl, g_t_io, g_t_wait, g_t_sleep, g_t_link, g_t_tex};
    static uint64_t old[7];
    double delta[7];
    for (unsigned i = 0; i < 7; i++) { delta[i] = (counters[i] - old[i]) * factor; old[i] = counters[i]; }
    // Main-thread intervals include waiting on the render pipeline. No GPU readback or forced finish here.
    if (active && !done) {
        rt_log("[audit] event=frame index=%u frame=%u world=%u scene=%u actual=%u:%u phase=%s ms=%.3f capture=%u heap_kb=%u body=%.3f gl=%.3f io=%.3f wait=%.3f sleep=%.3f link=%.3f tex=%.3f bg=%u visit=%u movie=%u control_blocked=%u script=%u player_state=%u hp=%.2f max_hp=%.2f\n",
               index_, frame, world, scene, R32(0xfd5a90), R32(0xfd5a94),
               g_movie_w && g_movie_h ? "movie" : arrived && age >= settle && age < settle + dwell ? "measure" : "load",
               ms, frame <= capture_until, (unsigned)(g_heap_used >> 10),
               delta[0], delta[1], delta[2], delta[3], delta[4], delta[5], delta[6],
               R32(0xfd5a90) == 1 || R32(0xfd5a90) == 10 ? R32(0xfd5ab0) : R32(0xfd5a94), visit,
               !!(g_movie_w && g_movie_h), R32(0x10a1914), R32(0xfd5c28), R32(0x10a1c34),
               rt_f32(R32(0x10a0f28)), rt_f32(R32(0x10a0f4c)));
    }
}

void rt_audit_watchdog(void) {
    if (!g_audit_enabled) return;
    unsigned frame = (unsigned)SDL_AtomicGet(&heartbeat_frame);
    unsigned idle = SDL_GetTicks() - (unsigned)SDL_AtomicGet(&heartbeat_ticks);
    rt_log("[audit-heartbeat] frame=%u idle_ms=%u\n", frame, idle);
    if (frame && idle > 10000) rt_log("[audit-error] frame=%u no Present for %u ms\n", frame, idle);
}

int rt_audit_capture(unsigned frame, char *path, size_t size) {
    unsigned wanted = __atomic_load_n(&capture_frame, __ATOMIC_ACQUIRE);
    if (!g_audit_enabled || !wanted || frame < wanted) return 0;
    // Render consumer can be one frame behind the producer. Request is stable until consumed.
#ifdef __vita__
    const char *suffix = getenv("SH4_AUDIT_GZIP") ? "tga.gz" : "tga";
#else
    const char *suffix = "bmp";
#endif
    snprintf(path, size, "%s/audit_%03u_w%u_s%02u_a%u.%s", g_game_dir,
             capture_index, capture_world, capture_scene, capture_angle, suffix);
    rt_log("[audit] event=capture index=%u frame=%u world=%u scene=%u angle=%u path=%s\n",
           capture_index, frame, capture_world, capture_scene, capture_angle, path);
    // Keep ownership until the complete request has been read. Publishing an
    // empty slot earlier lets a fast producer replace the metadata mid-copy.
    __atomic_store_n(&capture_frame, 0, __ATOMIC_RELEASE);
    return 1;
}
