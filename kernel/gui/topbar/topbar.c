#include "topbar.h"
#include "../../../drivers/rtc.h"
#include "../../../drivers/serial.h"
#include "../../../drivers/vbe.h"
#include "../../app/app.h"
#include "../../app/app_manager.h"
#include "../../gfx/font/font.h"
#include "../../gfx/primitives.h"
#include "../../include/gui_loop.h"
#include "../../ui/focus.h"
#include "../../ui/menu.h"
#include "../../ui/theme.h"
#include "../../ui/ui_scale.h"
#include "../wm/wm.h"
#include <stddef.h>

#define TOPBAR_ITEM_WIDTH 70

/* Menus */
static menu_t *menu_gemos = NULL;
static menu_t *menu_apps = NULL;

static app_t *active_app_with_menu = NULL; /* Cached focus state */

/* The time the clock showed in the last frame, hours * 60 + minutes */
static int clock_drawn = -1;

void action_exit(void) { kernel_request_shutdown(); }
void action_open_about(void) { app_open("About GemOS"); }
void action_open_terminal(void) { app_open("User Terminal"); }
void action_open_log_viewer(void) { app_open("Log Viewer"); }
void action_open_textedit(void) { app_open("Text Editor"); }
void action_open_explorer(void) { app_open("File Explorer"); }

void topbar_init(void) {
  serial_print("[TOPBAR] Initialized\n");

  menu_init();

  /* GemOS Menu */
  menu_gemos = menu_create();
  if (menu_gemos) {
    menu_add_item(menu_gemos, "About GemOS", action_open_about);
    menu_add_item(menu_gemos, "Shutdown", action_exit);
  }

  /* Apps Menu */
  menu_apps = menu_create();
  if (menu_apps) {
    menu_add_item(menu_apps, "File Explorer", action_open_explorer);
    menu_add_item(menu_apps, "Terminal", action_open_terminal);
    menu_add_item(menu_apps, "Log Viewer", action_open_log_viewer);
    menu_add_item(menu_apps, "Text Editor", action_open_textedit);
  }
}

void topbar_damage(void) {
  kernel_damage(0, 0, (int)(vbe_get_width() / ui_scale), TOPBAR_HEIGHT);
}

void topbar_on_focus_changed(focus_state_t *state) {
  if (!state)
    return;

  app_t *was = active_app_with_menu;
  if (state->type == FOCUS_WINDOW && state->app && state->app->menu) {
    active_app_with_menu = state->app;
  } else {
    active_app_with_menu = NULL;
  }
  if (active_app_with_menu != was) {
    topbar_damage(); /* the app's menu title appears or goes */
  }

  if (menu_is_active()) {
    menu_hide();
  }
}

static void topbar_draw_menu(gfx_context_t *ctx, int x, const char *label,
                             const menu_t *menu) {
  font_draw_text_fit(ctx, x, 6, label, 14, UI_PAPER, TOPBAR_ITEM_WIDTH - 6);
  if (menu && menu->visible) {
    gfx_fill_rect(ctx, x, TOPBAR_HEIGHT - 3, TOPBAR_ITEM_WIDTH - 8, 2,
                  UI_ACCENT);
  }
}

void topbar_render(gfx_context_t *ctx) {
  int logical_width = (int)(ctx->width / ui_scale);

  gfx_fill_rect(ctx, 0, 0, logical_width, TOPBAR_HEIGHT, UI_BAR);
  gfx_fill_rect(ctx, 0, TOPBAR_HEIGHT - 1, logical_width, 1, UI_BAR_BORDER);
  topbar_draw_menu(ctx, 12, "GemOS", menu_gemos);
  topbar_draw_menu(ctx, 92, "Apps", menu_apps);
  if (active_app_with_menu && active_app_with_menu->menu) {
    menu_t *menu = active_app_with_menu->menu;
    topbar_draw_menu(ctx, 172, menu->title, menu);
  }

  /* 3. Clock (Right) - Real Time from RTC */
  int clock_x = logical_width - 80;

  uint8_t hours = rtc_get_hours();
  uint8_t minutes = rtc_get_minutes();

  clock_drawn = hours * 60 + minutes;

  char clock_str[6];
  clock_str[0] = '0' + (hours / 10);
  clock_str[1] = '0' + (hours % 10);
  clock_str[2] = ':';
  clock_str[3] = '0' + (minutes / 10);
  clock_str[4] = '0' + (minutes % 10);
  clock_str[5] = '\0';

  font_draw_text(ctx, clock_x, 6, clock_str, 14, UI_PAPER);
}

bool topbar_clock_changed(void) {
  return rtc_get_hours() * 60 + rtc_get_minutes() != clock_drawn;
}

bool topbar_handle_event(event_t *event) {
  if (!event)
    return false;

  if (event->type == EVENT_MOUSE_CLICK || event->type == EVENT_MOUSE_RELEASE) {
    int my = event->data.mouse.y;

    if (my < TOPBAR_HEIGHT) {
      if (event->type == EVENT_MOUSE_CLICK) {
        int x = event->data.mouse.x;
        int origin = 12;
        menu_t *menu = NULL;

        if (x >= 10 && x < 10 + TOPBAR_ITEM_WIDTH) {
          menu = menu_gemos;
        } else if (x >= 90 && x < 90 + TOPBAR_ITEM_WIDTH) {
          menu = menu_apps;
          origin = 92;
        } else if (x >= 170 && x < 170 + TOPBAR_ITEM_WIDTH &&
                   active_app_with_menu) {
          menu = active_app_with_menu->menu;
          origin = 172;
        }
        if (menu && !menu->visible) {
          menu_show(menu, origin, TOPBAR_HEIGHT);
        } else {
          menu_hide();
        }
      }
      return true; /* CONSUME EVENT */
    }
  }

  return false;
}
