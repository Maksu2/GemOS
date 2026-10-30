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

  rect_t clip_rect;  /* Current drawing bounds */
  rect_t bound_rect; /* Never drawn outside: the part of the frame being
                      * drawn (physical pixels, the screen by default) */
} gfx_context_t;

/* Initialize a context struct from VBE info */
void gfx_init_context(gfx_context_t *ctx, uint32_t *fb, uint32_t w, uint32_t h,
                      uint32_t p, uint32_t bpp);

/* Set the clipping region (intersecting with the bound rectangle) */
void gfx_set_clip(gfx_context_t *ctx, int x, int y, int w, int h);

/* Reset clipping to the bound rectangle */
void gfx_reset_clip(gfx_context_t *ctx);

/* Draw only inside a rectangle of physical pixels from now on, NULL for
 * the whole screen; resets the clip to it */
void gfx_set_bound(gfx_context_t *ctx, const rect_t *physical);

/* Rows of a rectangle of physical pixels from one buffer of the context's
 * size to another */
void gfx_copy_rect(const gfx_context_t *ctx, uint32_t *dst,
                   const uint32_t *src, rect_t r);

#endif /* GFX_CONTEXT_H */
