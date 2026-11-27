#include "desktop.h"
#include "../gfx/primitives.h"
#include "../ui/theme.h"
#include "../ui/ui_scale.h"

void desktop_draw(gfx_context_t *ctx) {
  gfx_fill_rect(ctx, 0, 0, (int)(ctx->width / ui_scale),
                (int)(ctx->height / ui_scale), UI_DESKTOP);
}
