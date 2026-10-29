#include "font.h"
#include <string.h>
#include "../../font/font_cache.h"
#include "../../font/scanline.h"
#include "../../font/truetype.h"
#include "../../font/font_mem.h"
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
      g_raster_buf = font_alloc(256 * 256);
      if (g_raster_buf) {
        rasterizer_init(&g_raster, 256, 256, g_raster_buf);
        font_cache_init();
      } else {
        g_font_loaded = false; // Mem fail
      }
    }
  }
}

/* The glyph of cp at size_phys pixels, from the cache, or rasterized and
 * put there; NULL if the font has no outline for it or memory ran out. The
 * entry stays valid until the next glyph is put in the cache. */
static glyph_cache_entry_t *font_glyph(uint32_t cp, int size_phys,
                                       float scale) {
  uint16_t gid = tt_get_glyph_index(&g_font, cp);
  glyph_cache_entry_t *entry = font_cache_get(gid, (uint16_t)size_phys);
  tt_glyph_t glyph;

  if (entry) {
    return entry;
  }
  if (!tt_load_glyph(&g_font, gid, &glyph)) {
    return NULL;
  }

  int advance = glyph.advance_width * scale;

  if (glyph.point_count > 0) {
    rasterizer_clear(&g_raster);

    float buf_off_x = 32.0f;
    float buf_off_y = 192.0f;

    // Draw with Y-flip (scale_y = -scale)
    rasterizer_draw_glyph(&g_raster, &glyph, scale, -scale, buf_off_x,
                          buf_off_y);

    /* Find bounding box of actual pixels */
    int min_x = 256, max_x = -1, min_y = 256, max_y = -1;

    // Scan the raster buffer, limited to the outline's bounds, for the
    // tight bitmap bounds
    int b_min_x = (int)((glyph.x_min * scale) + buf_off_x) - 1;
    int b_max_x = (int)((glyph.x_max * scale) + buf_off_x) + 2;
    int b_min_y = (int)((glyph.y_max * -scale) + buf_off_y) - 1;
    int b_max_y = (int)((glyph.y_min * -scale) + buf_off_y) + 2;

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
      uint8_t *bmp = font_alloc(w * h);
      if (bmp) {
        for (int r = 0; r < h; r++) {
          // memcpy row
          memcpy(bmp + r * w, g_raster_buf + (min_y + r) * 256 + min_x, w);
        }

        /* Store in Cache: it keeps its own copy */
        font_cache_put(gid, (uint16_t)size_phys, bmp, w, h, off_x, off_y,
                       advance);
        font_free(bmp);
      }
    } else {
      // Empty glyph (space?)
      font_cache_put(gid, (uint16_t)size_phys, NULL, 0, 0, 0, 0, advance);
    }
  } else {
    // Empty glyph
    font_cache_put(gid, (uint16_t)size_phys, NULL, 0, 0, 0, 0, advance);
  }

  tt_free_glyph(&glyph);
  return font_cache_get(gid, (uint16_t)size_phys);
}

/* The glyph's bitmap with its top left corner at physical (x, y) */
static void font_blit(gfx_context_t *ctx, const glyph_cache_entry_t *entry,
                      int x, int y, uint32_t color) {
  if (!entry->bitmap) {
    return;
  }
  for (int r = 0; r < entry->height; r++) {
    for (int c = 0; c < entry->width; c++) {
      uint8_t a = entry->bitmap[r * entry->width + c];
      if (a > 0) {
        aa_blend_pixel(ctx, x + c, y + r, color, a);
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
  int size_phys = size_px * ui_scale;
  float combined_scale = (float)size_phys / g_font.units_per_em;

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

    glyph_cache_entry_t *entry =
        font_glyph((unsigned char)c, size_phys, combined_scale);
    if (entry) {
      font_blit(ctx, entry, (int)(pen_x + entry->offset_x),
                (int)(baseline_y + entry->offset_y), color);
      pen_x += entry->advance;
    } else {
      pen_x += (float)size_phys / 2.0f;
    }
  }
}

int font_text_width(const char *text, int size_px) {
  if (!g_font_loaded || !text || !g_raster_buf)
    return 0;

  int size_phys = size_px * ui_scale;
  float combined_scale = (float)size_phys / g_font.units_per_em;
  float width = 0.0f;

  /* the pen as font_draw_text moves it */
  for (; *text && *text != '\n'; text++) {
    glyph_cache_entry_t *entry =
        font_glyph((unsigned char)*text, size_phys, combined_scale);
    width += entry ? (float)entry->advance : (float)size_phys / 2.0f;
  }
  return (int)width / ui_scale;
}

void font_draw_cell(gfx_context_t *ctx, int x, int y, int cell_w, char c,
                    int size_px, uint32_t color) {
  if (!g_font_loaded || !g_raster_buf)
    return;

  int size_phys = size_px * ui_scale;
  float combined_scale = (float)size_phys / g_font.units_per_em;
  glyph_cache_entry_t *entry =
      font_glyph((unsigned char)c, size_phys, combined_scale);

  if (!entry || !entry->bitmap) {
    return;
  }
  /* centred in physical pixels: at ui_scale 2 a logical coordinate could
   * not reach an odd pixel */
  int left = x * ui_scale + (cell_w * ui_scale - entry->width) / 2;
  int baseline = (int)((float)(y * ui_scale) + g_font.ascender * combined_scale);

  font_blit(ctx, entry, left, baseline + entry->offset_y, color);
}
