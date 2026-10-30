#include "cursor.h"
#include "../gfx/primitives.h"
#include "../include/irq.h"
#include "ui_scale.h"
#include <stddef.h>
#include <string.h>

/* Global Instance. The mouse IRQ handler moves it: access it from task
 * context with interrupts off. */
cursor_state_t cursor = {0, 0};

/* The backbuffer's context. The backbuffer holds the scene without the
 * cursor: a full frame takes the arrow along only for the copy to the
 * screen, so the backbuffer is also what lies under the arrow there. */
static gfx_context_t *cursor_ctx = NULL;

/*
 * Standard Arrow Cursor Bitmap (12x17)
 * X = Black Border
 * . = White Fill
 *   = Transparent
 */
#define CURSOR_W 12
#define CURSOR_H 17
static const char *cursor_bitmap[CURSOR_H + 1] = {
    "X           ", "XX          ", "X.X         ",
    "X..X        ", "X...X       ", "X....X      ",
    "X.....X     ", "X......X    ", "X.......X   ",
    "X........X  ", "X.....XXXX  ", "X..X..X     ",
    "X.X X..X    ", "XX  X..X    ", "X    X..X   ",
    "     X..X   ", "      XX    ", NULL};

/* Where the arrow is on the screen, in physical pixels, cut to the screen
 * (w = 0: not drawn yet) */
static rect_t shown;
/* The scene under the arrow while a full frame carries it (ui_scale 1 or
 * 2, kernel/kernel.c) */
static uint32_t under[CURSOR_W * 2 * CURSOR_H * 2];

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

static void cursor_position(int *cx, int *cy) {
  uint32_t flags = irq_save();
  *cx = cursor.x;
  *cy = cursor.y;
  irq_restore(flags);
}

/* The arrow's rectangle at logical (cx, cy) in physical pixels, cut to the
 * screen */
static rect_t cursor_rect_at(int cx, int cy) {
  rect_t arrow = {cx * ui_scale, cy * ui_scale, CURSOR_W * ui_scale,
                  CURSOR_H * ui_scale};
  rect_t screen = {0, 0, (int)cursor_ctx->width, (int)cursor_ctx->height};
  rect_t out;

  rect_intersect(&arrow, &screen, &out); /* empty: w = 0 */
  return out;
}

/* The arrow at logical (cx, cy), drawn into a screen-sized buffer */
static void paint(uint32_t *framebuffer, int cx, int cy) {
  gfx_context_t ctx = *cursor_ctx;

  /* The mouse driver keeps the position on screen; gfx_put_pixel clips the
   * parts of the arrow that stick out past the screen edge. */
  ctx.framebuffer = framebuffer;
  gfx_set_bound(&ctx, NULL); /* the whole screen, whatever frame is drawn */
  for (int y = 0; cursor_bitmap[y] != NULL; y++) {
    const char *row = cursor_bitmap[y];
    for (int x = 0; row[x] != '\0'; x++) {
      if (row[x] == 'X') {
        gfx_put_pixel(&ctx, cx + x, cy + y, 0x000000); /* black border */
      } else if (row[x] == '.') {
        gfx_put_pixel(&ctx, cx + x, cy + y, 0xFFFFFF); /* white fill */
      }
    }
  }
}

void cursor_frame_begin(void) {
  uint32_t stride;
  rect_t r;
  int cx;
  int cy;

  if (!cursor_ctx)
    return;
  cursor_position(&cx, &cy);
  r = cursor_rect_at(cx, cy);
  if ((uint32_t)(r.w * r.h) > sizeof(under) / sizeof(under[0])) {
    r.w = 0; /* larger than any arrow: not drawn */
  }
  stride = cursor_ctx->pitch / 4U;
  for (int row = 0; row < r.h && r.w > 0; ++row) {
    memcpy(under + row * r.w,
           cursor_ctx->framebuffer + (uint32_t)(r.y + row) * stride +
               (uint32_t)r.x,
           (size_t)r.w * 4U);
  }
  if (r.w > 0) {
    paint(cursor_ctx->framebuffer, cx, cy);
  }
  shown = r;
}

void cursor_frame_end(void) {
  uint32_t stride;

  if (!cursor_ctx)
    return;
  stride = cursor_ctx->pitch / 4U;
  for (int row = 0; row < shown.h && shown.w > 0; ++row) {
    memcpy(cursor_ctx->framebuffer + (uint32_t)(shown.y + row) * stride +
               (uint32_t)shown.x,
           under + row * shown.w, (size_t)shown.w * 4U);
  }
}

void cursor_move_on(uint32_t *screen) {
  int cx;
  int cy;

  if (!cursor_ctx)
    return;
  if (shown.w > 0) {
    /* the old arrow */
    gfx_copy_rect(cursor_ctx, screen, cursor_ctx->framebuffer, shown);
  }
  cursor_position(&cx, &cy);
  paint(screen, cx, cy);
  shown = cursor_rect_at(cx, cy);
}
