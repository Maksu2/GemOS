#ifndef GFX_PRIMITIVES_H
#define GFX_PRIMITIVES_H

#include "context.h"
#include <stdint.h>

/* Colors (0xRRGGBB) */
#define COLOR_BLACK 0x000000
#define COLOR_WHITE 0xFFFFFF
#define COLOR_RED 0xFF0000
#define COLOR_GREEN 0x00FF00
#define COLOR_BLUE 0x0000FF

/* Draw a single pixel with clipping */
void gfx_put_pixel(gfx_context_t *ctx, int x, int y, uint32_t color);

/* Draw a line (Bresenham) */
void gfx_draw_line(gfx_context_t *ctx, int x0, int y0, int x1, int y1,
                   uint32_t color);

/* Fill a rectangle with color (clipped) */
void gfx_fill_rect(gfx_context_t *ctx, int x, int y, int w, int h,
                   uint32_t color);

/* Draw a rectangle outline (clipped) */
void gfx_draw_rect(gfx_context_t *ctx, int x, int y, int w, int h,
                   uint32_t color);

/* Fill a circle (clipped) */
void gfx_fill_circle(gfx_context_t *ctx, int cx, int cy, int radius,
                     uint32_t color);

/* Fill a rectangle with alpha blending (0=transparent, 255=opaque) */
void gfx_fill_rect_alpha(gfx_context_t *ctx, int x, int y, int w, int h,
                         uint32_t color, uint8_t alpha);

/* Fill a rectangle with a vertical gradient (top_color → bottom_color) */
void gfx_gradient_rect_v(gfx_context_t *ctx, int x, int y, int w, int h,
                         uint32_t top_color, uint32_t bottom_color);

/* Text functions moved to gfx/font/font.h */

/* Clear the entire context (clipped to clip_rect) */
void gfx_clear(gfx_context_t *ctx, uint32_t color);

/* ========================================================================= */
/* Icon / Bitmap Support                                                     */
/* ========================================================================= */

/* Icon structure - 32x32 RGBA bitmap */
typedef struct {
  int width;
  int height;
  const uint32_t *data; /* RGBA pixels (0xAARRGGBB) */
} icon_t;

/* Blit an RGBA icon with alpha blending (logical coordinates) */
void gfx_blit_icon(gfx_context_t *ctx, int x, int y, const icon_t *icon);

#endif /* GFX_PRIMITIVES_H */
