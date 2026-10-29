// Console options extensions. Guest calling conventions are preserved by each wrapper.
#include "rt.h"
#include "staged_file.h"
#include <stdlib.h>
#include <errno.h>

#define OPT 0x1091ad0u
#define MENU 0x1091a20u
#define CAM_ROW (0x654d88u + 3 * 0x1c)
#define CAM_LABEL 0x7000u
#define CAM_HELP 0x7001u

void rt_options_language_default(void) {
    const char *language = getenv("SH4_LANG");
    if (!language || !*language) return;
    static const char *const names[] = { "English", "French", "German", "Italian", "Spanish" };
    for (unsigned i = 0; i < sizeof names / sizeof *names; ++i) {
        if (strcmp(language, names[i])) continue;
        uint8_t *const mb = g_mem;
        // 0x524470 copies this template only when no options save is loaded.
        // Do not write the live options: saved and later menu choices take priority.
        W8(0x5c5af4u + 0x11u, i + 1);
        rt_log("[language] initial game default %s (%u)\n", language, i + 1);
        return;
    }
}

static int camera_saved = 3, camera_edit = 3, camera_loaded, camera_open;
static int options_save_failed;
static int camera_save_error;
static int camera_label;

static void camera_path(char *path, size_t size, const char *suffix) {
    snprintf(path, size, "%s/camera.cfg%s", g_game_dir, suffix);
}

static void camera_load(void) {
    if (camera_loaded) return;
    camera_loaded = 1;
    char path[600]; camera_path(path, sizeof path, "");
    FILE *f = fopen(path, "r");
    int n;
    if (f) {
        if (fscanf(f, "%d", &n) == 1 && n >= 0 && n <= 10) camera_saved = n;
        fclose(f);
    }
    camera_edit = camera_saved;
    rt_log("[camera] loaded sensitivity %d/10\n", camera_saved);
}

float rt_camera_scale(void) {
    camera_load();
    return 0.4f + 0.2f * (float)(camera_open ? camera_edit : camera_saved);
}

static int camera_save(void) {
    if (camera_edit == camera_saved) return 1;
    char path[600], value[16];
    camera_path(path, sizeof path, "");
    int length = snprintf(value, sizeof value, "%d\n", camera_edit);
    int ok = length > 0 && (size_t)length < sizeof value &&
        rt_staged_file_write(path, value, (size_t)length);
    camera_save_error = ok ? 0 : errno;
    if (ok) {
        camera_saved = camera_edit;
        rt_log("[camera] saved sensitivity %d/10\n", camera_saved);
    } else rt_log("[camera] saving sensitivity failed\n");
    return ok;
}

extern void F_00523e40_orig(CPU *c);
void nat_optbegin(CPU *c) {
    camera_load();
    camera_edit = camera_saved; camera_open = 1;
    options_save_failed = 0;
    F_00523e40_orig(c);
    rt_log("[camera] options begin %d/10\n", camera_edit);
}

// 0x526810 returns 1 after completing the save dialog; +0x114 flags No/discard.
extern void F_00526810_orig(CPU *c);
void nat_optsave(CPU *c) {
    HOSTFN;
    unsigned before = rt_save_failure_serial();
    F_00526810_orig(c);
    if (rt_save_failure_serial() != before && !options_save_failed) {
        options_save_failed = 1;
        rt_save_report_error(R8(OPT + 0x11), rt_save_error_code());
    }
    if (c->eax == 1 && !R32(MENU + 0x114) && !options_save_failed && !camera_save()) {
        options_save_failed = 1;
        rt_save_report_error(R8(OPT + 0x11), camera_save_error == ENOSPC ? 112 : 29);
    }
}

void rt_camera_end(void) {
    camera_open = 0; camera_edit = camera_saved;
}

// Same keyboard/pad repeat and mouse handling as 0x524c00, without its BGM side effects.
void nat_camslider(CPU *c) {
    HOSTFN;
    uint32_t bits = rt_guest_call(c, 0x4f85a0, 1, 0);
    int right = rt_guest_call(c, 0x4f84a0, 2, 0, 0x11) != 0;
    int left = R32(0x1092214) && rt_guest_call(c, 0x405490, 0);
    right |= R32(0x1092218) && rt_guest_call(c, 0x405490, 0);
    left |= (bits & 4) != 0; right |= (bits & 8) != 0;
    int n = camera_edit;
    if (left) { if (n > 0) n--; }
    else if (right && n < 10) n++;
    if (n != camera_edit) {
        camera_edit = n;
        rt_guest_call(c, 0x56cc40, 1, 0x9c45);
        rt_log("[camera] preview sensitivity %d/10\n", n);
    }
    STDRET(0, 0);
}
void nat_camvalue(CPU *c) { STDRET(0, camera_edit); }
void nat_camhelp(CPU *c) { STDRET(0, CAM_HELP); }

