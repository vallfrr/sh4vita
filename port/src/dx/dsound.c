// Emulated DirectSound 8: SDL software mixer (48 kHz stereo), buffers in guest memory,
// position notifications (Win32 events), simple 3D sound (attenuation + panning).
#include "d3d8/d3d8.h"
#include "win32/win32.h"
#include <SDL.h>
#include <stdlib.h>
#include <time.h>

#define OUT_RATE 48000
#define M(p, name) static void p##_##name(CPU *c)

typedef struct DSBuf {
    ComObj com;
    uint32_t mem, size, flags;
    uint32_t ch, rate, bits, align;
    int primary, playing, looping;
    double pos;                 // position de lecture en trames
    uint32_t freq;
    int32_t volume, pan;
    int is3d, mode;
    float p3[3], mind, maxd;
    int nnotify;
    struct { uint32_t off, ev; } notify[32];
    struct DSBuf *next;
    int *shared_refs;           // memory shared between duplicates
} DSBuf;
typedef struct { ComObj com; DSBuf *b; } DSSub; // interfaces 3D buffer / notify

static SDL_mutex *mx;
static DSBuf *bufs;
static struct { float pos[3], front[3], top[3], rolloff, dist; } L = {{0, 0, 0}, {0, 0, 1}, {0, 1, 0}, 1, 1};
static SDL_AudioDeviceID audio_dev;
static ComObj *listener;
static uint32_t buf_release_ref(DSBuf *b);
// The sole SDL device requests 1024 stereo frames. Its callback is serial, so
// this scratch buffer needs no heap allocation while normal audio is running.
#define MIX_FRAMES 1024u
static float mix_acc[MIX_FRAMES * 2];

static float db_gain(int32_t v) { return v <= -10000 ? 0.f : powf(10.f, (float)v / 2000.f); }

static float sample(DSBuf *b, uint32_t frame, int chn) {
    if (frame * b->align >= b->size) return 0;
    const uint8_t *p = g_mem + b->mem + frame * b->align;
    int ch = b->ch > 1 ? chn : 0;
    if (b->bits == 16) { int16_t s; memcpy(&s, p + 2 * ch, 2); return s / 32768.f; }
    return ((int)p[ch] - 128) / 128.f;
}

static void gains3d(DSBuf *b, float *gl, float *gr) {
    float d[3];
    for (int i = 0; i < 3; i++) d[i] = b->mode == 1 ? b->p3[i] : b->p3[i] - L.pos[i];
    float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * L.dist;
    float att = 1.f;
    float mind = b->mind > 0 ? b->mind : 1.f;
    if (dist > mind) {
        float dd = b->maxd > 0 && dist > b->maxd ? b->maxd : dist;
        att = mind / (mind + L.rolloff * (dd - mind));
    }
    float right[3] = {L.top[1] * L.front[2] - L.top[2] * L.front[1], L.top[2] * L.front[0] - L.top[0] * L.front[2],
                      L.top[0] * L.front[1] - L.top[1] * L.front[0]};
    float x = 0;
    if (dist > 1e-4f && b->mode != 1) x = (d[0] * right[0] + d[1] * right[1] + d[2] * right[2]) / (dist / L.dist);
    else if (b->mode == 1 && dist > 1e-4f) x = d[0] / (dist / L.dist);
    float a = (x + 1.f) * 0.25f * 3.14159265f; // constant-power panning
    *gl = att * cosf(a) * 1.41421356f;
    *gr = att * sinf(a) * 1.41421356f;
}

