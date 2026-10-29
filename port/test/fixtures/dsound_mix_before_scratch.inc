// Previous port mixer from the distributed 28 October source snapshot.
// Test oracle only; no original game code or game data.
static void mix(void *ud, Uint8 *stream, int len) {
    (void)ud;
    static int registered;
    if (!registered) registered=rt_cpu_profile_thread("audio");
    int n = len / 4;
    float *acc = calloc((size_t)n * 2, sizeof(float));
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
    free(acc);
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

