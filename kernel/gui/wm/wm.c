#include "wm.h"
#include "../../../drivers/serial.h"
#include "../../../drivers/vbe.h" /* For screen size */
#include "../../app/app.h"
#include "../../gfx/font/font.h"
#include "../../gfx/primitives.h"
#include "../../ui/dock/dock.h"
#include "../../ui/focus.h"
#include "../../ui/ui_scale.h"
#include "../topbar/topbar.h" /* For TOPBAR_HEIGHT */
#include <stddef.h>

// Global state
static window_t *windows_head = NULL; // Bottom-most window
static window_t *windows_tail = NULL; // Top-most window (active)
static window_t *focused_window = NULL;
static gfx_context_t *wm_screen_ctx = NULL;

/* Dragging State */
static bool wm_is_dragging = false;
static int wm_drag_offset_x = 0;
static int wm_drag_offset_y = 0;

/* ===========================================================================
 * Window Decoration Constants
 * =========================================================================*/

/* Title Bar */
#define TITLE_BAR_H 26
#define BORDER_W 1

/* Shadow */
#define SHADOW_OFFSET_X 5
#define SHADOW_OFFSET_Y 5
#define SHADOW_ALPHA 70
#define SHADOW_BLUR_RADIUS 8
#define SHADOW_EXPAND 4 /* Extra pixels around shadow for blur bleed */

/* Colors - Active */
#define COLOR_TITLE_TOP_ACTIVE 0x2266CC /* Deep blue */
#define COLOR_TITLE_BOT_ACTIVE 0x4499EE /* Lighter blue */
#define COLOR_BORDER_ACTIVE 0x1A55AA    /* Border blue */

/* Colors - Inactive */
#define COLOR_TITLE_TOP_INACTIVE 0x707070 /* Dark gray */
#define COLOR_TITLE_BOT_INACTIVE 0xA0A0A0 /* Light gray */
#define COLOR_BORDER_INACTIVE 0x808080    /* Border gray */

#define COLOR_TITLE_TEXT 0xFFFFFF /* White */
#define COLOR_CLIENT_BG 0xFFFFFF  /* White default */

/* Button Constants */
#define BTN_SIZE 16
#define BTN_RIGHT_MARGIN 6
#define BTN_GAP 4
#define BTN_Y_OFFSET 5 /* Vertically centered: (26 - 16) / 2 = 5 */

/* Button Colors */
#define COLOR_BTN_CLOSE 0xCC3333   /* Red */
#define COLOR_BTN_CLOSE_X 0xFFFFFF /* White X */
#define COLOR_BTN_NORMAL 0xC0C0C0  /* Light gray bg */
#define COLOR_BTN_ICON 0x404040    /* Dark icon */

