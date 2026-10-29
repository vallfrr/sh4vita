#pragma once
#include <SDL_gamecontroller.h>

typedef struct { unsigned info, evade, ambiguous; } Sh4PadButtons;
enum { SH4_PAD_GAMEPLAY = 0, SH4_PAD_OPTIONS = 1, SH4_PAD_POINTER = 2 };

// Translate one physical sample. Consumed chord partners stay suppressed until
// release so letting go in either order cannot also open the map/cancel a menu.
static inline unsigned sh4_pad_buttons(Sh4PadButtons *state, unsigned raw, int context, int combos) {
    int options = (context & SH4_PAD_OPTIONS) != 0;
    const unsigned info_keys = (1u << SDL_CONTROLLER_BUTTON_LEFTSHOULDER) | (1u << SDL_CONTROLLER_BUTTON_X);
    const unsigned evade_keys = (1u << SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) | (1u << SDL_CONTROLLER_BUTTON_B);
    int info = 0, evade = 0;
    if (!combos) state->info = state->evade = state->ambiguous = 0;
    else {
        if (!(raw & info_keys)) state->info = 0;
        if (!(raw & evade_keys)) state->evade = 0;
        if (!(raw & (info_keys | evade_keys))) state->ambiguous = 0;
        if (context == SH4_PAD_GAMEPLAY) {
            info = (raw & info_keys) == info_keys;
            evade = (raw & evade_keys) == evade_keys;
            if (info) state->info = 1;
            if (evade) state->evade = 1;
            if (info && evade) state->ambiguous = 1;
        }
        if (state->ambiguous) raw &= ~(info_keys | evade_keys);
        else {
            if (state->info) raw &= ~info_keys;
            if (state->evade) {
                raw &= ~(1u << SDL_CONTROLLER_BUTTON_B);
                if (options) raw &= ~(1u << SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
            }
        }
    }
    static const int buttons[12] = {
        SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        -1, -1, SDL_CONTROLLER_BUTTON_START, -1, -1, SDL_CONTROLLER_BUTTON_BACK
    };
    unsigned result = 0;
    for (unsigned i = 0; i < 12; ++i)
        if (buttons[i] >= 0 && (raw & (1u << buttons[i]))) result |= 1u << i;
    if (options) {
        result &= ~((1u << 4) | (1u << 5));
        if (raw & (1u << SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) result |= 1u << 6;
        if (raw & (1u << SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) result |= 1u << 7;
    } else if (context == SH4_PAD_GAMEPLAY && combos && !state->ambiguous) {
        if (info) result |= 1u << 9;
        else if (evade) result |= 1u << 10;
    }
    return result;
}
