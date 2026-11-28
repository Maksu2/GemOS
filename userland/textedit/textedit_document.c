#include "textedit_document.h"

#include <stddef.h>

/* What the open group of the history is collecting */
enum {
  UTEXTEDIT_RUN_NONE = 0,
  UTEXTEDIT_RUN_TYPING,    /* characters typed one after the other */
  UTEXTEDIT_RUN_BACKSPACE, /* characters removed to the left */
  UTEXTEDIT_RUN_DELETE,    /* characters removed to the right */
};

static void utextedit_copy_bytes(char *to, const char *from, uint32_t length) {
  if (to < from) {
    for (uint32_t i = 0U; i < length; ++i) {
      to[i] = from[i];
    }
  } else {
    for (uint32_t i = length; i > 0U; --i) {
      to[i - 1U] = from[i - 1U];
    }
  }
}

static void utextedit_document_reset_preferred_col(
    utextedit_document_t *document) {
  document->preferred_visual_col = 0U;
  document->preferred_visual_col_valid = 0U;
}

/* -- text without history --------------------------------------------------- */

static void utextedit_raw_insert(utextedit_document_t *document,
                                 uint32_t offset, const char *text,
                                 uint32_t length) {
  utextedit_copy_bytes(document->text + offset + length,
                       document->text + offset, document->length - offset);
  utextedit_copy_bytes(document->text + offset, text, length);
  document->length += length;
  document->text[document->length] = '\0';
}

static void utextedit_raw_remove(utextedit_document_t *document,
                                 uint32_t offset, uint32_t length) {
  utextedit_copy_bytes(document->text + offset,
                       document->text + offset + length,
                       document->length - offset - length);
  document->length -= length;
  document->text[document->length] = '\0';
}

/* -- history -------------------------------------------------------------- */

static uint32_t utextedit_base_state; /* state before the oldest step kept */

static void utextedit_history_clear(utextedit_document_t *document) {
  document->count = 0U;
  document->applied = 0U;
  document->pool_used = 0U;
  document->open_group = 0U;
  document->typing = UTEXTEDIT_RUN_NONE;
  utextedit_base_state = document->state;
}

/* Forget the oldest steps until a step of length more bytes fits. */
static int utextedit_history_make_room(utextedit_document_t *document,
                                       uint32_t length) {
  uint32_t drop = 0U;
  uint32_t freed = 0U;

  if (length > UTEXTEDIT_HISTORY_BYTES) {
    utextedit_history_clear(document);
    return 0;
  }
  while (drop < document->count &&
         (document->count - drop >= UTEXTEDIT_HISTORY_STEPS ||
          document->pool_used - freed + length > UTEXTEDIT_HISTORY_BYTES)) {
    freed += document->steps[drop].length;
    drop++;
  }
  if (drop == 0U) {
    return 1;
  }
  /* a group is undone whole: drop the rest of the last one too */
  while (drop < document->count &&
         document->steps[drop].group == document->steps[drop - 1U].group) {
    freed += document->steps[drop].length;
    drop++;
  }
  utextedit_base_state = document->steps[drop - 1U].group;
  utextedit_copy_bytes(document->pool, document->pool + freed,
                       document->pool_used - freed);
  for (uint32_t i = drop; i < document->count; ++i) {
    document->steps[i - drop] = document->steps[i];
    document->steps[i - drop].data -= freed;
  }
  document->count -= drop;
  document->applied = document->applied > drop ? document->applied - drop : 0U;
  document->pool_used -= freed;
  if (document->count == 0U) {
    document->open_group = 0U;
  }
  return 1;
}

/* Start a new group unless the run asked for continues the open one. */
static void utextedit_history_begin(utextedit_document_t *document,
                                    uint8_t run) {
  if (run == UTEXTEDIT_RUN_NONE || document->typing != run ||
      document->open_group == 0U || document->applied != document->count) {
    document->open_group = ++document->next_group;
  }
  document->typing = run;
}