// The stock renderer recognizes volume gauges by text id 0x25/0x26, not by nvals.
// Temporarily select that path while the text hook supplies our own label.
extern void F_005254f0_orig(CPU *c);
void nat_optrow(CPU *c) {
    HOSTFN;
    if (ARG(1) != CAM_ROW) { F_005254f0_orig(c); return; }
    camera_label = 1; W32(CAM_ROW + 0x14, 0x25);
    F_005254f0_orig(c);
    W32(CAM_ROW + 0x14, CAM_LABEL); camera_label = 0;
}

// Whole-struct comparison for the exit prompt; row highlighting keeps the original
// comparisons with remapped indices (including the resolution-change side effect).
void nat_optanychanged(CPU *c) {
    HOSTFN;
    int changed = memcmp(mb + OPT, mb + OPT + 0x24, 0x24) != 0;
    STDRET(0, changed || camera_edit != camera_saved);
}
extern void F_00523aa0_orig(CPU *c);
void nat_optchanged(CPU *c) {
    static const unsigned char map[3][7] = {
        {1, 2, 3, 4, 5, 6, 6}, {0, 1, 5, 2, 6, 6, 6}, {0, 1, 2, 3, 4, 5, 7}
    };
    unsigned page = c->edx, row = c->eax;
    if (page == 1 && row == 3) { STDRET(0, camera_edit != camera_saved); }
    if (page < 3 && row < 7) c->eax = map[page][row];
    F_00523aa0_orig(c);
}

// European message strings start with 0x8000 (byte glyphs), end with 0xffff.
// Latin letters/digits/punctuation map to 0x100-ASCII; space and accented letters differ.
static uint32_t camera_string(unsigned lang, int help) {
    static uint32_t strings[6][2];
    static const char *const labels[6] = {
        "Camera sensitivity", "Camera sensitivity", "Sensibilité de la caméra",
        "Kameraempfindlichkeit", "Sensibilità telecamera", "Sensibilidad de cámara"
    };
    static const char *const hints[6] = {
        "Adjust the right stick camera speed.", "Adjust the right stick camera speed.",
        "Règle la vitesse de la caméra au stick droit.",
        "Kameratempo mit dem rechten Stick einstellen.",
        "Regola la velocità della telecamera.", "Ajusta la velocidad de la cámara."
    };
    if (lang > 5) lang = 1;
    if (!strings[lang][help]) {
        const unsigned char *s = (const unsigned char *)(help ? hints[lang] : labels[lang]);
        uint32_t p = gheap_alloc((uint32_t)strlen((const char *)s) + 4), at = p;
        if (!p) rt_fatal("guest memory exhausted while creating options text");
        HOSTFN;
        W16(at, 0x8000); at += 2;
        while (*s) {
            unsigned ch = *s++, glyph;
            if (ch == 0xc3 && *s) {
                ch = *s++;
                glyph = ch == 0xa9 ? 0x17 : ch == 0xa8 ? 0x18 : ch == 0xa0 ? 0x20 : ch == 0xa1 ? 0x1f : 0x9b;
            } else glyph = ch == ' ' ? 0 : 0x100 - ch;
            W8(at++, glyph);
        }
        W16(at, 0xffff);
        strings[lang][help] = p;
    }
    return strings[lang][help];
}
extern void F_0050c440_orig(CPU *c);
void nat_opttext(CPU *c) {
    HOSTFN;
    unsigned id = ARG(2);
    if (ARG(1) == 0 && (id == 0x7002 || id == 0x7003)) {
        extern uint32_t rt_controls_string(unsigned language, int help);
        STDRET(0, rt_controls_string(R8(OPT + 0x11), (int)id));
    }
    if (ARG(1) == 0 && (id == CAM_LABEL || id == CAM_HELP || (camera_label && id == 0x25))) {
        uint32_t p = camera_string(R8(OPT + 0x11), id == CAM_HELP);
        STDRET(0, p);
    }
    F_0050c440_orig(c);
}
