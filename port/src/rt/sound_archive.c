#include "sound_archive.h"
#include <string.h>
#include <limits.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}

int sh4_sound_archive_open(SH4SoundArchive *bank, SH4SoundRead read,
                           void *ctx, uint32_t file_bytes) {
    uint8_t header[8], directory[SH4_SOUND_ENTRIES*12];
    if (!bank || !read || file_bytes < 8+sizeof directory || file_bytes > UINT32_MAX-4095u) return 0;
    if (!read(ctx,0,header,8) || memcmp(header,"SDPA",4) || le32(header+4)!=SH4_SOUND_ENTRIES) return 0;
    if (!read(ctx,8,directory,sizeof directory)) return 0;
    uint32_t end=8+sizeof directory;
    /* Validate before changing the destination, including a truncated source. */
    for (unsigned i=0;i<SH4_SOUND_ENTRIES;i++) {
        const uint8_t *p=directory+12*i;
        uint32_t offset=le32(p), bytes=le32(p+4);
        if (offset!=end || bytes>file_bytes-offset) return 0;
        end=offset+bytes;
    }
    if (end!=file_bytes) return 0;
    bank->file_bytes=file_bytes;
    for (unsigned i=0;i<SH4_SOUND_ENTRIES;i++) {
        const uint8_t *p=directory+12*i;
        bank->entries[i]=(SH4SoundEntry){le32(p),le32(p+4),le32(p+8)};
    }
    return 1;
}

int sh4_sound_archive_read(const SH4SoundArchive *bank, SH4SoundRead read,
                           void *ctx, uint32_t offset, void *dst, uint32_t bytes) {
    if (!bank || !read || (!dst && bytes) || bank->file_bytes > UINT32_MAX-4095u) return 0;
    uint32_t padded=(bank->file_bytes+4095u)&~4095u;
    if (offset>padded || bytes>padded-offset) return 0;
    uint32_t available=offset<bank->file_bytes ? bank->file_bytes-offset : 0;
    uint32_t from_file=bytes<available ? bytes : available;
    if (from_file && !read(ctx,offset,dst,from_file)) return 0;
    if (bytes>from_file) memset((uint8_t *)dst+from_file,0,bytes-from_file);
    return 1;
}
