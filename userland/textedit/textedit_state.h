#ifndef GEMOS_USERLAND_TEXTEDIT_STATE_H
#define GEMOS_USERLAND_TEXTEDIT_STATE_H

#include "textedit_document.h"

#include <gemos/console_abi.h>

#include <stdint.h>

#define UTEXTEDIT_STATUS_MAX 64U
#define UTEXTEDIT_PATH_MAX 128U /* the kernel's limit, NUL included */

/* A question shown over the text; keys go to it until it is answered. */
typedef enum {
  UTEXTEDIT_PROMPT_NONE = 0,
  UTEXTEDIT_PROMPT_SAVE_AS, /* the path to save to */
  UTEXTEDIT_PROMPT_OPEN,    /* the path to open */
  UTEXTEDIT_PROMPT_UNSAVED, /* save the changes first? Y / N / Esc */
} utextedit_prompt_t;

/* What happens once the changes are saved or discarded */
typedef enum {
  UTEXTEDIT_NEXT_NONE = 0,
  UTEXTEDIT_NEXT_EXIT,
  UTEXTEDIT_NEXT_NEW,
  UTEXTEDIT_NEXT_OPEN,
} utextedit_next_t;

typedef struct {
  utextedit_document_t document;
  char path[UTEXTEDIT_PATH_MAX]; /* empty until the document has a file */
  char status[UTEXTEDIT_STATUS_MAX];
  char input[UTEXTEDIT_PATH_MAX]; /* the path typed into a prompt */
  uint32_t input_length;
  uint32_t viewport_top_row;
  utextedit_prompt_t prompt;
  utextedit_next_t next;
  uint8_t status_error;
  uint8_t dirty; /* the frame has to be drawn again */
  uint8_t should_exit;
} utextedit_state_t;

void utextedit_state_init(utextedit_state_t *state);
/* The file the editor was started with (argv[1]). A file that does not
 * exist yet gives an empty document that is saved under that path. */
void utextedit_state_open_start_file(utextedit_state_t *state,
                                     const char *path);
void utextedit_state_handle_event(utextedit_state_t *state,
                                  const gemos_console_event_t *event);
void utextedit_state_mark_clean(utextedit_state_t *state);

#endif /* GEMOS_USERLAND_TEXTEDIT_STATE_H */
