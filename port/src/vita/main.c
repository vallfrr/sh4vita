// PS Vita entry point: vitaGL for display, SDL2 (without video) for audio/input/threads.
//
// Memory: the newlib heap stays small; guest memory (game image + game heap) is requested from the kernel first,
// in decreasing blocks until a contiguous block fits (with vitaGL first, the largest hole depended on the size
// of the eboot); vitaGL then takes 32 MB of RAM, the rest goes to SDL and the threads. Each step logs the free memory.
#include "rt.h"
#include "plat.h"
#include "data_index.h"
#include <vitaGL.h>
#include "qgl.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
#include <psp2/power.h>
#include <psp2/display.h>
#include <psp2/message_dialog.h>
#include <psp2/io/stat.h>
#include "dbgfont.h"
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <errno.h>

int _newlib_heap_size_user = SH4_HOST_HEAP_MB * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;

#define DATA_DIR "ux0:data/sh4"

// Disable the vitaGL boot splash: this overrides the library's invoke_splashscreen()
// (CMake links with --allow-multiple-definition so the first definition, this one, wins).
void invoke_splashscreen(void) {}

SDL_Window *plat_sdl_window(void) { return NULL; }
int plat_width(void) { return 640; }
int plat_height(void) { return 480; }
void plat_show_window(void) {}
int plat_pump_events(void) {
    extern void rt_vita_lifecycle_poll(void);
    rt_vita_lifecycle_poll();
    return 0;
}
// SH4_SHOT=N[,M...]: writes ux0:data/sh4/shot_N.tga (image displayed at present N) to see the rendering without a hardware capture.
void plat_shot_raw(const char *path);
static void take_shot(unsigned n) {
    char path[64];
    snprintf(path, sizeof path, DATA_DIR "/shot_%u.tga", n);
    q_sync("shot");
    plat_shot_raw(path);
}

static unsigned g_swap_n;
void plat_gl_swap(void) {
    rt_text_check();
    { static const char *sh; static int sh_init; if (!sh_init) { sh = getenv("SH4_SHOT"); sh_init = 1; }
      if (sh) for (const char *q = sh; *q;) { unsigned v = (unsigned)strtoul(q, (char **)&q, 10); if (v == g_swap_n + 1) take_shot(v); if (*q == ',') q++; else if (*q) q++; } }
    // Live capture: create ux0:data/sh4/shotnow (FTP) -> shot_<swap>.tga is written within ~1 s, then the trigger is deleted.
    static int poll_flags = -1;
    if (poll_flags < 0) poll_flags = ((!getenv("SH4_LIVE") && !getenv("SH4_CTL")) || getenv("SH4_POLL_DISABLE") ? 1 : 0) | (getenv("SH4_POLL_TRACE") ? 2 : 0);
    if (!(poll_flags & 1) && g_swap_n % 30 == 0) {
        uint64_t begin = (poll_flags & 2) ? SDL_GetPerformanceCounter() : 0;
        FILE *f = fopen(DATA_DIR "/shotnow", "rb");
        if (f) { fclose(f); remove(DATA_DIR "/shotnow"); take_shot(g_swap_n + 1); }
        if (poll_flags & 2) rt_log("[poll-shot] swap=%u us=%llu\n", g_swap_n,
            (unsigned long long)((SDL_GetPerformanceCounter() - begin) * 1000000ull / SDL_GetPerformanceFrequency()));
    }
    // SH4_AUTOSHOT=N: every N presents, one of 16 rotating files auto_<k>.tga (the last 16 images to look at after a bug); the index is logged.
    { static unsigned every = ~0u; if (every == ~0u) { const char *e = getenv("SH4_AUTOSHOT"); every = e ? (unsigned)atoi(e) : 0; }
      if (every && g_swap_n % every == 0) {
          char path[64]; unsigned k = (g_swap_n / every) % 16;
          snprintf(path, sizeof path, DATA_DIR "/auto_%u.tga", k);
          q_sync("shot"); plat_shot_raw(path);
          rt_log("[auto] %u = swap %u\n", k, g_swap_n + 1);
      } }
    static unsigned n;
    if (n++ % 150 == 0) {
        struct mallinfo mi = mallinfo();
        rt_log("[vgl] swap %u: free vram %u KB ram %u KB phycont %u KB budget %u KB; host heap: arena %u KB used %u KB\n", n,
               (unsigned)(vglMemFree(VGL_MEM_VRAM) >> 10), (unsigned)(vglMemFree(VGL_MEM_RAM) >> 10),
               (unsigned)(vglMemFree(VGL_MEM_PHYCONT) >> 10), (unsigned)(vglMemFree(VGL_MEM_BUDGET) >> 10),
               (unsigned)mi.arena >> 10, (unsigned)mi.uordblks >> 10);
    }
    g_swap_n++;
    q_swap();
}
void plat_drawable_size(int *w, int *h) { *w = 960; *h = 544; }

