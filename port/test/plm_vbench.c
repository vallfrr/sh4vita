// pl_mpeg video bench outside the game: plm_vbench movie.pac [frames]
// Decodes N frames, prints the time per frame and an FNV-1a hash of the Y/Cb/Cr planes (must stay identical
// between the C and NEON versions; compare with SH4_MOVIE_BENCH on the console).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define PL_MPEG_IMPLEMENTATION
#include "third_party/pl_mpeg/pl_mpeg.h"

static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: plm_vbench movie.pac [frames]\n"); return 1; }
    int n = argc > 2 ? atoi(argv[2]) : 900;
    plm_t *p = plm_create_with_filename(argv[1]);
    if (!p) { fprintf(stderr, "cannot open\n"); return 1; }
    plm_set_audio_enabled(p, 0);
    uint32_t h = 2166136261u;
    clock_t t0 = clock();
    int i = 0;
    for (; i < n; i++) {
        plm_frame_t *f = plm_decode_video(p);
        if (!f) break;
        h = fnv(h, f->y.data, (size_t)f->y.width * f->y.height);
        h = fnv(h, f->cb.data, (size_t)f->cb.width * f->cb.height);
        h = fnv(h, f->cr.data, (size_t)f->cr.width * f->cr.height);
        if (i % 30 == 0) printf("frame %d: hash %08x\n", i, h);
    }
    double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    printf("%d frames, %.2f ms/frame (with hash), hash %08x\n", i, i ? ms / i : 0, h);
    plm_destroy(p);
    return 0;
}
