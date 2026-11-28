#include "wm.h"
#include "../../../drivers/serial.h"
#include "../../../drivers/vbe.h" /* For screen size */
#include "../../app/app.h"
#include "../../gfx/font/font.h"
#include "../../gfx/primitives.h"
#include "../../ui/dock/dock.h"
#include "../../ui/focus.h"
#include "../../ui/menu.h"
#include "../../ui/theme.h"
#include "../../ui/ui_scale.h"
#include "../../include/gui_loop.h"
#include "../topbar/topbar.h" /* For TOPBAR_HEIGHT */
#include <stddef.h>


// Global state
static window_t *windows_head = NULL; // Bottom-most window
static window_t *windows_tail = NULL; // Top-most window in the stack
static window_t *focused_window = NULL;
static gfx_context_t *wm_screen_ctx = NULL;

/* Dragging State */
static bool wm_is_dragging = false;
static int wm_drag_offset_x = 0;
static int wm_drag_offset_y = 0;

/* The window that got the last button press: its app gets the release
 * too, wherever it happens, so a drag inside the window always ends */
static window_t *wm_press_window = NULL;

/* Placement */
#define WM_CASCADE_STEP 24  /* a new window on another one's corner moves */
#define WM_CASCADE_TRIES 8
#define WM_CORNER_NEAR 8    /* corners closer than this are the same */
#define WM_KEEP_VISIBLE 64  /* a dragged window keeps this much on screen */

/* ===========================================================================
 * Window Decoration Constants
 * =========================================================================*/

/* Title Bar (kernel/gui/window/window.h) */
#define TITLE_BAR_H WINDOW_TITLE_BAR_H
#define BORDER_W WINDOW_BORDER_W

/* Title bar controls. Drawing and hit testing share these positions. */
#define BTN_SIZE 20
#define BTN_RIGHT_MARGIN 5
#define BTN_GAP 2

/* "[WM] <what> x= y= w= h=", read by tools/smoke.py */
static void wm_log_rect(const char *what, rect_t rect) {
  serial_print(what);
  serial_print(" x=");
  serial_print_dec((uint32_t)rect.x);
  serial_print(" y=");
  serial_print_dec((uint32_t)rect.y);
  serial_print(" w=");
  serial_print_dec((uint32_t)rect.w);
  serial_print(" h=");
  serial_print_dec((uint32_t)rect.h);
  serial_print("\n");
}

void wm_init(gfx_context_t *ctx) {
  if (!ctx)
    return;
  wm_screen_ctx = ctx;
  windows_head = NULL;
  windows_tail = NULL;
  focused_window = NULL;
  serial_print("[WM] Initialized with graphics context\n");
  wm_log_rect("[WM] Work area", wm_work_area());
}

static void wm_emit_focus_event(window_t *win) {
  focus_state_t state;
  if (win) {
    state.type = FOCUS_WINDOW;
    state.window = win;
    state.app = win->app;
  } else {
    state.type = FOCUS_DESKTOP;
    state.window = NULL;
    state.app = NULL;
  }
  ui_on_focus_changed(&state);
}

static bool wm_visible(const window_t *win) {
  return win->visible && !win->minimized;
}

static window_t *wm_top_visible(void) {
  window_t *win = windows_tail;
  while (win && !wm_visible(win)) {
    win = win->prev;
  }
  return win;
}

static void wm_set_focus(window_t *win) {
  if (focused_window == win) {
    return;
  }
  if (focused_window) {
    menu_close_for_window(focused_window);
    focused_window->focused = false;
    wm_is_dragging = false;
  }
  focused_window = win;
  if (win) {
    win->focused = true;
  }
  wm_emit_focus_event(win);
}

static rect_t wm_button(const window_t *win, int from_right) {
  return (rect_t){
      win->x + win->width - BTN_RIGHT_MARGIN - BTN_SIZE -
          from_right * (BTN_SIZE + BTN_GAP),
      win->y + BORDER_W + (TITLE_BAR_H - BTN_SIZE) / 2, BTN_SIZE, BTN_SIZE};
}

/* Helper to unlink a window safely */
static void wm_unlink(window_t *win) {
  if (!win)
    return;

  if (win->prev) {
    win->prev->next = win->next;
  } else if (windows_head == win) {
    windows_head = win->next;
  }

  if (win->next) {
    win->next->prev = win->prev;
  } else if (windows_tail == win) {
    windows_tail = win->prev;
  }

  win->prev = NULL;
  win->next = NULL;
}

