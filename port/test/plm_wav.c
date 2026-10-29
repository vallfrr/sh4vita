// Movie sound check: decodes the audio of a .pac (MPEG-PS, MP1 Layer I) to a 16-bit WAV and prints
// statistics (peak, RMS, zero-crossing rate: white noise gives ~0.5, music < 0.1).
// gcc -O2 -I../src test/plm_wav.c -o plm_wav && ./plm_wav m_00.pac out.wav 20
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define PL_MPEG_IMPLEMENTATION
#include "third_party/pl_mpeg/pl_mpeg.h"

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    double secs = argc > 3 ? atof(argv[3]) : 20;
    plm_t *p = plm_create_with_filename(argv[1]);
    if (!p) return 1;
    plm_set_video_enabled(p, 0);
    FILE *o = fopen(argv[2], "wb");
    uint8_t hd[44] = {0};
    fwrite(hd, 1, 44, o);
    double sum = 0, peak = 0;
    long n = 0, zc = 0, frames = 0;
    float last = 0;
    plm_samples_t *s;
    while ((s = plm_decode_audio(p)) && s->time < secs) {
        frames++;
        for (unsigned i = 0; i < s->count * 2; i++) {
            float v = s->interleaved[i];
            short q = (short)(v > 1 ? 32767 : v < -1 ? -32767 : v * 32767);
            fwrite(&q, 2, 1, o);
            if (i & 1) continue;
            sum += v * v; n++;
            if (fabsf(v) > peak) peak = fabsf(v);
            if ((v > 0) != (last > 0)) zc++;
            last = v;
        }
    }
    int rate = plm_get_samplerate(p);
    uint32_t data = (uint32_t)(n * 4);
    memcpy(hd, "RIFF", 4); *(uint32_t *)(hd + 4) = 36 + data; memcpy(hd + 8, "WAVEfmt ", 8);
    *(uint32_t *)(hd + 16) = 16; *(uint16_t *)(hd + 20) = 1; *(uint16_t *)(hd + 22) = 2;
    *(uint32_t *)(hd + 24) = rate; *(uint32_t *)(hd + 28) = rate * 4; *(uint16_t *)(hd + 32) = 4;
    *(uint16_t *)(hd + 34) = 16; memcpy(hd + 36, "data", 4); *(uint32_t *)(hd + 40) = data;
    fseek(o, 0, SEEK_SET);
    fwrite(hd, 1, 44, o);
    fclose(o);
    printf("%ld frames, %ld samples at %d Hz, peak %.3f, RMS %.4f, zero crossings %.4f\n", frames, n, rate, peak,
           n ? sqrt(sum / n) : 0, n ? (double)zc / n : 0);
    return frames ? 0 : 1;
}
