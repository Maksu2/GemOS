#ifndef KERNEL_FONT_H
#define KERNEL_FONT_H

#include "../gfx/context.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * New Font Subsystem (Phase 4.0)
 * Features: Float positioning, Grayscale AA, Segregated data.
 */

// Glyph Data Structures (Internal-ish, but exposed for rasterizer)
typedef struct {
  int x, y;
  bool end_contour;
} font_point_t;

typedef struct {
  const font_point_t *points;
  int point_count;
  int advance; // 0-100 grid
} glyph_t;

// API
void font_sys_init(void);
const glyph_t *font_get_glyph_sys(char c);

/*
 * Render string with Grayscale Anti-Aliasing (4x4 Supersampling)
 * x, y: Float coordinates for subpixel positioning.
 * size: Font size in pixels (float).
 */
void font_render_string_aa(gfx_context_t *ctx, float x, float y,
                           const char *text, float size, uint32_t color);

#endif
