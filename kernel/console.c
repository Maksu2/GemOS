#include "console.h"

#include "../drivers/serial.h"
#include "app/app.h"
#include "gfx/font/font.h"
#include "gfx/icons.h"
#include "gfx/primitives.h"
#include "gui/window/window.h"
#include "gui/wm/wm.h"
#include "include/heap.h"
#include "process.h"
#include "../include/gemos/syscall_abi.h"

#include <stddef.h>
#include <string.h>

#define CONSOLE_MAX_SESSIONS 8
#define CONSOLE_MIN_COLS 32U
#define CONSOLE_MAX_COLS 96U
#define CONSOLE_MIN_ROWS 8U
#define CONSOLE_MAX_ROWS 32U
#define CONSOLE_SCROLLBACK_LINES 256U
#define CONSOLE_INPUT_QUEUE_SIZE 64U
#define CONSOLE_FONT_SIZE 12
#define CONSOLE_LINE_HEIGHT 15
#define CONSOLE_CHAR_WIDTH 8
#define CONSOLE_PADDING 8
#define CONSOLE_BG_COLOR 0x111111
#define CONSOLE_TEXT_COLOR 0xD8D8D8

typedef struct {
  int in_use;
  uint32_t handle;
  uint32_t owner_pid;
  uint32_t cols;
  uint32_t rows;
  uint32_t flags;
  window_t *window;
  char title[GEMOS_CONSOLE_MAX_TITLE + 1];
  char lines[CONSOLE_SCROLLBACK_LINES][CONSOLE_MAX_COLS + 1];
  uint16_t line_lengths[CONSOLE_SCROLLBACK_LINES];
  uint16_t line_start;
  uint16_t line_count;
  uint16_t current_line;
  uint16_t cursor_col;
  gemos_console_event_t input_queue[CONSOLE_INPUT_QUEUE_SIZE];
  uint32_t input_head;
  uint32_t input_tail;
} console_session_t;

static app_t console_window_app;
static console_session_t console_sessions[CONSOLE_MAX_SESSIONS];
static uint32_t next_console_handle = 1;

static void console_reset_screen(console_session_t *session) {
  if (session == NULL) {
    return;
  }

  memset(session->lines, 0, sizeof(session->lines));
  memset(session->line_lengths, 0, sizeof(session->line_lengths));
  session->line_start = 0;
  session->line_count = 1;
  session->current_line = 0;
  session->cursor_col = 0;
}

static uint32_t console_clamp(uint32_t value, uint32_t min, uint32_t max) {
  if (value < min) {
    return min;
  }
  if (value > max) {
    return max;
  }
  return value;
}

static console_session_t *console_find_free_session(void) {
  for (size_t i = 0; i < CONSOLE_MAX_SESSIONS; ++i) {
    if (!console_sessions[i].in_use) {
      return &console_sessions[i];
    }
  }

  return NULL;
}

static console_session_t *console_find_by_pid(uint32_t owner_pid) {
  for (size_t i = 0; i < CONSOLE_MAX_SESSIONS; ++i) {
    if (console_sessions[i].in_use && console_sessions[i].owner_pid == owner_pid) {
      return &console_sessions[i];
    }
  }

  return NULL;
}

static console_session_t *console_find_owned(uint32_t owner_pid, int handle) {
  if (handle <= 0) {
    return NULL;
  }

  for (size_t i = 0; i < CONSOLE_MAX_SESSIONS; ++i) {
    if (console_sessions[i].in_use && console_sessions[i].owner_pid == owner_pid &&
        console_sessions[i].handle == (uint32_t)handle) {
      return &console_sessions[i];
    }
  }

  return NULL;
}

static uint16_t console_line_slot(const console_session_t *session,
                                  uint16_t logical_index) {
  return (uint16_t)((session->line_start + logical_index) %
                    CONSOLE_SCROLLBACK_LINES);
}

