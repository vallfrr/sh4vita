#pragma once
#include "d3d8.h"

enum { UI_CROSS, UI_CIRCLE, UI_SQUARE, UI_TRIANGLE, UI_L, UI_R,
       UI_START, UI_SELECT, UI_LEFT_STICK, UI_RIGHT_STICK, UI_DPAD, UI_VITA };
int console_ui_begin(void); // scoped D3D state; 960 x 544 design coordinates
void console_ui_sprite(int id, float x, float y, float w, float h);
void console_ui_sprite_color(int id, float x, float y, float w, float h, uint32_t color);
void console_ui_fill(float x, float y, float w, float h, uint32_t color);
void console_ui_line(float x1, float y1, float x2, float y2, uint32_t color);
void console_ui_end(void);
void console_ui_release(void); // only after the last queued use, when help closes
void console_ui_finish_frame(void); // restore game target before Present
void console_ui_font_scale(float *x, float *y);
void console_ui_glyph(Texture *texture, const float pos[4], const float uv[4], uint32_t top, uint32_t bottom);
