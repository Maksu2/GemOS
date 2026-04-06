#ifndef DESKTOP_H
#define DESKTOP_H

#include "../gfx/context.h"

/* Initialize desktop (load wallpaper etc. later) */
void desktop_init(void);

/* Draw the desktop background */
void desktop_draw(gfx_context_t *ctx);

#endif /* DESKTOP_H */
