#ifndef WINDOW_H
#define WINDOW_H

#include "types.h"

// Forward declaration
typedef struct Window Window;

// Callback type for window updates (e.g. click handling)
typedef void (*WindowPaintCallback)(Window *win);
typedef void (*WindowKeyCallback)(Window *win, char c);

typedef struct Window {
  int x, y;
  int width, height;
  char *title;
  uint32_t bg_color;
  uint32_t title_color;

  // Linked list (Z-Order: Head = Top, Tail = Bottom)
  struct Window *next;

  WindowPaintCallback on_paint;
  WindowKeyCallback on_key;
  void (*on_click)(struct Window *win, int x, int y);
  void (*on_mouse_move)(struct Window *win, int x, int y, int b);

  // App specific data / For internal state (e.g. minimized)
  void *extra_data;
} Window;

// Theme Globals
extern uint32_t theme_desktop;
extern uint32_t theme_window_bg;
extern uint32_t theme_title_bg;
extern uint32_t theme_text;

void init_window_manager();
Window *create_window(int x, int y, int w, int h, char *title);
void desktop_paint(); // Main paint routine
void wm_handle_mouse(int x, int y, int buttons);
void wm_handle_keyboard(char c);

extern Window *focused_window;

#endif
