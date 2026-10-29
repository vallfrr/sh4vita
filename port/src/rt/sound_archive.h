#pragma once
#include <stdint.h>

/* Immutable GOG v5 SDPA directory. The reader must return the exact bytes
   requested; a failed read must never be published as a valid cache entry. */
#define SH4_SOUND_ENTRIES 355u
typedef int (*SH4SoundRead)(void *ctx, uint32_t offset, void *dst, uint32_t bytes);
typedef struct SH4SoundEntry { uint32_t offset, bytes, opaque; } SH4SoundEntry;
typedef struct SH4SoundArchive {
    uint32_t file_bytes;
    SH4SoundEntry entries[SH4_SOUND_ENTRIES];
} SH4SoundArchive;

int sh4_sound_archive_open(SH4SoundArchive *bank, SH4SoundRead read,
                           void *ctx, uint32_t file_bytes);
/* Logical offsets preserve cross-entry reads. VirtualAlloc's final page tail
   is zero-filled; RIFF sizes and the directory's opaque field are unmodified. */
int sh4_sound_archive_read(const SH4SoundArchive *bank, SH4SoundRead read,
                           void *ctx, uint32_t offset, void *dst, uint32_t bytes);

/* Caller-owned cache, no allocation and no exported pointers into its slots.
   Not thread-safe: the future bridge must serialize bank reads explicitly. */
#define SH4_SOUND_CACHE_BLOCK 65536u
#define SH4_SOUND_CACHE_SLOTS 8u
typedef struct SH4SoundCache {
    const SH4SoundArchive *bank;
    uint8_t *storage;
    uint32_t block[SH4_SOUND_CACHE_SLOTS], stamp[SH4_SOUND_CACHE_SLOTS];
    uint32_t clock, valid;
} SH4SoundCache;
int sh4_sound_cache_init(SH4SoundCache *cache, const SH4SoundArchive *bank,
                         void *storage, uint32_t storage_bytes);
int sh4_sound_cache_read(SH4SoundCache *cache, SH4SoundRead read, void *ctx,
                         uint32_t offset, void *dst, uint32_t bytes);
