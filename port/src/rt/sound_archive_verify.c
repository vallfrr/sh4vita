/* Diagnostic shadow reader: compare streamed bytes to the resident GOG bank.
   Never replaces game data or changes the allocation policy. Main CPU only. */
#include "rt.h"
#include "sound_archive.h"
#include <stdio.h>
#include <stdlib.h>

static SH4SoundArchive bank;
static SH4SoundCache cache;
static FILE *source;
static uint8_t *scratch;
static unsigned checked,reads;
static int initialized,failed;
static uint32_t resident_base;
int g_sound_transfer_on;
int g_sound_read_headers;

static void disable_reader(void) {
    failed=1;__atomic_store_n(&g_sound_transfer_on,0,__ATOMIC_RELAXED);
    __atomic_store_n(&g_sound_read_headers,0,__ATOMIC_RELAXED);
    if (source) { fclose(source);source=NULL; }
    free(cache.storage);cache.storage=NULL;
    free(scratch);scratch=NULL;
}

static int read_file(void *ctx,uint32_t offset,void *dst,uint32_t n) {
    FILE *f=ctx;reads++;
    return !fseek(f,(long)offset,SEEK_SET) && fread(dst,1,n,f)==n;
}

void rt_sound_archive_verify(CPU *c,uint32_t obj,uint32_t id) {
    if (c!=g_main_cpu || failed) return;
    uint8_t *const mb=g_mem;
    if (obj>g_mem_size-0x1100u || id>=SH4_SOUND_ENTRIES) return;
    if (!initialized) {
        initialized=1;
        char path[600];snprintf(path,sizeof path,"%s/sound/sound.pac",g_game_dir);
        source=fopen(path,"rb");
        if (!source || fseek(source,0,SEEK_END)) goto failure;
        long length=ftell(source);
        if (length<=0 || (uint64_t)length>UINT32_MAX || !sh4_sound_archive_open(&bank,read_file,source,(uint32_t)length)) goto failure;
        void *storage=malloc(SH4_SOUND_CACHE_BLOCK*SH4_SOUND_CACHE_SLOTS);
        if (!storage) goto failure;
        scratch=malloc(SH4_SOUND_CACHE_BLOCK);
        if (!scratch) { free(storage);goto failure; }
        if (!sh4_sound_cache_init(&cache,&bank,storage,SH4_SOUND_CACHE_BLOCK*SH4_SOUND_CACHE_SLOTS)) {free(storage);goto failure;}
    }
    SH4SoundEntry *e=&bank.entries[id];uint32_t row=obj+12u*id,base=R32(obj+0x10ecu);
    uint32_t padded=(bank.file_bytes+4095u)&~4095u;
    if (base>g_mem_size || padded>g_mem_size-base ||
        R32(row)!=e->offset || R32(row+4)!=e->bytes || R32(row+8)!=e->opaque) goto failure;
    const char *transfer=getenv("SH4_SOUND_ARCHIVE_TRANSFER");
    int deliver=transfer && !strcmp(transfer,"1");
    resident_base=base;
    __atomic_store_n(&g_sound_transfer_on,deliver,__ATOMIC_RELAXED);
    const char *headers=getenv("SH4_SOUND_ARCHIVE_HEADERS");
    __atomic_store_n(&g_sound_read_headers,deliver && headers && !strcmp(headers,"1"),__ATOMIC_RELAXED);
    /* Real delivery checks exactly the copied bytes below; do not scan the
       entire requested entry a second time in the performance experiment. */
    if (deliver && !getenv("SH4_SOUND_ARCHIVE_VERIFY")) return;
    for (uint32_t done=0;done<e->bytes;) {
        uint32_t n=e->bytes-done;
        if (n>SH4_SOUND_CACHE_BLOCK) n=SH4_SOUND_CACHE_BLOCK;
        if (!sh4_sound_cache_read(&cache,read_file,source,e->offset+done,scratch,n) ||
            memcmp(scratch,mb+base+e->offset+done,n)) goto failure;
        done+=n;
    }
    ++checked;
    if (checked<=20 || checked%100==0)
        rt_log("[sound-verify] checked=%u id=%u source_reads=%u failed=0 cache_KiB=512\n",checked,id,reads);
    return;
failure:
    rt_log("[sound-verify] FAILED checked=%u id=%u source_reads=%u; original bank remains active\n",checked,id,reads);
    disable_reader();
}

/* Experimental delivery through the bounded reader. Keep the original bank
   allocated for fallback until every header reader/callback has been ported. */
int rt_sound_archive_transfer(CPU *c,uint32_t dst,uint32_t src,uint32_t bytes) {
    if (c!=g_main_cpu || !__atomic_load_n(&g_sound_transfer_on,__ATOMIC_RELAXED) || failed || !bytes || !resident_base) return 0;
    uint32_t padded=(bank.file_bytes+4095u)&~4095u;
    if (src<resident_base || src-resident_base>padded || bytes>padded-(src-resident_base) ||
        dst>g_mem_size || bytes>g_mem_size-dst ||
        ((uint64_t)dst<(uint64_t)src+bytes && (uint64_t)src<(uint64_t)dst+bytes)) return 0;
    for (uint32_t done=0;done<bytes;) {
        uint32_t n=bytes-done;if(n>SH4_SOUND_CACHE_BLOCK)n=SH4_SOUND_CACHE_BLOCK;
        if (!sh4_sound_cache_read(&cache,read_file,source,src-resident_base+done,scratch,n) ||
            memcmp(scratch,g_mem+src+done,n)) {
            rt_log("[sound-transfer] FAILED src=%08x bytes=%u; resident fallback\n",src,bytes);
            disable_reader();
            return 0; // Caller overwrites any partial destination with the original.
        }
        memcpy(g_mem+dst+done,scratch,n);done+=n;
    }
    static unsigned transfers;
    if (++transfers<=10 || transfers%100==0)
        rt_log("[sound-transfer] delivered=%u bytes=%u source_reads=%u failed=0\n",transfers,bytes,reads);
    return 1;
}

int rt_sound_archive_scalar(CPU *c,uint32_t src,void *out,uint32_t bytes) {
    if (c!=g_main_cpu || failed || !resident_base || (bytes!=1 && bytes!=2 && bytes!=4)) return 0;
    uint32_t padded=(bank.file_bytes+4095u)&~4095u;
    if (src<resident_base || src-resident_base>padded || bytes>padded-(src-resident_base)) return 0;
    if (!sh4_sound_cache_read(&cache,read_file,source,src-resident_base,out,bytes) ||
        memcmp(out,g_mem+src,bytes)) {
        rt_log("[sound-header] FAILED src=%08x bytes=%u; resident fallback\n",src,bytes);
        disable_reader();return 0;
    }
    static unsigned headers;
    if (++headers<=10 || headers%100==0)
        rt_log("[sound-header] delivered=%u source_reads=%u failed=0\n",headers,reads);
    return 1;
}