static void log_free(const char *when) {
    SceKernelFreeMemorySizeInfo info = {.size = sizeof info};
    sceKernelGetFreeMemorySize(&info);
    rt_log("[mem] %s: user free %d KB, cdram %d KB, phycont %d KB\n", when, info.size_user >> 10, info.size_cdram >> 10,
           info.size_phycont >> 10);
}

// Shader compiler messages (GLSL -> Cg -> GXP): essential to understand a rejection.
static void shark_log(const char *msg, shark_log_level level, int line) {
    rt_log("[shark %s l.%d] %s\n", level == SHARK_LOG_ERROR ? "ERROR" : level == SHARK_LOG_WARNING ? "warn." : "info", line, msg);
}

static int g_vgl_ready;
void plat_gl_init(void) {
    if (g_vgl_ready) return;
    g_vgl_ready = 1;
    log_free("before vitaGL");
    // Careful: vglInit* returns GL_TRUE only if the requested resolution had to be reduced;
    // GL_FALSE is the normal case (success). Never treat it as an error.
    // The game chains many passes/copies per frame: sceGxm command buffers enlarged (defaults: VDM 128 KB,
    // fragment 512 KB, USSE 16 KB) because an overflow crashes sceGxm (data abort) in the middle of a frame.
    // Scenes per frame and per render target (vitaGL default 1/1): beyond that, sceGxm reuses the target's driver memory and waits for the previous scene
    const char *sc = getenv("SH4_SCENES");
    if (sc) { int sa = 1, sb = 1; sscanf(sc, "%d,%d", &sa, &sb); vglSetupRenderTargetScenesNum((uint8_t)sa, (uint8_t)sb); rt_log("[gl] scenes per target: %d / %d\n", sa, sb); }
    vglSetVDMBufferSize(1024 * 1024);
    vglSetFragmentBufferSize(1024 * 1024);
    // USSE ring holds the per-draw uniforms (up to 96 vec4 = 1.5 KB x ~400 draws/frame): too small = late draws (actors) silently lost
    { const char *us = getenv("SH4_USSE_KB"); vglSetUSSEBufferSize((us ? atoi(us) : 2048) * 1024); }
    // vitaGL leaves "threshold" bytes free and takes the rest: we aim it at SH4_VGL_RAM_MB of RAM.
    SceKernelFreeMemorySizeInfo info = {.size = sizeof info};
    sceKernelGetFreeMemorySize(&info);
    int threshold = (int)info.size_user - (SH4_VGL_RAM_MB << 20);
    GLboolean res_reduced = vglInitExtended(0, 960, 544, threshold, SCE_GXM_MULTISAMPLE_NONE);
    rt_log("[gl] vglInitExtended(RAM threshold %d) -> resolution reduced: %d\n", threshold, res_reduced);
    shark_install_log_cb(shark_log);
    shark_set_warnings_level(SHARK_WARN_MAX);
    // Opt-in compiler experiment. Separate cache namespaces in draw.c prevent
    // an older binary from hiding the selected optimization level in a trial.
    const char *shader_opt = getenv("SH4_SHADER_OPT");
    if (shader_opt && shader_opt[0] >= '1' && shader_opt[0] <= '3' && shader_opt[1] == 0) {
        int level = shader_opt[0] - '0';
        vglSetupRuntimeShaderCompiler((shark_opt)level, GL_TRUE, GL_FALSE, GL_TRUE);
        rt_log("[shader-compiler] optimization O%d, fastmath=1 fastprecision=0 fastint=1\n", level);
    }
    log_free("after vitaGL");
    if (getenv("SH4_NOVSYNC")) vglWaitVblankStart(GL_FALSE);   // measurement: without vertical sync wait
}

