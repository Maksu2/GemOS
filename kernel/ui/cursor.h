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

/* Render cursor (call at end of frame) */
void cursor_draw(void);

#endif