static void mix(void *ud, Uint8 *stream, int len) {
    (void)ud;
    static int registered;
    if (!registered) registered=rt_cpu_profile_thread("audio");
    int n = len / 4;
    if (len <= 0) return;
    float *acc = mix_acc;
    if ((unsigned)n <= MIX_FRAMES) memset(acc, 0, (size_t)n * 2 * sizeof(float));
    else {
        // Preserve larger callback support on other SDL backends. Allocation
        // refusal must output silence, not dereference NULL or advance buffers.
        acc = calloc((size_t)n * 2, sizeof(float));
        if (!acc) { memset(stream, 0, (size_t)len); return; }
    }
    static int nplay, pk; static Uint32 t0, tmix; Uint32 ts = SDL_GetTicks(); int np = 0;
    SDL_LockMutex(mx);
    for (DSBuf *b = bufs; b; b = b->next) {
        if (!b->playing || b->primary || !b->align) continue;
        np++;
        float g = db_gain(b->volume), gl = g, gr = g;
        if (b->pan < 0) gr *= db_gain(b->pan); else if (b->pan > 0) gl *= db_gain(-b->pan);
        if (b->is3d && b->mode != 2) { float l, r; gains3d(b, &l, &r); gl *= l; gr *= r; }
        double step = (double)(b->freq ? b->freq : b->rate) / OUT_RATE;
        uint32_t frames = b->size / b->align;
        double before = b->pos;
        for (int i = 0; i < n && b->playing; i++) {
            uint32_t f = (uint32_t)b->pos;
            float t = (float)(b->pos - f);
            uint32_t f2 = f + 1 < frames ? f + 1 : (b->looping ? 0 : f);
            float l = sample(b, f, 0) * (1 - t) + sample(b, f2, 0) * t;
            float r = sample(b, f, 1) * (1 - t) + sample(b, f2, 1) * t;
            acc[2 * i] += l * gl;
            acc[2 * i + 1] += r * gr;
            b->pos += step;
            if (b->pos >= frames) {
                if (b->looping) b->pos -= frames;
                else { b->pos = 0; b->playing = 0; }
            }
        }
        // notifications crossed during this block (byte positions)
        for (int k = 0; k < b->nnotify; k++) {
            uint32_t off = b->notify[k].off;
            if (off == 0xffffffffu) { if (!b->playing) w32_event_set(b->notify[k].ev, 1); continue; }
            double fo = (double)off / b->align;
            int crossed = b->pos >= before ? (fo >= before && fo < b->pos) : (fo >= before || fo < b->pos);
            if (crossed) w32_event_set(b->notify[k].ev, 1);
        }
    }
    SDL_UnlockMutex(mx);
    extern void movie_audio_mix(float *acc, int n);
    movie_audio_mix(acc, n);
    int16_t *out = (int16_t *)stream;
    for (int i = 0; i < n * 2; i++) {
        float v = acc[i] * 32767.f;
        out[i] = (int16_t)(v > 32767.f ? 32767 : v < -32768.f ? -32768 : v);
        int a = out[i] < 0 ? -out[i] : out[i]; if (a > pk) pk = a;
    }
    if (acc != mix_acc) free(acc);
#ifndef __vita__
    {   // SH4_WAV=file: raw s16le 48 kHz stereo PCM of the mix (PC video capture)
        static FILE *wav; static int init;
        if (!init) { init = 1; if (getenv("SH4_WAV")) wav = fopen(getenv("SH4_WAV"), "wb"); }
        if (wav) {
            static int first = 1;
            if (first) { first = 0; struct timespec ts; timespec_get(&ts, TIME_UTC); rt_log("[wav] start epoch_ms=%lld\n", (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000); }
            fwrite(stream, 1, (size_t)len, wav); fflush(wav);
        }
    }
#endif
    if (np > nplay) nplay = np;
    tmix += SDL_GetTicks() - ts;
    if (ts - t0 >= 10000) {   // diagnostics: proof that audio comes out, and its cost
        rt_log("[audio] 10 s: max buffers %d, peak %d, mix %u ms\n", nplay, pk, tmix);
        t0 = ts; nplay = pk = 0; tmix = 0;
    }
}

static void audio_start(void) {
    if (audio_dev) return;
    mx = SDL_CreateMutex();
    SDL_AudioSpec want = {0}, have;
    want.freq = OUT_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = MIX_FRAMES;
    want.callback = mix;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!audio_dev) rt_log("[dsound] SDL audio : %s\n", SDL_GetError());
    else SDL_PauseAudioDevice(audio_dev, 0);
}

