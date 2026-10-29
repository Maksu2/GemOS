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

/* The end of text in at most max characters, "..." in front if cut */
static void utextedit_render_tail(char *out, size_t capacity,
                                  const char *text, size_t max) {
  size_t length = gemos_strlen(text);

  if (length <= max || max < 4U) {
    gemos_copy(out, capacity, text);
    return;
  }
  gemos_copy(out, capacity, "...");
  gemos_copy(out + 3, capacity - 3U, text + length - (max - 3U));
}

static void utextedit_render_header(const utextedit_state_t *state,
                                    gemos_console_cell_t *cells) {
  char title_right[UTEXTEDIT_HEADER_NAME_MAX + 3U];
  size_t length;

  gemos_text_surface_fill_row(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS,
                              UTEXTEDIT_HEADER_ROW, UTEXTEDIT_STATUS_STYLE,
                              ' ');
  utextedit_render_tail(title_right, sizeof(title_right),
                        state->path[0] != '\0' ? state->path : "Untitled",
                        UTEXTEDIT_HEADER_NAME_MAX);
  if (state->document.modified) {
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
                                 state->status_error ? UTEXTEDIT_ERROR_STYLE
                                                     : UTEXTEDIT_DIM_STYLE,
                                 right);
}

/* One line of the dialog box: "| text". No bar on the right: the console
 * draws text in a proportional font, so it would not line up. */
static void utextedit_render_dialog_line(gemos_console_cell_t *cells,
                                         uint32_t row, uint8_t style,
                                         const char *text) {
  gemos_text_surface_write_hline(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS, row,
                                 UTEXTEDIT_DIALOG_COL, UTEXTEDIT_DIALOG_WIDTH,
                                 UTEXTEDIT_TEXT_STYLE, ' ');
  utextedit_render_put(cells, row, UTEXTEDIT_DIALOG_COL, UTEXTEDIT_ACCENT_STYLE,
                       '|');
  gemos_text_surface_write_text(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS, row,
                                UTEXTEDIT_DIALOG_COL + 2U, style, text);
}

/* A box over the text while the editor asks something. */
static void utextedit_render_dialog(const utextedit_state_t *state,
                                    gemos_console_cell_t *cells) {
  char line[UTEXTEDIT_DIALOG_TEXT_MAX + 1U];
  char name[UTEXTEDIT_DIALOG_TEXT_MAX + 1U];
  const char *title;
  const char *keys;
  size_t length;
  uint32_t row = UTEXTEDIT_DIALOG_ROW;

  if (state->prompt == UTEXTEDIT_PROMPT_NONE) {
    return;
  }
  if (state->prompt == UTEXTEDIT_PROMPT_UNSAVED) {
    utextedit_render_tail(name, sizeof(name),
                          state->path[0] != '\0' ? state->path : "Untitled",
                          UTEXTEDIT_DIALOG_TEXT_MAX - 30U);
    gemos_copy(line, sizeof(line), "Save the changes to ");
    length = gemos_strlen(line);
    gemos_copy(line + length, sizeof(line) - length, name);
    length = gemos_strlen(line);
    gemos_copy(line + length, sizeof(line) - length, "?");
    title = line;
    keys = "Y  save      N  discard      Esc  cancel";
  } else {
    title = state->prompt == UTEXTEDIT_PROMPT_OPEN ? "Open file:" : "Save as:";
    keys = state->prompt == UTEXTEDIT_PROMPT_OPEN
               ? "Enter  open      Esc  cancel"
               : "Enter  save      Esc  cancel";
    utextedit_render_tail(name, sizeof(name), state->input,
                          UTEXTEDIT_DIALOG_TEXT_MAX);
  }

  gemos_text_surface_write_hline(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS, row,
                                 UTEXTEDIT_DIALOG_COL, UTEXTEDIT_DIALOG_WIDTH,
                                 UTEXTEDIT_ACCENT_STYLE, '-');
  utextedit_render_dialog_line(cells, ++row, UTEXTEDIT_ACCENT_STYLE, title);
  utextedit_render_dialog_line(
      cells, ++row, UTEXTEDIT_TEXT_STYLE,
      state->prompt == UTEXTEDIT_PROMPT_UNSAVED ? "" : name);
  utextedit_render_dialog_line(cells, ++row, UTEXTEDIT_DIM_STYLE, keys);
  gemos_text_surface_write_hline(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS, ++row,
                                 UTEXTEDIT_DIALOG_COL, UTEXTEDIT_DIALOG_WIDTH,
                                 UTEXTEDIT_ACCENT_STYLE, '-');
}

static void utextedit_render_footer(gemos_console_cell_t *cells) {
  gemos_text_surface_fill_row(cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS,
                              UTEXTEDIT_FOOTER_ROW, UTEXTEDIT_DIM_STYLE, ' ');
  gemos_text_surface_write_text(
      cells, UTEXTEDIT_COLS, UTEXTEDIT_ROWS, UTEXTEDIT_FOOTER_ROW,
      UTEXTEDIT_CONTENT_COL, UTEXTEDIT_DIM_STYLE,
      "Ctrl+N New  Ctrl+O Open  Ctrl+S Save  Ctrl+Shift+S Save as");
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
  utextedit_render_dialog(state, cells);
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
  if (state->prompt == UTEXTEDIT_PROMPT_UNSAVED) {
    frame->cursor_visible = 0U;
  } else if (state->prompt != UTEXTEDIT_PROMPT_NONE) {
    frame->cursor_row = UTEXTEDIT_DIALOG_ROW + 2U;
    frame->cursor_col =
        UTEXTEDIT_DIALOG_COL + 2U +
        (state->input_length < UTEXTEDIT_DIALOG_TEXT_MAX
             ? state->input_length
             : UTEXTEDIT_DIALOG_TEXT_MAX);
  }
  frame->cells = cells;
}