static void utextedit_history_record(utextedit_document_t *document,
                                     uint8_t insert, uint32_t offset,
                                     const char *text, uint32_t length) {
  utextedit_step_t *step;

  /* a new change drops what was undone */
  document->count = document->applied;
  document->pool_used = document->count == 0U
                            ? 0U
                            : document->steps[document->count - 1U].data +
                                  document->steps[document->count - 1U].length;
  document->state = document->open_group;
  if (!utextedit_history_make_room(document, length)) {
    return; /* too large to keep: this change cannot be undone */
  }
  step = &document->steps[document->count++];
  step->offset = offset;
  step->length = length;
  step->data = document->pool_used;
  step->group = document->open_group;
  step->insert = insert;
  utextedit_copy_bytes(document->pool + document->pool_used, text, length);
  document->pool_used += length;
  document->applied = document->count;
}

/* The last step, if the open group can grow it */
static utextedit_step_t *utextedit_history_last(utextedit_document_t *document) {
  utextedit_step_t *step;

  if (document->count == 0U || document->applied != document->count) {
    return NULL;
  }
  step = &document->steps[document->count - 1U];
  return step->group == document->open_group ? step : NULL;
}

/* -- state ---------------------------------------------------------------- */

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
  document->anchor = 0U;
  utextedit_document_reset_preferred_col(document);
  document->state = ++document->next_group;
  document->saved_state = document->state;
  utextedit_history_clear(document);
}

void utextedit_document_loaded(utextedit_document_t *document,
                               uint32_t length) {
  if (length > UTEXTEDIT_DOC_MAX) {
    length = UTEXTEDIT_DOC_MAX;
  }
  document->text[length] = '\0';
  document->length = length;
  document->cursor = 0U;
  document->anchor = 0U;
  utextedit_document_reset_preferred_col(document);
  document->state = ++document->next_group;
  document->saved_state = document->state;
  utextedit_history_clear(document);
}

int utextedit_document_modified(const utextedit_document_t *document) {
  return document->state != document->saved_state;
}

void utextedit_document_mark_saved(utextedit_document_t *document) {
  document->saved_state = document->state;
}

void utextedit_document_mark_unsaved(utextedit_document_t *document) {
  document->saved_state = 0xFFFFFFFFU;
}

/* -- selection -------------------------------------------------------------- */

int utextedit_document_selection(const utextedit_document_t *document,
                                 uint32_t *start, uint32_t *end) {
  uint32_t low = document->anchor < document->cursor ? document->anchor
                                                     : document->cursor;
  uint32_t high = document->anchor < document->cursor ? document->cursor
                                                      : document->anchor;

  if (start != NULL) {
    *start = low;
  }
  if (end != NULL) {
    *end = high;
  }
  return low != high;
}

void utextedit_document_select_all(utextedit_document_t *document) {
  document->anchor = 0U;
  document->cursor = document->length;
  document->typing = UTEXTEDIT_RUN_NONE;
}

static int utextedit_is_word(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
         (ch >= '0' && ch <= '9') || ch == '_';
}

/* Characters of one kind: a word, a run of spaces, other punctuation */
static int utextedit_char_class(char ch) {
  if (utextedit_is_word(ch)) {
    return 1;
  }
  if (ch == ' ') {
    return 2;
  }
  return ch == '\n' ? 3 : 4;
}

void utextedit_document_select_word(utextedit_document_t *document) {
  uint32_t start = document->cursor;
  uint32_t end = document->cursor;
  int kind;

  if (document->length == 0U) {
    return;
  }
  if (start == document->length || document->text[start] == '\n') {
    if (start == 0U) {
      return;
    }
    start--; /* past the end of a line: the word before */
    end = start;
  }
  kind = utextedit_char_class(document->text[start]);
  if (kind == 3) {
    return;
  }
  while (start > 0U && utextedit_char_class(document->text[start - 1U]) == kind) {
    start--;
  }
  while (end < document->length &&
         utextedit_char_class(document->text[end]) == kind) {
    end++;
  }
  document->anchor = start;
  document->cursor = end;
  document->typing = UTEXTEDIT_RUN_NONE;
}