static bool wm_contains(window_t *win) {
  window_t *current = windows_head;

  while (current) {
    if (current == win) {
      return true;
    }
    current = current->next;
  }

  return false;
}

/* Helper to append to tail (top of Z-order) */
static void wm_append(window_t *win) {
  if (!win)
    return;

  if (wm_contains(win)) {
    wm_unlink(win);
  } else {
    win->prev = NULL;
    win->next = NULL;
  }

  if (!windows_tail) {
    windows_head = win;
    windows_tail = win;
    win->prev = NULL;
    win->next = NULL;
  } else {
    windows_tail->next = win;
    win->prev = windows_tail;
    win->next = NULL;
    windows_tail = win;
  }
}

rect_t wm_work_area(void) {
  int width = (int)(vbe_get_width() / ui_scale);
  int height = (int)(vbe_get_height() / ui_scale);

  return (rect_t){0, TOPBAR_HEIGHT, width,
                  height - TOPBAR_HEIGHT - DOCK_HEIGHT};
}

/* Inside the area where the window fits; one larger than the area keeps
 * its top left corner in it, so its title bar can be reached. */
static void wm_clamp_to(window_t *win, const rect_t *area, int x, int y) {
  if (x + win->width > area->x + area->w) {
    x = area->x + area->w - win->width;
  }
  if (y + win->height > area->y + area->h) {
    y = area->y + area->h - win->height;
  }
  win->x = x < area->x ? area->x : x;
  win->y = y < area->y ? area->y : y;
}

static bool wm_corner_taken(const window_t *win) {
  for (window_t *other = windows_head; other; other = other->next) {
    int dx = other->x - win->x;
    int dy = other->y - win->y;

    if (other != win && other->visible && dx > -WM_CORNER_NEAR &&
        dx < WM_CORNER_NEAR && dy > -WM_CORNER_NEAR && dy < WM_CORNER_NEAR) {
      return true;
    }
  }
  return false;
}

/* The place the window asked for, moved into the work area, then down and
 * right in steps while another window's corner is there. If every step is
 * taken the window stays at the first one. */
static void wm_place_window(window_t *win) {
  rect_t area = wm_work_area();
  int want_x = win->x;
  int want_y = win->y;

  for (int step = 0; step < WM_CASCADE_TRIES; ++step) {
    wm_clamp_to(win, &area, want_x + step * WM_CASCADE_STEP,
                want_y + step * WM_CASCADE_STEP);
    if (!wm_corner_taken(win)) {
      break;
    }
    if (step == WM_CASCADE_TRIES - 1) {
      wm_clamp_to(win, &area, want_x, want_y);
    }
  }
  window_update_rects(win);
  wm_log_rect("[WM] Placed", win->frame_rect);
}

void wm_add_window(window_t *win) {
  if (!win || !wm_screen_ctx)
    return;

  win->ctx = *wm_screen_ctx;
  wm_place_window(win);
  win->minimized = false;
  win->maximized = false;

  wm_append(win);

  if (!(win->flags & WINDOW_FLAG_NO_DOCK)) {
    dock_register_window(win);
  }

  wm_focus_window(win);
}

void wm_remove_window(window_t *win) {
  if (!win)
    return;

  menu_close_for_window(win);
  if (wm_press_window == win) {
    wm_press_window = NULL;
  }
  wm_unlink(win);
  if (focused_window == win) {
    wm_set_focus(wm_top_visible());
  }

  if (!(win->flags & WINDOW_FLAG_NO_DOCK)) {
    dock_unregister_window(win);
  }

  if (win->app && win->app->close) {
    win->app->close(win);
  }
}

void wm_focus_window(window_t *win) {
  if (!win || !wm_visible(win))
    return;

  if (focused_window == win && windows_tail == win)
    return;

  wm_append(win);
  wm_set_focus(win);
}

void wm_restore_window(window_t *win) {
  if (!win) {
    return;
  }
  win->minimized = false;
  win->visible = true;
  wm_focus_window(win);
}

window_t *wm_get_focused_window(void) { return focused_window; }

/* ===========================================================================
 * Rendering
 * =========================================================================*/