void rt_platform_fatal(const char *msg) {
    (void)msg;
    rt_log_flush();
    sceKernelDelayThread(3 * 1000 * 1000);
}

// Readable startup error: text (8x16 font enlarged x2) written straight into the display buffer, before vitaGL.
static void boot_error(const char *const *lines, int n) {
    rt_log("[boot] %s\n", lines[0]);
    SceUID id = sceKernelAllocMemBlock("boot_msg", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 0x240000, NULL);
    void *base = NULL;
    if (id < 0 || sceKernelGetMemBlockBase(id, &base) < 0) return;
    uint32_t *px = base;
    for (int i = 0; i < 960 * 544; i++) px[i] = 0xff301010;
    for (int l = 0; l < n; l++)
        for (int c = 0; lines[l][c] && c < 58; c++) {
            unsigned ch = (unsigned char)lines[l][c];
            if (ch < 32 || ch > 126) continue;
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 16; x++)
                    if (dbg_font[ch - 32][y / 2] & (0x80 >> (x / 2)))
                        px[(48 + l * 40 + y) * 960 + 16 + c * 16 + x] = l == 0 ? 0xff40c0ff : 0xffffffff;
        }
    SceDisplayFrameBuf fb = {.size = sizeof fb, .base = base, .pitch = 960, .pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8, .width = 960, .height = 544};
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceKernelDelayThread(8 * 1000 * 1000);
}
static int file_exists(const char *p) { SceIoStat st; return sceIoGetstat(p, &st) >= 0; }
static void boot_check_index(void) {
    char metadata[2048] = {0};
    FILE *f = fopen(DATA_DIR "/sh4vita.json", "rb");
    int metadata_error = !f && errno != ENOENT;
    if (f) {
        size_t n = fread(metadata, 1, sizeof metadata - 1, f);
        metadata[n] = 0;
        metadata_error = ferror(f);
        fclose(f);
    }
    if (metadata_error) {
        static const char *const m[] = {"GAME METADATA READ FAILED", "", "Could not read sh4vita.json.", "Check the memory card and copy the file again.", "Your saves have not been changed."};
        boot_error(m, 5); sceKernelExitProcess(0);
    }
    int mode = rt_data_index_mode(metadata);
    if (!mode) { rt_log("[data-check] legacy data: no index requested\n"); return; }
    RtDataIndexReport report = {0};
    uint64_t begin = sceKernelGetProcessTimeWide();
    int result = mode < 0 ? -1 : rt_vita_data_index_check(DATA_DIR "/files.idx", DATA_DIR, &report);
    rt_log("[data-check] result=%d files=%u bytes=%llu ms=%.3f problem=%s expected=%llu actual=%llu\n",
           result, report.files, (unsigned long long)report.bytes,
           (sceKernelGetProcessTimeWide() - begin) / 1000.0, report.problem,
           (unsigned long long)report.expected_size, (unsigned long long)report.actual_size);
    if (result == 1) return;
    char detail[100];
    snprintf(detail, sizeof detail, "Expected %llu bytes; found %llu.",
             (unsigned long long)report.expected_size, (unsigned long long)report.actual_size);
    const char *m[] = {"GAME FILE CHECK FAILED", "", report.problem[0] ? report.problem : "files.idx is missing or unsupported.",
                       result == -3 ? detail : "The file inventory could not be validated.", "",
                       "Re-copy files from your prepared sh4 folder.", "Do not remove the save folder.", "See sh4log.txt for details."};
    boot_error(m, 8);
    sceKernelExitProcess(0);
}
static void boot_checks(void) {
    if (!file_exists(DATA_DIR "/SILENT HILL 4.exe") || !file_exists(DATA_DIR "/data")) {
        static const char *const m[] = {"GAME FILES NOT FOUND", "", "Expected in ux0:data/sh4/", "", "Run sh4prep on your PC with your own copy", "of the game, then copy the sh4 folder", "to ux0:data/ on the Vita (see README)."};
        boot_error(m, 7);
        sceKernelExitProcess(0);
    }
    boot_check_index(); // metadata/stat only, before guest allocation and before the engine
    if (!file_exists("ur0:data/libshacccg.suprx") && !file_exists("ur0:/data/external/libshacccg.suprx") && !file_exists("vs0:sys/external/libshacccg.suprx")) {
        static const char *const m[] = {"SHADER COMPILER MISSING", "", "libshacccg.suprx was not found.", "Install it on your own console (ShaRKF00D),", "then restart the game (see README)."};
        boot_error(m, 5);   // we try anyway: the message stays displayed long enough to read it
    }
}

