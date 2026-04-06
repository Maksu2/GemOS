#ifndef SCANLINE_H
#define SCANLINE_H

#include "truetype.h"

#define MAX_EDGES 8192

typedef struct {
  float x0, y0;
  float x1, y1;
  float min_y; /* For sorting */
} edge_t;

typedef struct {
  int width;
  int height;
  uint8_t *buffer; /* Alpha buffer 8bpp */

  /* Edge List */
  edge_t *edges;
  int edge_count;
  int max_edges;
} rasterizer_t;

/* Initialize rasterizer with target buffer */
void rasterizer_init(rasterizer_t *r, int width, int height, uint8_t *buffer);

/* Clear buffer and reset edges */
void rasterizer_clear(rasterizer_t *r);

/* Rasterize a TrueType glyph into the buffer */
/* Supports separate X/Y scaling (e.g. scale_y = -scale_x for flip) */
void rasterizer_draw_glyph(rasterizer_t *r, tt_glyph_t *glyph, float scale_x,
                           float scale_y, float offset_x, float offset_y);

#endif
