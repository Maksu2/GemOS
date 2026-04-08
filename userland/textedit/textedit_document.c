#include "textedit_document.h"

#include <stddef.h>

static void utextedit_document_reset_preferred_col(
    utextedit_document_t *document) {
  if (document != NULL) {
    document->preferred_visual_col = 0U;
    document->preferred_visual_col_valid = 0U;
  }
}

static void utextedit_document_mark_dirty(utextedit_document_t *document) {
  if (document != NULL) {
    document->dirty = 1U;
  }
}

static uint32_t utextedit_document_line_start(
    const utextedit_document_t *document, uint32_t offset) {
  if (document == NULL) {
    return 0U;
  }

  if (offset > document->length) {
    offset = document->length;
  }

  while (offset > 0U && document->text[offset - 1U] != '\n') {
    offset--;
  }

  return offset;
}

static uint32_t utextedit_document_line_end(const utextedit_document_t *document,
                                            uint32_t offset) {
  if (document == NULL) {
    return 0U;
  }

  if (offset > document->length) {
    offset = document->length;
  }

  while (offset < document->length && document->text[offset] != '\n') {
    offset++;
  }

  return offset;
}

static uint32_t utextedit_document_offset_for_visual_position(
    const utextedit_document_t *document, uint32_t wrap_cols,
    uint32_t target_row, uint32_t target_col) {
  uint32_t row = 0U;
  uint32_t col = 0U;
  uint32_t last_offset_in_row = 0U;
  uint8_t have_row = target_row == 0U;
  uint32_t offset;

  if (document == NULL || wrap_cols == 0U) {
    return 0U;
  }

  for (offset = 0U; offset <= document->length; ++offset) {
    if (row == target_row) {
      last_offset_in_row = offset;
      have_row = 1U;
      if (col == target_col) {
        return offset;
      }
    }

    if (offset == document->length) {
      break;
    }

    if (document->text[offset] == '\n') {
      row++;
      col = 0U;
    } else {
      col++;
      if (col >= wrap_cols) {
        row++;
        col = 0U;
      }
    }

    if (have_row && row > target_row) {
      return last_offset_in_row;
    }
  }

  if (have_row) {
    return last_offset_in_row;
  }

  return document->length;
}

static int utextedit_document_insert_raw(utextedit_document_t *document,
                                         char ch) {
  uint32_t index;

  if (document == NULL || document->length >= UTEXTEDIT_DOC_MAX) {
    return 0;
  }

  for (index = document->length + 1U; index > document->cursor; --index) {
    document->text[index] = document->text[index - 1U];
  }

  document->text[document->cursor] = ch;
  document->cursor++;
  document->length++;
  utextedit_document_reset_preferred_col(document);
  utextedit_document_mark_dirty(document);
  return 1;
}

void utextedit_document_init(utextedit_document_t *document) {
  utextedit_document_reset(document);
}

void utextedit_document_reset(utextedit_document_t *document) {
  if (document == NULL) {
    return;
  }

  document->text[0] = '\0';
  document->length = 0U;
  document->cursor = 0U;
  document->preferred_visual_col = 0U;
  document->preferred_visual_col_valid = 0U;
  document->dirty = 0U;
}

int utextedit_document_insert_char(utextedit_document_t *document, char ch) {
  if (ch < 0x20 || ch > 0x7E) {
    return 0;
  }

  return utextedit_document_insert_raw(document, ch);
}

int utextedit_document_insert_newline(utextedit_document_t *document) {
  return utextedit_document_insert_raw(document, '\n');
}

int utextedit_document_backspace(utextedit_document_t *document) {
  uint32_t index;

  if (document == NULL || document->cursor == 0U || document->length == 0U) {
    return 0;
  }

  for (index = document->cursor - 1U; index < document->length; ++index) {
    document->text[index] = document->text[index + 1U];
  }

  document->cursor--;
  document->length--;
  utextedit_document_reset_preferred_col(document);
  utextedit_document_mark_dirty(document);
  return 1;
}

int utextedit_document_move_left(utextedit_document_t *document) {
  if (document == NULL || document->cursor == 0U) {
    return 0;
  }

  document->cursor--;
  utextedit_document_reset_preferred_col(document);
  return 1;
}

