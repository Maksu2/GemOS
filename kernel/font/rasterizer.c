#include "font.h"
#include <stdbool.h>

extern void aa_blend_pixel(gfx_context_t *ctx, int x, int y, uint32_t color,
                           uint8_t alpha);

/*
   Point in Polygon Test (Ray Casting)
   Returns true if (tx, ty) is inside the glyph contours.
*/
static bool is_inside(const glyph_t *g, float tx, float ty) {
  bool inside = false;
  // Removed unused k

  // Iterate Contours
  int start_idx = 0;
  for (int i = 0; i < g->point_count; i++) {
    font_point_t p1 = g->points[i];
    font_point_t p2;

    if (p1.end_contour) {
      p2 = g->points[start_idx];
      start_idx = i + 1;
    } else {
      if (i + 1 < g->point_count) {
        p2 = g->points[i + 1];
      } else {
        // Implicit close (safety)
        p2 = g->points[start_idx];
      }
    }

    // Ray cast to right (x > tx)
    // Check edge (p1, p2)
    float x1 = p1.x, y1 = p1.y;
    float x2 = p2.x, y2 = p2.y;

    // Check if edge crosses horizontal line at ty
    if (((y1 > ty) != (y2 > ty)) &&
        (tx < (x2 - x1) * (ty - y1) / (y2 - y1) + x1)) {
      inside = !inside;
    }
  }
  return inside;
}

/*
   Render Glyph with 4x4 Supersampling
*/
static void render_glyph_aa(gfx_context_t *ctx, float x_origin, float y_origin,
                            const glyph_t *g, float size, uint32_t color) {
  if (!g || g->point_count < 3)
    return;

  // Calculate Pixel Bounding Box
  // Normalized glyph is 0..100
  // Scale factor
  float scale = size / 100.0f;

  // Bounding Box in Pixels
  int min_px = (int)(x_origin);
  int max_px = (int)(x_origin + 100 * scale) + 1;
  int min_py = (int)(y_origin); // Baseline is 80, so top is y_origin
  int max_py = (int)(y_origin + 100 * scale) + 1;

  // Clip to screen
  if (min_px < 0)
    min_px = 0;
  if (min_py < 0)
    min_py = 0;
  // Cast to uint32_t for comparison, safe because confirmed >= 0
  if ((uint32_t)max_px >= ctx->width)
    max_px = (int)ctx->width;
  if ((uint32_t)max_py >= ctx->height)
    max_py = (int)ctx->height;

  // Iterate Pixels
  for (int py = min_py; py < max_py; py++) {
    for (int px = min_px; px < max_px; px++) {

      // Supersample 4x4
      int coverage = 0;
      for (int sy = 0; sy < 4; sy++) {
        for (int sx = 0; sx < 4; sx++) {
          // Sample center
          float sub_x = px + (sx / 4.0f) + 0.125f;
          float sub_y = py + (sy / 4.0f) + 0.125f;

          // Transform back to Glyph Space (0..100)
          // P_screen = origin + P_glyph * scale
          // P_glyph = (P_screen - origin) / scale

          float gx = (sub_x - x_origin) / scale;
          float gy = (sub_y - y_origin) / scale;

          if (is_inside(g, gx, gy)) {
            coverage++;
          }
        }
      }

      if (coverage > 0) {
        // Alpha 0..255
        // coverage 0..16
        uint8_t alpha = (coverage * 255) / 16;
        aa_blend_pixel(ctx, px, py, color, alpha);
      }
    }
  }
}

void font_render_string_aa(gfx_context_t *ctx, float x, float y,
                           const char *text, float size, uint32_t color) {
  if (!text)
    return;

  float pen_x = x;
  float pen_y = y;
  float scale = size / 100.0f;

  while (*text) {
    char c = *text;
    if (c == ' ') {
      pen_x += 40 * scale; // Space advance
    } else {
      const glyph_t *g = font_get_glyph_sys(c);
      if (g) {
        render_glyph_aa(ctx, pen_x, pen_y, g, size, color);
        pen_x += g->advance * scale;
      }
    }
    text++;
  }
}

void font_sys_init(void) {
  // Nothing to init for now
}
