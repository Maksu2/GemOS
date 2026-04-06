#ifndef GFX_CONTEXT_H
#define GFX_CONTEXT_H

#include "rect.h"
#include <stdint.h>

typedef struct {
  uint32_t *framebuffer; /* Start of buffer */
  uint32_t width;
  uint32_t height;
  uint32_t pitch; /* Bytes per scanline */
  uint32_t bpp;   /* Bits per pixel */

  rect_t clip_rect; /* Current drawing bounds */
} gfx_context_t;

/* Initialize a context struct from VBE info */
void gfx_init_context(gfx_context_t *ctx, uint32_t *fb, uint32_t w, uint32_t h,
                      uint32_t p, uint32_t bpp);

/* Set the clipping region (intersecting with current screen bounds) */
void gfx_set_clip(gfx_context_t *ctx, int x, int y, int w, int h);

/* Reset clipping to full screen */
void gfx_reset_clip(gfx_context_t *ctx);

#endif /* GFX_CONTEXT_H */