static void console_advance_line(console_session_t *session) {
  uint16_t next_index;
  uint16_t slot;

  if (session->line_count < CONSOLE_SCROLLBACK_LINES) {
    next_index = session->line_count;
    session->line_count++;
  } else {
    session->line_start =
        (uint16_t)((session->line_start + 1U) % CONSOLE_SCROLLBACK_LINES);
    next_index = (uint16_t)(CONSOLE_SCROLLBACK_LINES - 1U);
  }

  session->current_line = next_index;
  session->cursor_col = 0;
  slot = console_line_slot(session, next_index);
  session->line_lengths[slot] = 0;
  session->lines[slot][0] = '\0';
}

static void console_append_char(console_session_t *session, char ch) {
  uint16_t slot;

  if (session == NULL) {
    return;
  }

  if (ch == '\r') {
    return;
  }
  if (ch == '\n') {
    console_advance_line(session);
    return;
  }
  if (ch == '\b') {
    if (session->cursor_col > 0) {
      slot = console_line_slot(session, session->current_line);
      session->cursor_col--;
      session->line_lengths[slot] = session->cursor_col;
      session->lines[slot][session->cursor_col] = '\0';
    }
    return;
  }
  if (ch < 0x20 || ch > 0x7E) {
    return;
  }

  if (session->cursor_col >= session->cols) {
    console_advance_line(session);
  }

  slot = console_line_slot(session, session->current_line);
  session->lines[slot][session->cursor_col] = ch;
  session->cursor_col++;
  session->line_lengths[slot] = session->cursor_col;
  session->lines[slot][session->cursor_col] = '\0';
}

static int console_push_event(console_session_t *session,
                              gemos_console_event_t event) {
  uint32_t next_head;

  if (session == NULL) {
    return 0;
  }

  next_head = (session->input_head + 1U) % CONSOLE_INPUT_QUEUE_SIZE;
  if (next_head == session->input_tail) {
    return 0;
  }

  session->input_queue[session->input_head] = event;
  session->input_head = next_head;
  return 1;
}

static int console_pop_event(console_session_t *session,
                             gemos_console_event_t *event) {
  if (session == NULL || event == NULL) {
    return 0;
  }
  if (session->input_head == session->input_tail) {
    return 0;
  }

  *event = session->input_queue[session->input_tail];
  session->input_tail = (session->input_tail + 1U) % CONSOLE_INPUT_QUEUE_SIZE;
  return 1;
}

static void console_release_session(console_session_t *session) {
  if (session == NULL) {
    return;
  }

  memset(session, 0, sizeof(*session));
}

static void console_render(window_t *win) {
  console_session_t *session = (console_session_t *)win->user_data;
  uint32_t visible_lines;
  uint32_t first_logical;
  int x;
  int y;

  if (session == NULL) {
    return;
  }

  gfx_fill_rect(&win->ctx, win->client_rect.x, win->client_rect.y,
                win->client_rect.w, win->client_rect.h, CONSOLE_BG_COLOR);

  visible_lines = session->rows;
  if ((uint32_t)(win->client_rect.h / CONSOLE_LINE_HEIGHT) < visible_lines) {
    visible_lines = (uint32_t)(win->client_rect.h / CONSOLE_LINE_HEIGHT);
  }
  if (visible_lines == 0) {
    return;
  }

  first_logical = 0;
  if (session->line_count > visible_lines) {
    first_logical = session->line_count - visible_lines;
  }

  x = win->client_rect.x + CONSOLE_PADDING;
  y = win->client_rect.y + CONSOLE_PADDING;
  for (uint32_t i = 0; i < visible_lines && first_logical + i < session->line_count;
       ++i) {
    uint16_t slot = console_line_slot(session, (uint16_t)(first_logical + i));
    font_draw_text(&win->ctx, x, y, session->lines[slot], CONSOLE_FONT_SIZE,
                   CONSOLE_TEXT_COLOR);
    y += CONSOLE_LINE_HEIGHT;
  }
}

static void console_handle_event(window_t *win, event_t *event) {
  console_session_t *session = (console_session_t *)win->user_data;
  gemos_console_event_t console_event;

  if (session == NULL || event == NULL) {
    return;
  }
  if (event->type != EVENT_KEY_PRESS) {
    return;
  }

  console_event.type = GEMOS_CONSOLE_EVENT_KEY;
  console_event.key_code = event->data.key.key_code;
  console_event.character = (uint32_t)(uint8_t)event->data.key.character;
  console_push_event(session, console_event);
}