// ---------- IDirectSound3DBuffer8 / IDirectSoundNotify8 ----------
M(sub, QueryInterface) { STDRET(3, 0x80004002u); }
M(sub, AddRef) { HOSTFN; ComObj *o = com_get(ARG(1)); STDRET(1, o ? ++o->refs : 0); }
M(sub, Release) {
    HOSTFN;
    ComObj *o = com_get(ARG(1));
    if (!o) STDRET(1, 0);
    int32_t r = --o->refs;
    if (r <= 0 && o != listener) {
        DSBuf *b = ((DSSub *)o)->b;
        com_free(o); free(o);
        if (b) buf_release_ref(b);
        r = 0;
    }
    STDRET(1, (uint32_t)(r < 0 ? 0 : r));
}
#define B3 DSSub *s = com_get_kind(ARG(1), K_DS3DB); DSBuf *b = s ? s->b : NULL; if (!b) STDRET(1, HR_FAIL)
static void rdvec(CPU *c, uint32_t n, float *v) { HOSTFN; v[0] = rt_f32(ARG(n)); v[1] = rt_f32(ARG(n + 1)); v[2] = rt_f32(ARG(n + 2)); }
M(b3, GetAllParameters) { STDRET(2, HR_OK); }
M(b3, GetConeAngles) { STDRET(3, HR_OK); }
M(b3, GetConeOrientation) { STDRET(2, HR_OK); }
M(b3, GetConeOutsideVolume) { STDRET(2, HR_OK); }
M(b3, GetMaxDistance) { HOSTFN; DSSub *s = com_get_kind(ARG(1), K_DS3DB); W32(ARG(2), rt_u32f(s ? s->b->maxd : 1e9f)); STDRET(2, HR_OK); }
M(b3, GetMinDistance) { HOSTFN; DSSub *s = com_get_kind(ARG(1), K_DS3DB); W32(ARG(2), rt_u32f(s ? s->b->mind : 1.f)); STDRET(2, HR_OK); }
M(b3, GetMode) { HOSTFN; DSSub *s = com_get_kind(ARG(1), K_DS3DB); W32(ARG(2), s ? (uint32_t)s->b->mode : 0); STDRET(2, HR_OK); }
M(b3, GetPosition) { HOSTFN; DSSub *s = com_get_kind(ARG(1), K_DS3DB); for (int i = 0; i < 3; i++) W32(ARG(2) + 4 * i, rt_u32f(s ? s->b->p3[i] : 0)); STDRET(2, HR_OK); }
M(b3, GetVelocity) { HOSTFN; memset(mb + ARG(2), 0, 12); STDRET(2, HR_OK); }
M(b3, SetAllParameters) {
    HOSTFN; DSSub *s = com_get_kind(ARG(1), K_DS3DB);
    uint32_t p = ARG(2);
    if (s) {
        SDL_LockMutex(mx);
        for (int i = 0; i < 3; i++) s->b->p3[i] = rt_f32(R32(p + 4 + 4 * i));
        s->b->mind = rt_f32(R32(p + 52)); s->b->maxd = rt_f32(R32(p + 56)); s->b->mode = (int)R32(p + 60);
        SDL_UnlockMutex(mx);
    }
    STDRET(3, HR_OK);
}
M(b3, SetConeAngles) { STDRET(4, HR_OK); }
M(b3, SetConeOrientation) { STDRET(5, HR_OK); }
M(b3, SetConeOutsideVolume) { STDRET(3, HR_OK); }
M(b3, SetMaxDistance) { HOSTFN; B3; b->maxd = rt_f32(ARG(2)); STDRET(3, HR_OK); }
M(b3, SetMinDistance) { HOSTFN; B3; b->mind = rt_f32(ARG(2)); STDRET(3, HR_OK); }
M(b3, SetMode) { HOSTFN; B3; b->mode = (int)ARG(2); STDRET(3, HR_OK); }
M(b3, SetPosition) { HOSTFN; B3; SDL_LockMutex(mx); rdvec(c, 2, b->p3); SDL_UnlockMutex(mx); STDRET(5, HR_OK); }
M(b3, SetVelocity) { STDRET(5, HR_OK); }
static const gfn b3_vt[] = {sub_QueryInterface, sub_AddRef, sub_Release, b3_GetAllParameters, b3_GetConeAngles,
    b3_GetConeOrientation, b3_GetConeOutsideVolume, b3_GetMaxDistance, b3_GetMinDistance, b3_GetMode,
    b3_GetPosition, b3_GetVelocity, b3_SetAllParameters, b3_SetConeAngles, b3_SetConeOrientation,
    b3_SetConeOutsideVolume, b3_SetMaxDistance, b3_SetMinDistance, b3_SetMode, b3_SetPosition, b3_SetVelocity};

