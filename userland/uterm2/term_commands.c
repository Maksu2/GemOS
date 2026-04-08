#include "term_commands.h"

#include "term_util.h"

#include <gemos/user_api.h>

#include <stddef.h>
#include <stdint.h>

static void term_append_u32_line(term_model_t *model, const char *label,
                                 uint32_t value) {
  char number[16];
  char line[UTERM2_COLS + 1U];
  size_t label_length;
  size_t number_length;

  term_u32_to_text(value, number, sizeof(number));
  label_length = term_strlen(label);
  number_length = term_strlen(number);

  line[0] = '\0';
  term_copy(line, sizeof(line), label);
  term_copy_n(line + label_length, sizeof(line) - label_length, number,
              number_length);
  term_model_append_text(model, UTERM2_TEXT_STYLE, line);
}

void term_commands_execute(term_model_t *model, const char *command) {
  if (model == 0 || command == 0) {
    return;
  }

  if (command[0] == '\0') {
    return;
  }

  term_model_history_commit(model, command);

  if (term_streq(command, "help")) {
    term_model_append_text(model, UTERM2_HINT_STYLE,
                           "help  echo  clear  exit  pid  ticks");
  } else if (term_streq(command, "echo")) {
    term_model_append_text(model, UTERM2_TEXT_STYLE, "");
  } else if (term_starts_with(command, "echo ")) {
    term_model_append_text(model, UTERM2_TEXT_STYLE, command + 5);
  } else if (term_streq(command, "clear")) {
    term_model_clear_transcript(model);
  } else if (term_streq(command, "exit")) {
    model->should_exit = 1;
    model->dirty = 1;
  } else if (term_streq(command, "pid")) {
    term_append_u32_line(model, "pid=", (uint32_t)gemos_getpid());
  } else if (term_streq(command, "ticks")) {
    term_append_u32_line(model, "ticks=", (uint32_t)gemos_ticks_ms());
  } else {
    term_model_append_text(model, UTERM2_ERROR_STYLE, "unknown command");
  }
}
