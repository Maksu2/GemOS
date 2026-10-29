#ifndef WM_H
#define WM_H

#include "../../include/event.h"
#include "../window/window.h"

/* Initialize the Window Manager with the main screen context */
void wm_init(gfx_context_t *ctx);

/* The part of the screen windows use, in logical pixels: between the
 * topbar and the dock. */
rect_t wm_work_area(void);

/* Window Management */
/* Puts the window inside the work area where it fits, and moves it off
 * another window's corner (cascade), then logs "[WM] Placed ...". */
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
