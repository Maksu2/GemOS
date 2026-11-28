#include "textedit_state.h"
#include <gemos/paths.h>

#include "../common/format.h"
#include "textedit_theme.h"

#include <gemos/user_api.h>

/* A file is read here first, so a failed open leaves the document as it
 * was. Room for one byte more than a document holds: a file that fills it
 * does not fit. */
static char utextedit_file_buffer[UTEXTEDIT_DOC_MAX + 2U];
/* Ctrl+C and Ctrl+X put the selection here, Ctrl+V takes it back. */
static char utextedit_clipboard[UTEXTEDIT_DOC_MAX];
static uint32_t utextedit_clipboard_length;

static void utextedit_state_set_status(utextedit_state_t *state,
                                       const char *text) {
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

static void utextedit_state_count_status(utextedit_state_t *state,
                                         const char *what, uint32_t count,
                                         const char *unit) {
  char count_text[16];

  gemos_u32_to_text(count, count_text, sizeof(count_text));
  utextedit_state_set_status(state, what);
  utextedit_state_append_status(state, count_text);
  utextedit_state_append_status(state, unit);
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

/* The cursor stays on screen: the view moves as little as it can. */
static void utextedit_state_sync_viewport(utextedit_state_t *state) {
  uint32_t cursor_row;

  utextedit_document_cursor_visual_position(&state->document,
                                            UTEXTEDIT_BODY_COLS, &cursor_row,
                                            0);
  if (cursor_row < state->viewport_top_row) {
    state->viewport_top_row = cursor_row;
  } else if (cursor_row >= state->viewport_top_row + UTEXTEDIT_BODY_ROWS) {
    state->viewport_top_row = cursor_row - UTEXTEDIT_BODY_ROWS + 1U;
  }
  state->dirty = 1U;
}

static void utextedit_state_after_move(utextedit_state_t *state, int changed) {
  if (changed) {
    utextedit_state_sync_viewport(state);
  }
}

static void utextedit_state_after_edit(utextedit_state_t *state, int changed,
                                       const char *refused) {
  if (changed) {
    utextedit_state_set_status(state, "Editing");
    utextedit_state_sync_viewport(state);
  } else if (refused != 0) {
    utextedit_state_set_error(state, refused, "");
  }
}

static void utextedit_state_new_document(utextedit_state_t *state) {
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
    utextedit_state_set_error(state, "Cannot open: ", "larger than 128 KB");
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
    state->document.text[kept++] = ch;
  }

  utextedit_document_loaded(&state->document, kept);
  if (changed != 0U) {
    utextedit_document_mark_unsaved(&state->document);
  }
  gemos_copy(state->path, sizeof(state->path), path);
  state->viewport_top_row = 0U;
  utextedit_state_set_status(state, "Opened ");
  utextedit_state_append_status(state, utextedit_path_name(path));
  if (changed != 0U) {
    char count_text[16];

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
  utextedit_document_mark_saved(&state->document);
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
  state->dragging = 0U;
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
                           state->path[0] != '\0' ? state->path : "~/");
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
  if (utextedit_document_modified(&state->document)) {
    state->prompt = UTEXTEDIT_PROMPT_UNSAVED;
    state->dragging = 0U;
    state->dirty = 1U;
    return;
  }
  utextedit_state_continue(state);
}

/* Save to the document's file, or ask for one; then go on with next. */
static void utextedit_state_save(utextedit_state_t *state) {
  if (state->path[0] == '\0') {
    utextedit_state_prompt(state, UTEXTEDIT_PROMPT_SAVE_AS, "~/");
    return;
  }
  if (utextedit_state_save_to(state, state->path)) {
    utextedit_state_continue(state);
  } else {
    state->prompt = UTEXTEDIT_PROMPT_NONE;
    state->next = UTEXTEDIT_NEXT_NONE;
  }
}

/* Search from the end of the selection, so the same search moves on. */
static void utextedit_state_find_next(utextedit_state_t *state) {
  uint32_t end;

  if (state->find[0] == '\0') {
    utextedit_state_prompt(state, UTEXTEDIT_PROMPT_FIND, "");
    return;
  }
  utextedit_document_selection(&state->document, 0, &end);
  if (utextedit_document_find(&state->document, state->find, end)) {
    utextedit_state_set_status(state, "Found ");
    utextedit_state_append_status(state, state->find);
    utextedit_state_sync_viewport(state);
  } else {
    utextedit_state_set_error(state, "Not found: ", state->find);
  }
}

/* Enter in a path prompt. Relative paths and ~/ start at the user's home.
 * A saved file gets .txt when its name has no extension. */
static void utextedit_state_prompt_enter(utextedit_state_t *state) {
  char path[UTEXTEDIT_PATH_MAX];
  const char *input = state->input;
  const char *prefix = input[0] == '/' ? "" : GEMOS_HOME_PATH "/";
  size_t prefix_length = gemos_strlen(prefix);
  const char *name;
  size_t length;

  if (state->prompt == UTEXTEDIT_PROMPT_FIND) {
    state->prompt = UTEXTEDIT_PROMPT_NONE;
    if (state->input_length == 0U) {
      state->dirty = 1U;
      return;
    }
    gemos_copy(state->find, sizeof(state->find), state->input);
    utextedit_state_find_next(state);
    return;
  }

  if (input[0] == '~' && input[1] == '/') {
    input += 2;
  }
  length = prefix_length + gemos_strlen(input);
  if (length >= sizeof(path)) {
    utextedit_state_set_error(state, "Path too long", "");
    return;
  }
  gemos_copy(path, sizeof(path), prefix);
  gemos_copy(path + prefix_length, sizeof(path) - prefix_length, input);

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

  while (*name != '\0' && *name != '.') {
    name++;
  }
  if (*name == '\0') {
    if (length + 4U >= sizeof(path)) {
      utextedit_state_set_error(state, "Path too long", "");
      return;
    }
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

/* -- clipboard -------------------------------------------------------------- */

static int utextedit_state_copy(utextedit_state_t *state) {
  uint32_t start;
  uint32_t end;

  if (!utextedit_document_selection(&state->document, &start, &end)) {
    utextedit_state_set_status(state, "Select text to copy");
    return 0;
  }
  for (uint32_t i = start; i < end; ++i) {
    utextedit_clipboard[i - start] = state->document.text[i];
  }
  utextedit_clipboard_length = end - start;
  utextedit_state_count_status(state, "Copied ", end - start, " bytes");
  return 1;
}

static void utextedit_state_paste(utextedit_state_t *state) {
  if (utextedit_clipboard_length == 0U) {
    utextedit_state_set_status(state, "Nothing to paste");
    return;
  }
  utextedit_state_after_edit(
      state,
      utextedit_document_insert(&state->document, utextedit_clipboard,
                                utextedit_clipboard_length),
      "No room: a document holds 128 KB");
}

/* Spaces up to the next tab stop (every 4 columns) */
static void utextedit_state_tab(utextedit_state_t *state) {
  static const char spaces[] = "    ";
  uint32_t col;

  utextedit_document_cursor_visual_position(&state->document,
                                            UTEXTEDIT_BODY_COLS, 0, &col);
  utextedit_state_after_edit(
      state, utextedit_document_insert(&state->document, spaces, 4U - col % 4U),
      "No room: a document holds 128 KB");
}

/* -- the mouse ---------------------------------------------------------------- */

/* The wrapped row and column under a cell of the grid. Above or below the
 * text the view scrolls by a row while a drag goes on. */
static void utextedit_state_pointer_cell(utextedit_state_t *state, int32_t col,
                                         int32_t row, int scroll,
                                         uint32_t *visual_row,
                                         uint32_t *visual_col) {
  int32_t body_row = row - (int32_t)UTEXTEDIT_BODY_TOP_ROW;
  int32_t body_col = col - (int32_t)UTEXTEDIT_CONTENT_COL;
  uint32_t total = utextedit_document_total_visual_rows(&state->document,
                                                         UTEXTEDIT_BODY_COLS);

  if (body_row < 0) {
    if (scroll && state->viewport_top_row > 0U) {
      state->viewport_top_row--;
      state->dirty = 1U;
    }
    body_row = 0;
  } else if (body_row >= (int32_t)UTEXTEDIT_BODY_ROWS) {
    if (scroll && state->viewport_top_row + UTEXTEDIT_BODY_ROWS < total) {
      state->viewport_top_row++;
      state->dirty = 1U;
    }
    body_row = (int32_t)UTEXTEDIT_BODY_ROWS - 1;
  }
  if (body_col < 0) {
    body_col = 0;
  } else if (body_col >= (int32_t)UTEXTEDIT_BODY_COLS) {
    body_col = (int32_t)UTEXTEDIT_BODY_COLS - 1;
  }
  *visual_row = state->viewport_top_row + (uint32_t)body_row;
  *visual_col = (uint32_t)body_col;
}

static void utextedit_state_pointer(utextedit_state_t *state,
                                    const gemos_console_event_t *event) {
  int32_t col = (int32_t)event->key_code;
  int32_t row = (int32_t)event->character;
  uint32_t visual_row;
  uint32_t visual_col;

  if (event->type == GEMOS_CONSOLE_EVENT_POINTER_RELEASE) {
    state->dragging = 0U;
    return;
  }
  if (state->prompt != UTEXTEDIT_PROMPT_NONE) {
    return;
  }
  if (event->type == GEMOS_CONSOLE_EVENT_POINTER_MOVE) {
    if (state->dragging) {
      utextedit_state_pointer_cell(state, col, row, 1, &visual_row,
                                   &visual_col);
      if (utextedit_document_move_to_visual(&state->document,
                                            UTEXTEDIT_BODY_COLS, visual_row,
                                            visual_col, 1)) {
        state->dirty = 1U;
      }
    }
    return;
  }

  /* a press, in the text: the cursor goes there */
  if (row < (int32_t)UTEXTEDIT_BODY_TOP_ROW ||
      row >= (int32_t)(UTEXTEDIT_BODY_TOP_ROW + UTEXTEDIT_BODY_ROWS)) {
    return;
  }
  uint32_t now = (uint32_t)gemos_ticks_ms();
  int twice = col == state->last_click_col && row == state->last_click_row &&
              now - state->last_click_ms <= UTEXTEDIT_DOUBLE_CLICK_MS;

  utextedit_state_pointer_cell(state, col, row, 0, &visual_row, &visual_col);
  utextedit_document_move_to_visual(
      &state->document, UTEXTEDIT_BODY_COLS, visual_row, visual_col,
      (event->modifiers & GEMOS_KEYMOD_SHIFT) != 0U);
  if (twice) {
    utextedit_document_select_word(&state->document);
    state->last_click_ms = now - UTEXTEDIT_DOUBLE_CLICK_MS - 1U;
  } else {
    state->last_click_ms = now;
    state->dragging = 1U;
  }
  state->last_click_col = col;
  state->last_click_row = row;
  state->dirty = 1U;
}

/* -- keys ----------------------------------------------------------------- */

/* Ctrl with a letter; 1 if it was one of the editor's. */
static int utextedit_state_control_key(utextedit_state_t *state,
                                       const gemos_console_event_t *event) {
  int shift = (event->modifiers & GEMOS_KEYMOD_SHIFT) != 0U;

  switch ((char)(uint8_t)event->character) {
  case 'n':
  case 'N':
    utextedit_state_request(state, UTEXTEDIT_NEXT_NEW);
    return 1;
  case 'o':
  case 'O':
    utextedit_state_request(state, UTEXTEDIT_NEXT_OPEN);
    return 1;
  case 's':
  case 'S':
    if (shift) {
      utextedit_state_prompt(state, UTEXTEDIT_PROMPT_SAVE_AS,
                             state->path[0] != '\0' ? state->path : "~/");
    } else {
      utextedit_state_save(state);
    }
    return 1;
  case 'q':
  case 'Q':
    utextedit_state_request(state, UTEXTEDIT_NEXT_EXIT);
    return 1;
  case 'a':
  case 'A':
    utextedit_document_select_all(&state->document);
    state->dirty = 1U;
    return 1;
  case 'c':
  case 'C':
    (void)utextedit_state_copy(state);
    return 1;
  case 'x':
  case 'X':
    if (utextedit_state_copy(state)) {
      utextedit_state_after_edit(
          state, utextedit_document_insert(&state->document, "", 0U), 0);
      utextedit_state_count_status(state, "Cut ", utextedit_clipboard_length,
                                   " bytes");
    }
    return 1;
  case 'v':
  case 'V':
    utextedit_state_paste(state);
    return 1;
  case 'z':
  case 'Z':
    if (shift ? utextedit_document_redo(&state->document)
              : utextedit_document_undo(&state->document)) {
      utextedit_state_set_status(state, shift ? "Redone" : "Undone");
      utextedit_state_sync_viewport(state);
    } else {
      utextedit_state_set_status(state, shift ? "Nothing to redo"
                                              : "Nothing to undo");
    }
    return 1;
  case 'y':
  case 'Y':
    if (utextedit_document_redo(&state->document)) {
      utextedit_state_set_status(state, "Redone");
      utextedit_state_sync_viewport(state);
    } else {
      utextedit_state_set_status(state, "Nothing to redo");
    }
    return 1;
  case 'f':
  case 'F':
    utextedit_state_prompt(state, UTEXTEDIT_PROMPT_FIND, state->find);
    return 1;
  case 'g':
  case 'G':
    utextedit_state_find_next(state);
    return 1;
  default:
    return 0;
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
  state->find[0] = '\0';
  state->viewport_top_row = 0U;
  state->prompt = UTEXTEDIT_PROMPT_NONE;
  state->next = UTEXTEDIT_NEXT_NONE;
  state->status_error = 0U;
  state->dirty = 1U;
  state->should_exit = 0U;
  state->dragging = 0U;
  state->last_click_col = -1;
  state->last_click_row = -1;
  state->last_click_ms = 0U;
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
  utextedit_document_t *document;
  int extend;
  int control;
  char ch;

  if (state == 0 || event == 0) {
    return;
  }
  document = &state->document;

  if (event->type == GEMOS_CONSOLE_EVENT_CLOSE_REQUEST) {
    /* the close button works like Esc; while asking, keep asking */
    if (state->prompt != UTEXTEDIT_PROMPT_UNSAVED) {
      state->prompt = UTEXTEDIT_PROMPT_NONE;
      utextedit_state_request(state, UTEXTEDIT_NEXT_EXIT);
    }
    return;
  }
  if (event->type == GEMOS_CONSOLE_EVENT_POINTER_PRESS ||
      event->type == GEMOS_CONSOLE_EVENT_POINTER_MOVE ||
      event->type == GEMOS_CONSOLE_EVENT_POINTER_RELEASE) {
    utextedit_state_pointer(state, event);
    return;
  }
  if (event->type != GEMOS_CONSOLE_EVENT_KEY) {
    return;
  }

  if (state->prompt != UTEXTEDIT_PROMPT_NONE) {
    utextedit_state_prompt_key(state, event);
    return;
  }

  /* Esc drops a selection first, then closes */
  if (event->character == GEMOS_KEY_ESC) {
    if (utextedit_document_clear_selection(document)) {
      state->dirty = 1U;
    } else {
      utextedit_state_request(state, UTEXTEDIT_NEXT_EXIT);
    }
    return;
  }

  extend = (event->modifiers & GEMOS_KEYMOD_SHIFT) != 0U;
  control = (event->modifiers & GEMOS_KEYMOD_CTRL) != 0U;
  if (control && event->character < 0x80U &&
      utextedit_state_control_key(state, event)) {
    return;
  }

  switch (event->character) {
  case GEMOS_KEY_BACKSPACE:
    utextedit_state_after_edit(state, utextedit_document_backspace(document), 0);
    break;
  case GEMOS_KEY_DELETE:
    utextedit_state_after_edit(state, utextedit_document_delete(document), 0);
    break;
  case GEMOS_KEY_ENTER:
    utextedit_state_after_edit(state, utextedit_document_type(document, '\n'),
                               "No room: a document holds 128 KB");
    break;
  case '\t':
    utextedit_state_tab(state);
    break;
  case GEMOS_KEY_LEFT:
    utextedit_state_after_move(
        state, control ? utextedit_document_move_word_left(document, extend)
                       : utextedit_document_move_left(document, extend));
    break;
  case GEMOS_KEY_RIGHT:
    utextedit_state_after_move(
        state, control ? utextedit_document_move_word_right(document, extend)
                       : utextedit_document_move_right(document, extend));
    break;
  case GEMOS_KEY_UP:
    utextedit_state_after_move(
        state, utextedit_document_move_up(document, UTEXTEDIT_BODY_COLS, 1U,
                                          extend));
    break;
  case GEMOS_KEY_DOWN:
    utextedit_state_after_move(
        state, utextedit_document_move_down(document, UTEXTEDIT_BODY_COLS, 1U,
                                            extend));
    break;
  case GEMOS_KEY_PAGE_UP:
    if (state->viewport_top_row > 0U) {
      state->viewport_top_row =
          state->viewport_top_row > UTEXTEDIT_BODY_ROWS - 1U
              ? state->viewport_top_row - (UTEXTEDIT_BODY_ROWS - 1U)
              : 0U;
      state->dirty = 1U;
    }
    utextedit_state_after_move(
        state, utextedit_document_move_up(document, UTEXTEDIT_BODY_COLS,
                                          UTEXTEDIT_BODY_ROWS - 1U, extend));
    break;
  case GEMOS_KEY_PAGE_DOWN: {
    uint32_t total =
        utextedit_document_total_visual_rows(document, UTEXTEDIT_BODY_COLS);

    if (state->viewport_top_row + UTEXTEDIT_BODY_ROWS < total) {
      state->viewport_top_row += UTEXTEDIT_BODY_ROWS - 1U;
      state->dirty = 1U;
    }
    utextedit_state_after_move(
        state, utextedit_document_move_down(document, UTEXTEDIT_BODY_COLS,
                                            UTEXTEDIT_BODY_ROWS - 1U, extend));
    break;
  }
  case GEMOS_KEY_HOME:
    utextedit_state_after_move(
        state, control ? utextedit_document_move_start(document, extend)
                       : utextedit_document_move_home(document, extend));
    break;
  case GEMOS_KEY_END:
    utextedit_state_after_move(
        state, control ? utextedit_document_move_finish(document, extend)
                       : utextedit_document_move_end(document, extend));
    break;
  default:
    ch = (char)(uint8_t)event->character;
    if (!control && ch >= 0x20 && ch <= 0x7E) {
      utextedit_state_after_edit(state, utextedit_document_type(document, ch),
                                 "No room: a document holds 128 KB");
    }
    break;
  }
}

void utextedit_state_mark_clean(utextedit_state_t *state) {
  if (state != 0) {
    state->dirty = 0U;
  }
}
