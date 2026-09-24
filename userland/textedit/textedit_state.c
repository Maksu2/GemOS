#include "textedit_state.h"

#include "../common/format.h"
#include "textedit_theme.h"

#include <gemos/user_api.h>

/* Room for one byte more than a document holds: a file that fills it does
 * not fit. */
static char utextedit_file_buffer[UTEXTEDIT_DOC_MAX + 2U];

static void utextedit_state_set_status(utextedit_state_t *state,
                                       const char *text) {
  if (state == 0) {
    return;
  }

  gemos_copy(state->status, sizeof(state->status), text);
  state->status_error = 0U;
  state->dirty = 1U;
}

static void utextedit_state_append_status(utextedit_state_t *state,
                                          const char *text) {
  size_t length = gemos_strlen(state->status);

  gemos_copy(state->status + length, sizeof(state->status) - length, text);
  state->dirty = 1U;
}

static void utextedit_state_set_error(utextedit_state_t *state,
                                      const char *what, const char *why) {
  utextedit_state_set_status(state, what);
  utextedit_state_append_status(state, why);
  state->status_error = 1U;
}

/* The name at the end of a path */
static const char *utextedit_path_name(const char *path) {
  const char *name = path;

  for (; *path != '\0'; ++path) {
    if (*path == '/') {
      name = path + 1;
    }
  }
  return name;
}

/* Why a file syscall failed, in a few words */
static const char *utextedit_file_error(int32_t error, int saving) {
  switch (error) {
  case GEMOS_ERR_DENIED:
    return "programs and system files are read-only";
  case GEMOS_ERR_NOENT:
    return saving ? "no such folder, or no disk" : "no such file";
  case GEMOS_ERR_TOO_BIG:
    return "the disk is full";
  case GEMOS_ERR_INVAL:
    return saving ? "bad name, or a folder" : "not a file";
  default:
    return "error";
  }
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
  state->path[0] = '\0';
  state->viewport_top_row = 0U;
  utextedit_state_set_status(state, "New document");
}

/* Read path into the document; 1 on success. A file that does not exist
 * is an error unless missing_ok: then the document starts empty and is
 * saved under that path. The editor keeps printable ASCII and newlines:
 * CR LF becomes LF, a tab a space, other bytes '?'. */
static int utextedit_state_open_file(utextedit_state_t *state,
                                     const char *path, int missing_ok) {
  char count_text[16];
  int32_t length = gemos_file_read(path, utextedit_file_buffer,
                                   sizeof(utextedit_file_buffer));
  uint32_t kept = 0U;
  uint32_t changed = 0U;

  if (length == GEMOS_ERR_NOENT && missing_ok) {
    utextedit_document_reset(&state->document);
    gemos_copy(state->path, sizeof(state->path), path);
    state->viewport_top_row = 0U;
    utextedit_state_set_status(state, "New file ");
    utextedit_state_append_status(state, utextedit_path_name(path));
    return 1;
  }
  if (length < 0) {
    utextedit_state_set_error(state, "Cannot open: ",
                              utextedit_file_error(length, 0));
    return 0;
  }
  if ((uint32_t)length > UTEXTEDIT_DOC_MAX) {
    utextedit_state_set_error(state, "Cannot open: ", "larger than 8 KB");
    return 0;
  }

  for (uint32_t i = 0U; i < (uint32_t)length; ++i) {
    char ch = utextedit_file_buffer[i];

    if (ch == '\0') {
      utextedit_state_set_error(state, "Cannot open: ", "not a text file");
      return 0;
    }
    if (ch == '\r' && i + 1U < (uint32_t)length &&
        utextedit_file_buffer[i + 1U] == '\n') {
      changed++;
      continue;
    }
    if (ch == '\t') {
      ch = ' ';
      changed++;
    } else if (ch != '\n' && (ch < 0x20 || ch > 0x7E)) {
      ch = '?';
      changed++;
    }
    utextedit_file_buffer[kept++] = ch;
  }

  utextedit_document_load(&state->document, utextedit_file_buffer, kept);
  gemos_copy(state->path, sizeof(state->path), path);
  state->viewport_top_row = 0U;
  state->document.modified = changed != 0U;
  utextedit_state_set_status(state, "Opened ");
  utextedit_state_append_status(state, utextedit_path_name(path));
  if (changed != 0U) {
    gemos_u32_to_text(changed, count_text, sizeof(count_text));
    utextedit_state_append_status(state, ", changed ");
    utextedit_state_append_status(state, count_text);
    utextedit_state_append_status(state, " characters");
  }
  return 1;
}

