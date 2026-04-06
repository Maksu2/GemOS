#include "visual_test.h"
#include "../drivers/pit.h"
#include "../kernel/gfx/primitives.h"

/*
 * Framebuffer Sanity Test (Phase 3.2.1)
 *
 * This test draws specific patterns to verify pixel addressing
 * and color correctness (RGB vs BGR).
 *
 * 1. Background: Full vertical gradient (Blue to Black).
 * 2. Foreground: Horizontal gradient bar (Red to Black).
 * 3. Grid: 1px white lines to detect skew/stride issues.
 */

static int phase = 0;

void visual_test_init(gfx_context_t *ctx) {
  /* 1. Full Screen Vertical Gradient (Blue) */
  /* Proves Y-addressing and Pitch are correct */
  for (int y = 0; y < (int)ctx->height; y++) {
    uint8_t b = 255 - (y * 255 / ctx->height);
    uint32_t color = b; /* 0x0000BB */
    gfx_fill_rect(ctx, 0, y, ctx->width, 1, color);
  }

  /* 2. Vertical White Lines every 100px */
  /* Proves X-addressing is consistent (no skew) */
  for (int x = 0; x < (int)ctx->width; x += 100) {
    gfx_fill_rect(ctx, x, 0, 1, ctx->height, 0xFFFFFF);
  }
}

void visual_test_update(gfx_context_t *ctx) {
  uint64_t ticks = timer_get_ticks();

  /* Animate a Horizontal Gradient Bar every frame */
  if (ticks % 2 != 0)
    return; /* Limit FPS slightly */
  phase++;

  int bar_y = 100;
  int bar_h = 50;

  /* Clear Bar area */
  /* Actually we don't clear, we re-draw gradient */

  /* Draw Horizontal Gradient (Red) */
  /* Proves 24/32bpp padding is correct */
  for (int x = 0; x < (int)ctx->width; x++) {
    /* Moving gradient */
    uint8_t r = (x + phase) % 255;
    uint32_t color = (r << 16); /* 0xRR0000 */

    /* Draw 1px wide vertical slice of the bar */
    gfx_fill_rect(ctx, x, bar_y, 1, bar_h, color);
  }

  /* Draw a moving white box to check performance/tearing */
  int box_x = (phase * 3) % ctx->width;
  gfx_fill_rect(ctx, box_x, 200, 30, 30, 0xFFFFFF);
}
