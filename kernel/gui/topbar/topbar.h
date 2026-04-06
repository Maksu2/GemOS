#ifndef TOPBAR_H
#define TOPBAR_H

#include "../../gfx/context.h"
#include "../../include/event.h"
#include <stdbool.h>

#define TOPBAR_HEIGHT 28

/* Initialize Top Bar state */
void topbar_init(void);

/* Render Top Bar to the given context */
void topbar_render(gfx_context_t *ctx);

/*
 * Handle input event.
 * Returns true if the event was consumed by the Top Bar.
 * Returns false if the event should act on the Desktop/Window Manager.
 */
bool topbar_handle_event(event_t *event);

#endif /* TOPBAR_H */
