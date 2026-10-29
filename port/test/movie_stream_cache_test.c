// Exercise the production shared reader with two real concurrent demux cursors.
#define SDL_MAIN_HANDLED
#include <assert.h>
#include "dx/movie_stream_cache.h"
typedef struct { MovieStream *s; const char *path; unsigned seed; int result; } Job;
static unsigned next_random(unsigned *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static int reader(void *arg) {
    Job *j = arg;
    MovieCursor c = {j->s, 0};
    FILE *reference = fopen(j->path, "rb");
    uint8_t *actual = malloc(400000), *expected = malloc(400000);
    assert(reference && actual && expected);
    for (unsigned i = 0; i < 2000; i++) {
        c.pos = next_random(&j->seed) % (j->s->size + 1024);
        size_t n = next_random(&j->seed) % 400000, pos = c.pos;
        assert(!fseek(reference, (long)pos, SEEK_SET));
        size_t a = movie_stream_read(&c, actual, n), e = fread(expected, 1, n, reference);
        assert(a == e && c.pos == pos + e && !memcmp(actual, expected, a));
    }
    c.pos = j->s->size;
    assert(!movie_stream_read(&c, actual, 10));
    fclose(reference); free(actual); free(expected); j->result = 1;
    return 0;
}
int main(int argc, char **argv) {
    assert(argc > 1 && !SDL_Init(0));
    for (int i = 1; i < argc; i++) {
        MovieStream *s = movie_stream_open(argv[i]); assert(s);
        Job jobs[2] = {{s, argv[i], 1, 0}, {s, argv[i], 424242, 0}};
        SDL_Thread *a = SDL_CreateThread(reader, "video-test", &jobs[0]);
        SDL_Thread *b = SDL_CreateThread(reader, "audio-test", &jobs[1]);
        assert(a && b); SDL_WaitThread(a, NULL); SDL_WaitThread(b, NULL);
        assert(jobs[0].result && jobs[1].result);
        // Identical sequential windows for both cursors must share disk reads.
        memset(s->slots, 0, sizeof s->slots);
        s->reads = s->read_bytes = 0;
        MovieCursor cursors[2] = {{s,0},{s,0}};
        uint8_t *buf = malloc(MOVIE_STREAM_BLOCK); assert(buf);
        while (cursors[0].pos < s->size) {
            size_t a = movie_stream_read(&cursors[0], buf, MOVIE_STREAM_BLOCK);
            size_t b = movie_stream_read(&cursors[1], buf, MOVIE_STREAM_BLOCK);
            assert(a == b && a);
        }
        assert(s->read_bytes == s->size);
        printf("%s: 4000 concurrent byte/seek/EOF comparisons; two full cursors, one file worth of disk bytes\n", argv[i]);
        free(buf); movie_stream_close(s);
    }
    assert(!movie_stream_open("__missing_movie_file__"));
    SDL_Quit();
}