static void console_window_close(window_t *win) {
  console_session_t *session;

  if (win == NULL) {
    return;
  }

  session = (console_session_t *)win->user_data;
  if (session != NULL) {
    session->window = NULL;
    process_kill_pid(session->owner_pid, -1);
    console_release_session(session);
  }

  kfree(win);
}

void console_init(void) {
  memset(console_sessions, 0, sizeof(console_sessions));
  next_console_handle = 1;

  memset(&console_window_app, 0, sizeof(console_window_app));
  console_window_app.name = "Console";
  console_window_app.icon = &icon_terminal;
  console_window_app.render = console_render;
  console_window_app.handle_event = console_handle_event;
  console_window_app.close = console_window_close;
}

int console_open(uint32_t owner_pid, const char *title, uint32_t cols,
                 uint32_t rows, uint32_t flags) {
  console_session_t *session;
  window_t *win;
  int width;
  int height;

  if (owner_pid == 0) {
    return GEMOS_ERR_INVAL;
  }
  if (console_find_by_pid(owner_pid) != NULL) {
    return GEMOS_ERR_BUSY;
  }

  session = console_find_free_session();
  if (session == NULL) {
    return GEMOS_ERR_BUSY;
  }

  win = (window_t *)kalloc(sizeof(window_t));
  if (win == NULL) {
    return GEMOS_ERR_BUSY;
  }

  memset(session, 0, sizeof(*session));
  session->in_use = 1;
  session->handle = next_console_handle++;
  session->owner_pid = owner_pid;
  session->cols = console_clamp(cols, CONSOLE_MIN_COLS, CONSOLE_MAX_COLS);
  session->rows = console_clamp(rows, CONSOLE_MIN_ROWS, CONSOLE_MAX_ROWS);
  session->flags = flags;
  session->window = win;
  session->input_head = 0;
  session->input_tail = 0;
  console_reset_screen(session);

  if (title != NULL && title[0] != '\0') {
    strncpy(session->title, title, GEMOS_CONSOLE_MAX_TITLE);
    session->title[GEMOS_CONSOLE_MAX_TITLE] = '\0';
  } else {
    strcpy(session->title, "User Terminal");
  }

  width = (int)(session->cols * CONSOLE_CHAR_WIDTH + (CONSOLE_PADDING * 2));
  height = (int)(session->rows * CONSOLE_LINE_HEIGHT + (CONSOLE_PADDING * 2) + 26);

  window_init(win, 120, 100, width, height);
  window_set_title(win, session->title);
  win->flags = WINDOW_FLAG_NO_RESIZE;
  win->app = &console_window_app;
  win->user_data = session;

  wm_add_window(win);
  serial_print("[CONSOLE] Opened PID=");
  serial_print_dec(owner_pid);
  serial_print(" handle=");
  serial_print_dec(session->handle);
  serial_print("\n");
  return (int)session->handle;
}

int console_write(uint32_t owner_pid, int handle, const char *buffer,
                  size_t length) {
  console_session_t *session = console_find_owned(owner_pid, handle);

  if (session == NULL || buffer == NULL) {
    return GEMOS_ERR_INVAL;
  }

  for (size_t i = 0; i < length; ++i) {
    console_append_char(session, buffer[i]);
  }

  return (int)length;
}

int console_poll_event(uint32_t owner_pid, int handle,
                       gemos_console_event_t *event) {
  console_session_t *session = console_find_owned(owner_pid, handle);

  if (session == NULL || event == NULL) {
    return GEMOS_ERR_INVAL;
  }

  if (!console_pop_event(session, event)) {
    return 0;
  }

  return 1;
}

int console_clear(uint32_t owner_pid, int handle) {
  console_session_t *session = console_find_owned(owner_pid, handle);

  if (session == NULL) {
    return GEMOS_ERR_INVAL;
  }

  console_reset_screen(session);
  return GEMOS_OK;
}

void console_destroy_for_pid(uint32_t owner_pid) {
  console_session_t *session = console_find_by_pid(owner_pid);

  if (session == NULL) {
    return;
  }
  if (session->window != NULL) {
    wm_remove_window(session->window);
    return;
  }

  console_release_session(session);
}
