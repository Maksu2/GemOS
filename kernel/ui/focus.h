#ifndef UI_FOCUS_H
#define UI_FOCUS_H

#include "../app/app.h"
#include "../gui/window/window.h"

typedef enum { FOCUS_DESKTOP, FOCUS_WINDOW } focus_type_t;

typedef struct {
  focus_type_t type;
  window_t *window; // NULL if desktop
  app_t *app;       // NULL if desktop or window has no app
} focus_state_t;

/* Called by WM when focus changes */
void ui_on_focus_changed(focus_state_t *state);

#endif