M(nt, SetNotificationPositions) {
    HOSTFN;
    DSSub *s = com_get_kind(ARG(1), K_DSNOTIFY);
    uint32_t n = ARG(2), p = ARG(3);
    if (s) {
        SDL_LockMutex(mx);
        s->b->nnotify = n > 32 ? 32 : (int)n;
        for (int i = 0; i < s->b->nnotify; i++) { s->b->notify[i].off = R32(p + 8 * i); s->b->notify[i].ev = R32(p + 8 * i + 4); }
        SDL_UnlockMutex(mx);
    }
    STDRET(3, HR_OK);
}
static const gfn nt_vt[] = {sub_QueryInterface, sub_AddRef, sub_Release, nt_SetNotificationPositions};

// ---------- IDirectSound3DListener8 ----------
M(l3, Get) { STDRET(2, HR_OK); }
M(l3, GetOrientation) { STDRET(3, HR_OK); }
M(l3, SetAllParameters) { STDRET(3, HR_OK); }
M(l3, SetDistanceFactor) { HOSTFN; L.dist = rt_f32(ARG(2)); STDRET(3, HR_OK); }
M(l3, SetDopplerFactor) { STDRET(3, HR_OK); }
M(l3, SetOrientation) {
    HOSTFN;
    SDL_LockMutex(mx);
    rdvec(c, 2, L.front);
    rdvec(c, 5, L.top);
    SDL_UnlockMutex(mx);
    STDRET(8, HR_OK);
}
M(l3, SetPosition) { HOSTFN; SDL_LockMutex(mx); rdvec(c, 2, L.pos); SDL_UnlockMutex(mx); STDRET(5, HR_OK); }
M(l3, SetRolloffFactor) { HOSTFN; L.rolloff = rt_f32(ARG(2)); STDRET(3, HR_OK); }
M(l3, SetVelocity) { STDRET(5, HR_OK); }
M(l3, Commit) { STDRET(1, HR_OK); }
static const gfn l3_vt[] = {sub_QueryInterface, sub_AddRef, sub_Release, l3_Get, l3_Get, l3_Get, l3_GetOrientation,
    l3_Get, l3_Get, l3_Get, l3_SetAllParameters, l3_SetDistanceFactor, l3_SetDopplerFactor, l3_SetOrientation,
    l3_SetPosition, l3_SetRolloffFactor, l3_SetVelocity, l3_Commit};

