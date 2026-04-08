#ifndef UTERM2_TERM_MODEL_H
#define UTERM2_TERM_MODEL_H

#include "term_theme.h"

#include <gemos/console_abi.h>

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint16_t length;
  uint8_t style;
  char text[UTERM2_COLS + 1U];
} term_line_t;

typedef struct {
  term_line_t lines[UTERM2_SCROLLBACK_LINES];
  uint16_t line_head;
  uint16_t line_count;
  char input[UTERM2_INPUT_MAX];
  uint16_t input_length;
  uint16_t scroll_offset;
  uint8_t dirty;
  uint8_t should_exit;
  char history[UTERM2_HISTORY_MAX][UTERM2_INPUT_MAX];
  uint16_t history_count;
  int16_t history_index;
  char history_draft[UTERM2_INPUT_MAX];
  uint16_t history_draft_length;
} term_model_t;

void term_model_init(term_model_t *model);
void term_model_mark_clean(term_model_t *model);
void term_model_append_text(term_model_t *model, uint8_t style,
                            const char *text);
void term_model_append_command(term_model_t *model, const char *command);
void term_model_clear_transcript(term_model_t *model);
int term_model_push_input_char(term_model_t *model, char ch);
int term_model_backspace(term_model_t *model);
void term_model_take_input(term_model_t *model, char *buffer, size_t capacity);
void term_model_history_commit(term_model_t *model, const char *command);
int term_model_history_prev(term_model_t *model);
int term_model_history_next(term_model_t *model);

#endif /* UTERM2_TERM_MODEL_H */
