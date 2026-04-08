#ifndef GEMOS_USERLAND_TEXTEDIT_STATE_H
#define GEMOS_USERLAND_TEXTEDIT_STATE_H

#include "textedit_document.h"

#include <gemos/console_abi.h>

#include <stdint.h>

#define UTEXTEDIT_STATUS_MAX 64U
#define UTEXTEDIT_FILENAME_MAX 20U

typedef struct {
  utextedit_document_t document;
  char filename[UTEXTEDIT_FILENAME_MAX + 1U];
  char status[UTEXTEDIT_STATUS_MAX];
  uint32_t viewport_top_row;
  uint8_t has_filename;
  uint8_t dirty;
  uint8_t should_exit;
} utextedit_state_t;

void utextedit_state_init(utextedit_state_t *state);
void utextedit_state_handle_event(utextedit_state_t *state,
                                  const gemos_console_event_t *event);
void utextedit_state_mark_clean(utextedit_state_t *state);

#endif /* GEMOS_USERLAND_TEXTEDIT_STATE_H */
