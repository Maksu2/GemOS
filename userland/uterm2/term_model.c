#include "term_model.h"

#include "term_util.h"

#include <stddef.h>

static uint16_t term_model_line_slot(const term_model_t *model,
                                     uint16_t logical_index) {
  return (uint16_t)((model->line_head + logical_index) % UTERM2_SCROLLBACK_LINES);
}

static void term_model_mark_dirty(term_model_t *model) {
  if (model != 0) {
    model->dirty = 1;
  }
}

static void term_model_store_line(term_model_t *model, uint8_t style,
                                  const char *text, size_t length) {
  uint16_t logical_index;
  uint16_t slot;
  size_t copy_length = length;

  if (model == 0 || text == 0) {
    return;
  }

  if (copy_length > UTERM2_COLS) {
    copy_length = UTERM2_COLS;
  }

  if (model->line_count < UTERM2_SCROLLBACK_LINES) {
    logical_index = model->line_count;
    model->line_count++;
  } else {
    model->line_head = (uint16_t)((model->line_head + 1U) % UTERM2_SCROLLBACK_LINES);
    logical_index = (uint16_t)(UTERM2_SCROLLBACK_LINES - 1U);
  }

  slot = term_model_line_slot(model, logical_index);
  term_copy_n(model->lines[slot].text, sizeof(model->lines[slot].text), text,
              copy_length);
  model->lines[slot].length = (uint16_t)term_strlen(model->lines[slot].text);
  model->lines[slot].style = style;
}

static void term_model_history_reset_browse(term_model_t *model) {
  if (model == 0) {
    return;
  }

  model->history_index = -1;
  model->history_draft[0] = '\0';
  model->history_draft_length = 0;
}

void term_model_init(term_model_t *model) {
  size_t row;

  if (model == 0) {
    return;
  }

  for (row = 0; row < UTERM2_SCROLLBACK_LINES; ++row) {
    model->lines[row].length = 0;
    model->lines[row].style = UTERM2_TEXT_STYLE;
    model->lines[row].text[0] = '\0';
  }

  for (row = 0; row < UTERM2_HISTORY_MAX; ++row) {
    model->history[row][0] = '\0';
  }

  model->line_head = 0;
  model->line_count = 0;
  model->input[0] = '\0';
  model->input_length = 0;
  model->scroll_offset = 0;
  model->dirty = 1;
  model->should_exit = 0;
  model->history_count = 0;
  model->history_index = -1;
  model->history_draft[0] = '\0';
  model->history_draft_length = 0;
}

void term_model_mark_clean(term_model_t *model) {
  if (model != 0) {
    model->dirty = 0;
  }
}

void term_model_append_text(term_model_t *model, uint8_t style,
                            const char *text) {
  size_t start = 0;
  size_t length;

  if (model == 0 || text == 0) {
    return;
  }

  length = term_strlen(text);
  if (length == 0) {
    term_model_store_line(model, style, "", 0);
    term_model_mark_dirty(model);
    return;
  }

  while (start < length) {
    size_t chunk = 0;

    while (start + chunk < length && text[start + chunk] != '\n' &&
           chunk < UTERM2_COLS) {
      chunk++;
    }

    term_model_store_line(model, style, text + start, chunk);
    start += chunk;

    if (start < length && text[start] == '\n') {
      start++;
      if (chunk == 0) {
        term_model_store_line(model, style, "", 0);
      }
    }
  }

  term_model_mark_dirty(model);
}

void term_model_append_command(term_model_t *model, const char *command) {
  char line[UTERM2_COLS + UTERM2_INPUT_MAX];

  if (model == 0 || command == 0) {
    return;
  }

  line[0] = '\0';
  term_copy(line, sizeof(line), UTERM2_PROMPT);
  term_copy(line + term_strlen(line), sizeof(line) - term_strlen(line), command);
  term_model_append_text(model, UTERM2_PROMPT_STYLE, line);
}

void term_model_clear_transcript(term_model_t *model) {
  if (model == 0) {
    return;
  }

  model->line_head = 0;
  model->line_count = 0;
  model->scroll_offset = 0;
  term_model_mark_dirty(model);
}

int term_model_push_input_char(term_model_t *model, char ch) {
  if (model == 0 || ch < 0x20 || ch > 0x7E) {
    return 0;
  }
  if (model->input_length + 1U >= UTERM2_INPUT_MAX) {
    return 0;
  }

  model->input[model->input_length++] = ch;
  model->input[model->input_length] = '\0';
  term_model_mark_dirty(model);
  return 1;
}

int term_model_backspace(term_model_t *model) {
  if (model == 0 || model->input_length == 0U) {
    return 0;
  }

  model->input_length--;
  model->input[model->input_length] = '\0';
  term_model_mark_dirty(model);
  return 1;
}

void term_model_take_input(term_model_t *model, char *buffer, size_t capacity) {
  if (model == 0 || buffer == 0 || capacity == 0U) {
    return;
  }

  term_copy(buffer, capacity, model->input);
  model->input_length = 0;
  model->input[0] = '\0';
  term_model_history_reset_browse(model);
  term_model_mark_dirty(model);
}

void term_model_history_commit(term_model_t *model, const char *command) {
  uint16_t index;

  if (model == 0 || command == 0 || command[0] == '\0') {
    return;
  }

  if (model->history_count > 0U &&
      term_streq(model->history[model->history_count - 1U], command)) {
    term_model_history_reset_browse(model);
    return;
  }

  if (model->history_count < UTERM2_HISTORY_MAX) {
    index = model->history_count++;
  } else {
    uint16_t i;

    for (i = 1U; i < UTERM2_HISTORY_MAX; ++i) {
      term_copy(model->history[i - 1U], sizeof(model->history[0]),
                model->history[i]);
    }
    index = (uint16_t)(UTERM2_HISTORY_MAX - 1U);
  }

  term_copy(model->history[index], sizeof(model->history[index]), command);
  term_model_history_reset_browse(model);
}

int term_model_history_prev(term_model_t *model) {
  if (model == 0 || model->history_count == 0U) {
    return 0;
  }

  if (model->history_index < 0) {
    term_copy(model->history_draft, sizeof(model->history_draft), model->input);
    model->history_draft_length = model->input_length;
    model->history_index = (int16_t)(model->history_count - 1U);
  } else if (model->history_index > 0) {
    model->history_index--;
  }

  term_copy(model->input, sizeof(model->input),
            model->history[(uint16_t)model->history_index]);
  model->input_length = (uint16_t)term_strlen(model->input);
  term_model_mark_dirty(model);
  return 1;
}

int term_model_history_next(term_model_t *model) {
  if (model == 0 || model->history_index < 0) {
    return 0;
  }

  if ((uint16_t)(model->history_index + 1) < model->history_count) {
    model->history_index++;
    term_copy(model->input, sizeof(model->input),
              model->history[(uint16_t)model->history_index]);
  } else {
    model->history_index = -1;
    term_copy(model->input, sizeof(model->input), model->history_draft);
  }

  model->input_length = (uint16_t)term_strlen(model->input);
  term_model_mark_dirty(model);
  return 1;
}