// ---------- IDirectSoundBuffer8 ----------
#define BUF DSBuf *b = com_get_kind(ARG(1), K_DSBUF); if (!b) STDRET(1, HR_FAIL)
static const gfn buf_vt[];
static DSBuf *buf_new(void) {
    DSBuf *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->mind = 1.f;
    b->maxd = 1e9f;
    return b;
}
M(buf, QueryInterface) {
    HOSTFN;
    DSBuf *b = com_get_kind(ARG(1), K_DSBUF);
    uint32_t iid = R32(ARG(2)), out = ARG(3);
    if (!b) STDRET(3, HR_FAIL);
    if (iid == 0x279afa84u) {
        if (!listener) { listener = calloc(1, sizeof(DSSub)); com_wrap(listener, K_DS3DL, l3_vt, 18, "IDirectSound3DListener8"); }
        listener->refs++;
        W32(out, listener->guest);
        STDRET(3, HR_OK);
    }
    if (iid == 0x279afa86u || iid == 0xb0210783u) {
        DSSub *s = calloc(1, sizeof *s);
        if (!s) { W32(out, 0); STDRET(3, 0x8007000eu); }
        s->b = b;
        if (iid == 0x279afa86u) com_wrap(&s->com, K_DS3DB, b3_vt, 21, "IDirectSound3DBuffer8");
        else com_wrap(&s->com, K_DSNOTIFY, nt_vt, 4, "IDirectSoundNotify8");
        b->com.refs++; // The child interface owns its parent until its final Release.
        W32(out, s->com.guest);
        STDRET(3, HR_OK);
    }
    if (iid == 0x6825a449u) { b->com.refs++; W32(out, b->com.guest); STDRET(3, HR_OK); }
    rt_log("[dsound] unsupported QueryInterface buffer IID %08x\n", iid);
    W32(out, 0);
    STDRET(3, 0x80004002u);
}
M(buf, AddRef) { HOSTFN; BUF; STDRET(1, ++b->com.refs); }
static uint32_t buf_release_ref(DSBuf *b) {
    int32_t r = --b->com.refs;
    if (r <= 0) {
        SDL_LockMutex(mx);
        for (DSBuf **pp = &bufs; *pp; pp = &(*pp)->next)
            if (*pp == b) { *pp = b->next; break; }
        SDL_UnlockMutex(mx);
        if (b->shared_refs && --*b->shared_refs == 0) { extern uint64_t g_mem_ds; g_mem_ds -= b->size; gheap_free(b->mem); free(b->shared_refs); }
        com_free(&b->com);
        free(b);
        r = 0;
    }
    return (uint32_t)r;
}
M(buf, Release) { HOSTFN; BUF; STDRET(1, buf_release_ref(b)); }
M(buf, GetCaps) {
    HOSTFN; BUF;
    uint32_t p = ARG(2);
    W32(p + 4, b->flags); W32(p + 8, b->size);
    STDRET(2, HR_OK);
}
M(buf, GetCurrentPosition) {
    HOSTFN; BUF;
    SDL_LockMutex(mx);
    uint32_t play = b->align ? (uint32_t)b->pos * b->align : 0;
    SDL_UnlockMutex(mx);
    uint32_t lead = b->align ? (b->rate / 50) * b->align : 0; // ~20 ms write-ahead
    if (ARG(2)) W32(ARG(2), play);
    if (ARG(3)) W32(ARG(3), b->size ? (play + lead) % b->size : 0);
    STDRET(3, HR_OK);
}
M(buf, GetFormat) {
    HOSTFN; BUF;
    uint32_t p = ARG(2);
    if (p) {
        W16(p, 1); W16(p + 2, b->ch); W32(p + 4, b->rate); W32(p + 8, b->rate * b->align);
        W16(p + 12, b->align); W16(p + 14, b->bits); W16(p + 16, 0);
    }
    if (ARG(4)) W32(ARG(4), 18);
    STDRET(4, HR_OK);
}
M(buf, GetVolume) { HOSTFN; BUF; W32(ARG(2), (uint32_t)b->volume); STDRET(2, HR_OK); }
M(buf, GetPan) { HOSTFN; BUF; W32(ARG(2), (uint32_t)b->pan); STDRET(2, HR_OK); }
M(buf, GetFrequency) { HOSTFN; BUF; W32(ARG(2), b->freq ? b->freq : b->rate); STDRET(2, HR_OK); }
M(buf, GetStatus) { HOSTFN; BUF; W32(ARG(2), (b->playing ? 1u : 0u) | (b->playing && b->looping ? 4u : 0u)); STDRET(2, HR_OK); }
M(buf, Initialize) { STDRET(3, 0x88780082u); }
M(buf, Lock) {
    HOSTFN; BUF;
    uint32_t off = ARG(2), n = ARG(3), flags = ARG(8);
    if (b->primary || !b->size) STDRET(8, D3DERR_INVALIDCALL);
    if (flags & 2) { off = 0; n = b->size; }
    if (flags & 1) {
        SDL_LockMutex(mx);
        off = b->align ? ((uint32_t)b->pos * b->align + (b->rate / 50) * b->align) % b->size : 0;
        SDL_UnlockMutex(mx);
    }
    off %= b->size;
    if (n > b->size) n = b->size;
    uint32_t n1 = n < b->size - off ? n : b->size - off;
    W32(ARG(4), b->mem + off);
    W32(ARG(5), n1);
    if (ARG(6)) W32(ARG(6), n1 < n ? b->mem : 0);
    if (ARG(7)) W32(ARG(7), n - n1);
    STDRET(8, HR_OK);
}
M(buf, Play) {
    HOSTFN; BUF;
    SDL_LockMutex(mx);
    b->playing = 1;
    b->looping = ARG(4) & 1;
    SDL_UnlockMutex(mx);
    STDRET(4, HR_OK);
}
M(buf, SetCurrentPosition) {
    HOSTFN; BUF;
    SDL_LockMutex(mx);
    if (b->align) b->pos = (double)(ARG(2) / b->align);
    SDL_UnlockMutex(mx);
    STDRET(2, HR_OK);
}
M(buf, SetFormat) { STDRET(2, HR_OK); }
M(buf, SetVolume) { HOSTFN; BUF; b->volume = (int32_t)ARG(2); STDRET(2, HR_OK); }
M(buf, SetPan) { HOSTFN; BUF; b->pan = (int32_t)ARG(2); STDRET(2, HR_OK); }
M(buf, SetFrequency) { HOSTFN; BUF; b->freq = ARG(2); STDRET(2, HR_OK); }
M(buf, Stop) {
    HOSTFN; BUF;
    SDL_LockMutex(mx);
    b->playing = 0;
    for (int k = 0; k < b->nnotify; k++)
        if (b->notify[k].off == 0xffffffffu) w32_event_set(b->notify[k].ev, 1);
    SDL_UnlockMutex(mx);
    STDRET(1, HR_OK);
}
M(buf, Unlock) { STDRET(5, HR_OK); }
M(buf, Restore) { STDRET(1, HR_OK); }
M(buf, SetFX) { STDRET(4, 0x80004001u); }
M(buf, AcquireResources) { STDRET(4, HR_OK); }
M(buf, GetObjectInPath) { STDRET(5, 0x80004002u); }
static const gfn buf_vt[] = {buf_QueryInterface, buf_AddRef, buf_Release, buf_GetCaps, buf_GetCurrentPosition,
    buf_GetFormat, buf_GetVolume, buf_GetPan, buf_GetFrequency, buf_GetStatus, buf_Initialize, buf_Lock, buf_Play,
    buf_SetCurrentPosition, buf_SetFormat, buf_SetVolume, buf_SetPan, buf_SetFrequency, buf_Stop, buf_Unlock,
    buf_Restore, buf_SetFX, buf_AcquireResources, buf_GetObjectInPath};

