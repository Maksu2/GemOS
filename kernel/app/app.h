#ifndef APP_H
#define APP_H

#include "../gfx/primitives.h" /* For icon_t */
#include "../gui/window/window.h"
#include "../include/event.h"
#include "../ui/menu.h"

typedef struct app {
  const char *name;
  const icon_t *icon; /* Application icon (32x32 RGBA) - REQUIRED */
  menu_t *menu;

  /* Callbacks */
  void (*init)(void);
  void (*open)(void);                  // Creates the window
  void (*open_file)(const char *path); // Opens app with file
  void (*render)(window_t *win);
  void (*handle_event)(window_t *win, event_t *ev);
  int (*request_close)(window_t *win);
  void (*close)(window_t *win);
} app_t;

#endif
