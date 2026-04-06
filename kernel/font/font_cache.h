#ifndef FONT_CACHE_H
#define FONT_CACHE_H

#include <stdbool.h>
#include <stdint.h>

/* Key for the cache: (glyph_index << 16) | size */
typedef uint32_t cache_key_t;

typedef struct {
  cache_key_t key;
  bool used;

  uint8_t *bitmap; /* 8bpp alpha map */
  int width;
  int height;
  int offset_x;
  int offset_y;
  int advance; /* Advance width (scaled) */
} glyph_cache_entry_t;

/* Initialize the cache */
void font_cache_init(void);

/* Look up a glyph. Returns NULL if not found. */
glyph_cache_entry_t *font_cache_get(uint16_t glyph_index, uint16_t size);

/* Insert a rasterized glyph into the cache.
   Allocates a copy of the bitmap. */
void font_cache_put(uint16_t glyph_index, uint16_t size, const uint8_t *bitmap,
                    int width, int height, int offset_x, int offset_y,
                    int advance);

/* Clear the cache (frees all bitmaps) */
void font_cache_clear(void);

#endif
