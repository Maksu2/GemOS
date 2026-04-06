#ifndef FONT_H
#define FONT_H

#include "../context.h"
#include <stdint.h>

/*
   Vector Font API
   No more font_t structs or fallbacks. We have ONE system font.
*/

void font_load_ttf(uint8_t *data, size_t size);

void font_draw_text(gfx_context_t *ctx, int x, int y, const char *text,
                    int size_px, uint32_t color);

#endif /* FONT_H */