int utextedit_document_clear_selection(utextedit_document_t *document) {
  if (document->anchor == document->cursor) {
    return 0;
  }
  document->anchor = document->cursor;
  return 1;
}

/* -- edits ---------------------------------------------------------------- */

static void utextedit_remove_selection(utextedit_document_t *document) {
  uint32_t start;
  uint32_t end;

  if (!utextedit_document_selection(document, &start, &end)) {
    return;
  }
  utextedit_history_record(document, 0U, start, document->text + start,
                           end - start);
  utextedit_raw_remove(document, start, end - start);
  document->cursor = start;
  document->anchor = start;
}

/* Room for length more bytes once the selection is gone */
static int utextedit_fits(const utextedit_document_t *document,
                          uint32_t length) {
  uint32_t start;
  uint32_t end;

  utextedit_document_selection(document, &start, &end);
  return document->length - (end - start) + length <= UTEXTEDIT_DOC_MAX;
}

int utextedit_document_insert(utextedit_document_t *document,
                              const char *text, uint32_t length) {
  if (document == NULL || text == NULL ||
      (length == 0U && document->anchor == document->cursor) ||
      !utextedit_fits(document, length)) {
    return 0;
  }
  utextedit_history_begin(document, UTEXTEDIT_RUN_NONE);
  utextedit_remove_selection(document);
  if (length != 0U) {
    utextedit_history_record(document, 1U, document->cursor, text, length);
    utextedit_raw_insert(document, document->cursor, text, length);
    document->cursor += length;
  }
  document->anchor = document->cursor;
  document->typing = UTEXTEDIT_RUN_NONE;
  utextedit_document_reset_preferred_col(document);
  return 1;
}

int utextedit_document_type(utextedit_document_t *document, char ch) {
  utextedit_step_t *last;

  if (document == NULL || ((ch < 0x20 || ch > 0x7E) && ch != '\n') ||
      !utextedit_fits(document, 1U)) {
    return 0;
  }
  /* a space or a new line starts a new step: undo takes back a word */
  if (ch == ' ' || ch == '\n' || document->anchor != document->cursor) {
    document->typing = UTEXTEDIT_RUN_NONE;
  }
  utextedit_history_begin(document, UTEXTEDIT_RUN_TYPING);
  utextedit_remove_selection(document);
  last = utextedit_history_last(document);
  if (last != NULL && last->insert &&
      last->offset + last->length == document->cursor &&
      last->data + last->length == document->pool_used &&
      document->pool_used < UTEXTEDIT_HISTORY_BYTES) {
    document->pool[document->pool_used++] = ch;
    last->length++;
    document->state = last->group;
  } else {
    utextedit_history_record(document, 1U, document->cursor, &ch, 1U);
  }
  utextedit_raw_insert(document, document->cursor, &ch, 1U);
  document->cursor++;
  document->anchor = document->cursor;
  utextedit_document_reset_preferred_col(document);
  return 1;
}

/* Remove one character at offset as part of a run of removals. */
static void utextedit_remove_char(utextedit_document_t *document,
                                  uint32_t offset, uint8_t run) {
  utextedit_step_t *last;
  char ch = document->text[offset];

  utextedit_history_begin(document, run);
  last = utextedit_history_last(document);
  if (last != NULL && !last->insert &&
      last->data + last->length == document->pool_used &&
      document->pool_used < UTEXTEDIT_HISTORY_BYTES &&
      ((run == UTEXTEDIT_RUN_BACKSPACE && offset + 1U == last->offset) ||
       (run == UTEXTEDIT_RUN_DELETE && offset == last->offset))) {
    if (run == UTEXTEDIT_RUN_BACKSPACE) {
      /* in front of what the step removed so far */
      utextedit_copy_bytes(document->pool + last->data + 1U,
                           document->pool + last->data, last->length);
      document->pool[last->data] = ch;
      last->offset = offset;
    } else {
      document->pool[last->data + last->length] = ch;
    }
    document->pool_used++;
    last->length++;
    document->state = last->group;
  } else {
    utextedit_history_record(document, 0U, offset, &ch, 1U);
  }
  utextedit_raw_remove(document, offset, 1U);
}