/* Write the document to path; on success that is the document's file. */
static int utextedit_state_save_to(utextedit_state_t *state,
                                   const char *path) {
  char size_text[16];
  int32_t result = gemos_file_write(path, state->document.text,
                                    state->document.length);

  if (result < 0) {
    utextedit_state_set_error(state, "Cannot save: ",
                              utextedit_file_error(result, 1));
    return 0;
  }
  if (path != state->path) {
    gemos_copy(state->path, sizeof(state->path), path);
  }
  state->document.modified = 0U;
  gemos_u32_to_text(state->document.length, size_text, sizeof(size_text));
  utextedit_state_set_status(state, "Saved ");
  utextedit_state_append_status(state, utextedit_path_name(state->path));
  utextedit_state_append_status(state, ", ");
  utextedit_state_append_status(state, size_text);
  utextedit_state_append_status(state, " bytes");
  return 1;
}

static void utextedit_state_prompt(utextedit_state_t *state,
                                   utextedit_prompt_t prompt,
                                   const char *initial) {
  state->prompt = prompt;
  gemos_copy(state->input, sizeof(state->input), initial);
  state->input_length = (uint32_t)gemos_strlen(state->input);
  state->dirty = 1U;
}

/* Carry out what waited until the changes were saved or discarded. */
static void utextedit_state_continue(utextedit_state_t *state) {
  utextedit_next_t next = state->next;

  state->next = UTEXTEDIT_NEXT_NONE;
  state->prompt = UTEXTEDIT_PROMPT_NONE;
  state->dirty = 1U;
  switch (next) {
  case UTEXTEDIT_NEXT_EXIT:
    state->should_exit = 1U;
    break;
  case UTEXTEDIT_NEXT_NEW:
    utextedit_state_new_document(state);
    break;
  case UTEXTEDIT_NEXT_OPEN:
    utextedit_state_prompt(state, UTEXTEDIT_PROMPT_OPEN,
                           state->path[0] != '\0' ? state->path : "/");
    break;
  default:
    break;
  }
}

static void utextedit_state_cancel(utextedit_state_t *state) {
  state->prompt = UTEXTEDIT_PROMPT_NONE;
  state->next = UTEXTEDIT_NEXT_NONE;
  utextedit_state_set_status(state, "Cancelled");
}

/* Close, a new document or another file: unsaved changes come first. */
static void utextedit_state_request(utextedit_state_t *state,
                                    utextedit_next_t next) {
  state->next = next;
  if (state->document.modified) {
    state->prompt = UTEXTEDIT_PROMPT_UNSAVED;
    state->dirty = 1U;
    return;
  }
  utextedit_state_continue(state);
}

/* Save to the document's file, or ask for one; then go on with next. */
static void utextedit_state_save(utextedit_state_t *state) {
  if (state->path[0] == '\0') {
    utextedit_state_prompt(state, UTEXTEDIT_PROMPT_SAVE_AS, "/");
    return;
  }
  if (utextedit_state_save_to(state, state->path)) {
    utextedit_state_continue(state);
  } else {
    state->prompt = UTEXTEDIT_PROMPT_NONE;
    state->next = UTEXTEDIT_NEXT_NONE;
  }
}

/* Enter in a path prompt. A saved file gets a leading '/' and ".txt" when
 * its name has no extension. */
static void utextedit_state_prompt_enter(utextedit_state_t *state) {
  char path[UTEXTEDIT_PATH_MAX];
  const char *name;
  size_t length;

  if (state->input[0] == '/') {
    gemos_copy(path, sizeof(path), state->input);
  } else {
    path[0] = '/';
    gemos_copy(path + 1, sizeof(path) - 1U, state->input);
  }
  name = utextedit_path_name(path);
  if (name[0] == '\0') {
    utextedit_state_set_error(state, "Type a file name", "");
    return;
  }

  if (state->prompt == UTEXTEDIT_PROMPT_OPEN) {
    state->prompt = UTEXTEDIT_PROMPT_NONE;
    (void)utextedit_state_open_file(state, path, 0);
    return;
  }

  length = gemos_strlen(path);
  while (*name != '\0' && *name != '.') {
    name++;
  }
  if (*name == '\0' && length + 4U < sizeof(path)) {
    gemos_copy(path + length, sizeof(path) - length, ".txt");
  }
  state->prompt = UTEXTEDIT_PROMPT_NONE;
  if (utextedit_state_save_to(state, path)) {
    utextedit_state_continue(state);
  } else {
    state->next = UTEXTEDIT_NEXT_NONE;
  }
}

