#pragma once
#include <stdint.h>

// Callback: 1 regular file, 0 missing/not regular, -1 I/O failure.
typedef int (*RtDataIndexStat)(void *user, const char *relative, uint64_t *bytes);
typedef struct {
    unsigned files;
    uint64_t bytes, expected_size, actual_size;
    char problem[448];
} RtDataIndexReport;
// 0 absent (legacy data), 1 valid, -1 malformed, -2 missing file,
// -3 wrong size, -4 I/O error. Never changes a game file or a save.
int rt_data_index_check(const char *index_path, RtDataIndexStat stat_file,
                        void *user, RtDataIndexReport *report);
// Recognises the canonical optional field emitted by sh4prep. Not a general
// JSON validator: 0 legacy/absent, 1 files.idx, -1 unsupported declared index.
int rt_data_index_mode(const char *metadata);
int rt_vita_data_index_check(const char *index_path, const char *root, RtDataIndexReport *report);
