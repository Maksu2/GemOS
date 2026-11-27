#include "dock.h"
#include "../../../drivers/serial.h"
#include "../../../drivers/vbe.h"
#include "../../app/app.h"
#include "../../gfx/font/font.h"
#include "../../gfx/icons.h"
#include "../../gfx/primitives.h"
#include "../../gui/wm/wm.h"
#include "../theme.h"
#include "../ui_scale.h"
#include <stddef.h>

#define DOCK_MARGIN 8
#define DOCK_GAP 8
#define DOCK_TITLE_SIZE 13
#define DOCK_MAX_ITEM_WIDTH 168

static window_t *dock_items[MAX_DOCK_ITEMS];
static int dock_item_count;

void dock_init(void) { dock_item_count = 0; }

void dock_register_window(window_t *win) {
  if (!win || dock_item_count >= MAX_DOCK_ITEMS) {
    return;
  }
  for (int i = 0; i < dock_item_count; ++i) {
    if (dock_items[i] == win) {
      return;
    }
  }
  dock_items[dock_item_count++] = win;
  serial_print("[DOCK] Window Registered\n");
}

void dock_unregister_window(window_t *win) {
  for (int i = 0; i < dock_item_count; ++i) {
    if (dock_items[i] == win) {
      for (int next = i + 1; next < dock_item_count; ++next) {
        dock_items[next - 1] = dock_items[next];
      }
      dock_items[--dock_item_count] = NULL;
      serial_print("[DOCK] Window Unregistered\n");
      return;
    }
  }
}

static rect_t dock_area(void) {
  return (rect_t){0, (int)(vbe_get_height() / ui_scale) - DOCK_HEIGHT,
                  (int)(vbe_get_width() / ui_scale), DOCK_HEIGHT};
}

static int dock_item_limit(rect_t bar) {
  int width = (bar.w - 2 * DOCK_MARGIN - (dock_item_count - 1) * DOCK_GAP) /
              dock_item_count;
  return width < DOCK_MAX_ITEM_WIDTH ? width : DOCK_MAX_ITEM_WIDTH;
}

static rect_t dock_item(int x, rect_t bar, int limit, const window_t *win) {
  int width = 34 + font_text_width(win->title, DOCK_TITLE_SIZE);
  return (rect_t){x, bar.y + 3, width < limit ? width : limit, bar.h - 6};
}

void dock_render(gfx_context_t *ctx) {
  rect_t bar = dock_area();
  gfx_fill_rect(ctx, bar.x, bar.y, bar.w, bar.h, UI_BAR);
  gfx_fill_rect(ctx, bar.x, bar.y, bar.w, 1, UI_BAR_BORDER);
  if (dock_item_count == 0) {
    return;
  }

  int x = DOCK_MARGIN;
  int limit = dock_item_limit(bar);
  for (int i = 0; i < dock_item_count; ++i) {
    window_t *win = dock_items[i];
    rect_t item = dock_item(x, bar, limit, win);
    uint32_t ink = win->minimized ? UI_BAR_MUTED : UI_PAPER;
    const icon_t *icon = win->icon;

    if (!icon && win->app) {
      icon = win->app->icon;
    }
    icon_draw_outline(ctx, item.x + 5, item.y + 5, icon, ink);
    font_draw_text_fit(ctx, item.x + 27, item.y + 5, win->title,
                       DOCK_TITLE_SIZE, ink, item.w - 31);
    if (win->focused) {
      gfx_fill_rect(ctx, item.x, item.y + item.h - 1, item.w, 2, UI_ACCENT);
    }
    x += item.w + DOCK_GAP;
  }
}

bool dock_handle_event(event_t *event) {
  if (event->type != EVENT_MOUSE_CLICK) {
    return false;
  }
  rect_t bar = dock_area();
  int mx = event->data.mouse.x;
  int my = event->data.mouse.y;
  if (!rect_contains(&bar, mx, my)) {
    return false;
  }
  if (dock_item_count != 0) {
    int x = DOCK_MARGIN;
    int limit = dock_item_limit(bar);
    for (int i = 0; i < dock_item_count; ++i) {
      rect_t item = dock_item(x, bar, limit, dock_items[i]);
      if (rect_contains(&item, mx, my)) {
        wm_restore_window(dock_items[i]);
        break;
      }
      x += item.w + DOCK_GAP;
    }
  }
  return true;
}