static void *alloc_guest(uint32_t *size) {
    SceKernelFreeMemorySizeInfo info = {.size = sizeof info};
    sceKernelGetFreeMemorySize(&info);
    uint32_t mb = (info.size_user >> 20) - SH4_VGL_RAM_MB - SH4_SPARE_MB;
    if (mb > SH4_GUEST_MB) mb = SH4_GUEST_MB;
    for (; mb >= 120; mb -= 4) {
        SceUID id = sceKernelAllocMemBlock("sh4guest", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, mb << 20, NULL);
        if (id >= 0) {
            void *base = NULL;
            sceKernelGetMemBlockBase(id, &base);
            rt_log("[mem] guest memory: %u MB obtained\n", mb);
            *size = mb << 20;
            return base;
        }
        rt_log("[mem] %u MB refused (%08x)\n", mb, id);
    }
    return NULL;
}

static int watchdog_thread(void *arg) {
    (void)arg;
    for (unsigned i = 1; ; i++) {
        SDL_Delay(5000);
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF);
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_DIMMING);
        rt_log("[wd] alive %u (%.0fs) batterie %d %% %s\n", i, i * 5.0f, scePowerGetBatteryLifePercent(), scePowerIsBatteryCharging() ? "(en charge)" : "");
        if (i % 6 == 0) {
            extern uint64_t gheap_used_snapshot(void);
            rt_log("[memory] guest-used=%llu KB\n", (unsigned long long)(gheap_used_snapshot() >> 10));
        }
        { extern void q_dump_stall(void); q_dump_stall(); }
        { extern void rt_audit_watchdog(void); rt_audit_watchdog(); }
    }
    return 0;
}

