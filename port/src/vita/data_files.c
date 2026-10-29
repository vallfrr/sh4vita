// FAT directory enumeration supplies metadata for all siblings in one pass.
// Avoid a fresh path lookup for every one of the hundreds of data/*.bin files.
#include "rt.h"
#include "data_index.h"
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <stdlib.h>

typedef struct { char name[256]; uint64_t bytes; } IndexFile;
typedef struct {
    const char *root;
    char parent[448];
    IndexFile *files;
    unsigned count, capacity, directories, entries, peak;
    int loaded;
} IndexDirectory;
static int compare_name(const char *a, const char *b) {
    for (;;) {
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y || !x) return (int)x - (int)y;
    }
}
static int compare_file(const void *a, const void *b) {
    return compare_name(((const IndexFile *)a)->name, ((const IndexFile *)b)->name);
}
static void clear_directory(IndexDirectory *cache) {
    free(cache->files); cache->files = NULL;
    cache->count = cache->capacity = 0; cache->loaded = 0;
}
static int load_directory(IndexDirectory *cache, const char *parent) {
    clear_directory(cache);
    char path[600];
    snprintf(path, sizeof path, "%s/%s", cache->root, parent);
    SceUID fd = sceIoDopen(path);
    if (fd < 0) return 0;
    ++cache->directories;
    int result;
    SceIoDirent entry;
    memset(&entry, 0, sizeof entry);
    while ((result = sceIoDread(fd, &entry)) > 0) {
        entry.d_name[255] = 0;
        if (!SCE_S_ISREG(entry.d_stat.st_mode)) continue;
        if (cache->count == 8192) { result = -1; break; }
        if (cache->count == cache->capacity) {
            unsigned capacity = cache->capacity ? cache->capacity * 2 : 64;
            IndexFile *files = realloc(cache->files, capacity * sizeof *files);
            if (!files) { result = -1; break; }
            cache->files = files; cache->capacity = capacity;
            if (capacity > cache->peak) cache->peak = capacity;
        }
        IndexFile *file = &cache->files[cache->count++];
        snprintf(file->name, sizeof file->name, "%s", entry.d_name);
        file->bytes = (uint64_t)entry.d_stat.st_size;
        ++cache->entries;
    }
    int closed = sceIoDclose(fd);
    if (result < 0 || closed < 0) { clear_directory(cache); return 0; }
    if (cache->count > 1) qsort(cache->files, cache->count, sizeof *cache->files, compare_file);
    snprintf(cache->parent, sizeof cache->parent, "%s", parent);
    cache->loaded = 1;
    return 1;
}
static int index_stat(void *user, const char *relative, uint64_t *bytes) {
    IndexDirectory *cache = user;
    const char *name = strrchr(relative, '/');
    if (!name) {
        // The sole indexed root file is the original executable. Do not scan
        // the root containing saves, logs and writable caches.
        char path[600]; snprintf(path, sizeof path, "%s/%s", cache->root, relative);
        SceIoStat st;
        if (sceIoGetstat(path, &st) < 0) return -1;
        if (!SCE_S_ISREG(st.st_mode)) return 0;
        *bytes = (uint64_t)st.st_size; return 1;
    }
    char parent[448];
    size_t n = (size_t)(name++ - relative);
    if (n >= sizeof parent) return -1;
    memcpy(parent, relative, n); parent[n] = 0;
    if ((!cache->loaded || compare_name(cache->parent, parent)) && !load_directory(cache, parent))
        return -1;
    unsigned lo = 0, hi = cache->count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        int order = compare_name(name, cache->files[mid].name);
        if (!order) { *bytes = cache->files[mid].bytes; return 1; }
        if (order < 0) hi = mid; else lo = mid + 1;
    }
    return 0;
}
int rt_vita_data_index_check(const char *path, const char *root, RtDataIndexReport *report) {
    IndexDirectory cache = {.root = root};
    int result = rt_data_index_check(path, index_stat, &cache, report);
    clear_directory(&cache); // always before guest memory allocation
    rt_log("[data-check] backend=directory-cache directories=%u entries=%u peak=%u KB\n",
           cache.directories, cache.entries, (unsigned)(cache.peak * sizeof(IndexFile) >> 10));
    return result;
}
