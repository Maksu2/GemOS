#include "../drivers/serial.h"
#include "../kernel/gfx/context.h"
#include "../kernel/gfx/font/font.h"
#include "../kernel/gfx/primitives.h"
#include <stddef.h>
#include <stdint.h>

/*
   Sandbox Verification for Real TTF Engine
   Renders "The quick brown fox" using the new TrueType engine.
*/

extern uint8_t _binary_font_ttf_start[];
extern uint8_t _binary_font_ttf_end[];

extern void font_load_ttf(uint8_t *data, size_t size);

void test_new_font_engine(gfx_context_t *ctx) {
  // 1. Clear background
  gfx_fill_rect(ctx, 0, 0, ctx->width, ctx->height, 0xFF222222); // Dark Grey

  // Load Font
  size_t font_size = (size_t)(_binary_font_ttf_end - _binary_font_ttf_start);

  serial_print("[TEST] Loading Font (Size: ");
  serial_print_dec(font_size);
  serial_print(" bytes)...\n");

  font_load_ttf(_binary_font_ttf_start, font_size);

  uint32_t color = 0xFFFFFFFF; // White

  // 2. Render Samples
  int y = 50;

  // 12px
  serial_print("[TEST] Rendering 12px...\n");
  font_draw_text(ctx, 50, y,
                 "12px: The quick brown fox jumps over the lazy dog", 12,
                 color);
  y += 20;

  // 14px
  serial_print("[TEST] Rendering 14px...\n");
  font_draw_text(ctx, 50, y,
                 "14px: The quick brown fox jumps over the lazy dog", 14,
                 color);
  y += 25;

  // 16px
  serial_print("[TEST] Rendering 16px...\n");
  font_draw_text(ctx, 50, y,
                 "16px: The quick brown fox jumps over the lazy dog", 16,
                 color);
  y += 30;

  // 24px
  serial_print("[TEST] Rendering 24px...\n");
  font_draw_text(ctx, 50, y, "24px: The quick brown fox", 24, color);
  y += 40;

  // 48px (Big test)
  serial_print("[TEST] Rendering 48px...\n");
  font_draw_text(ctx, 50, y, "GemOS AA", 48, color);

  // Grid fit test
  y += 80;
  serial_print("[TEST] Rendering Subpixel...\n");
  font_draw_text(ctx, 50, y, "Subpixel Test (Int Coords for now)", 16,
                 0xFF00FF00);
}
