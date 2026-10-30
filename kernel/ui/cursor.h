#ifndef UI_CURSOR_H
#define UI_CURSOR_H

#include "../gfx/context.h"
#include <stdint.h>

/* Global Cursor State - Single Source of Truth */
typedef struct {
  int x;
  int y;
} cursor_state_t;

/* The global cursor instance */
extern cursor_state_t cursor;

/* Initialize cursor system */
void cursor_init(gfx_context_t *ctx);

/* A full frame takes the cursor along: cursor_frame_begin() draws it into
 * the finished frame in the backbuffer, keeping the scene under it, and
 * cursor_frame_end(), after the copy to the screen, puts the scene back.
 * The backbuffer never keeps the cursor. */
void cursor_frame_begin(void);
void cursor_frame_end(void);

/* Only the cursor moved: on the screen (the visible frame, screen sized),
 * the old arrow gives way to the scene from the backbuffer and the arrow
 * is drawn where the cursor is now. Needs the backbuffer to hold the
 * frame on the screen, i.e. no full frame drawn but not yet shown. */
void cursor_move_on(uint32_t *screen);

#endif
