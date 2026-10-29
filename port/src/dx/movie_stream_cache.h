// Two independent MPEG demux cursors share at most 512 KiB of read-only data.
// Caller destroys the source only after both decoder threads have joined.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <SDL.h>

#define MOVIE_STREAM_BLOCK (128u * 1024u)
#define MOVIE_STREAM_SLOTS 4
typedef struct {
    FILE *file;
    SDL_mutex *lock;
    uint8_t *data;
    size_t size, file_pos;
    struct { size_t base, length; uint64_t age; int valid; } slots[MOVIE_STREAM_SLOTS];
    uint64_t age, requests, hit_bytes, read_bytes, reads, io_ticks;
} MovieStream;
typedef struct { MovieStream *source; size_t pos; } MovieCursor;

static void movie_stream_close(MovieStream *s) {
    if (!s) return;
    if (s->file) fclose(s->file);
    if (s->lock) SDL_DestroyMutex(s->lock);
    free(s->data);
    free(s);
}
static MovieStream *movie_stream_open(const char *path) {
    MovieStream *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->file = fopen(path, "rb");
    if (!s->file) { movie_stream_close(s); return NULL; }
    setvbuf(s->file, NULL, _IONBF, 0);
    if (fseek(s->file, 0, SEEK_END)) { movie_stream_close(s); return NULL; }
    long length = ftell(s->file);
    if (length <= 0 || fseek(s->file, 0, SEEK_SET)) { movie_stream_close(s); return NULL; }
    s->size = (size_t)length;
    s->lock = SDL_CreateMutex();
    s->data = malloc((size_t)MOVIE_STREAM_BLOCK * MOVIE_STREAM_SLOTS);
    if (!s->lock || !s->data) { movie_stream_close(s); return NULL; }
    return s;
}
static size_t movie_stream_read(MovieCursor *c, uint8_t *dst, size_t count) {
    MovieStream *s = c->source;
    if (!s || !count || c->pos >= s->size) return 0;
    if (count > s->size - c->pos) count = s->size - c->pos;
    size_t copied = 0;
    SDL_LockMutex(s->lock);
    s->requests++;
    while (copied < count) {
        size_t base = c->pos - c->pos % MOVIE_STREAM_BLOCK;
        int slot = -1, victim = 0;
        for (int i = 0; i < MOVIE_STREAM_SLOTS; i++) {
            if (s->slots[i].valid && s->slots[i].base == base) slot = i;
            if (!s->slots[i].valid || s->slots[i].age < s->slots[victim].age) victim = i;
        }
        int hit = slot >= 0;
        if (!hit) {
            slot = victim;
            s->slots[slot].valid = 0;
            uint64_t t = SDL_GetPerformanceCounter();
            if (s->file_pos != base && fseek(s->file, (long)base, SEEK_SET)) break;
            size_t n = fread(s->data + (size_t)slot * MOVIE_STREAM_BLOCK, 1, MOVIE_STREAM_BLOCK, s->file);
            s->io_ticks += SDL_GetPerformanceCounter() - t;
            s->reads++; s->read_bytes += n; s->file_pos = base + n;
            if (!n) break;
            s->slots[slot].base = base; s->slots[slot].length = n; s->slots[slot].valid = 1;
        }
        size_t offset = c->pos - base;
        if (offset >= s->slots[slot].length) break;
        size_t n = s->slots[slot].length - offset;
        if (n > count - copied) n = count - copied;
        memcpy(dst + copied, s->data + (size_t)slot * MOVIE_STREAM_BLOCK + offset, n);
        s->slots[slot].age = ++s->age;
        if (hit) s->hit_bytes += n;
        copied += n; c->pos += n;
    }
    SDL_UnlockMutex(s->lock);
    return copied;
}
