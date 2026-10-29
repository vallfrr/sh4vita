#include "rt.h"
#include "plat.h"
#include "input_gate.h"
#include "win32/win32.h"

static unsigned failure_serial;
static uint32_t failure_code;
unsigned rt_save_failure_serial(void) { return failure_serial; }
uint32_t rt_save_error_code(void) { return failure_code; }
void rt_save_failed(CPU *c, uint32_t error) {
    failure_code = error;
    ++failure_serial;
    w32_set_last_error(c, error);
}

extern void F_00408960_orig(CPU *c);
void nat_savedispatch(CPU *c) {
    unsigned before = failure_serial;
    F_00408960_orig(c);
    if (!c->eax && failure_serial == before) {
        HOSTFN;
        uint32_t error = R32(c->fs_base + 0x34);
        rt_save_failed(c, error ? error : 29);
    }
}

void rt_save_report_error(unsigned language, uint32_t error) {
    static const char *const generic[6] = {
        "Could not save. Please check the memory card and try again.",
        "Could not save. Please check the memory card and try again.",
        "Impossible d'enregistrer. V\xe9rifiez la carte et r\xe9""essayez.",
        "Speichern fehlgeschlagen. Bitte Speicherkarte pr\xfc""fen und erneut versuchen.",
        "Impossibile salvare. Controllare la scheda e riprovare.",
        "Error al guardar. Comprueba la tarjeta y vuelve a intentarlo."
    };
    static const char *const full[6] = {
        "Not enough free space on the memory card. Please free some space and try again.",
        "Not enough free space on the memory card. Please free some space and try again.",
        "Pas assez d'espace libre sur la carte. Lib\xe9rez de l'espace et r\xe9""essayez.",
        "Nicht genug freier Speicher auf der Karte. Bitte Speicher freigeben und erneut versuchen.",
        "Spazio libero insufficiente sulla scheda. Liberare spazio e riprovare.",
        "No hay espacio libre suficiente en la tarjeta. Libera espacio y vuelve a intentarlo."
    };
    if (language > 5) language = 1;
    rt_log("[save-ui] failed error=%u language=%u\n", error, language);
    plat_message_box("Silent Hill 4", error == 112 ? full[language] : generic[language], 0x10);
    rt_input_neutralize();
}
