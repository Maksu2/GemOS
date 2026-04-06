#ifndef GFX_BLUR_H
#define GFX_BLUR_H

#include "context.h"

/**
 * Apply a box blur to a rectangular region of the framebuffer.
 *
 * Uses a two-pass separable algorithm (horizontal then vertical)
 * for O(n) per-pixel cost regardless of radius.
 *
 * Coordinates are LOGICAL (pre-scale). The blur operates on physical
 * pixels after scaling.
 *
 * @param ctx    Graphics context (framebuffer)
 * @param x, y   Top-left corner (logical)
 * @param w, h   Size (logical)
 * @param radius  Blur radius in physical pixels (typical: 6-10)
 */
void gfx_box_blur(gfx_context_t *ctx, int x, int y, int w, int h, int radius);

#endif /* GFX_BLUR_H */