// Startup progress using the existing bitmap font, before entering game code.
static GLuint prep_program;
static GLint prep_color;
static void prep_text(const char *text, int x, int y) {
    size_t length = strlen(text);
    float *vertices = malloc(length * 8 * 16 * 12 * sizeof(float));
    if (!vertices) return;
    unsigned count = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned ch = (unsigned char)text[i];
        if (ch < 32 || ch > 126) continue;
        for (int row = 0; row < 16; row++) for (int col = 0; col < 8; col++) {
            if (!(dbg_font[ch - 32][row] & (0x80 >> col))) continue;
            float left = (x + (int)i * 16 + col * 2) / 480.0f - 1.0f;
            float right = left + 2.0f / 480.0f;
            float top = 1.0f - (y + row * 2) / 272.0f;
            float bottom = top - 2.0f / 272.0f;
            float quad[] = {left,top, right,top, left,bottom, left,bottom, right,top, right,bottom};
            memcpy(vertices + count * 2, quad, sizeof quad);
            count += 6;
        }
    }
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glDrawArrays(GL_TRIANGLES, 0, count);
    free(vertices);
}
static void prep_progress(unsigned done, unsigned total) {
    if (!prep_program) {
        static const char *sources[] = {
            "attribute vec2 aV0; void main(){gl_Position=vec4(aV0,0.0,1.0);}",
            "precision mediump float; uniform vec4 color; void main(){gl_FragColor=color;}"
        };
        GLuint shaders[2];
        prep_program = glCreateProgram();
        for (unsigned i = 0; i < 2; i++) {
            shaders[i] = glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
            glShaderSource(shaders[i], 1, &sources[i], NULL);
            glCompileShader(shaders[i]);
            glAttachShader(prep_program, shaders[i]);
        }
        glBindAttribLocation(prep_program, 0, "aV0");
        glLinkProgram(prep_program);
        for (unsigned i = 0; i < 2; i++) glDeleteShader(shaders[i]);
        GLint linked = 0;
        glGetProgramiv(prep_program, GL_LINK_STATUS, &linked);
        rt_log("[prewarm] progress renderer linked=%d\n", linked);
        prep_color = glGetUniformLocation(prep_program, "color");
    }
    glClearColor(0.025f, 0.025f, 0.035f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(100, 230, 760, 18);
    glClearColor(0.17f, 0.17f, 0.19f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(100, 230, total ? done * 760 / total : 0, 18);
    glClearColor(0.3f, 0.65f, 0.48f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(prep_program);
    glEnableVertexAttribArray(0);
    glUniform4f(prep_color, 0.9f, 0.9f, 0.9f, 1.0f);
    prep_text("Preparation du jeu", 336, 170);
    prep_text("Cette operation peut prendre quelques minutes.", 104, 350);
    char label[12];
    snprintf(label, sizeof label, "%u %%", total ? done * 100 / total : 0);
    prep_text(label, 448, 250);
    vglSwapBuffers(GL_FALSE);
    if (done == 10 && getenv("SH4_PREWARM_SHOT")) plat_shot_raw(DATA_DIR "/prewarm.tga");
    glDisableVertexAttribArray(0);
    glUseProgram(0);
    if (done == total) { glDeleteProgram(prep_program); prep_program = 0; }
}

// Keep the parameters alive for the whole dialog, including its close animation.
static SceMsgDialogParam prep_native_param;
static SceMsgDialogSystemMessageParam prep_native_system;
static int prep_native_state;
static void prep_native_frame(void) {
    glClearColor(0.02f, 0.02f, 0.025f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    vglSwapBuffers(GL_TRUE);
}
static void prep_native_progress(unsigned done, unsigned total) {
    if (!prep_native_state && !done) {
        memset(&prep_native_system, 0, sizeof prep_native_system);
        prep_native_system.sysMsgType = SCE_MSG_DIALOG_SYSMSG_TYPE_WAIT;
        sceMsgDialogParamInit(&prep_native_param);
        prep_native_param.mode = SCE_MSG_DIALOG_MODE_SYSTEM_MSG;
        prep_native_param.sysMsgParam = &prep_native_system;
        int result = sceMsgDialogInit(&prep_native_param);
        prep_native_state = result < 0 ? -1 : 1;
        rt_log("[prewarm-native] init=%08x status=%d\n", (unsigned)result, sceMsgDialogGetStatus());
        if (prep_native_state > 0) {
            // Let the system finish opening the window before a long compilation.
            for (unsigned i = 0; i < 60; i++) prep_native_frame();
            if (getenv("SH4_NATIVE_WAIT_SHOT")) plat_shot_raw(DATA_DIR "/prewarm-native.tga");
        }
    }
    if (prep_native_state < 0) { prep_progress(done, total); return; }
    prep_native_frame();
    if (done == total) {
        int result = sceMsgDialogClose();
        for (unsigned i = 0; i < 120 && sceMsgDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING; i++) prep_native_frame();
        int terminated = sceMsgDialogTerm();
        rt_log("[prewarm-native] close=%08x term=%08x status=%d\n", (unsigned)result, (unsigned)terminated, sceMsgDialogGetStatus());
        prep_native_state = 0;
    }
}

// ux0:data/sh4/env.txt: one KEY=VALUE line per variable (SH4_TRACE_FRAME=439, SH4_TRACE_COPY=1...), no recompiling.
// Read before vitaGL (some settings, like SH4_SCENES, are used for GPU initialisation).
static void load_env_txt(void) {
    FILE *f = fopen(DATA_DIR "/env.txt", "rb");
    if (!f) return;
    char line[200];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        eq[1 + strcspn(eq + 1, "\r\n")] = 0;
        setenv(line, eq + 1, 1);
    }
    fclose(f);
}

static void set_test_optimizations(void) {
#ifdef SH4_TEST_OPTIMIZATIONS
    static const struct { const char *name, *value; } options[] = {
        {"SH4_NATIVE_PALETTE", "1"},
        {"SH4_NATIVE_VERTICES", "1"},
        {"SH4_NATIVE_COLLISION", "1"},
        {"SH4_NATIVE_MODEL_UV_CACHE", "2"},
        {"SH4_MODEL_UV_PREFETCH", "2"},
        {"SH4_NATIVE_SEGMENTS", "1"},
        {"SH4_NATIVE_SPHERE_LIST", "1"},
        {"SH4_PREFETCH_REUSE", "1"},
        {"SH4_NATIVE_ADPCM", "1"},
        {"SH4_NATIVE_AUDIO_PACKETS", "1"},
#ifdef SH4_TEST_QUALITY_512
        // Combined qualification build, paired with the isolated block-swizzle
        // vitaGL fix. Keep the normal build's defaults available for comparison.
        {"SH4_TEXMAX", "512"},
        {"SH4_ALPHA32_MAX", "512"},
        {"SH4_HWDXT", "1"},
        {"SH4_PREP_SCALAR", "1"},
        {"SH4_INTERP_SKIP_LARGE", "1"},
        {"SH4_PREFETCH_GUARD", "1"},
        {"SH4_AUDIO_HEAD_CACHE", "1"},
        {"SH4_PREFETCH_SMALL", "1"},
#endif
    };
    for (unsigned i = 0; i < sizeof options / sizeof options[0]; ++i)
        setenv(options[i].name, options[i].value, 0);
    rt_log("[build] experimental tester optimizations active; env.txt may override each option\n");
#endif
}

int main(void) {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0);   // cores 1-2: movie, audio
    log_free("startup");
    boot_checks();
    uint32_t gsize = 0;
    void *gmem = alloc_guest(&gsize);
    if (!gmem) rt_fatal("guest memory: no block of %u MB or less was granted", SH4_GUEST_MB);
    load_env_txt();
    set_test_optimizations();
    rt_cpu_profile_init();
    plat_gl_init();
    // Standalone diagnostic of system-dialog opening/closing; never enabled
    // in a normal player configuration. The backend closes it automatically.
    if (getenv("SH4_MESSAGE_TEST")) plat_message_box("Silent Hill 4", "System message test", 0);
    SDL_strlcpy(g_game_dir, DATA_DIR, sizeof g_game_dir);
    // Language chosen in sh4prep (sh4vita.json: "language": "French").
    FILE *f = fopen(DATA_DIR "/sh4vita.json", "rb");
    if (f) {
        char buf[512] = {0};
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = 0;
        const char *k = strstr(buf, "\"language\"");
        const char *q = k ? strchr(k + 10, '"') : NULL;
        if (q) {
            char lang[16] = {0};
            sscanf(q + 1, "%15[A-Za-z]", lang);
            if (lang[0]) setenv("SH4_LANG", lang, 1);
        }
    }
    // 16:9 rendering (the game widens its field of view) at 640x362, upscaled x1.5 at present: at 960x544 the GPU takes ~21 ms per pass
    // in the forest, too much for two passes (60 fps interpolation) in 33 ms; at 640x362, ~11.7 ms.
    setenv("SH4_RES", "640x362", 0);
    SDL_SetHint(SDL_HINT_THREAD_STACK_SIZE, "1048576");   // video playback threads/timers: guest code + pl_mpeg
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS)) {
        rt_log("SDL_Init : %s\n", SDL_GetError());
        return 1;
    }
    if (getenv("SH4_MOVIE_BENCH")) { void movie_bench_start(const char *, const char *); movie_bench_start(g_game_dir, getenv("SH4_MOVIE_BENCH")); }
    rt_init_mem(gmem, gsize);
    log_free("after guest memory");
    char exe[300];
    SDL_snprintf(exe, sizeof exe, "%s/SILENT HILL 4.exe", g_game_dir);
    int r = rt_load_exe(exe);
    if (r) rt_fatal("cannot load %s (%d): copy the sh4/ folder to ux0:data/", exe, r);
    rt_fill_iat();
    rt_log("game loaded from %s\n", g_game_dir);
    SDL_CreateThread(watchdog_thread, "watchdog", NULL);
    if (getenv("SH4_PREWARM") && !strcmp(getenv("SH4_PREWARM"), "1")) {
        int gfx_prewarm_shader_bank(const char *, void (*)(unsigned, unsigned));
        const char *ui = getenv("SH4_PREWARM_UI");
        void (*progress)(unsigned, unsigned) = prep_native_progress;
        if (ui && !strcmp(ui, "0")) progress = NULL;
        else if (ui && !strcmp(ui, "font")) progress = prep_progress;
        gfx_prewarm_shader_bank(DATA_DIR "/shaders.bin", progress);
    }
    rt_run_entry();
    rt_log_flush();
    sceKernelExitProcess(0);
    return 0;
}

