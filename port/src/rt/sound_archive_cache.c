#include "sound_archive.h"
#include <string.h>
#include <limits.h>

int sh4_sound_cache_init(SH4SoundCache *cache, const SH4SoundArchive *bank,
                         void *storage, uint32_t storage_bytes) {
    if (!cache || !bank || !storage || storage_bytes < SH4_SOUND_CACHE_BLOCK*SH4_SOUND_CACHE_SLOTS ||
        bank->file_bytes > UINT32_MAX-4095u) return 0;
    memset(cache,0,sizeof *cache);
    cache->bank=bank;cache->storage=storage;
    return 1;
}

int sh4_sound_cache_read(SH4SoundCache *cache, SH4SoundRead read, void *ctx,
                         uint32_t offset, void *dst, uint32_t bytes) {
    if (!cache || !cache->bank || !cache->storage || !read || (!dst && bytes)) return 0;
    uint32_t length=cache->bank->file_bytes;
    if (length > UINT32_MAX-4095u) return 0;
    uint32_t padded=(length+4095u)&~4095u;
    if (offset>padded || bytes>padded-offset) return 0;
    uint8_t *out=dst;
    while (bytes) {
        uint32_t block=offset/SH4_SOUND_CACHE_BLOCK, inside=offset%SH4_SOUND_CACHE_BLOCK;
        unsigned slot=SH4_SOUND_CACHE_SLOTS;
        for (unsigned i=0;i<SH4_SOUND_CACHE_SLOTS;i++)
            if ((cache->valid&(1u<<i)) && cache->block[i]==block) { slot=i;break; }
        if (slot==SH4_SOUND_CACHE_SLOTS) {
            uint32_t oldest=0;
            slot=0;
            for (unsigned i=0;i<SH4_SOUND_CACHE_SLOTS;i++) {
                if (!(cache->valid&(1u<<i))) { slot=i;break; }
                uint32_t age=cache->clock-cache->stamp[i];
                if (age>=oldest) { oldest=age;slot=i; }
            }
            uint32_t at=block*SH4_SOUND_CACHE_BLOCK;
            uint32_t n=padded-at;
            if (n>SH4_SOUND_CACHE_BLOCK) n=SH4_SOUND_CACHE_BLOCK;
            /* A partial failed read must never become a cache hit. */
            cache->valid&=~(1u<<slot);
            if (!sh4_sound_archive_read(cache->bank,read,ctx,at,
                    cache->storage+slot*SH4_SOUND_CACHE_BLOCK,n)) return 0;
            cache->block[slot]=block;cache->valid|=1u<<slot;
        }
        cache->stamp[slot]=++cache->clock;
        uint32_t n=SH4_SOUND_CACHE_BLOCK-inside;
        if (n>bytes) n=bytes;
        memcpy(out,cache->storage+slot*SH4_SOUND_CACHE_BLOCK+inside,n);
        out+=n;offset+=n;bytes-=n;
    }
    return 1;
}
