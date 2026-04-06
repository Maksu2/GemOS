#include "../../drivers/serial.h"
#include "../../kernel/app/app.h"
#include "../../kernel/app/app_manager.h"
#include "../../kernel/gfx/font/font.h"
#include "../../kernel/gfx/primitives.h"
#include "../../kernel/gui/wm/wm.h"
#include "../../kernel/include/heap.h"
#include "../../kernel/ui/menu.h"

static app_t testapp;
static menu_t *test_menu = NULL;

void action_test_hello(void) { serial_print("[TestApp] Hello Action!\n"); }

void test_render(window_t *win) {
  /* Draw Content in Client Rect */
  /* Coordinates are logical, typically relative to window or screen?
     wm_render_window sets clip to client_rect.
     Coordinates passed to primitives are usually GLOBAL logical.
     So we need to draw at win->client_rect.x, etc.
  */

  int x = win->client_rect.x;
  int y = win->client_rect.y;
  int w = win->client_rect.w;
  int h = win->client_rect.h;

  /* Background */
  gfx_fill_rect(&win->ctx, x, y, w, h, 0xFFFFFF); // White BG

  /* Text */
  font_draw_text(&win->ctx, x + 20, y + 20, "Hello from Test App!", 16,
                 0x000000);
  font_draw_text(&win->ctx, x + 20, y + 50, "This is a userspace-like module.",
                 12, 0x444444);
  font_draw_text(&win->ctx, x + 20, y + 70, "Managed by App Framework.", 12,
                 0x444444);

  /* Interactive Element Visualization */
  gfx_draw_rect(&win->ctx, x + 20, y + 100, 100, 30, 0x0000FF);
  font_draw_text(&win->ctx, x + 30, y + 108, "Button?", 12, 0x0000FF);
}

void test_handle_event(window_t *win, event_t *ev) {
  (void)win;
  if (ev->type == EVENT_KEY_PRESS) {
    serial_print("[TestApp] Key Pressed\n");
  }
}

void test_open(void) {
  /* Allocate Window */
  window_t *win = (window_t *)kalloc(sizeof(window_t));
  if (!win)
    return;

  window_init(win, 150, 150, 400, 300);
  win->app = &testapp; // Bind app

  wm_add_window(win);
  serial_print("[TestApp] Window Opened\n");
}

void test_init(void) {
  testapp.name = "Test App";
  testapp.open = test_open;
  testapp.render = test_render;
  testapp.handle_event = test_handle_event;
  testapp.close = NULL;

  /* Create Menu */
  test_menu = menu_create();
  if (test_menu) {
    menu_add_item(test_menu, "Print Hello", action_test_hello);
    menu_add_item(test_menu, "Close App", NULL); // Placeholder
  }
  testapp.menu = test_menu;
}

/* Public entry point to register */
/* We need a header or allow kernel to call this? */
/* For now, kernel main will call testapp_register or we expose app struct? */
/* Better: expose a register function */
void testapp_register(void) {
  test_init();
  app_register(&testapp);
}
