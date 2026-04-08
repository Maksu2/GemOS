#include "textedit_state.h"

#include "../common/format.h"
#include "textedit_theme.h"

static void utextedit_state_set_status(utextedit_state_t *state,
                                       const char *text) {
  if (state == 0) {
    return;
  }

  gemos_copy(state->status, sizeof(state->status), text);
  state->dirty = 1U;
}

static void utextedit_state_sync_viewport(utextedit_state_t *state) {
  uint32_t cursor_row;
  uint32_t cursor_col;

  if (state == 0) {
    return;
  }

  utextedit_document_cursor_visual_position(&state->document, UTEXTEDIT_BODY_COLS,
                                            &cursor_row, &cursor_col);
  (void)cursor_col;

  if (cursor_row < state->viewport_top_row) {
    state->viewport_top_row = cursor_row;
    state->dirty = 1U;
    return;
  }

  if (cursor_row >= state->viewport_top_row + UTEXTEDIT_BODY_ROWS) {
    state->viewport_top_row = cursor_row - UTEXTEDIT_BODY_ROWS + 1U;
    state->dirty = 1U;
  }
}

static void utextedit_state_after_move(utextedit_state_t *state, int changed) {
  if (state == 0 || !changed) {
    return;
  }

  utextedit_state_sync_viewport(state);
  state->dirty = 1U;
}

static void utextedit_state_new_document(utextedit_state_t *state) {
  if (state == 0) {
    return;
  }

  utextedit_document_reset(&state->document);
  state->filename[0] = '\0';
  state->has_filename = 0U;
  state->viewport_top_row = 0U;
  utextedit_state_set_status(state, "New document");
}

void utextedit_state_init(utextedit_state_t *state) {
  if (state == 0) {
    return;
  }

  utextedit_document_init(&state->document);
  state->filename[0] = '\0';
  state->status[0] = '\0';
  state->viewport_top_row = 0U;
  state->has_filename = 0U;
  state->dirty = 1U;
  state->should_exit = 0U;
  utextedit_state_set_status(state, "Ready");
}

void utextedit_state_handle_event(utextedit_state_t *state,
                                  const gemos_console_event_t *event) {
  char ch;

  if (state == 0 || event == 0) {
    return;
  }

  if (event->type == GEMOS_CONSOLE_EVENT_CLOSE_REQUEST) {
    state->should_exit = 1U;
    state->dirty = 1U;
    return;
  }
  if (event->type != GEMOS_CONSOLE_EVENT_KEY) {
    return;
  }

  if (event->character == GEMOS_KEY_ESC) {
    state->should_exit = 1U;
    state->dirty = 1U;
    return;
  }

  if ((event->modifiers & GEMOS_KEYMOD_CTRL) != 0U) {
    switch ((char)(uint8_t)event->character) {
    case 'n':
    case 'N':
      utextedit_state_new_document(state);
      return;
    case 'o':
    case 'O':
      utextedit_state_set_status(state, "Open arrives in next step");
      return;
    case 's':
    case 'S':
      utextedit_state_set_status(state, "Save arrives in next step");
      return;
    case 'q':
    case 'Q':
      state->should_exit = 1U;
      state->dirty = 1U;
      return;
    default:
      break;
    }
  }

  switch (event->character) {
  case GEMOS_KEY_BACKSPACE:
    if (utextedit_document_backspace(&state->document)) {
      utextedit_state_set_status(state, "Editing");
      utextedit_state_sync_viewport(state);
    }
    break;
  case GEMOS_KEY_ENTER:
    if (utextedit_document_insert_newline(&state->document)) {
      utextedit_state_set_status(state, "Editing");
      utextedit_state_sync_viewport(state);
    }
    break;
  case GEMOS_KEY_LEFT:
    utextedit_state_after_move(
        state, utextedit_document_move_left(&state->document));
    break;
  case GEMOS_KEY_RIGHT:
    utextedit_state_after_move(
        state, utextedit_document_move_right(&state->document));
    break;
  case GEMOS_KEY_UP:
    utextedit_state_after_move(state,
                               utextedit_document_move_up(&state->document,
                                                          UTEXTEDIT_BODY_COLS));
    break;
  case GEMOS_KEY_DOWN:
    utextedit_state_after_move(
        state,
        utextedit_document_move_down(&state->document, UTEXTEDIT_BODY_COLS));
    break;
  case GEMOS_KEY_HOME:
    utextedit_state_after_move(
        state, utextedit_document_move_home(&state->document));
    break;
  case GEMOS_KEY_END:
    utextedit_state_after_move(
        state, utextedit_document_move_end(&state->document));
    break;
  default:
    ch = (char)(uint8_t)event->character;
    if (ch >= 0x20 && ch <= 0x7E &&
        utextedit_document_insert_char(&state->document, ch)) {
      utextedit_state_set_status(state, "Editing");
      utextedit_state_sync_viewport(state);
    }
    break;
  }
}

void utextedit_state_mark_clean(utextedit_state_t *state) {
  if (state != 0) {
    state->dirty = 0U;
  }
}
