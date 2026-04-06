/**
 * Terminal Application (v1 - Log Viewer)
 *
 * Displays kernel logs in real-time.
 * Read-only viewer, no input capability.
 *
 * Performance: tracks last known line count to skip re-rendering
 * when log content hasn't changed. TrueType font rendering is expensive.
 */

#include "terminal.h"
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
static app_t terminal_app;
static menu_t *terminal_menu = NULL;

/* Display configuration */
#define TERM_BG_COLOR 0x1E1E1E   /* Dark background */
#define TERM_TEXT_COLOR 0xCCCCCC /* Light gray text */
#define TERM_FONT_SIZE 11
#define TERM_LINE_HEIGHT 14
#define TERM_PADDING 8

/* Render state: avoid re-rendering when nothing changed */
static int last_rendered_line_count = -1;

/* Menu Actions */
static void action_close_terminal(void) {
  window_t *win = wm_get_focused_window();
  if (win && win->app == &terminal_app) {
    wm_remove_window(win);
    last_rendered_line_count = -1; /* Reset on close */
  }
}

/* Render callback - draws log lines */
static void terminal_render(window_t *win) {
  int x = win->client_rect.x;
  int y = win->client_rect.y;
  int w = win->client_rect.w;
  int h = win->client_rect.h;

  int total_lines = klog_get_line_count();

  /* Skip if content unchanged and window hasn't moved */
  if (total_lines == last_rendered_line_count) {
    /* Still need to draw background since screen is redrawn each frame */
    gfx_fill_rect(&win->ctx, x, y, w, h, TERM_BG_COLOR);

    /* Re-render text since the framebuffer was cleared */
    /* (with page flipping, we render to a new page each frame) */
  } else {
    last_rendered_line_count = total_lines;
  }

  /* Background */
  gfx_fill_rect(&win->ctx, x, y, w, h, TERM_BG_COLOR);

  /* Calculate how many lines fit */
  int visible_lines = (h - 2 * TERM_PADDING) / TERM_LINE_HEIGHT;

  /* Start from bottom (most recent logs) */
  int start_line = 0;
  if (total_lines > visible_lines) {
    start_line = total_lines - visible_lines;
  }

  /* Draw lines */
  int text_x = x + TERM_PADDING;
  int text_y = y + TERM_PADDING;

  for (int i = 0; i < visible_lines && (start_line + i) < total_lines; i++) {
    const char *line = klog_get_line(start_line + i);
    font_draw_text(&win->ctx, text_x, text_y, line, TERM_FONT_SIZE,
                   TERM_TEXT_COLOR);
    text_y += TERM_LINE_HEIGHT;
  }
}

/* Event handler - Terminal v1 has no input */
static void terminal_handle_event(window_t *win, event_t *ev) {
  (void)win;
  (void)ev;
}

/* Open callback - creates window */
static void terminal_open(void) {
  window_t *win = (window_t *)kalloc(sizeof(window_t));
  if (!win)
    return;

  /* Larger window for log viewing */
  window_init(win, 80, 80, 500, 350);
  win->app = &terminal_app;
  last_rendered_line_count = -1; /* Force initial render */

  wm_add_window(win);
  serial_print("[Terminal] Window opened\n");
}

/* Init callback */
static void terminal_init(void) {
  terminal_app.name = "Log Viewer";
  terminal_app.icon = &icon_terminal;
  terminal_app.init = NULL;
  terminal_app.open = terminal_open;
  terminal_app.render = terminal_render;
  terminal_app.handle_event = terminal_handle_event;
  terminal_app.close = NULL;

  /* Create application menu */
  terminal_menu = menu_create();
  if (terminal_menu) {
    menu_add_item(terminal_menu, "Close", action_close_terminal);
  }
  terminal_app.menu = terminal_menu;
}

/* Public registration function */
void terminal_register(void) {
  terminal_init();
  app_register(&terminal_app);
}