static void buf_link(DSBuf *b) {
    SDL_LockMutex(mx);
    b->next = bufs;
    bufs = b;
    SDL_UnlockMutex(mx);
}

// ---------- IDirectSound8 ----------
M(ds, QueryInterface) { STDRET(3, 0x80004002u); }
M(ds, AddRef) { HOSTFN; ComObj *o = com_get(ARG(1)); STDRET(1, o ? ++o->refs : 0); }
M(ds, Release) { HOSTFN; ComObj *o = com_get(ARG(1)); STDRET(1, o && o->refs > 1 ? --o->refs : 0); }
M(ds, CreateSoundBuffer) {
    HOSTFN;
    W32(ARG(3), 0);
    uint32_t d = ARG(2), flags = R32(d + 4), bytes = R32(d + 8), wfx = R32(d + 16);
    DSBuf *b = buf_new();
    if (!b) STDRET(4, 0x8007000eu); // E_OUTOFMEMORY
    b->flags = flags;
    b->primary = flags & 1;
    b->is3d = (flags & 0x10) != 0;
    if (!b->primary && wfx) {
        b->ch = R16(wfx + 2); b->rate = R32(wfx + 4); b->align = R16(wfx + 12); b->bits = R16(wfx + 14);
        b->size = bytes;
        b->mem = gheap_calloc(bytes ? bytes : 4);
        if (!b->mem) { free(b); STDRET(4, 0x8007000eu); }
        b->shared_refs = calloc(1, sizeof(int));
        if (!b->shared_refs) { gheap_free(b->mem); free(b); STDRET(4, 0x8007000eu); }
        extern uint64_t g_mem_ds;
        g_mem_ds += bytes;
        *b->shared_refs = 1;
    }
    com_wrap(&b->com, K_DSBUF, buf_vt, 24, "IDirectSoundBuffer8");
    buf_link(b);
    W32(ARG(3), b->com.guest);
    STDRET(4, HR_OK);
}
M(ds, GetCaps) { STDRET(2, HR_OK); }
M(ds, DuplicateSoundBuffer) {
    HOSTFN;
    DSBuf *o = com_get_kind(ARG(2), K_DSBUF);
    if (!o) STDRET(3, D3DERR_INVALIDCALL);
    DSBuf *b = buf_new();
    if (!b) { W32(ARG(3), 0); STDRET(3, 0x8007000eu); }
    *b = *o;
    b->com = (ComObj){0};
    b->playing = 0; b->pos = 0; b->nnotify = 0; b->next = NULL;
    if (b->shared_refs) ++*b->shared_refs;
    com_wrap(&b->com, K_DSBUF, buf_vt, 24, "IDirectSoundBuffer8");
    buf_link(b);
    W32(ARG(3), b->com.guest);
    STDRET(3, HR_OK);
}
M(ds, SetCooperativeLevel) { STDRET(3, HR_OK); }
M(ds, Compact) { STDRET(1, HR_OK); }
M(ds, GetSpeakerConfig) { HOSTFN; W32(ARG(2), 4); STDRET(2, HR_OK); }
M(ds, SetSpeakerConfig) { STDRET(2, HR_OK); }
M(ds, Initialize) { STDRET(2, HR_OK); }
M(ds, VerifyCertification) { HOSTFN; W32(ARG(2), 0); STDRET(2, HR_OK); }
static const gfn ds_vt[] = {ds_QueryInterface, ds_AddRef, ds_Release, ds_CreateSoundBuffer, ds_GetCaps,
    ds_DuplicateSoundBuffer, ds_SetCooperativeLevel, ds_Compact, ds_GetSpeakerConfig, ds_SetSpeakerConfig,
    ds_Initialize, ds_VerifyCertification};

void imp_DSOUND_ord11(CPU *c) {
    HOSTFN;
    audio_start();
    ComObj *o = calloc(1, sizeof *o);
    W32(ARG(2), com_wrap(o, K_DSOUND, ds_vt, 12, "IDirectSound8"));
    rt_log("[dsound] DirectSoundCreate8\n");
    STDRET(3, HR_OK);
}
