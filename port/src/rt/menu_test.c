// Deterministic entry into a real title-menu destination for UI qualification.
// Unlike a raw state poke, the title runs its normal fade/dispatch afterwards.
#include "rt.h"
#include <stdlib.h>

extern unsigned g_frame;
extern void F_00528a50_orig(CPU *c);
void nat_titlechoice(CPU *c) {
    // Terminate the shared title-row table before the original code creates
    // its navigation list. All four remaining actions retain their indices.
    static int console_patched;
    if (!console_patched) {
        const char *value=getenv("SH4_CONSOLE_TITLE");
#ifdef __vita__
        int console=value? !strcmp(value,"1"):1;
#else
        int console=value && !strcmp(value,"1");
#endif
        if(console) {
            HOSTFN;
            memset(mb+0x655178,0,16); // was {48 (Quit),7,0,0}; now end of table
        }
        console_patched=1;
    }
    F_00528a50_orig(c);
    static int initialized, enabled, done;
    static unsigned frame, choice;
    if (!initialized) {
        initialized = 1;
        const char *value = getenv("SH4_TITLE_SELECT");
        char tail;
        // Continue/Load availability depends on actual narrative slots. Do not
        // bypass their disabled menu rows merely because a settings save exists.
        enabled = value && sscanf(value, "%u:%u%c", &frame, &choice, &tail) == 2 &&
                  (choice == 0 || choice == 3);
    }
    if (enabled && !done && g_frame >= frame && c->eax == 0xffffffffu) {
        done = 1;
        c->eax = choice;
        rt_log("[menu-test] title choice=%u frame=%u\n", choice, g_frame);
    }
}
