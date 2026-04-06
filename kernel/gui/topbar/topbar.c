#include "topbar.h"
#include "../../../drivers/rtc.h"
#include "../../../drivers/serial.h"
#include "../../app/app.h"
#include "../../app/app_manager.h"
#include "../../gfx/font/font.h"
#include "../../gfx/primitives.h"
#include "../../ui/focus.h"
#include "../../ui/menu.h"
#include "../../ui/ui_scale.h"
#include "../wm/wm.h"
#include <stddef.h>

/* Top Bar Colors */
#define COLOR_TOPBAR_BG 0x202020 /* Dark Gray */
#define COLOR_TEXT 0xFFFFFF      /* White */
#define COLOR_HIGHLIGHT 0x505050 /* Lighter Gray for hover/click */

/* Menus */
static menu_t *menu_gemos = NULL;
static menu_t *menu_apps = NULL;

static app_t *active_app_with_menu = NULL; /* Cached focus state */

void action_about(void) { serial_print("[ACTION] About GemOS\n"); }
void action_exit(void) { serial_print("[ACTION] Shutdown Request\n"); }
void action_open_testapp(void) { app_open("Test App"); }
void action_open_about(void) { app_open("About GemOS"); }
void action_open_terminal(void) { app_open("Terminal"); }
void action_open_textedit(void) { app_open("Text Editor"); }
void action_open_explorer(void) { app_open("File Explorer"); }

void topbar_init(void) {
  serial_print("[TOPBAR] Initialized\n");

  menu_init();

  /* GemOS Menu */
  menu_gemos = menu_create();
  if (menu_gemos) {
    menu_add_item(menu_gemos, "About GemOS", action_open_about);
    menu_add_item(menu_gemos, "System Info", NULL);
    menu_add_item(menu_gemos, "Shutdown", action_exit);
  }

  /* Apps Menu */
  menu_apps = menu_create();
  if (menu_apps) {
    menu_add_item(menu_apps, "File Explorer", action_open_explorer);
    menu_add_item(menu_apps, "Terminal", action_open_terminal);
    menu_add_item(menu_apps, "Text Editor", action_open_textedit);
  }
}

void topbar_on_focus_changed(focus_state_t *state) {
  if (!state)
    return;

  if (state->type == FOCUS_WINDOW && state->app && state->app->menu) {
    active_app_with_menu = state->app;
  } else {
    active_app_with_menu = NULL;
  }

  if (menu_is_active()) {
    menu_hide();
  }
}

void topbar_render(gfx_context_t *ctx) {
  int logical_width = (int)(ctx->width / ui_scale);

  /* 1. Background - dark semi-transparent bar */
  gfx_fill_rect(ctx, 0, 0, logical_width, TOPBAR_HEIGHT, COLOR_TOPBAR_BG);

  /* Subtle bottom line */
  gfx_fill_rect(ctx, 0, TOPBAR_HEIGHT - 1, logical_width, 1, 0x404040);

  /* 2. Menu Items (Left) */
  int x_offset = 12;
  int spacing = 80;

  /* "GemOS" - Bold, slightly larger */
  font_draw_text(ctx, x_offset, 6, "GemOS", 16, 0xFFFFFF);
  x_offset += spacing;

  /* "Apps" */
  font_draw_text(ctx, x_offset, 8, "Apps", 14, 0xCCCCCC);
  x_offset += spacing;

  /* Context Aware Menus */
  if (active_app_with_menu && active_app_with_menu->menu) {
    font_draw_text(ctx, x_offset, 8, active_app_with_menu->menu->title, 14,
                   0xCCCCCC);
  }

  /* 3. Clock (Right) - Real Time from RTC */
  int clock_x = logical_width - 80;

  uint8_t hours = rtc_get_hours();
  uint8_t minutes = rtc_get_minutes();

  char clock_str[6];
  clock_str[0] = '0' + (hours / 10);
  clock_str[1] = '0' + (hours % 10);
  clock_str[2] = ':';
  clock_str[3] = '0' + (minutes / 10);
  clock_str[4] = '0' + (minutes % 10);
  clock_str[5] = '\0';

  font_draw_text(ctx, clock_x, 8, clock_str, 14, 0xFFFFFF);
}

bool topbar_handle_event(event_t *event) {
  if (!event)
    return false;

  if (event->type == EVENT_MOUSE_CLICK || event->type == EVENT_MOUSE_RELEASE) {
    int my = event->data.mouse.y;

    if (my < TOPBAR_HEIGHT) {
      if (event->type == EVENT_MOUSE_CLICK) {
        int x = event->data.mouse.x;

        if (x > 10 && x < 80) {
          if (menu_is_active())
            menu_hide();
          else
            menu_show(menu_gemos, 12, TOPBAR_HEIGHT);
        } else if (x > 90 && x < 160) {
          if (menu_is_active())
            menu_hide();
          else
            menu_show(menu_apps, 92, TOPBAR_HEIGHT);
        } else if (x > 170 && x < 240) {
          if (active_app_with_menu && active_app_with_menu->menu) {
            if (menu_is_active())
              menu_hide();
            else
              menu_show(active_app_with_menu->menu, 172, TOPBAR_HEIGHT);
          }
        }
      }
      return true; /* CONSUME EVENT */
    }
  }

  return false;
}
