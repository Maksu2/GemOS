/**
 * About GemOS Application
 *
 * Reference implementation for GemOS native applications.
 * Shows system information in a simple window.
 */

#include "about.h"
#include "../../drivers/serial.h"
#include "../../kernel/app/app.h"
#include "../../kernel/app/app_manager.h"
#include "../../kernel/gfx/font/font.h"
#include "../../kernel/gfx/icons.h"
#include "../../kernel/gfx/primitives.h"
#include "../../kernel/gui/wm/wm.h"
#include "../../kernel/include/heap.h"
#include "../../kernel/ui/menu.h"

/* Application state */
static app_t about_app;
static menu_t *about_menu = NULL;

/* Menu Actions */
static void action_close_about(void) {
  serial_print("[About] Close action\n");
  window_t *win = wm_get_focused_window();
  if (win && win->app == &about_app) {
    wm_remove_window(win);
  }
}

/* Render callback */
static void about_render(window_t *win) {
  int x = win->client_rect.x;
  int y = win->client_rect.y;
  int w = win->client_rect.w;
  int h = win->client_rect.h;

  /* Background - Light gray */
  gfx_fill_rect(&win->ctx, x, y, w, h, 0xF0F0F0);

  /* Content Layout */
  int padding = 20;
  int text_x = x + padding;
  int text_y = y + padding;
  int line_height = 24;

  /* Title */
  font_draw_text(&win->ctx, text_x, text_y, "GemOS", 24, 0x000000);
  text_y += line_height + 10;

  /* Version */
  font_draw_text(&win->ctx, text_x, text_y, "Version 0.1-dev", 14, 0x404040);
  text_y += line_height;

  /* Separator line */
  text_y += 10;
  gfx_fill_rect(&win->ctx, text_x, text_y, w - 2 * padding, 1, 0xC0C0C0);
  text_y += 20;

  /* Description */
  font_draw_text(&win->ctx, text_x, text_y, "A hobbyist operating system", 12,
                 0x404040);
  text_y += line_height - 4;

  font_draw_text(&win->ctx, text_x, text_y, "written from scratch in C.", 12,
                 0x404040);
  text_y += line_height + 10;

  /* Author / Year */
  font_draw_text(&win->ctx, text_x, text_y, "2026", 12, 0x606060);
}

/* Event handler */
static void about_handle_event(window_t *win, event_t *ev) {
  (void)win;
  (void)ev;
  /* About app has no interactive elements */
}

/* Open callback - creates window */
static void about_open(void) {
  /* Allocate window */
  window_t *win = (window_t *)kalloc(sizeof(window_t));
  if (!win)
    return;

  /* Initialize window: centered, modest size */
  window_init(win, 200, 120, 320, 220);
  win->app = &about_app;

  wm_add_window(win);
  serial_print("[About] Window opened\n");
}

/* Init callback */
static void about_init(void) {
  about_app.name = "About GemOS";
  about_app.icon = &icon_about;
  about_app.init = NULL; /* Already initialized */
  about_app.open = about_open;
  about_app.render = about_render;
  about_app.handle_event = about_handle_event;
  about_app.close = NULL;

  /* Create application menu */
  about_menu = menu_create();
  if (about_menu) {
    menu_add_item(about_menu, "Close", action_close_about);
  }
  about_app.menu = about_menu;
}

/* Public registration function */
void about_register(void) {
  about_init();
  app_register(&about_app);
}
