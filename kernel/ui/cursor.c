#include "cursor.h"
#include "../gfx/primitives.h"
#include "ui_scale.h"
#include <stddef.h>

/* Global Instance */
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
    cursor.x = (int)(ctx->width / ui_scale) / 2;
    cursor.y = (int)(ctx->height / ui_scale) / 2;
  }
}

void cursor_draw(void) {
  if (!cursor_ctx)
    return;

  /* Clamp Logic handled by Driver usually, but safe to clamp drawing too?
     Driver clamps to 0..W-1.
     Drawing needs to handle clipping if cursor is near edge.
     gfx_put_pixel handles clipping!
  */

  int cx = cursor.x;
  int cy = cursor.y;

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