static void wm_render_decorations(window_t *win) {
  gfx_context_t *ctx = &win->ctx;
  uint32_t ink = win->focused ? UI_INK : UI_MUTED;
  bool resizable = !(win->flags & WINDOW_FLAG_NO_RESIZE);
  rect_t close = wm_button(win, 0);
  rect_t leftmost = wm_button(win, resizable ? 2 : 0);
  int title_x = win->x + 8;

  gfx_set_clip(ctx, win->x, win->y, win->width, win->height);
  gfx_draw_rect(ctx, win->x, win->y, win->width, win->height, UI_FRAME);
  gfx_fill_rect(ctx, win->x + BORDER_W, win->y + BORDER_W,
                win->width - 2 * BORDER_W, TITLE_BAR_H,
                win->focused ? UI_PAPER : UI_PAPER_SHADED);
  gfx_fill_rect(ctx, win->x + BORDER_W, win->client_rect.y - 2,
                win->width - 2 * BORDER_W, 2,
                win->focused ? UI_ACCENT : UI_RULE);
  font_draw_text_fit(ctx, title_x, win->y + 5, win->title, 14, ink,
                     leftmost.x - 4 - title_x);

  int cx = close.x + BTN_SIZE / 2;
  int cy = close.y + BTN_SIZE / 2;
  gfx_draw_line(ctx, cx - 4, cy - 4, cx + 4, cy + 4, ink);
  gfx_draw_line(ctx, cx - 4, cy + 4, cx + 4, cy - 4, ink);
  if (resizable) {
    rect_t max = wm_button(win, 1);
    rect_t min = wm_button(win, 2);
    gfx_draw_rect(ctx, max.x + 5, max.y + 5, 10, 10, ink);
    gfx_draw_line(ctx, min.x + 5, cy + 2, min.x + 14, cy + 2, ink);
  }
}

static void wm_render_window(window_t *win) {
  wm_render_decorations(win);
  gfx_set_clip(&win->ctx, win->client_rect.x, win->client_rect.y,
               win->client_rect.w, win->client_rect.h);
  if (win->app && win->app->render) {
    win->app->render(win);
  } else {
    gfx_fill_rect(&win->ctx, win->client_rect.x, win->client_rect.y,
                  win->client_rect.w, win->client_rect.h, UI_PAPER);
  }
}

static bool wm_in_bound(const window_t *win) {
  rect_t area = {win->x * ui_scale, win->y * ui_scale,
                 win->width * ui_scale, win->height * ui_scale};
  rect_t bound = wm_screen_ctx->bound_rect;
  rect_t out;

  return rect_intersect(&area, &bound, &out);
}

void wm_render_all(void) {
  /* Sync all window framebuffer pointers to the current screen context.
   * Required for BGA page flipping: screen_ctx.framebuffer alternates
   * between VRAM pages each frame, but win->ctx copies the pointer
   * by value at window creation time. Without this sync, windows
   * render to the wrong buffer and cause flickering. The bound (the part
   * of the frame drawn) is copied the same way. */
  window_t *sync = windows_head;
  while (sync) {
    sync->ctx.framebuffer = wm_screen_ctx->framebuffer;
    sync->ctx.bound_rect = wm_screen_ctx->bound_rect;
    gfx_reset_clip(&sync->ctx);
    sync = sync->next;
  }

  /* Render from Head (Bottom) to Tail (Top), only what the bound touches */
  window_t *curr = windows_head;
  while (curr) {
    if (wm_visible(curr) && wm_in_bound(curr)) {
      wm_render_window(curr);
    }
    curr = curr->next;
  }
}

void wm_damage_app_clients(const struct app *app) {
  for (window_t *win = windows_head; win; win = win->next) {
    if (win->app == app && wm_visible(win)) {
      rect_t client = win->client_rect;
      kernel_damage(client.x, client.y, client.w, client.h);
    }
  }
}

/* ===========================================================================
 * Event Handling
 * =========================================================================*/

/* Hit test in front-to-back order. Hidden windows remain in the dock. */
static window_t *wm_window_at(int x, int y) {
  for (window_t *win = windows_tail; win; win = win->prev) {
    if (wm_visible(win) && rect_contains(&win->frame_rect, x, y)) {
      return win;
    }
  }
  return NULL;
}

static void wm_toggle_maximize(window_t *win) {
  rect_t frame = win->restore_rect;
  if (!win->maximized) {
    win->restore_rect = win->frame_rect;
    frame = wm_work_area();
  }
  win->x = frame.x;
  win->y = frame.y;
  win->width = frame.w;
  win->height = frame.h;
  win->maximized = !win->maximized;
  window_update_rects(win);
  if (win->app && win->app->handle_event) {
    event_t event = {.type = EVENT_WINDOW_RESIZE};
    win->app->handle_event(win, &event);
  }
  serial_print("[WM] Toggle Maximize\n");
}

