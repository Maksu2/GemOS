#ifndef WM_H
#define WM_H

#include "../../include/event.h"
#include "../window/window.h"

/* Initialize the Window Manager with the main screen context */
void wm_init(gfx_context_t *ctx);

/* Window Management */
void wm_add_window(window_t *win);
void wm_remove_window(window_t *win);

/* Focus */
void wm_focus_window(window_t *win); // Also brings to front
window_t *wm_get_focused_window(void);

/* Rendering */
// Renders all windows in Z-order (painter's algorithm: back to front)
void wm_render_all(void);

/* Input */
// Determine target window for event and dispatch
void wm_handle_event(event_t *event);

#endif /* WM_H */