static void utextedit_state_prompt_key(utextedit_state_t *state,
                                       const gemos_console_event_t *event) {
  uint32_t ch = event->character;

  if ((event->modifiers & GEMOS_KEYMOD_CTRL) != 0U) {
    return;
  }

  if (state->prompt == UTEXTEDIT_PROMPT_UNSAVED) {
    if (ch == 'y' || ch == 'Y') {
      state->prompt = UTEXTEDIT_PROMPT_NONE;
      utextedit_state_save(state);
    } else if (ch == 'n' || ch == 'N') {
      utextedit_state_continue(state);
    } else if (ch == GEMOS_KEY_ESC) {
      utextedit_state_cancel(state);
    }
    return;
  }

  switch (ch) {
  case GEMOS_KEY_ESC:
    utextedit_state_cancel(state);
    break;
  case GEMOS_KEY_ENTER:
    utextedit_state_prompt_enter(state);
    break;
  case GEMOS_KEY_BACKSPACE:
    if (state->input_length > 0U) {
      state->input[--state->input_length] = '\0';
      state->dirty = 1U;
    }
    break;
  default:
    /* keep room for the ".txt" a save may add */
    if (ch >= 0x20U && ch <= 0x7EU &&
        state->input_length + 5U < sizeof(state->input)) {
      state->input[state->input_length++] = (char)ch;
      state->input[state->input_length] = '\0';
      state->dirty = 1U;
    }
    break;
  }
}

void utextedit_state_init(utextedit_state_t *state) {
  if (state == 0) {
    return;
  }

  utextedit_document_init(&state->document);
  state->path[0] = '\0';
  state->status[0] = '\0';
  state->input[0] = '\0';
  state->input_length = 0U;
  state->viewport_top_row = 0U;
  state->prompt = UTEXTEDIT_PROMPT_NONE;
  state->next = UTEXTEDIT_NEXT_NONE;
  state->status_error = 0U;
  state->dirty = 1U;
  state->should_exit = 0U;
  utextedit_state_set_status(state, "Ready");
}

void utextedit_state_open_start_file(utextedit_state_t *state,
                                     const char *path) {
  if (state == 0 || path == 0 || path[0] == '\0') {
    return;
  }

  (void)utextedit_state_open_file(state, path, 1);
}

void utextedit_state_handle_event(utextedit_state_t *state,
                                  const gemos_console_event_t *event) {
  char ch;

  if (state == 0 || event == 0) {
    return;
  }

  if (event->type == GEMOS_CONSOLE_EVENT_CLOSE_REQUEST) {
    /* the close button works like Esc; while asking, keep asking */
    if (state->prompt != UTEXTEDIT_PROMPT_UNSAVED) {
      state->prompt = UTEXTEDIT_PROMPT_NONE;
      utextedit_state_request(state, UTEXTEDIT_NEXT_EXIT);
    }
    return;
  }
  if (event->type != GEMOS_CONSOLE_EVENT_KEY) {
    return;
  }

  if (state->prompt != UTEXTEDIT_PROMPT_NONE) {
    utextedit_state_prompt_key(state, event);
    return;
  }

  if (event->character == GEMOS_KEY_ESC) {
    utextedit_state_request(state, UTEXTEDIT_NEXT_EXIT);
    return;
  }

  if ((event->modifiers & GEMOS_KEYMOD_CTRL) != 0U) {
    switch ((char)(uint8_t)event->character) {
    case 'n':
    case 'N':
      utextedit_state_request(state, UTEXTEDIT_NEXT_NEW);
      return;
    case 'o':
    case 'O':
      utextedit_state_request(state, UTEXTEDIT_NEXT_OPEN);
      return;
    case 's':
    case 'S':
      if ((event->modifiers & GEMOS_KEYMOD_SHIFT) != 0U) {
        utextedit_state_prompt(state, UTEXTEDIT_PROMPT_SAVE_AS,
                               state->path[0] != '\0' ? state->path : "/");
      } else {
        utextedit_state_save(state);
      }
      return;
    case 'q':
    case 'Q':
      utextedit_state_request(state, UTEXTEDIT_NEXT_EXIT);
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
