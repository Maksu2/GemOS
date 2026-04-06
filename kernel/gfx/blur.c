/**
 * Box Blur - Separable two-pass algorithm
 *
 * Operates directly on the framebuffer in physical coordinates.
 * Uses a sliding window accumulator for O(1) per-pixel cost.
 *
 * Architecture note: This is a rendering subsystem primitive,
 * not a visual effect. It exists to enable correct shadow rendering
 * in the window manager's compositor pipeline.
 */

#include "blur.h"
#include "../include/heap.h"
#include "../ui/ui_scale.h"

void gfx_box_blur(gfx_context_t *ctx, int x, int y, int w, int h, int radius) {
  if (!ctx || !ctx->framebuffer || radius <= 0 || w <= 0 || h <= 0)
    return;

  /* Convert logical → physical coordinates */
  int sx = (int)(x * ui_scale);
  int sy = (int)(y * ui_scale);
  int sw = (int)(w * ui_scale);
  int sh = (int)(h * ui_scale);

  /* Clamp to framebuffer bounds */
  if (sx < 0) {
    sw += sx;
    sx = 0;
  }
  if (sy < 0) {
    sh += sy;
    sy = 0;
  }
  if (sx + sw > (int)ctx->width)
    sw = (int)ctx->width - sx;
  if (sy + sh > (int)ctx->height)
    sh = (int)ctx->height - sy;

  if (sw <= 0 || sh <= 0)
    return;

  int bytes_per_pixel = ctx->bpp / 8;
  int kernel = 2 * radius + 1;

  /* Allocate temp buffer for one row/column of RGB values */
  /* Max dimension we need: max(sw, sh) * 3 bytes */
  int max_dim = sw > sh ? sw : sh;
  uint8_t *tmp = (uint8_t *)kalloc(max_dim * 3);
  if (!tmp)
    return;

  /* === PASS 1: Horizontal blur === */
  for (int row = 0; row < sh; row++) {
    int py = sy + row;
    uint8_t *row_ptr =
        (uint8_t *)ctx->framebuffer + py * ctx->pitch + sx * bytes_per_pixel;

    /* Initialize accumulator with first pixel * radius (left edge extension) */
    int acc_r = 0, acc_g = 0, acc_b = 0;

    /* Pre-fill accumulator: left edge is clamped */
    for (int k = -radius; k <= radius; k++) {
      int col = k < 0 ? 0 : (k >= sw ? sw - 1 : k);
      uint8_t *p = row_ptr + col * bytes_per_pixel;
      acc_b += p[0];
      acc_g += p[1];
      acc_r += p[2];
    }

    /* Store first pixel */
    tmp[0] = (uint8_t)(acc_b / kernel);
    tmp[1] = (uint8_t)(acc_g / kernel);
    tmp[2] = (uint8_t)(acc_r / kernel);

    /* Slide window across the row */
    for (int col = 1; col < sw; col++) {
      /* Add new right pixel */
      int add_col = col + radius;
      if (add_col >= sw)
        add_col = sw - 1;
      uint8_t *add_p = row_ptr + add_col * bytes_per_pixel;
      acc_b += add_p[0];
      acc_g += add_p[1];
      acc_r += add_p[2];

      /* Remove old left pixel */
      int rem_col = col - radius - 1;
      if (rem_col < 0)
        rem_col = 0;
      uint8_t *rem_p = row_ptr + rem_col * bytes_per_pixel;
      acc_b -= rem_p[0];
      acc_g -= rem_p[1];
      acc_r -= rem_p[2];

      tmp[col * 3 + 0] = (uint8_t)(acc_b / kernel);
      tmp[col * 3 + 1] = (uint8_t)(acc_g / kernel);
      tmp[col * 3 + 2] = (uint8_t)(acc_r / kernel);
    }

    /* Write back to framebuffer */
    for (int col = 0; col < sw; col++) {
      uint8_t *p = row_ptr + col * bytes_per_pixel;
      p[0] = tmp[col * 3 + 0];
      p[1] = tmp[col * 3 + 1];
      p[2] = tmp[col * 3 + 2];
    }
  }

  /* === PASS 2: Vertical blur === */
  int pitch = ctx->pitch;

  for (int col = 0; col < sw; col++) {
    int px_offset = (sx + col) * bytes_per_pixel;

    /* Pre-fill accumulator */
    int acc_r = 0, acc_g = 0, acc_b = 0;

    for (int k = -radius; k <= radius; k++) {
      int row = k < 0 ? 0 : (k >= sh ? sh - 1 : k);
      uint8_t *p = (uint8_t *)ctx->framebuffer + (sy + row) * pitch + px_offset;
      acc_b += p[0];
      acc_g += p[1];
      acc_r += p[2];
    }

    tmp[0] = (uint8_t)(acc_b / kernel);
    tmp[1] = (uint8_t)(acc_g / kernel);
    tmp[2] = (uint8_t)(acc_r / kernel);

    for (int row = 1; row < sh; row++) {
      int add_row = row + radius;
      if (add_row >= sh)
        add_row = sh - 1;
      uint8_t *add_p =
          (uint8_t *)ctx->framebuffer + (sy + add_row) * pitch + px_offset;
      acc_b += add_p[0];
      acc_g += add_p[1];
      acc_r += add_p[2];

      int rem_row = row - radius - 1;
      if (rem_row < 0)
        rem_row = 0;
      uint8_t *rem_p =
          (uint8_t *)ctx->framebuffer + (sy + rem_row) * pitch + px_offset;
      acc_b -= rem_p[0];
      acc_g -= rem_p[1];
      acc_r -= rem_p[2];

      tmp[row * 3 + 0] = (uint8_t)(acc_b / kernel);
      tmp[row * 3 + 1] = (uint8_t)(acc_g / kernel);
      tmp[row * 3 + 2] = (uint8_t)(acc_r / kernel);
    }

    /* Write back */
    for (int row = 0; row < sh; row++) {
      uint8_t *p = (uint8_t *)ctx->framebuffer + (sy + row) * pitch + px_offset;
      p[0] = tmp[row * 3 + 0];
      p[1] = tmp[row * 3 + 1];
      p[2] = tmp[row * 3 + 2];
    }
  }

  kfree(tmp);
}
