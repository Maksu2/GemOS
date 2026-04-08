#ifndef GEMOS_USERLAND_ABOUT_RENDER_H
#define GEMOS_USERLAND_ABOUT_RENDER_H

#include "about_state.h"

#include <gemos/console_abi.h>

void about_render_build_frame(const about_state_t *state,
                              gemos_console_frame_t *frame,
                              gemos_console_cell_t *cells);

#endif /* GEMOS_USERLAND_ABOUT_RENDER_H */