int utextedit_document_backspace(utextedit_document_t *document) {
  if (document == NULL) {
    return 0;
  }
  if (document->anchor != document->cursor) {
    return utextedit_document_insert(document, "", 0U);
  }
  if (document->cursor == 0U) {
    return 0;
  }
  utextedit_remove_char(document, document->cursor - 1U,
                        UTEXTEDIT_RUN_BACKSPACE);
  document->cursor--;
  document->anchor = document->cursor;
  utextedit_document_reset_preferred_col(document);
  return 1;
}

int utextedit_document_delete(utextedit_document_t *document) {
  if (document == NULL) {
    return 0;
  }
  if (document->anchor != document->cursor) {
    return utextedit_document_insert(document, "", 0U);
  }
  if (document->cursor >= document->length) {
    return 0;
  }
  utextedit_remove_char(document, document->cursor, UTEXTEDIT_RUN_DELETE);
  utextedit_document_reset_preferred_col(document);
  return 1;
}

void utextedit_document_break_typing(utextedit_document_t *document) {
  document->typing = UTEXTEDIT_RUN_NONE;
  document->open_group = 0U;
}

int utextedit_document_undo(utextedit_document_t *document) {
  uint32_t group;

  if (document == NULL || document->applied == 0U) {
    return 0;
  }
  group = document->steps[document->applied - 1U].group;
  while (document->applied > 0U &&
         document->steps[document->applied - 1U].group == group) {
    const utextedit_step_t *step = &document->steps[--document->applied];

    if (step->insert) {
      utextedit_raw_remove(document, step->offset, step->length);
      document->cursor = step->offset;
    } else {
      utextedit_raw_insert(document, step->offset, document->pool + step->data,
                           step->length);
      document->cursor = step->offset + step->length;
    }
  }
  document->anchor = document->cursor;
  document->state = document->applied == 0U
                        ? utextedit_base_state
                        : document->steps[document->applied - 1U].group;
  utextedit_document_break_typing(document);
  utextedit_document_reset_preferred_col(document);
  return 1;
}

int utextedit_document_redo(utextedit_document_t *document) {
  uint32_t group;

  if (document == NULL || document->applied == document->count) {
    return 0;
  }
  group = document->steps[document->applied].group;
  while (document->applied < document->count &&
         document->steps[document->applied].group == group) {
    const utextedit_step_t *step = &document->steps[document->applied++];

    if (step->insert) {
      utextedit_raw_insert(document, step->offset, document->pool + step->data,
                           step->length);
      document->cursor = step->offset + step->length;
    } else {
      utextedit_raw_remove(document, step->offset, step->length);
      document->cursor = step->offset;
    }
  }
  document->anchor = document->cursor;
  document->state = group;
  utextedit_document_break_typing(document);
  utextedit_document_reset_preferred_col(document);
  return 1;
}

/* -- moves ---------------------------------------------------------------- */

/* Put the cursor at offset; without extend the selection goes. */
static int utextedit_move_to(utextedit_document_t *document, uint32_t offset,
                             int extend) {
  uint32_t old_cursor = document->cursor;
  uint32_t old_anchor = document->anchor;

  if (offset > document->length) {
    offset = document->length;
  }
  document->cursor = offset;
  if (!extend) {
    document->anchor = offset;
  }
  utextedit_document_break_typing(document);
  return document->cursor != old_cursor || document->anchor != old_anchor;
}

static uint32_t utextedit_line_start(const utextedit_document_t *document,
                                     uint32_t offset) {
  while (offset > 0U && document->text[offset - 1U] != '\n') {
    offset--;
  }
  return offset;
}