void wm_init(gfx_context_t *ctx) {
  if (!ctx)
    return;
  wm_screen_ctx = ctx;
  windows_head = NULL;
  windows_tail = NULL;
  focused_window = NULL;
  serial_print("[WM] Initialized with graphics context\n");
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

void wm_add_window(window_t *win) {
  if (!win || !wm_screen_ctx)
    return;

  win->ctx = *wm_screen_ctx;
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

  if (focused_window == win) {
    focused_window = win->prev;
    if (focused_window)
      focused_window->focused = true;

    if (focused_window)
      dock_notify_focus(focused_window);

    wm_emit_focus_event(focused_window);
  }

  wm_unlink(win);

  if (!(win->flags & WINDOW_FLAG_NO_DOCK)) {
    dock_unregister_window(win);
  }

  if (win->app && win->app->close) {
    win->app->close(win);
  }
}

void wm_focus_window(window_t *win) {
  if (!win)
    return;

  if (focused_window == win && windows_tail == win)
    return;

  if (focused_window) {
    focused_window->focused = false;
  }

  if (wm_contains(win)) {
    wm_unlink(win);
  }
  wm_append(win);

  focused_window = win;
  win->focused = true;

  dock_notify_focus(win);
  wm_emit_focus_event(win);
}

window_t *wm_get_focused_window(void) { return focused_window; }

/* ===========================================================================
 * Rendering
 * =========================================================================*/

static void wm_render_shadow(window_t *win) {
  /* Skip shadow during drag for instant responsiveness */
  if (wm_is_dragging)
    return;

  /* Single solid shadow: offset rectangle, no alpha blending needed.
   * Alpha blending reads every destination pixel which is catastrophically
   * slow at Full HD. A solid fill is write-only = ~10× faster. */
  gfx_reset_clip(&win->ctx);
  gfx_fill_rect(&win->ctx, win->frame_rect.x + 4, win->frame_rect.y + 4,
                win->frame_rect.w, win->frame_rect.h,
                0x101018); /* Very dark blue, blends with dark wallpaper */
}

static void wm_render_decorations(window_t *win) {
  bool active = win->focused;

  uint32_t border_color = active ? COLOR_BORDER_ACTIVE : COLOR_BORDER_INACTIVE;
  uint32_t title_top =
      active ? COLOR_TITLE_TOP_ACTIVE : COLOR_TITLE_TOP_INACTIVE;
  uint32_t title_bot =
      active ? COLOR_TITLE_BOT_ACTIVE : COLOR_TITLE_BOT_INACTIVE;

  /* Set clip to frame rect */
  gfx_set_clip(&win->ctx, win->frame_rect.x, win->frame_rect.y,
               win->frame_rect.w, win->frame_rect.h);

  /* 1. Border frame */
  gfx_fill_rect(&win->ctx, win->frame_rect.x, win->frame_rect.y,
                win->frame_rect.w, win->frame_rect.h, border_color);

  /* 2. Title bar - gradient fill */
  gfx_gradient_rect_v(&win->ctx, win->x + BORDER_W, win->y + BORDER_W,
                      win->width - 2 * BORDER_W, TITLE_BAR_H, title_top,
                      title_bot);

  /* 3. Title text */
  font_draw_text(&win->ctx, win->x + 8, win->y + 6, win->title, 14,
                 COLOR_TITLE_TEXT);

  /* 4. Window buttons */
  int btn_y = win->y + BTN_Y_OFFSET;

  /* Close button (rightmost) - Red circle with white × */
  int close_x = win->x + win->width - BTN_RIGHT_MARGIN - BTN_SIZE;
  gfx_fill_circle(&win->ctx, close_x + BTN_SIZE / 2, btn_y + BTN_SIZE / 2,
                  BTN_SIZE / 2, COLOR_BTN_CLOSE);
  /* × icon (two diagonal lines) */
  gfx_draw_line(&win->ctx, close_x + 4, btn_y + 4, close_x + BTN_SIZE - 4,
                btn_y + BTN_SIZE - 4, COLOR_BTN_CLOSE_X);
  gfx_draw_line(&win->ctx, close_x + BTN_SIZE - 4, btn_y + 4, close_x + 4,
                btn_y + BTN_SIZE - 4, COLOR_BTN_CLOSE_X);

  if (!(win->flags & WINDOW_FLAG_NO_RESIZE)) {
    /* Maximize button - Gray circle with □ */
    int max_x = close_x - BTN_SIZE - BTN_GAP;
    gfx_fill_circle(&win->ctx, max_x + BTN_SIZE / 2, btn_y + BTN_SIZE / 2,
                    BTN_SIZE / 2, COLOR_BTN_NORMAL);
    /* □ icon */
    gfx_draw_rect(&win->ctx, max_x + 4, btn_y + 4, BTN_SIZE - 8, BTN_SIZE - 8,
                  COLOR_BTN_ICON);

    /* Minimize button - Gray circle with ▬ */
    int min_x = max_x - BTN_SIZE - BTN_GAP;
    gfx_fill_circle(&win->ctx, min_x + BTN_SIZE / 2, btn_y + BTN_SIZE / 2,
                    BTN_SIZE / 2, COLOR_BTN_NORMAL);
    /* ▬ icon (horizontal line in center) */
    gfx_draw_line(&win->ctx, min_x + 4, btn_y + BTN_SIZE / 2,
                  min_x + BTN_SIZE - 4, btn_y + BTN_SIZE / 2, COLOR_BTN_ICON);
  }
}

void wm_render_window(window_t *win) {
  if (!win || !win->visible)
    return;

  /* 1. Shadow (behind everything) */
  wm_render_shadow(win);

  /* 2. Window decorations (border, title bar, buttons) */
  wm_render_decorations(win);

  /* 3. Client area content */
  if (win->app && win->app->render) {
    gfx_set_clip(&win->ctx, win->client_rect.x, win->client_rect.y,
                 win->client_rect.w, win->client_rect.h);
    win->app->render(win);
  } else {
    gfx_fill_rect(&win->ctx, win->client_rect.x, win->client_rect.y,
                  win->client_rect.w, win->client_rect.h, COLOR_CLIENT_BG);
  }
}

void wm_render_all(void) {
  /* Sync all window framebuffer pointers to the current screen context.
   * Required for BGA page flipping: screen_ctx.framebuffer alternates
   * between VRAM pages each frame, but win->ctx copies the pointer
   * by value at window creation time. Without this sync, windows
   * render to the wrong buffer and cause flickering. */
  window_t *sync = windows_head;
  while (sync) {
    sync->ctx.framebuffer = wm_screen_ctx->framebuffer;
    sync = sync->next;
  }

  /* Render from Head (Bottom) to Tail (Top) */
  window_t *curr = windows_head;
  while (curr) {
    wm_render_window(curr);
    curr = curr->next;
  }
}

/* ===========================================================================
 * Event Handling
 * =========================================================================*/

void wm_handle_event(event_t *event) {
  if (!event)
    return;

  if (event->type == EVENT_MOUSE_CLICK) {
    int mx = event->data.mouse.x;
    int my = event->data.mouse.y;

    window_t *curr = windows_tail;

    while (curr) {
      if (rect_contains(&curr->frame_rect, mx, my)) {
        if (curr != focused_window) {
          wm_focus_window(curr);
        }

        /* Forward Click to App */
        if (curr->app && curr->app->handle_event) {
          curr->app->handle_event(curr, event);
        }

        /* HIT TEST: Window Controls */
        int btn_y = curr->y + BTN_Y_OFFSET;

        int close_x = curr->x + curr->width - BTN_RIGHT_MARGIN - BTN_SIZE;
        int max_x = close_x - BTN_SIZE - BTN_GAP;
        int min_x = max_x - BTN_SIZE - BTN_GAP;

        rect_t r_close = {close_x, btn_y, BTN_SIZE, BTN_SIZE};
        rect_t r_max = {max_x, btn_y, BTN_SIZE, BTN_SIZE};
        rect_t r_min = {min_x, btn_y, BTN_SIZE, BTN_SIZE};

        if (rect_contains(&r_close, mx, my)) {
          if (curr->app && curr->app->request_close &&
              curr->app->request_close(curr)) {
            break;
          }
          serial_print("[WM] Close Window\n");
          wm_remove_window(curr);
          break;
        }

        if (!(curr->flags & WINDOW_FLAG_NO_RESIZE)) {
          if (rect_contains(&r_max, mx, my)) {
            serial_print("[WM] Toggle Maximize\n");
            if (curr->maximized) {
              curr->x = curr->restore_rect.x;
              curr->y = curr->restore_rect.y;
              curr->width = curr->restore_rect.w;
              curr->height = curr->restore_rect.h;
              curr->maximized = false;
            } else {
              curr->restore_rect =
                  (rect_t){curr->x, curr->y, curr->width, curr->height};
              curr->x = 0;
              curr->y = TOPBAR_HEIGHT;
              int logical_w = (int)(vbe_get_width() / ui_scale);
              int logical_h = (int)(vbe_get_height() / ui_scale);
              curr->width = logical_w;
              curr->height = logical_h - TOPBAR_HEIGHT;
              curr->maximized = true;
            }
            window_update_rects(curr);
            break;
          } else if (rect_contains(&r_min, mx, my)) {
            serial_print("[WM] Minimize (Hide)\n");
            curr->minimized = true;
            curr->visible = false;
            break;
          }
        }

        /* Title bar drag */
        rect_t title_rect = curr->frame_rect;
        title_rect.h = BORDER_W + TITLE_BAR_H;

        if (rect_contains(&title_rect, mx, my)) {
          wm_is_dragging = true;
          wm_drag_offset_x = curr->x - mx;
          wm_drag_offset_y = curr->y - my;
        }
        break;
      }
      curr = curr->prev;
    }

    if (!curr) {
      if (focused_window) {
        focused_window->focused = false;
        focused_window = NULL;
        wm_emit_focus_event(NULL);
      }
    }
  } else if (event->type == EVENT_MOUSE_MOVE) {
    if (wm_is_dragging && focused_window) {
      int mx = event->data.mouse.x;
      int my = event->data.mouse.y;

      focused_window->x = mx + wm_drag_offset_x;
      focused_window->y = my + wm_drag_offset_y;

      if (focused_window->y < TOPBAR_HEIGHT) {
        focused_window->y = TOPBAR_HEIGHT;
      }

      window_update_rects(focused_window);
    }
  } else if (event->type == EVENT_MOUSE_RELEASE) {
    wm_is_dragging = false;
  } else if (event->type == EVENT_KEY_PRESS ||
             event->type == EVENT_KEY_RELEASE) {
    if (focused_window) {
      if (focused_window->app && focused_window->app->handle_event) {
        focused_window->app->handle_event(focused_window, event);
      }
    }
  }
}
