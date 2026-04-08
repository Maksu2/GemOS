#ifndef GEMOS_USERLAND_TEXTEDIT_DOCUMENT_H
#define GEMOS_USERLAND_TEXTEDIT_DOCUMENT_H

#include <stdint.h>

#define UTEXTEDIT_DOC_MAX 8192U

typedef struct {
  char text[UTEXTEDIT_DOC_MAX + 1U];
  uint32_t length;
  uint32_t cursor;
  uint32_t preferred_visual_col;
  uint8_t preferred_visual_col_valid;
  uint8_t dirty;
} utextedit_document_t;

void utextedit_document_init(utextedit_document_t *document);
void utextedit_document_reset(utextedit_document_t *document);
int utextedit_document_insert_char(utextedit_document_t *document, char ch);
int utextedit_document_insert_newline(utextedit_document_t *document);
int utextedit_document_backspace(utextedit_document_t *document);
int utextedit_document_move_left(utextedit_document_t *document);
int utextedit_document_move_right(utextedit_document_t *document);
int utextedit_document_move_home(utextedit_document_t *document);
int utextedit_document_move_end(utextedit_document_t *document);
int utextedit_document_move_up(utextedit_document_t *document,
                               uint32_t wrap_cols);
int utextedit_document_move_down(utextedit_document_t *document,
                                 uint32_t wrap_cols);
void utextedit_document_cursor_visual_position(
    const utextedit_document_t *document, uint32_t wrap_cols, uint32_t *row,
    uint32_t *col);
uint32_t utextedit_document_total_visual_rows(
    const utextedit_document_t *document, uint32_t wrap_cols);

#endif /* GEMOS_USERLAND_TEXTEDIT_DOCUMENT_H */
