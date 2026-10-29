// Standalone Vita storage probe, run with a restore wrapper. Never opens sh4.sav.
// Build with src/rt/staged_file.c; all probe files are in .console-audit/.
#include "staged_file.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define DIR "ux0:data/sh4/.console-audit"
#define PATH DIR "/probe.sav"
int main(void) {
    sceIoMkdir(DIR, 0777);
    FILE *log = fopen(DIR "/result.txt", "w");
    if (!log) return 1;
    const size_t size = 694288;
    unsigned char *data = malloc(size), *readback = malloc(size);
    int ok = data && readback;
    remove(PATH); remove(PATH ".sh4stage");
    for (unsigned pass = 0; ok && pass < 8; ++pass) {
        for (unsigned i = 0; i < size; ++i) data[i] = (unsigned char)(i * 7 + pass);
        errno = 0;
        int written = rt_staged_file_write(PATH, data, size), error = errno;
        fprintf(log, "pass=%u written=%d errno=%d\n", pass, written, error);
        if (!written) { ok = 0; break; }
        FILE *file = fopen(PATH, "rb");
        if (!file) { ok = 0; break; }
        ok = fread(readback, 1, size, file) == size && fgetc(file) == EOF && !memcmp(data, readback, size);
        if (fclose(file)) ok = 0;
        if (!ok) break;
        file = fopen(PATH ".sh4stage", "rb");
        if (file) { fclose(file); ok = 0; }
    }
    fprintf(log, "result=%s\n", ok ? "PASS" : "FAIL");
    fclose(log);
    free(data); free(readback);
    remove(PATH); remove(PATH ".sh4stage");
    sceKernelExitProcess(ok ? 0 : 1);
    return ok ? 0 : 1;
}
