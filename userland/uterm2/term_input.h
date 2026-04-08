#ifndef UTERM2_TERM_INPUT_H
#define UTERM2_TERM_INPUT_H

#include "term_model.h"

#include <gemos/console_abi.h>

#include <stddef.h>

typedef enum {
  TERM_INPUT_NONE = 0,
  TERM_INPUT_SUBMIT = 1,
} term_input_action_t;

term_input_action_t term_input_handle_event(term_model_t *model,
                                            const gemos_console_event_t *event,
                                            char *command_buffer,
                                            size_t command_capacity);

#endif /* UTERM2_TERM_INPUT_H */
