#include "textedit_render.h"

#include "../common/format.h"
#include "../common/text_surface.h"
#include "textedit_theme.h"

static void utextedit_render_put(gemos_console_cell_t *cells, uint32_t row,
                                 uint32_t col, uint8_t style, char ch) {
  if (cells == 0 || row >= UTEXTEDIT_ROWS || col >= UTEXTEDIT_COLS) {
    return;
  }

  cells[(row * UTEXTEDIT_COLS) + col].ch = ch;
  cells[(row * UTEXTEDIT_COLS) + col].style = style;
  cells[(row * UTEXTEDIT_COLS) + col].flags = 0U;
}

static void utextedit_render_header(const utextedit_state_t *state,
                                    gemos_console_cell_t *cells) {
  char title_right[UTEXTEDIT_FILENAME_MAX + 8U];
  size_t length;
  const char *name = state->has_filename ? state->filename : "Untitled";

  gemos_text_surface_fill_row(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS,
                              UTEXTEDIT_HEADER_ROW, UTEXTEDIT_STATUS_STYLE,
                              ' ');
  gemos_copy(title_right, sizeof(title_right), name);
  if (state->document.dirty) {
    length = gemos_strlen(title_right);
    if (length + 2U < sizeof(title_right)) {
      title_right[length++] = ' ';
      title_right[length++] = '*';
      title_right[length] = '\0';
    }
  }

  gemos_text_surface_write_text(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS,
                                UTEXTEDIT_HEADER_ROW, UTEXTEDIT_CONTENT_COL,
                                UTEXTEDIT_STATUS_STYLE, UTEXTEDIT_TITLE);
  gemos_text_surface_write_right(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS,
                                 UTEXTEDIT_HEADER_ROW, UTEXTEDIT_CONTENT_COL,
                                 UTEXTEDIT_STATUS_STYLE, title_right);
  gemos_text_surface_write_hline(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS, 1U,
                                 UTEXTEDIT_CONTENT_COL,
                                 UTEXTEDIT_COLS - (UTEXTEDIT_CONTENT_COL * 2U),
                                 UTEXTEDIT_DIM_STYLE, '-');
}

static void utextedit_render_body(const utextedit_state_t *state,
                                  gemos_console_cell_t *cells) {
  uint32_t visual_row = 0U;
  uint32_t visual_col = 0U;
  uint32_t offset;

  if (state->document.length == 0U) {
    gemos_text_surface_write_text(
        cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS, UTEXTEDIT_BODY_TOP_ROW,
        UTEXTEDIT_CONTENT_COL, UTEXTEDIT_DIM_STYLE,
        "Untitled document. Start typing.");
    return;
  }

  for (offset = 0U; offset < state->document.length; ++offset) {
    char ch = state->document.text[offset];

    if (ch != '\n' && visual_row >= state->viewport_top_row &&
        visual_row < state->viewport_top_row + UTEXTEDIT_BODY_ROWS) {
      utextedit_render_put(
          cells, UTEXTEDIT_BODY_TOP_ROW + (visual_row - state->viewport_top_row),
          UTEXTEDIT_CONTENT_COL + visual_col, UTEXTEDIT_TEXT_STYLE, ch);
    }

    if (ch == '\n') {
      visual_row++;
      visual_col = 0U;
    } else {
      visual_col++;
      if (visual_col >= UTEXTEDIT_BODY_COLS) {
        visual_row++;
        visual_col = 0U;
      }
    }
  }
}

static void utextedit_render_status(const utextedit_state_t *state,
                                    gemos_console_cell_t *cells) {
  char left[48];
  char right[UTEXTEDIT_STATUS_MAX];
  char row_text[16];
  char col_text[16];
  char bytes_text[16];
  uint32_t cursor_row;
  uint32_t cursor_col;
  size_t offset = 0U;

  utextedit_document_cursor_visual_position(&state->document, UTEXTEDIT_BODY_COLS,
                                            &cursor_row, &cursor_col);
  gemos_u32_to_text(cursor_row + 1U, row_text, sizeof(row_text));
  gemos_u32_to_text(cursor_col + 1U, col_text, sizeof(col_text));
  gemos_u32_to_text(state->document.length, bytes_text, sizeof(bytes_text));

  gemos_copy(left, sizeof(left), "Ln ");
  offset = gemos_strlen(left);
  gemos_copy(left + offset, sizeof(left) - offset, row_text);
  offset = gemos_strlen(left);
  gemos_copy(left + offset, sizeof(left) - offset, "  Col ");
  offset = gemos_strlen(left);
  gemos_copy(left + offset, sizeof(left) - offset, col_text);
  offset = gemos_strlen(left);
  gemos_copy(left + offset, sizeof(left) - offset, "  Bytes ");
  offset = gemos_strlen(left);
  gemos_copy(left + offset, sizeof(left) - offset, bytes_text);

  gemos_copy(right, sizeof(right), state->status);

  gemos_text_surface_fill_row(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS,
                              UTEXTEDIT_STATUS_ROW, UTEXTEDIT_DIM_STYLE, ' ');
  gemos_text_surface_write_text(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS,
                                UTEXTEDIT_STATUS_ROW, UTEXTEDIT_CONTENT_COL,
                                UTEXTEDIT_DIM_STYLE, left);
  gemos_text_surface_write_right(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS,
                                 UTEXTEDIT_STATUS_ROW, UTEXTEDIT_CONTENT_COL,
                                 UTEXTEDIT_DIM_STYLE, right);
}

static void utextedit_render_footer(gemos_console_cell_t *cells) {
  gemos_text_surface_fill_row(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS,
                              UTEXTEDIT_FOOTER_ROW, UTEXTEDIT_DIM_STYLE, ' ');
  gemos_text_surface_write_text(
      cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS, UTEXTEDIT_FOOTER_ROW,
      UTEXTEDIT_CONTENT_COL, UTEXTEDIT_DIM_STYLE,
      "Ctrl+N New  Ctrl+O Open  Ctrl+S Save  Ctrl+Q Close");
  gemos_text_surface_write_right(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS,
                                 UTEXTEDIT_FOOTER_ROW, UTEXTEDIT_CONTENT_COL,
                                 UTEXTEDIT_DIM_STYLE, "Esc close");
}

void utextedit_render_build_frame(const utextedit_state_t *state,
                                  gemos_console_frame_t *frame,
                                  gemos_console_cell_t *cells) {
  uint32_t cursor_row;
  uint32_t cursor_col;

  if (state == 0 || frame == 0 || cells == 0) {
    return;
  }

  gemos_text_surface_clear(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS,
                           UTEXTEDIT_TEXT_STYLE);
  utextedit_render_header(state, cells);
  utextedit_render_body(state, cells);
  utextedit_render_status(state, cells);
  utextedit_render_footer(cells);
  utextedit_document_cursor_visual_position(&state->document, UTEXTEDIT_BODY_COLS,
                                            &cursor_row, &cursor_col);

  frame->cols = UTEXTEDIT_COLS;
  frame->rows = UTEXTEDIT_ROWS;
  frame->cursor_row = UTEXTEDIT_BODY_TOP_ROW;
  frame->cursor_col = UTEXTEDIT_CONTENT_COL;
  frame->cursor_visible = 1U;
  if (cursor_row >= state->viewport_top_row &&
      cursor_row < state->viewport_top_row + UTEXTEDIT_BODY_ROWS) {
    frame->cursor_row =
        UTEXTEDIT_BODY_TOP_ROW + (cursor_row - state->viewport_top_row);
    frame->cursor_col = UTEXTEDIT_CONTENT_COL + cursor_col;
  }
  frame->cells = cells;
}
