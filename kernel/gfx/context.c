#include "context.h"
#include <string.h>
#include "../ui/ui_scale.h"

void gfx_init_context(gfx_context_t *ctx, uint32_t *fb, uint32_t w, uint32_t h,
                      uint32_t p, uint32_t bpp) {
  ctx->framebuffer = fb;
  ctx->width = w;
  ctx->height = h;
  ctx->pitch = p;
  ctx->bpp = bpp;

  /* Default bound and clip are the full screen */
  gfx_set_bound(ctx, NULL);
}

void gfx_set_clip(gfx_context_t *ctx, int x, int y, int w, int h) {
  /* Scale Coordinates */
  int sx = (int)(x * ui_scale);
  int sy = (int)(y * ui_scale);
  int sw = (int)(w * ui_scale);
  int sh = (int)(h * ui_scale);

  rect_t new_clip = {sx, sy, sw, sh};

  /* Clip requested rect to the bound, which lies on the screen */
  rect_intersect(&new_clip, &ctx->bound_rect, &ctx->clip_rect);
}

void gfx_reset_clip(gfx_context_t *ctx) { ctx->clip_rect = ctx->bound_rect; }

void gfx_set_bound(gfx_context_t *ctx, const rect_t *physical) {
  rect_t screen_rect = {0, 0, (int)ctx->width, (int)ctx->height};

  if (physical != NULL) {
    rect_t wanted = *physical;
    rect_intersect(&wanted, &screen_rect, &ctx->bound_rect);
  } else {
    ctx->bound_rect = screen_rect;
  }
  ctx->clip_rect = ctx->bound_rect;
}

void gfx_copy_rect(const gfx_context_t *ctx, uint32_t *dst,
                   const uint32_t *src, rect_t r) {
  uint32_t stride = ctx->pitch / 4U;

  for (int row = 0; row < r.h; ++row) {
    uint32_t at = (uint32_t)(r.y + row) * stride + (uint32_t)r.x;
    memcpy(dst + at, src + at, (size_t)r.w * 4U);
  }
}
