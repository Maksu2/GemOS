#include "cursor.h"
#include "../gfx/primitives.h"
#include "../include/irq.h"
#include "ui_scale.h"
#include <stddef.h>

/* Global Instance. The mouse IRQ handler moves it: access it from task
 * context with interrupts off. */
cursor_state_t cursor = {0, 0};

static gfx_context_t *cursor_ctx = NULL;

/*
 * Standard Arrow Cursor Bitmap (12x19 approx for main shape)
 * X = Black Border
 * . = White Fill
 *   = Transparent
 */
static const char *cursor_bitmap[] = {
    "X           ", "XX          ", "X.X         ",
    "X..X        ", "X...X       ", "X....X      ",
    "X.....X     ", "X......X    ", "X.......X   ",
    "X........X  ", "X.....XXXX  ", "X..X..X     ",
    "X.X X..X    ", "XX  X..X    ", "X    X..X   ",
    "     X..X   ", "      XX    ", NULL};

void cursor_init(gfx_context_t *ctx) {
  cursor_ctx = ctx;
  /* Start in center */
  if (ctx) {
    int x = (int)(ctx->width / ui_scale) / 2;
    int y = (int)(ctx->height / ui_scale) / 2;
    uint32_t flags = irq_save();
    cursor.x = x;
    cursor.y = y;
    irq_restore(flags);
  }
}

void cursor_draw(void) {
  if (!cursor_ctx)
    return;

  /* The mouse driver keeps the position on screen; gfx_put_pixel clips the
   * parts of the arrow that stick out past the screen edge. */

  uint32_t flags = irq_save();
  int cx = cursor.x;
  int cy = cursor.y;
  irq_restore(flags);

  /* Iterate Bitmap */
  for (int y = 0; cursor_bitmap[y] != NULL; y++) {
    const char *row = cursor_bitmap[y];
    for (int x = 0; row[x] != '\0'; x++) {
      char pixel = row[x];
      if (pixel == 'X') {
        /* Black Border */
        gfx_put_pixel(cursor_ctx, cx + x, cy + y, 0x000000); // Black
      } else if (pixel == '.') {
        /* White Fill */
        gfx_put_pixel(cursor_ctx, cx + x, cy + y, 0xFFFFFF); // White
      }
      /* Space is transparent */
    }
  }
}
