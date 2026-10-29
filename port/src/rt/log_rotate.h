#ifndef SH4_LOG_ROTATE_H
#define SH4_LOG_ROTATE_H

#include <errno.h>
#include <stdio.h>

/* Keep the current log and at most 19 previous launches. On any filesystem
 * error the caller must append to the current log instead of truncating it. */
static int sh4_log_rotate(const char *path) {
    char src[1024], dst[1024];
    int oldest_len = snprintf(dst, sizeof dst, "%s.%02d", path, 19);
    if (oldest_len < 0 || oldest_len >= (int)sizeof dst)
        return 0;
    if (remove(dst) != 0 && errno != ENOENT) return 0;
    for (int i = 18; i >= 0; --i) {
        int n = i ? snprintf(src, sizeof src, "%s.%02d", path, i)
                  : snprintf(src, sizeof src, "%s", path);
        if (n < 0 || n >= (int)sizeof src ||
            snprintf(dst, sizeof dst, "%s.%02d", path, i + 1) >= (int)sizeof dst)
            return 0;
        if (rename(src, dst) != 0 && errno != ENOENT) return 0;
    }
    return 1;
}
#endif
