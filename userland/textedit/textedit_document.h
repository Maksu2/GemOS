#ifndef GEMOS_USERLAND_TEXTEDIT_DOCUMENT_H
#define GEMOS_USERLAND_TEXTEDIT_DOCUMENT_H

#include <stdint.h>

/* The largest document; a file one byte longer does not open. */
#define UTEXTEDIT_DOC_MAX (128U * 1024U)

/* Undo history: the bytes that changes inserted or removed, and the
 * changes themselves. When a change does not fit, the oldest history goes
 * (all of it, if one change is larger than the whole pool). */
#define UTEXTEDIT_HISTORY_BYTES (32U * 1024U)
#define UTEXTEDIT_HISTORY_STEPS 512U

typedef struct {
  uint32_t offset; /* where the text went in or came out */
  uint32_t length;
  uint32_t data;   /* its bytes in the history pool */
  uint32_t group;  /* one user action: undone and redone together */
  uint8_t insert;  /* 1: inserted, 0: removed */
} utextedit_step_t;

typedef struct {
  /* +1 for the NUL, +1 so that loading a file can tell one too long */
  char text[UTEXTEDIT_DOC_MAX + 2U];
  uint32_t length;
  uint32_t cursor;
  uint32_t anchor; /* the other end of the selection; == cursor: none */
  uint32_t preferred_visual_col;
  uint8_t preferred_visual_col_valid;

  /* history: steps[0 .. applied) are done, steps[applied .. count) undone */
  utextedit_step_t steps[UTEXTEDIT_HISTORY_STEPS];
  char pool[UTEXTEDIT_HISTORY_BYTES];
  uint32_t count;
  uint32_t applied;
  uint32_t pool_used;
  uint32_t next_group;
  uint32_t open_group;  /* the group new steps join; 0: start a new one */
  uint8_t typing;       /* the open group is a run of typed characters */
  uint32_t state;       /* the group of the last applied step, 0 at load */
  uint32_t saved_state; /* state when last opened or saved */
} utextedit_document_t;

void utextedit_document_init(utextedit_document_t *document);
void utextedit_document_reset(utextedit_document_t *document);
/* Replace everything with the length bytes already in document->text
 * (loaded there by the caller), cursor at the start, no history. */
void utextedit_document_loaded(utextedit_document_t *document,
                               uint32_t length);
/* Changed since it was opened or saved */
int utextedit_document_modified(const utextedit_document_t *document);
void utextedit_document_mark_saved(utextedit_document_t *document);
/* Opened with changes (a converted file): modified until saved */
void utextedit_document_mark_unsaved(utextedit_document_t *document);

/* The selection, start <= end; 0 when there is none. */
int utextedit_document_selection(const utextedit_document_t *document,
                                 uint32_t *start, uint32_t *end);
void utextedit_document_select_all(utextedit_document_t *document);
void utextedit_document_select_word(utextedit_document_t *document);
int utextedit_document_clear_selection(utextedit_document_t *document);

/* Edits. Each is one step of the undo history; typed characters next to
 * each other join one. They return 1 if the text changed, 0 if not (no
 * room, nothing to delete). A selection is replaced or deleted. */
int utextedit_document_type(utextedit_document_t *document, char ch);
int utextedit_document_insert(utextedit_document_t *document,
                              const char *text, uint32_t length);
int utextedit_document_backspace(utextedit_document_t *document);
int utextedit_document_delete(utextedit_document_t *document);
/* End the current run of typing: the next character starts a new step. */
void utextedit_document_break_typing(utextedit_document_t *document);
int utextedit_document_undo(utextedit_document_t *document);
int utextedit_document_redo(utextedit_document_t *document);

/* Moves. With extend the selection grows from its anchor, without it a
 * selection is dropped (left and right stop at its edge). Return 1 if the
 * cursor or the selection changed. */
int utextedit_document_move_left(utextedit_document_t *document, int extend);
int utextedit_document_move_right(utextedit_document_t *document, int extend);
int utextedit_document_move_word_left(utextedit_document_t *document,
                                      int extend);
int utextedit_document_move_word_right(utextedit_document_t *document,
                                       int extend);
int utextedit_document_move_home(utextedit_document_t *document, int extend);
int utextedit_document_move_end(utextedit_document_t *document, int extend);
int utextedit_document_move_start(utextedit_document_t *document, int extend);
int utextedit_document_move_finish(utextedit_document_t *document, int extend);
int utextedit_document_move_up(utextedit_document_t *document,
                               uint32_t wrap_cols, uint32_t rows, int extend);
int utextedit_document_move_down(utextedit_document_t *document,
                                 uint32_t wrap_cols, uint32_t rows, int extend);
/* Put the cursor on a cell of the wrapped text (the mouse). */
int utextedit_document_move_to_visual(utextedit_document_t *document,
                                      uint32_t wrap_cols, uint32_t row,
                                      uint32_t col, int extend);

/* The next match of needle at or after from, wrapping round to the start,
 * ignoring ASCII case. Selects it; 0 if there is none. */
int utextedit_document_find(utextedit_document_t *document,
                            const char *needle, uint32_t from);

void utextedit_document_visual_position(const utextedit_document_t *document,
                                        uint32_t offset, uint32_t wrap_cols,
                                        uint32_t *row, uint32_t *col);
void utextedit_document_cursor_visual_position(
    const utextedit_document_t *document, uint32_t wrap_cols, uint32_t *row,
    uint32_t *col);
uint32_t utextedit_document_total_visual_rows(
    const utextedit_document_t *document, uint32_t wrap_cols);

#endif /* GEMOS_USERLAND_TEXTEDIT_DOCUMENT_H */
