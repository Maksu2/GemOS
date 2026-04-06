#include "context.h"
#include "../include/string.h" /* For memset if needed */
#include "../ui/ui_scale.h"

void gfx_init_context(gfx_context_t *ctx, uint32_t *fb, uint32_t w, uint32_t h,
                      uint32_t p, uint32_t bpp) {
  ctx->framebuffer = fb;
  ctx->width = w;
  ctx->height = h;
  ctx->pitch = p;
  ctx->bpp = bpp;

  /* Default clip is full screen */
  gfx_reset_clip(ctx);
}

void gfx_set_clip(gfx_context_t *ctx, int x, int y, int w, int h) {
  /* Scale Coordinates */
  int sx = (int)(x * ui_scale);
  int sy = (int)(y * ui_scale);
  int sw = (int)(w * ui_scale);
  int sh = (int)(h * ui_scale);

  rect_t new_clip = {sx, sy, sw, sh};
  rect_t screen_rect = {0, 0, (int)ctx->width, (int)ctx->height};

  /* Clip requested rect to screen bounds */
  rect_intersect(&new_clip, &screen_rect, &ctx->clip_rect);
}

void gfx_reset_clip(gfx_context_t *ctx) {
  ctx->clip_rect.x = 0;
  ctx->clip_rect.y = 0;
  ctx->clip_rect.w = (int)ctx->width;
  ctx->clip_rect.h = (int)ctx->height;
}
