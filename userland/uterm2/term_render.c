#include "../common/text_surface.h"
#include "term_render.h"

#include "../common/format.h"

#include <stdint.h>

static void term_render_status(term_model_t *model, int32_t pid,
                               gemos_console_cell_t *cells) {
  char pid_text[16];
  char status_right[24];

  (void)model;
  gemos_u32_to_text((uint32_t)pid, pid_text, sizeof(pid_text));
  gemos_copy(status_right, sizeof(status_right), UTERM2_HEADER_HINT);
  gemos_copy(status_right + gemos_strlen(status_right),
             sizeof(status_right) - gemos_strlen(status_right), pid_text);

  gemos_text_surface_fill_row(cells, UTERM2_COLS, UTERM2_ROWS,
                              UTERM2_HEADER_ROW, UTERM2_STATUS_STYLE, ' ');
  gemos_text_surface_write_text(cells, UTERM2_COLS, UTERM2_ROWS,
                                UTERM2_HEADER_ROW, UTERM2_CONTENT_COL,
                                UTERM2_STATUS_STYLE, UTERM2_TITLE);
  gemos_text_surface_write_right(cells, UTERM2_COLS, UTERM2_ROWS,
                                 UTERM2_HEADER_ROW, UTERM2_CONTENT_COL,
                                 UTERM2_STATUS_STYLE, status_right);
}

static void term_render_transcript(term_model_t *model,
                                   gemos_console_cell_t *cells) {
  uint32_t visible_rows = UTERM2_FOOTER_ROW - UTERM2_TRANSCRIPT_ROW;
  uint32_t start = 0;
  uint32_t row = 0;

  if (model->line_count > visible_rows) {
    start = model->line_count - visible_rows;
  }
  if (model->scroll_offset > start) {
    model->scroll_offset = (uint16_t)start;
  }
  start -= model->scroll_offset;

  while (row < visible_rows && start + row < model->line_count) {
    uint16_t slot =
        (uint16_t)((model->line_head + start + row) % UTERM2_SCROLLBACK_LINES);
    gemos_text_surface_write_text(cells, UTERM2_COLS, UTERM2_ROWS,
                                  UTERM2_TRANSCRIPT_ROW + row,
                                  UTERM2_CONTENT_COL, model->lines[slot].style,
                                  model->lines[slot].text);
    row++;
  }
}

static void term_render_footer(gemos_console_cell_t *cells) {
  gemos_text_surface_fill_row(cells, UTERM2_COLS, UTERM2_ROWS, UTERM2_FOOTER_ROW,
                              UTERM2_HINT_STYLE, ' ');
  gemos_text_surface_write_text(cells, UTERM2_COLS, UTERM2_ROWS,
                                UTERM2_FOOTER_ROW, UTERM2_CONTENT_COL,
                                UTERM2_HINT_STYLE, UTERM2_FOOTER_HINT);
  gemos_text_surface_write_right(cells, UTERM2_COLS, UTERM2_ROWS,
                                 UTERM2_FOOTER_ROW, UTERM2_CONTENT_COL,
                                 UTERM2_HINT_STYLE, UTERM2_FOOTER_CLOSE_HINT);
}

static uint32_t term_render_prompt(term_model_t *model,
                                   gemos_console_cell_t *cells) {
  uint32_t prompt_length = (uint32_t)gemos_strlen(UTERM2_PROMPT);
  uint32_t available = UTERM2_COLS - UTERM2_CONTENT_COL - prompt_length;
  uint32_t input_start = 0;
  uint32_t visible_length = model->input_length;

  gemos_text_surface_write_text(cells, UTERM2_COLS, UTERM2_ROWS, UTERM2_PROMPT_ROW,
                                UTERM2_CONTENT_COL, UTERM2_PROMPT_STYLE,
                                UTERM2_PROMPT);

  if (visible_length > available) {
    input_start = visible_length - available;
    visible_length = available;
  }

  gemos_text_surface_write_text(cells, UTERM2_COLS, UTERM2_ROWS,
                                UTERM2_PROMPT_ROW,
                                UTERM2_CONTENT_COL + prompt_length,
                                UTERM2_TEXT_STYLE,
                                model->input + input_start);
  return UTERM2_CONTENT_COL + prompt_length + visible_length;
}

void term_render_build_frame(term_model_t *model, int32_t pid,
                             gemos_console_frame_t *frame,
                             gemos_console_cell_t *cells) {
  uint32_t cursor_col;

  if (model == 0 || frame == 0 || cells == 0) {
    return;
  }

  gemos_text_surface_clear(cells, UTERM2_COLS, UTERM2_ROWS, UTERM2_TEXT_STYLE);
  term_render_status(model, pid, cells);
  term_render_transcript(model, cells);
  term_render_footer(cells);
  cursor_col = term_render_prompt(model, cells);

  frame->cols = UTERM2_COLS;
  frame->rows = UTERM2_ROWS;
  frame->cursor_row = UTERM2_PROMPT_ROW;
  frame->cursor_col = cursor_col;
  frame->cursor_visible = 1U;
  frame->cells = cells;
}
