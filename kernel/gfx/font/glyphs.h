#ifndef GLYPHS_H
#define GLYPHS_H

#include <stdbool.h>

/*
 * Vector Glyph Definition (Solid Polygons)
 * Coordinate System: 100x100 grid
 * (0,0) is Top-Left of the em-square.
 * (100,100) is Bottom-Right.
 */

typedef struct {
  int x, y;
  bool end_contour; /* True if this point closes the current shape (contour) */
} font_point_t;

typedef struct {
  const font_point_t *points;
  int point_count;
  int advance; // Horizontal advance in 100-unit grid
} glyph_t;

/* Retrieve glyph for a character */
const glyph_t *font_get_glyph(char c);

#endif /* GLYPHS_H */
