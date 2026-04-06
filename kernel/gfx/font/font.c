#include "font.h"
#include "../../../include/string.h"
#include "../../font/font_cache.h"
#include "../../font/scanline.h"
#include "../../font/truetype.h"
#include "../../include/heap.h"
#include "../../ui/ui_scale.h"
#include "../primitives.h"
#include <stddef.h>

static tt_font_t g_font;
static rasterizer_t g_raster;
static uint8_t *g_raster_buf = NULL;
static bool g_font_loaded = false;

/* Extern from aa.c */
extern void aa_blend_pixel(gfx_context_t *ctx, int x, int y, uint32_t color,
                           uint8_t alpha);

void font_load_ttf(uint8_t *data, size_t size) {
  if (tt_load(&g_font, data, size)) {
    g_font_loaded = true;

    // Initialize rasterizer buffer (256x256 max glyph size for now)
    if (!g_raster_buf) {
      g_raster_buf = kalloc(256 * 256);
      if (g_raster_buf) {
        rasterizer_init(&g_raster, 256, 256, g_raster_buf);
        font_cache_init();
      } else {
        g_font_loaded = false; // Mem fail
      }
    }
  }
}

void font_draw_text(gfx_context_t *ctx, int x, int y, const char *text,
                    int size_px, uint32_t color) {
  if (!g_font_loaded || !text || !g_raster_buf)
    return;

  // Scale calculation
  /* Apply UI Scale to font size */
  float combined_scale = (float)(size_px * ui_scale) / g_font.units_per_em;

  // Coordinates
  /* Apply UI Scale to position */
  float pen_x = (float)(x * ui_scale);
  float pen_y = (float)(y * ui_scale);

  // Baseline adjustment
  float ascent_px = g_font.ascender * combined_scale;
  float baseline_y = pen_y + ascent_px;

  while (*text) {
    char c = *text;
    text++;

    if (c == '\n') {
      float line_height =
          (g_font.ascender - g_font.descender + g_font.line_gap) *
          combined_scale;
      pen_y += line_height;
      pen_x = (float)(x * ui_scale);
      baseline_y = pen_y + ascent_px;
      continue;
    }

    uint32_t cp = (unsigned char)c;
    uint16_t gid = tt_get_glyph_index(&g_font, cp);

    if (gid == 0) {
      // Missing glyph logic?
    }

    /* 1. Try Cache */
    glyph_cache_entry_t *entry = font_cache_get(gid, (int)(size_px * ui_scale));
    if (entry) {
      for (int r = 0; r < entry->height; r++) {
        for (int c = 0; c < entry->width; c++) {
          uint8_t a = entry->bitmap[r * entry->width + c];
          if (a > 0) {
            aa_blend_pixel(ctx, (int)(pen_x + entry->offset_x + c),
                           (int)(baseline_y + entry->offset_y + r), color, a);
          }
        }
      }
      pen_x += entry->advance;
      continue;
    }

    /* 2. Rasterize (Miss) */
    tt_glyph_t glyph;
    if (tt_load_glyph(&g_font, gid, &glyph)) {

      int advance = glyph.advance_width * combined_scale;

      if (glyph.point_count > 0) {
        rasterizer_clear(&g_raster);

        float buf_off_x = 32.0f;
        float buf_off_y = 192.0f;

        // Draw with Y-flip (scale_y = -scale)
        rasterizer_draw_glyph(&g_raster, &glyph, combined_scale,
                              -combined_scale, buf_off_x, buf_off_y);

        /* Find bounding box of actual pixels */
        int min_x = 256, max_x = -1, min_y = 256, max_y = -1;

        // Use the conservative bounds from vector as start? No, scan actual
        // buffer for tight bounds Optimization: scan the buffer
        int b_min_x = (int)((glyph.x_min * combined_scale) + buf_off_x) - 1;
        int b_max_x = (int)((glyph.x_max * combined_scale) + buf_off_x) + 2;
        int b_min_y = (int)((glyph.y_max * -combined_scale) + buf_off_y) - 1;
        int b_max_y = (int)((glyph.y_min * -combined_scale) + buf_off_y) + 2;

        /* Clamp */
        if (b_min_x < 0)
          b_min_x = 0;
        if (b_max_x > 255)
          b_max_x = 255;
        if (b_min_y < 0)
          b_min_y = 0;
        if (b_max_y > 255)
          b_max_y = 255;

        for (int by = b_min_y; by <= b_max_y; by++) {
          for (int bx = b_min_x; bx <= b_max_x; bx++) {
            if (g_raster_buf[by * 256 + bx] > 0) {
              if (bx < min_x)
                min_x = bx;
              if (bx > max_x)
                max_x = bx;
              if (by < min_y)
                min_y = by;
              if (by > max_y)
                max_y = by;
            }
          }
        }

        if (max_x >= min_x && max_y >= min_y) {
          int w = max_x - min_x + 1;
          int h = max_y - min_y + 1;
          int off_x = min_x - (int)buf_off_x;
          int off_y = min_y - (int)buf_off_y;

          /* Extract Bitmap */
          uint8_t *bmp = kalloc(w * h);
          if (bmp) {
            for (int r = 0; r < h; r++) {
              // memcpy row
              memcpy(bmp + r * w, g_raster_buf + (min_y + r) * 256 + min_x, w);
            }

            /* Store in Cache */
            font_cache_put(gid, (int)(size_px * ui_scale), bmp, w, h, off_x,
                           off_y, advance);

            /* Draw it now */
            for (int r = 0; r < h; r++) {
              for (int c = 0; c < w; c++) {
                uint8_t a = bmp[r * w + c];
                if (a > 0) {
                  aa_blend_pixel(ctx, (int)(pen_x + off_x + c),
                                 (int)(baseline_y + off_y + r), color, a);
                }
              }
            }
            kfree(bmp); // Cache made a copy? Yes cache_put makes a copy.
          }
        } else {
          // Empty glyph (space?)
          font_cache_put(gid, (int)(size_px * ui_scale), NULL, 0, 0, 0, 0,
                         advance);
        }

      } else {
        // Empty glyph
        font_cache_put(gid, (int)(size_px * ui_scale), NULL, 0, 0, 0, 0,
                       advance);
      }

      pen_x += advance;
      tt_free_glyph(&glyph);
    } else {
      pen_x += (size_px * ui_scale) / 2;
    }
  }
}
