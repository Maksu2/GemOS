#ifndef WINDOW_H
#define WINDOW_H

#include "../../gfx/context.h"
#include "../../gfx/rect.h"
#include <stdbool.h>

#define WINDOW_FLAG_NONE 0x00
#define WINDOW_FLAG_MODAL 0x01
#define WINDOW_FLAG_NO_RESIZE 0x02
#define WINDOW_FLAG_NO_DOCK 0x04

typedef struct window {
  char title[32];
  uint32_t flags;

  int x;
  int y;
  int width;
  int height;

  // Geometry
  rect_t frame_rect;  // Total area including border
  rect_t client_rect; // Inner area for content

  // State
  bool visible;
  bool focused;
  bool minimized;
  bool maximized;
  rect_t restore_rect; // To restore size/pos after maximize

  // App Link
  struct app *app; // Forward declared implicitly or explicit

  // Graphics Context
  gfx_context_t ctx;

  // Linked List for Z-Order
  struct window *prev;
  struct window *next;

} window_t;

// Helpers

/* Initialize a window structure.
 * Calculates frame_rect and client_rect based on x,y,w,h.
 * Sets up the graphics context.
 */
/* Initialize a window structure. */
void window_init(window_t *win, int x, int y, int w, int h);

/* Set window title */
void window_set_title(window_t *win, const char *title);

/* Called when window is moved/resized (internal helper mostly) */
void window_update_rects(window_t *win);

#endif /* WINDOW_H */