static uint32_t utextedit_line_end(const utextedit_document_t *document,
                                   uint32_t offset) {
  while (offset < document->length && document->text[offset] != '\n') {
    offset++;
  }
  return offset;
}

int utextedit_document_move_left(utextedit_document_t *document, int extend) {
  uint32_t start;

  utextedit_document_reset_preferred_col(document);
  if (!extend && utextedit_document_selection(document, &start, NULL)) {
    return utextedit_move_to(document, start, 0);
  }
  return document->cursor > 0U &&
         utextedit_move_to(document, document->cursor - 1U, extend);
}

int utextedit_document_move_right(utextedit_document_t *document, int extend) {
  uint32_t end;

  utextedit_document_reset_preferred_col(document);
  if (!extend && utextedit_document_selection(document, NULL, &end)) {
    return utextedit_move_to(document, end, 0);
  }
  return document->cursor < document->length &&
         utextedit_move_to(document, document->cursor + 1U, extend);
}

int utextedit_document_move_word_left(utextedit_document_t *document,
                                      int extend) {
  uint32_t offset = document->cursor;

  while (offset > 0U && !utextedit_is_word(document->text[offset - 1U])) {
    offset--;
  }
  while (offset > 0U && utextedit_is_word(document->text[offset - 1U])) {
    offset--;
  }
  utextedit_document_reset_preferred_col(document);
  return utextedit_move_to(document, offset, extend);
}

int utextedit_document_move_word_right(utextedit_document_t *document,
                                       int extend) {
  uint32_t offset = document->cursor;

  while (offset < document->length && utextedit_is_word(document->text[offset])) {
    offset++;
  }
  while (offset < document->length && !utextedit_is_word(document->text[offset])) {
    offset++;
  }
  utextedit_document_reset_preferred_col(document);
  return utextedit_move_to(document, offset, extend);
}

int utextedit_document_move_home(utextedit_document_t *document, int extend) {
  utextedit_document_reset_preferred_col(document);
  return utextedit_move_to(document,
                           utextedit_line_start(document, document->cursor),
                           extend);
}

int utextedit_document_move_end(utextedit_document_t *document, int extend) {
  utextedit_document_reset_preferred_col(document);
  return utextedit_move_to(document,
                           utextedit_line_end(document, document->cursor),
                           extend);
}

int utextedit_document_move_start(utextedit_document_t *document, int extend) {
  utextedit_document_reset_preferred_col(document);
  return utextedit_move_to(document, 0U, extend);
}

int utextedit_document_move_finish(utextedit_document_t *document, int extend) {
  utextedit_document_reset_preferred_col(document);
  return utextedit_move_to(document, document->length, extend);
}

/* -- the wrapped text ------------------------------------------------------- */

void utextedit_document_visual_position(const utextedit_document_t *document,
                                        uint32_t offset, uint32_t wrap_cols,
                                        uint32_t *row, uint32_t *col) {
  uint32_t current_row = 0U;
  uint32_t current_col = 0U;

  for (uint32_t index = 0U; index < offset && index < document->length;
       ++index) {
    if (document->text[index] == '\n') {
      current_row++;
      current_col = 0U;
    } else if (++current_col >= wrap_cols) {
      current_row++;
      current_col = 0U;
    }
  }
  if (row != NULL) {
    *row = current_row;
  }
  if (col != NULL) {
    *col = current_col;
  }
}

void utextedit_document_cursor_visual_position(
    const utextedit_document_t *document, uint32_t wrap_cols, uint32_t *row,
    uint32_t *col) {
  utextedit_document_visual_position(document, document->cursor, wrap_cols,
                                     row, col);
}

uint32_t utextedit_document_total_visual_rows(
    const utextedit_document_t *document, uint32_t wrap_cols) {
  uint32_t row;

  utextedit_document_visual_position(document, document->length, wrap_cols,
                                     &row, NULL);
  return row + 1U;
}

/* The offset shown at (row, col), or the last one of that row; past the
 * last row, the end of the text. */
