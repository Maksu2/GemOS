#ifndef GEMOS_USERLAND_TEXTEDIT_RENDER_H
#define GEMOS_USERLAND_TEXTEDIT_RENDER_H

#include "textedit_state.h"

#include <gemos/console_abi.h>

void utextedit_render_build_frame(const utextedit_state_t *state,
                                  gemos_console_frame_t *frame,
                                  gemos_console_cell_t *cells);

#endif /* GEMOS_USERLAND_TEXTEDIT_RENDER_H */
