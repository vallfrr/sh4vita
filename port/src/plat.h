// Couche plateforme (PC : src/pc/, Vita : src/vita/).
#pragma once
#include <SDL.h>

void plat_show_window(void);
// Returns the Win32 button ID, or zero if the dialogue could not be shown.
int plat_message_box(const char *title, const char *text, unsigned type);
int plat_pump_events(void);   // 1 if the user asked to quit
int plat_width(void);
int plat_height(void);
SDL_Window *plat_sdl_window(void);