static void wm_handle_click(event_t *event) {
  int x = event->data.mouse.x;
  int y = event->data.mouse.y;
  window_t *win = wm_window_at(x, y);

  wm_press_window = NULL;
  if (!win) {
    wm_set_focus(NULL);
    return;
  }
  wm_focus_window(win);

  rect_t close = wm_button(win, 0);
  if (rect_contains(&close, x, y)) {
    if (!win->app || !win->app->request_close || !win->app->request_close(win)) {
      serial_print("[WM] Close Window\n");
      wm_remove_window(win);
    }
    return;
  }
  if (!(win->flags & WINDOW_FLAG_NO_RESIZE)) {
    rect_t max = wm_button(win, 1);
    rect_t min = wm_button(win, 2);
    if (rect_contains(&max, x, y)) {
      wm_toggle_maximize(win);
      return;
    }
    if (rect_contains(&min, x, y)) {
      serial_print("[WM] Minimize (Hide)\n");
      win->minimized = true;
      win->visible = false;
      wm_set_focus(wm_top_visible());
      return;
    }
  }

  rect_t title = {win->x, win->y, win->width, BORDER_W + TITLE_BAR_H};
  if (rect_contains(&title, x, y)) {
    wm_is_dragging = true;
    wm_drag_offset_x = win->x - x;
    wm_drag_offset_y = win->y - y;
  } else if (rect_contains(&win->client_rect, x, y) && win->app &&
             win->app->handle_event) {
    wm_press_window = win;
    win->app->handle_event(win, event);
  }
}

static void wm_handle_context_click(event_t *event) {
  int x = event->data.mouse.x;
  int y = event->data.mouse.y;
  rect_t area = wm_work_area();
  if (!rect_contains(&area, x, y)) {
    return;
  }
  window_t *win = wm_window_at(x, y);
  if (win && win->app && win->app->handle_event &&
      rect_contains(&win->client_rect, x, y)) {
    if (focused_window != win) {
      wm_focus_window(win);
      kernel_request_redraw();
    }
    win->app->handle_event(win, event);
  }
}

void wm_handle_event(event_t *event) {
  if (!event)
    return;

  if (event->type == EVENT_MOUSE_CLICK) {
    if (event->data.mouse.button == MOUSE_BUTTON_LEFT) {
      wm_handle_click(event);
    } else if (event->data.mouse.button == MOUSE_BUTTON_RIGHT) {
      wm_handle_context_click(event);
    }
  } else if (event->type == EVENT_MOUSE_MOVE) {
    if (wm_is_dragging && focused_window) {
      int mx = event->data.mouse.x;
      int my = event->data.mouse.y;

      rect_t area = wm_work_area();
      rect_t was = focused_window->frame_rect;
      int x = mx + wm_drag_offset_x;
      int y = my + wm_drag_offset_y;
      /* the title bar stays where it can be grabbed again: below the
       * topbar, above the dock, partly on screen */
      int min_x = area.x + WM_KEEP_VISIBLE - focused_window->width;
      int max_x = area.x + area.w - WM_KEEP_VISIBLE;
      int max_y = area.y + area.h - (BORDER_W + TITLE_BAR_H);

      focused_window->x = x < min_x ? min_x : (x > max_x ? max_x : x);
      focused_window->y = y < area.y ? area.y : (y > max_y ? max_y : y);

      window_update_rects(focused_window);
      rect_t frame = focused_window->frame_rect;
      kernel_damage(was.x, was.y, was.w, was.h);
      kernel_damage(frame.x, frame.y, frame.w, frame.h);
    } else {
      window_t *win = wm_press_window ? wm_press_window : focused_window;
      if (win && wm_visible(win) && win->app && win->app->handle_event) {
        win->app->handle_event(win, event);
      }
    }
  } else if (event->type == EVENT_MOUSE_RELEASE) {
    window_t *pressed = wm_press_window;

    if (wm_is_dragging && focused_window) {
      wm_log_rect("[WM] Dropped", focused_window->frame_rect);
    }
    wm_is_dragging = false;
    wm_press_window = NULL;
    if (pressed && pressed->app && pressed->app->handle_event) {
      pressed->app->handle_event(pressed, event);
    }
  } else if (event->type == EVENT_KEY_PRESS ||
             event->type == EVENT_KEY_RELEASE) {
    if (focused_window) {
      if (focused_window->app && focused_window->app->handle_event) {
        focused_window->app->handle_event(focused_window, event);
      }
    }
  }
}
