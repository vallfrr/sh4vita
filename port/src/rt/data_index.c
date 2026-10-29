#include "data_index.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#define FNV_OFFSET UINT64_C(0xcbf29ce484222325)
#define FNV_PRIME UINT64_C(0x100000001b3)
#define MAX_TOTAL (UINT64_C(1) << 40)

int rt_data_index_mode(const char *metadata) {
    const char *p = metadata;
    while ((p = strstr(p, "\"file_index\"")) != NULL) {
        p += 12;
        while (isspace((unsigned char)*p)) ++p;
        if (*p != ':') continue;
        ++p;
        while (isspace((unsigned char)*p)) ++p;
        return !strncmp(p, "\"files.idx\"", 11) ? 1 : -1;
    }
    return 0;
}

static int number(const char *p, unsigned base, uint64_t *value) {
    if (!*p) return 0;
    uint64_t n = 0;
    for (; *p; ++p) {
        unsigned d = *p >= '0' && *p <= '9' ? (unsigned)(*p - '0') :
            base == 16 && *p >= 'a' && *p <= 'f' ? (unsigned)(*p - 'a' + 10) :
            base == 16 && *p >= 'A' && *p <= 'F' ? (unsigned)(*p - 'A' + 10) : base;
        if (d >= base || n > (UINT64_MAX - d) / base) return 0;
        n = n * base + d;
    }
    *value = n; return 1;
}
static uint64_t checksum(uint64_t hash, const char *p, size_t n) {
    while (n--) hash = (hash ^ (unsigned char)*p++) * FNV_PRIME;
    return hash;
}
static int safe_path(const char *p) {
    size_t n = strlen(p);
    if (!n || n > 440) return 0;
    if (strcmp(p, "SILENT HILL 4.exe") && strncmp(p, "data/", 5) &&
        strncmp(p, "demo/", 5) && strncmp(p, "movie/", 6) && strncmp(p, "sound/", 6)) return 0;
    const char *part = p;
    for (;;) {
        unsigned char ch = (unsigned char)*p;
        if (!ch || ch == '/') {
            size_t size = (size_t)(p - part);
            if (!size || (size == 1 && *part == '.') ||
                (size == 2 && part[0] == '.' && part[1] == '.')) return 0;
            if (!ch) break;
            part = p + 1;
        } else if (ch < 32 || ch == 127 || ch == ':' || ch == '\\') return 0;
        ++p;
    }
    return 1;
}
static int finish(FILE *file, RtDataIndexReport *report, int code, const char *problem) {
    snprintf(report->problem, sizeof report->problem, "%s", problem);
    if (file) fclose(file);
    return code;
}
int rt_data_index_check(const char *path, RtDataIndexStat stat_file, void *user,
                        RtDataIndexReport *report) {
    memset(report, 0, sizeof *report);
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? 0 : finish(NULL, report, -4, "files.idx");
    char line[512];
    if (!fgets(line, sizeof line, file) || strcmp(line, "SH4FILES1\n"))
        return finish(file, report, -1, "files.idx header");
    uint64_t wanted_hash = 0;
    unsigned wanted_files = 0;
    uint64_t wanted_bytes = 0;
    // Verify the complete inventory before consulting paths on the filesystem.
    // No entry may target saves, private settings, caches or a parent directory.
    for (unsigned pass = 0; pass < 2; ++pass) {
        uint64_t hash = FNV_OFFSET, total = 0;
        unsigned count = 0;
        int has_exe = 0, footer = 0;
        while (fgets(line, sizeof line, file)) {
            size_t length = strlen(line);
            if (!length || line[length - 1] != '\n')
                return finish(file, report, -1, "files.idx line");
            uint64_t next_hash = checksum(hash, line, length);
            line[length - 1] = 0;
            if (!strncmp(line, "END\t", 4)) {
                char *a = line + 4, *b = strchr(a, '\t'), *c;
                if (!b || !(c = strchr(b + 1, '\t')))
                    return finish(file, report, -1, "files.idx footer");
                *b++ = 0; *c++ = 0;
                uint64_t files, bytes, sum;
                if (!number(a, 10, &files) || !number(b, 10, &bytes) ||
                    strlen(c) != 16 || !number(c, 16, &sum) || !has_exe ||
                    files != count || bytes != total || sum != hash ||
                    fgetc(file) != EOF || ferror(file))
                    return finish(file, report, -1, "files.idx checksum/count");
                if (pass && (files != wanted_files || bytes != wanted_bytes || sum != wanted_hash))
                    return finish(file, report, -4, "files.idx changed during validation");
                wanted_files = count; wanted_bytes = total; wanted_hash = hash;
                footer = 1; break;
            }
            char *name = strchr(line, '\t');
            if (!name) return finish(file, report, -1, "files.idx record");
            *name++ = 0;
            uint64_t bytes;
            if (!number(line, 10, &bytes) || !safe_path(name) || ++count > 8192 ||
                bytes > MAX_TOTAL || total > MAX_TOTAL - bytes)
                return finish(file, report, -1, "files.idx record");
            has_exe |= !strcmp(name, "SILENT HILL 4.exe");
            hash = next_hash; total += bytes;
            if (pass && stat_file) {
                uint64_t actual = 0;
                int status = stat_file(user, name, &actual);
                report->expected_size = bytes; report->actual_size = actual;
                if (status < 0) return finish(file, report, -4, name);
                if (!status) return finish(file, report, -2, name);
                if (actual != bytes) return finish(file, report, -3, name);
            }
        }
        if (!footer) return finish(file, report, ferror(file) ? -4 : -1, "files.idx incomplete");
        if (!pass) {
            if (fseek(file, 10, SEEK_SET)) return finish(file, report, -4, "files.idx seek");
        }
    }
    report->files = wanted_files; report->bytes = wanted_bytes;
    if (fclose(file)) return finish(NULL, report, -4, "files.idx close");
    return 1;
}
