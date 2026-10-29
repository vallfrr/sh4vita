#pragma once
#include <SDL_gamecontroller.h>

// One physical sample per rendered game frame, shared by axes, POV and actions.
// Device objects own their cache; scripted inputs are applied afterwards.
typedef struct {
    unsigned frame, buttons;
    int valid;
    Sint16 axes[4];
} Sh4PadSample;

static inline const Sh4PadSample *sh4_pad_sample(Sh4PadSample *sample,
                                                SDL_GameController *pad, unsigned frame) {
    if (!sample->valid || sample->frame != frame) {
        sample->valid = 1;
        sample->frame = frame;
        sample->buttons = 0;
        for (int i = 0; i < 4; ++i)
            sample->axes[i] = SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)i);
        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i)
            if (SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)i))
                sample->buttons |= 1u << i;
    }
    return sample;
}
