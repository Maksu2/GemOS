#include "window.h"
#include "../../gfx/context.h"
#include <string.h>

void window_update_rects(window_t *win) {
  // Frame rect is the total external area
  win->frame_rect.x = win->x;
  win->frame_rect.y = win->y;
  win->frame_rect.w = win->width;
  win->frame_rect.h = win->height;

  // Client rect is inside the border and below the title bar:
  // Client = [x+1, y+1+Title, w-2, h-2-Title]

  win->client_rect.x = win->x + WINDOW_BORDER_W;
  win->client_rect.y = win->y + WINDOW_BORDER_W + WINDOW_TITLE_BAR_H;
  win->client_rect.w = win->width - (2 * WINDOW_BORDER_W);
  win->client_rect.h = win->height - (2 * WINDOW_BORDER_W) - WINDOW_TITLE_BAR_H;

  // Safety clamp
  if (win->client_rect.w < 0)
    win->client_rect.w = 0;
  if (win->client_rect.h < 0)
    win->client_rect.h = 0;
}

void window_init(window_t *win, int x, int y, int w, int h) {
  if (!win) {
    return;
  }

  memset(win, 0, sizeof(*win));

  win->x = x;
  win->y = y;
  win->width = w;
  win->height = h;

  win->title[0] = 'W';
  win->title[1] = 'i';
  win->title[2] = 'n';
  win->title[3] = 'd';
  win->title[4] = 'o';
  win->title[5] = 'w';
  win->title[6] = '\0';
  win->flags = WINDOW_FLAG_NONE;

  win->visible = true;
  win->focused = false;
  window_update_rects(win);

  // Initialize context
  // ...
}

void window_set_title(window_t *win, const char *title) {
  if (!win || !title)
    return;
  int i = 0;
  while (title[i] && i < 31) {
    win->title[i] = title[i];
    i++;
  }
  win->title[i] = '\0';
}