// Direct read of the displayed buffer (vitaGL, no queue): also called by the render thread (SH4_IPSHOT, intermediate frame).
#undef glBindFramebuffer
#undef glReadPixels
#include <zlib.h>
typedef struct { FILE *raw; gzFile gzip; int ok; } ShotWriter;
static void shot_write(ShotWriter *w, const void *data, unsigned size) {
    if (!w->ok) return;
    w->ok = w->gzip ? gzwrite(w->gzip, data, size) == (int)size : fwrite(data, 1, size, w->raw) == size;
}
void plat_shot_raw(const char *path) {
    size_t npath = strlen(path);
    int compress = npath >= 3 && !strcmp(path + npath - 3, ".gz");
    ShotWriter writer = {.ok=1};
    if (compress) writer.gzip = gzopen(path, "wb1");
    else writer.raw = fopen(path, "wb");
    if (!writer.raw && !writer.gzip) { rt_log("[shot-error] cannot open %s\n", path); return; }
    unsigned char hd[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 960 & 255, 960 >> 8, 544 & 255, 544 >> 8, 32, 0x28};
    shot_write(&writer, hd, 18);
    static uint8_t strip[960 * 16 * 4];
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    for (int y = 0; y < 544; y += 16) {
        glReadPixels(0, 543 - y - 15, 960, 16, GL_RGBA, GL_UNSIGNED_BYTE, strip);   // GL : origine en bas
        for (int r = 15; r >= 0; r--) {
            uint8_t *p = strip + r * 960 * 4;
            for (int x = 0; x < 960; x++, p += 4) { uint8_t t = p[0]; p[0] = p[2]; p[2] = t; }   // RGBA -> BGRA
            shot_write(&writer, strip + r * 960 * 4, 960 * 4);
        }
    }
    if (writer.gzip) { if (gzclose(writer.gzip) != Z_OK) writer.ok = 0; }
    else if (fclose(writer.raw)) writer.ok = 0;
    rt_log("[%s] %s\n", writer.ok ? "shot" : "shot-error", path);
}