int utextedit_document_move_right(utextedit_document_t *document) {
  if (document == NULL || document->cursor >= document->length) {
    return 0;
  }

  document->cursor++;
  utextedit_document_reset_preferred_col(document);
  return 1;
}

int utextedit_document_move_home(utextedit_document_t *document) {
  uint32_t new_offset;

  if (document == NULL) {
    return 0;
  }

  new_offset = utextedit_document_line_start(document, document->cursor);
  if (new_offset == document->cursor) {
    return 0;
  }

  document->cursor = new_offset;
  utextedit_document_reset_preferred_col(document);
  return 1;
}

int utextedit_document_move_end(utextedit_document_t *document) {
  uint32_t new_offset;

  if (document == NULL) {
    return 0;
  }

  new_offset = utextedit_document_line_end(document, document->cursor);
  if (new_offset == document->cursor) {
    return 0;
  }

  document->cursor = new_offset;
  utextedit_document_reset_preferred_col(document);
  return 1;
}

void utextedit_document_cursor_visual_position(
    const utextedit_document_t *document, uint32_t wrap_cols, uint32_t *row,
    uint32_t *col) {
  uint32_t current_row = 0U;
  uint32_t current_col = 0U;
  uint32_t index;

  if (row != NULL) {
    *row = 0U;
  }
  if (col != NULL) {
    *col = 0U;
  }
  if (document == NULL || wrap_cols == 0U) {
    return;
  }

  for (index = 0U; index < document->cursor && index < document->length;
       ++index) {
    if (document->text[index] == '\n') {
      current_row++;
      current_col = 0U;
    } else {
      current_col++;
      if (current_col >= wrap_cols) {
        current_row++;
        current_col = 0U;
      }
    }
  }

  if (row != NULL) {
    *row = current_row;
  }
  if (col != NULL) {
    *col = current_col;
  }
}

uint32_t utextedit_document_total_visual_rows(
    const utextedit_document_t *document, uint32_t wrap_cols) {
  uint32_t row = 0U;
  uint32_t col = 0U;
  uint32_t index;

  if (document == NULL || wrap_cols == 0U) {
    return 1U;
  }

  for (index = 0U; index < document->length; ++index) {
    if (document->text[index] == '\n') {
      row++;
      col = 0U;
    } else {
      col++;
      if (col >= wrap_cols) {
        row++;
        col = 0U;
      }
    }
  }

  return row + 1U;
}

int utextedit_document_move_up(utextedit_document_t *document,
                               uint32_t wrap_cols) {
  uint32_t row;
  uint32_t col;
  uint32_t target_col;
  uint32_t new_offset;

  if (document == NULL || wrap_cols == 0U) {
    return 0;
  }

  utextedit_document_cursor_visual_position(document, wrap_cols, &row, &col);
  if (row == 0U) {
    return 0;
  }

  if (!document->preferred_visual_col_valid) {
    document->preferred_visual_col = col;
    document->preferred_visual_col_valid = 1U;
  }
  target_col = document->preferred_visual_col;
  new_offset = utextedit_document_offset_for_visual_position(
      document, wrap_cols, row - 1U, target_col);

  if (new_offset == document->cursor) {
    return 0;
  }

  document->cursor = new_offset;
  return 1;
}

int utextedit_document_move_down(utextedit_document_t *document,
                                 uint32_t wrap_cols) {
  uint32_t row;
  uint32_t col;
  uint32_t total_rows;
  uint32_t target_col;
  uint32_t new_offset;

  if (document == NULL || wrap_cols == 0U) {
    return 0;
  }

  utextedit_document_cursor_visual_position(document, wrap_cols, &row, &col);
  total_rows = utextedit_document_total_visual_rows(document, wrap_cols);
  if (row + 1U >= total_rows) {
    return 0;
  }

  if (!document->preferred_visual_col_valid) {
    document->preferred_visual_col = col;
    document->preferred_visual_col_valid = 1U;
  }
  target_col = document->preferred_visual_col;
  new_offset = utextedit_document_offset_for_visual_position(
      document, wrap_cols, row + 1U, target_col);

  if (new_offset == document->cursor) {
    return 0;
  }

  document->cursor = new_offset;
  return 1;
}
