#include "desktop.h"
#include "../gfx/primitives.h"
#include "../ui/ui_scale.h"

/* Wallpaper gradient colors */
#define WALLPAPER_TOP 0x1A1A3E    /* Dark navy */
#define WALLPAPER_BOTTOM 0x0A4D4D /* Deep teal */

void desktop_init(void) { /* Nothing to init */ }

void desktop_draw(gfx_context_t *ctx) {
  /* Gradient fill: writes each row with its interpolated color.
   * This is FASTER than a cached memcpy because:
   *   - Gradient: 8MB writes + trivial per-row math
   *   - Cache:    8MB reads + 8MB writes = 16MB memory ops
   * The per-row color computation is negligible vs. the memory savings. */
  int logical_w = (int)(ctx->width / ui_scale);
  int logical_h = (int)(ctx->height / ui_scale);

  gfx_gradient_rect_v(ctx, 0, 0, logical_w, logical_h, WALLPAPER_TOP,
                      WALLPAPER_BOTTOM);
}
