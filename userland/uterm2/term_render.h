#ifndef UTERM2_TERM_RENDER_H
#define UTERM2_TERM_RENDER_H

#include "term_model.h"

#include <gemos/console_abi.h>

#include <stdint.h>

void term_render_build_frame(term_model_t *model, int32_t pid,
                             gemos_console_frame_t *frame,
                             gemos_console_cell_t *cells);

#endif /* UTERM2_TERM_RENDER_H */
