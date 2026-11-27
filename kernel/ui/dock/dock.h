#ifndef DOCK_H
#define DOCK_H

#include "../../gfx/context.h"
#include "../../gui/window/window.h" // For window_t
#include "../../include/event.h"
#include <stdbool.h>

#define MAX_DOCK_ITEMS 16
#define DOCK_HEIGHT 32

/* Global API */
void dock_init(void);

/* Notifications from WM */
void dock_register_window(window_t *win);
void dock_unregister_window(window_t *win);

/* Input & Rendering */
bool dock_handle_event(event_t *event);
void dock_render(gfx_context_t *ctx);

#endif
