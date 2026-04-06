#include "dock.h"
#include "../../../drivers/serial.h"
#include "../../../drivers/vbe.h" /* For screen size */
#include "../../app/app.h"        /* For app_t with icon */
#include "../../gfx/font/font.h"
#include "../../gfx/icons.h" /* For icon_missing fallback */
#include "../../gfx/primitives.h"
#include "../../gui/wm/wm.h" /* To restore windows */
#include "../../ui/ui_scale.h"
#include <stddef.h>

/* Colors */
#define COR_DOCK_BG 0x202020
#define COR_DOCK_BORDER 0x505050
#define COR_ITEM_BG 0x404040
#define COR_ITEM_ACTIVE 0x606060
#define COR_ITEM_MINIMIZED 0x303030
#define COR_TEXT 0xFFFFFF

static dock_item_t dock_items[MAX_DOCK_ITEMS];
static int dock_item_count = 0;

void dock_init(void) {
  dock_item_count = 0;
  for (int i = 0; i < MAX_DOCK_ITEMS; i++) {
    dock_items[i].window = NULL;
    dock_items[i].active = false;
  }
}

void dock_register_window(window_t *win) {
  if (!win || dock_item_count >= MAX_DOCK_ITEMS)
    return;

  /* Check if already exists */
  for (int i = 0; i < dock_item_count; i++) {
    if (dock_items[i].window == win)
      return;
  }

  dock_items[dock_item_count].window = win;
  dock_items[dock_item_count].active =
      false; // Initially false? Or check win->focused?
  dock_item_count++;
  serial_print("[DOCK] Window Registered\n");
}

void dock_unregister_window(window_t *win) {
  if (!win)
    return;

  int found_idx = -1;
  for (int i = 0; i < dock_item_count; i++) {
    if (dock_items[i].window == win) {
      found_idx = i;
      break;
    }
  }

  if (found_idx != -1) {
    /* Shift remaining */
    for (int i = found_idx; i < dock_item_count - 1; i++) {
      dock_items[i] = dock_items[i + 1];
    }
    dock_item_count--;
    serial_print("[DOCK] Window Unregistered\n");
  }
}

void dock_notify_focus(window_t *win) {
  for (int i = 0; i < dock_item_count; i++) {
    if (dock_items[i].window == win) {
      dock_items[i].active = true;
    } else {
      dock_items[i].active = false;
    }
  }
}

void dock_render(gfx_context_t *ctx) {
  /* Render at bottom of screen */
  /* LOGICAL coordinates */
  int screen_w = (int)(vbe_get_width() / ui_scale);
  int screen_h = (int)(vbe_get_height() / ui_scale);

  int dock_w = screen_w;
  int dock_h = DOCK_HEIGHT;
  int dock_y = screen_h - dock_h;

  /* Draw Background */
  gfx_fill_rect(ctx, 0, dock_y, dock_w, dock_h, COR_DOCK_BG);

  /* Draw Top Border */
  gfx_fill_rect(ctx, 0, dock_y, dock_w, 1, COR_DOCK_BORDER);

  /* Render Items with Icons */
  int item_w = 40;
  int item_h = 32;
  int margin = 8;
  int start_x = margin;
  int item_y = dock_y + (dock_h - item_h) / 2;

  for (int i = 0; i < dock_item_count; i++) {
    int x = start_x + i * (item_w + margin);

    /* Draw item background */
    uint32_t color = COR_ITEM_BG;
    if (dock_items[i].window->minimized)
      color = COR_ITEM_MINIMIZED;
    if (dock_items[i].active && !dock_items[i].window->minimized)
      color = COR_ITEM_ACTIVE;

    gfx_fill_rect(ctx, x, item_y, item_w, item_h, color);

    /* Draw Icon */
    const icon_t *icon = NULL;
    if (dock_items[i].window->app && dock_items[i].window->app->icon) {
      icon = dock_items[i].window->app->icon;
    } else {
      icon = &icon_missing; /* Fallback */
    }

    /* Center icon in item box (32x32 icon in 40x32 box) */
    int icon_x = x + (item_w - 32) / 2;
    int icon_y = item_y;
    gfx_blit_icon(ctx, icon_x, icon_y, icon);

    /* Draw Indicator if active */
    if (dock_items[i].active && !dock_items[i].window->minimized) {
      gfx_fill_rect(ctx, x, item_y + item_h + 2, item_w, 2, 0x00FF00);
    }
  }
}

bool dock_handle_event(event_t *event) {
  if (event->type == EVENT_MOUSE_CLICK) {
    int mx = event->data.mouse.x;
    int my = event->data.mouse.y;

    int screen_h = (int)(vbe_get_height() / ui_scale);
    int dock_y = screen_h - DOCK_HEIGHT;

    /* Hit Test Dock Area */
    if (my >= dock_y) {
      /* Inside Dock vertical area */
      /* Check items */
      int item_w = 40;
      int margin = 8;
      int start_x = margin;

      /* Simple hit test assuming left alignment */
      /* This is rough, assumes item spacing. Should match render logic exactly.
       */

      for (int i = 0; i < dock_item_count; i++) {
        int x = start_x + i * (item_w + margin);
        if (mx >= x && mx < x + item_w) {
          /* Clicked Item i */
          window_t *win = dock_items[i].window;
          if (win) {
            if (win->minimized) {
              /* Restore */
              win->minimized = false;
              win->visible = true; // Ensure visible
              wm_focus_window(win);
            } else {
              /* If already active, maybe minimize? Or just focus? */
              if (win->focused) {
                // Minimize on click if already focused? Windows/macOS behavior
                // varies. User said: "Restore (klik w Dock)..." Let's implement
                // Restore/Focus. If I click focused window in dock, usually
                // nothing or minimize. Let's stick to Restore/Focus for now.
              } else {
                wm_focus_window(win);
              }
            }
          }
          return true; /* Consumed */
        }
      }

      return true; /* Consumed click in dock background? User: "Dock nie
                      przechwytuje imputu globalnie - tylko w swoim obszarze".
                      So YES, consume click in dock area. */
    }
  }

  return false;
}
