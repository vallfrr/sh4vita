#include "staged_file.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int rt_staged_file_write(const char *path, const void *data, size_t size) {
    char stage[640];
    if (!path || (!data && size)) { errno = EINVAL; return 0; }
    int length = snprintf(stage, sizeof stage, "%s.sh4stage", path);
    if (length < 0 || (size_t)length >= sizeof stage) { errno = ENAMETOOLONG; return 0; }
    FILE *file = fopen(stage, "wb");
    if (!file) return 0;
    int error = 0;
    errno = 0;
    if (size && fwrite(data, 1, size, file) != size) error = errno ? errno : EIO;
    if (!error) {
        errno = 0;
        if (fflush(file)) error = errno ? errno : EIO;
    }
    errno = 0;
    if (fclose(file) && !error) error = errno ? errno : EIO;
    if (!error) {
#ifdef _WIN32
        // CRT rename cannot replace on Windows. Never delete the good save
        // first: a failed replacement must leave it available to the loader.
        if (!MoveFileExA(stage, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            error = EIO;
#else
        errno = 0;
        if (rename(stage, path)) error = errno ? errno : EIO;
#endif
    }
    if (error) { remove(stage); errno = error; return 0; }
    return 1;
}