static uint32_t utextedit_offset_at(const utextedit_document_t *document,
                                    uint32_t wrap_cols, uint32_t target_row,
                                    uint32_t target_col) {
  uint32_t row = 0U;
  uint32_t col = 0U;

  for (uint32_t offset = 0U; offset < document->length; ++offset) {
    if (row == target_row && col == target_col) {
      return offset;
    }
    if (document->text[offset] == '\n') {
      if (row == target_row) {
        return offset; /* the end of a shorter line */
      }
      row++;
      col = 0U;
    } else if (++col >= wrap_cols) {
      if (row == target_row) {
        return offset + 1U == document->length ? offset + 1U : offset;
      }
      row++;
      col = 0U;
    }
  }
  return document->length;
}

static int utextedit_move_rows(utextedit_document_t *document,
                               uint32_t wrap_cols, uint32_t rows, int down,
                               int extend) {
  uint32_t row;
  uint32_t col;
  uint32_t total = utextedit_document_total_visual_rows(document, wrap_cols);

  utextedit_document_cursor_visual_position(document, wrap_cols, &row, &col);
  if (!document->preferred_visual_col_valid) {
    document->preferred_visual_col = col;
    document->preferred_visual_col_valid = 1U;
  }
  if (down) {
    if (row + 1U >= total) {
      return utextedit_move_to(document, document->length, extend);
    }
    row = row + rows < total ? row + rows : total - 1U;
  } else {
    if (row == 0U) {
      return utextedit_move_to(document, 0U, extend);
    }
    row = row > rows ? row - rows : 0U;
  }
  return utextedit_move_to(
      document,
      utextedit_offset_at(document, wrap_cols, row,
                          document->preferred_visual_col),
      extend);
}

int utextedit_document_move_up(utextedit_document_t *document,
                               uint32_t wrap_cols, uint32_t rows, int extend) {
  return utextedit_move_rows(document, wrap_cols, rows, 0, extend);
}

int utextedit_document_move_down(utextedit_document_t *document,
                                 uint32_t wrap_cols, uint32_t rows, int extend) {
  return utextedit_move_rows(document, wrap_cols, rows, 1, extend);
}

int utextedit_document_move_to_visual(utextedit_document_t *document,
                                      uint32_t wrap_cols, uint32_t row,
                                      uint32_t col, int extend) {
  utextedit_document_reset_preferred_col(document);
  return utextedit_move_to(document,
                           utextedit_offset_at(document, wrap_cols, row, col),
                           extend);
}

/* -- find ------------------------------------------------------------------ */

static char utextedit_lower(char ch) {
  return ch >= 'A' && ch <= 'Z' ? (char)(ch - 'A' + 'a') : ch;
}

static int utextedit_matches(const utextedit_document_t *document,
                             uint32_t offset, const char *needle,
                             uint32_t length) {
  for (uint32_t i = 0U; i < length; ++i) {
    if (utextedit_lower(document->text[offset + i]) !=
        utextedit_lower(needle[i])) {
      return 0;
    }
  }
  return 1;
}

int utextedit_document_find(utextedit_document_t *document,
                            const char *needle, uint32_t from) {
  uint32_t length = 0U;

  while (needle[length] != '\0') {
    length++;
  }
  if (length == 0U || length > document->length) {
    return 0;
  }
  if (from > document->length) {
    from = document->length;
  }
  for (uint32_t pass = 0U; pass < 2U; ++pass) {
    uint32_t start = pass == 0U ? from : 0U;
    uint32_t stop = pass == 0U ? document->length - length + 1U
                               : (from < document->length - length + 1U
                                      ? from
                                      : document->length - length + 1U);

    for (uint32_t offset = start; offset < stop; ++offset) {
      if (utextedit_matches(document, offset, needle, length)) {
        document->anchor = offset;
        document->cursor = offset + length;
        utextedit_document_break_typing(document);
        utextedit_document_reset_preferred_col(document);
        return 1;
      }
    }
  }
  return 0;
}
