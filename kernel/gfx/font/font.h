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

/* Logical width of the first line of text as font_draw_text draws it */
int font_text_width(const char *text, int size_px);

/* One character centred in a cell cell_w logical pixels wide whose left
 * edge is at x; y is the top of the line, as for font_draw_text. A grid of
 * cells keeps its columns whatever the letters' widths. */
void font_draw_cell(gfx_context_t *ctx, int x, int y, int cell_w, char c,
                    int size_px, uint32_t color);

#endif /* FONT_H */
