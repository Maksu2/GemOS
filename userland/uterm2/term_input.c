#include "term_input.h"

#include "term_util.h"

term_input_action_t term_input_handle_event(term_model_t *model,
                                            const gemos_console_event_t *event,
                                            char *command_buffer,
                                            size_t command_capacity) {
  char ch;

  if (model == 0 || event == 0) {
    return TERM_INPUT_NONE;
  }
  if (event->type == GEMOS_CONSOLE_EVENT_CLOSE_REQUEST) {
    model->should_exit = 1;
    model->dirty = 1;
    return TERM_INPUT_NONE;
  }
  if (event->type != GEMOS_CONSOLE_EVENT_KEY) {
    return TERM_INPUT_NONE;
  }

  if (event->character == GEMOS_KEY_ENTER) {
    term_model_take_input(model, command_buffer, command_capacity);
    return TERM_INPUT_SUBMIT;
  }
  if (event->character == GEMOS_KEY_BACKSPACE) {
    term_model_backspace(model);
    return TERM_INPUT_NONE;
  }
  if (event->character == GEMOS_KEY_ESC) {
    model->should_exit = 1;
    model->dirty = 1;
    return TERM_INPUT_NONE;
  }
  if (event->character == GEMOS_KEY_UP) {
    term_model_history_prev(model);
    return TERM_INPUT_NONE;
  }
  if (event->character == GEMOS_KEY_DOWN) {
    term_model_history_next(model);
    return TERM_INPUT_NONE;
  }

  ch = (char)(uint8_t)event->character;
  if (ch >= 0x20 && ch <= 0x7E) {
    term_model_push_input_char(model, ch);
  }

  return TERM_INPUT_NONE;
}
