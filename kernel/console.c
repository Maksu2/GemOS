#include "console.h"

#include "../drivers/keyboard.h"
#include "../drivers/serial.h"
#include "app/app.h"
#include "gfx/font/font.h"
#include "gfx/icons.h"
#include "gfx/primitives.h"
#include "gui/window/window.h"
#include "fs/gemfs.h"
#include "gui/user_draw.h"
#include "gui/wm/wm.h"
#include "include/gui_loop.h"
#include "include/heap.h"
#include "process.h"
#include "ui/menu.h"
#include "ui/theme.h"
#include "../include/gemos/syscall_abi.h"

#include <stddef.h>
#include <string.h>

#define CONSOLE_MAX_SESSIONS 8
#define CONSOLE_MIN_COLS 32U
#define CONSOLE_MIN_ROWS 8U
#define CONSOLE_MAX_COLS GEMOS_CONSOLE_MAX_COLS
#define CONSOLE_MAX_ROWS GEMOS_CONSOLE_MAX_ROWS
#define CONSOLE_SCROLLBACK_LINES 256U
#define CONSOLE_INPUT_QUEUE_SIZE 64U
#define CONSOLE_FONT_SIZE 12
#define CONSOLE_LINE_HEIGHT 15
#define CONSOLE_CHAR_WIDTH 8
#define CONSOLE_PADDING 8
#define CONSOLE_BG_COLOR UI_DESKTOP
#define CONSOLE_TEXT_COLOR UI_PAPER
#define CONSOLE_ACCENT_COLOR 0xE4A48D
#define CONSOLE_DIM_COLOR UI_BAR_MUTED
#define CONSOLE_STATUS_COLOR UI_PAPER
#define CONSOLE_ERROR_COLOR 0xFF9D8A
#define CONSOLE_SELECTED_BG 0x7A3E2B /* the rust accent, dark enough for light text */
#define CONSOLE_SURFACE_CELLS GEMOS_CONSOLE_MAX_CELLS

typedef enum {
  CONSOLE_RENDER_LEGACY = 0,
  CONSOLE_RENDER_SURFACE = 1,
} console_render_mode_t;

typedef enum {
  CONSOLE_CLOSE_NONE = 0,
  CONSOLE_CLOSE_REAP = 1,
} console_close_reason_t;

typedef struct {
  int in_use;
  uint32_t handle;
  uint32_t owner_pid;
  uint32_t cols;
  uint32_t rows;
  uint32_t flags;
  console_render_mode_t render_mode;
  console_close_reason_t close_reason;
  uint32_t dirty;
  gemos_draw_command_t *commands; /* non-NULL for a graphical window */
  uint32_t command_count;
  window_t *window;
  char title[GEMOS_CONSOLE_MAX_TITLE + 1];
  char lines[CONSOLE_SCROLLBACK_LINES][CONSOLE_MAX_COLS + 1];
  uint16_t line_lengths[CONSOLE_SCROLLBACK_LINES];
  uint16_t line_start;
  uint16_t line_count;
  uint16_t current_line;
  uint16_t cursor_col;
  uint32_t surface_cols;
  uint32_t surface_rows;
  uint32_t surface_cursor_row;
  uint32_t surface_cursor_col;
  uint32_t surface_cursor_visible;
  gemos_console_cell_t surface_cells[CONSOLE_SURFACE_CELLS];
  gemos_console_event_t input_queue[CONSOLE_INPUT_QUEUE_SIZE];
  uint32_t input_head;
  uint32_t input_tail;
} console_session_t;

static app_t console_window_app;
static console_session_t console_sessions[CONSOLE_MAX_SESSIONS];
static uint32_t next_console_handle = 1;

/* What the program shows changed: only its window's client area is drawn
 * again */
static void console_damage(const console_session_t *session) {
  const window_t *win = session->window;

  if (win != NULL) {
    kernel_damage(win->client_rect.x, win->client_rect.y, win->client_rect.w,
                  win->client_rect.h);
  }
}

static void console_reset_legacy_screen(console_session_t *session) {
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

static void console_reset_surface(console_session_t *session) {
  if (session == NULL) {
    return;
  }

  memset(session->surface_cells, 0, sizeof(session->surface_cells));
  session->surface_cols = session->cols;
  session->surface_rows = session->rows;
  session->surface_cursor_row = 0;
  session->surface_cursor_col = 0;
  session->surface_cursor_visible = 0;
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

  if ((event.type == GEMOS_WINDOW_POINTER_MOVE ||
       event.type == GEMOS_WINDOW_FS_CHANGED) &&
      session->input_head != session->input_tail) {
    uint32_t last = (session->input_head + CONSOLE_INPUT_QUEUE_SIZE - 1U) %
                    CONSOLE_INPUT_QUEUE_SIZE;
    if (session->input_queue[last].type == event.type) {
      session->input_queue[last] = event;
      return 1;
    }
  }
  next_head = (session->input_head + 1U) % CONSOLE_INPUT_QUEUE_SIZE;
  if (next_head == session->input_tail) {
    return 0;
  }

  session->input_queue[session->input_head] = event;
  session->input_head = next_head;
  process_wake(session->owner_pid);  /* it may sleep in wait_event */
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

  if (session->commands) {
    kfree(session->commands);
  }
  memset(session, 0, sizeof(*session));
}

static uint32_t console_style_color(uint8_t style) {
  switch (style) {
  case GEMOS_CONSOLE_STYLE_DIM:
    return CONSOLE_DIM_COLOR;
  case GEMOS_CONSOLE_STYLE_ACCENT:
    return CONSOLE_ACCENT_COLOR;
  case GEMOS_CONSOLE_STYLE_STATUS:
    return CONSOLE_STATUS_COLOR;
  case GEMOS_CONSOLE_STYLE_ERROR:
    return CONSOLE_ERROR_COLOR;
  default:
    return CONSOLE_TEXT_COLOR;
  }
}

static uint16_t console_surface_slot(const console_session_t *session,
                                     uint32_t row, uint32_t col) {
  return (uint16_t)(row * session->surface_cols + col);
}

/* One character in its cell of the grid. Programs send any byte, but only
 * printable ASCII is drawn: a control character would draw the font's
 * empty box, and a newline would move the rest of the row down. */
static void console_draw_cell(window_t *win, int x, int y, uint32_t col,
                              char ch, uint32_t color) {
  if (ch <= ' ' || ch > '~') {
    return;
  }
  font_draw_cell(&win->ctx, x + (int)(col * CONSOLE_CHAR_WIDTH), y,
                 CONSOLE_CHAR_WIDTH, ch, CONSOLE_FONT_SIZE, color);
}

static void console_render_legacy(window_t *win, const console_session_t *session) {
  uint32_t visible_lines;
  uint32_t first_logical;
  int x;
  int y;

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
    const char *line = session->lines[slot];

    for (uint32_t col = 0; col < session->cols && line[col] != '\0'; ++col) {
      console_draw_cell(win, x, y, col, line[col], CONSOLE_TEXT_COLOR);
    }
    y += CONSOLE_LINE_HEIGHT;
  }
}

static void console_render_surface(window_t *win,
                                   const console_session_t *session) {
  uint32_t visible_rows;
  uint32_t visible_cols;
  int x;
  int y;

  gfx_fill_rect(&win->ctx, win->client_rect.x, win->client_rect.y,
                win->client_rect.w, win->client_rect.h, CONSOLE_BG_COLOR);

  if (session->surface_cols == 0 || session->surface_rows == 0) {
    return;
  }

  visible_rows = session->surface_rows;
  visible_cols = session->surface_cols;

  if ((uint32_t)(win->client_rect.h / CONSOLE_LINE_HEIGHT) < visible_rows) {
    visible_rows = (uint32_t)(win->client_rect.h / CONSOLE_LINE_HEIGHT);
  }
  if ((uint32_t)(win->client_rect.w / CONSOLE_CHAR_WIDTH) < visible_cols) {
    visible_cols = (uint32_t)(win->client_rect.w / CONSOLE_CHAR_WIDTH);
  }
  if (visible_rows == 0 || visible_cols == 0) {
    return;
  }

  x = win->client_rect.x + CONSOLE_PADDING;
  y = win->client_rect.y + CONSOLE_PADDING;

  for (uint32_t row = 0; row < visible_rows; ++row) {
    for (uint32_t col = 0; col < visible_cols; ++col) {
      const gemos_console_cell_t *cell =
          &session->surface_cells[console_surface_slot(session, row, col)];

      if (cell->style == GEMOS_CONSOLE_STYLE_SELECTED) {
        gfx_fill_rect(&win->ctx, x + (int)(col * CONSOLE_CHAR_WIDTH), y,
                      CONSOLE_CHAR_WIDTH, CONSOLE_LINE_HEIGHT,
                      CONSOLE_SELECTED_BG);
      }
      console_draw_cell(win, x, y, col, (char)cell->ch,
                        console_style_color(cell->style));
    }

    if (session->surface_cursor_visible &&
        row == session->surface_cursor_row &&
        session->surface_cursor_col < visible_cols) {
      int cursor_x = x + (int)(session->surface_cursor_col * CONSOLE_CHAR_WIDTH);
      gfx_fill_rect(&win->ctx, cursor_x, y + CONSOLE_LINE_HEIGHT - 3,
                    CONSOLE_CHAR_WIDTH - 1, 2, CONSOLE_ACCENT_COLOR);
    }

    y += CONSOLE_LINE_HEIGHT;
  }
}

static void console_render(window_t *win) {
  console_session_t *session = (console_session_t *)win->user_data;

  if (session == NULL) {
    return;
  }

  if (session->commands) {
    user_draw_render(win, session->commands, session->command_count);
  } else if (session->render_mode == CONSOLE_RENDER_SURFACE) {
    console_render_surface(win, session);
  } else {
    console_render_legacy(win, session);
  }
}

static void console_window_resized(console_session_t *session) {
  rect_t client = session->window->client_rect;
  console_push_event(session, (gemos_console_event_t){
      GEMOS_WINDOW_RESIZE, (uint32_t)client.w, (uint32_t)client.h, 0});
}

/* Floor division: a pointer left of or above the grid gives -1, not 0. */
static int32_t console_cell_index(int offset, int size) {
  return offset >= 0 ? offset / size : -((-offset + size - 1) / size);
}

/* The left button in a text console that asked for it, in grid cells. */
static void console_text_pointer(console_session_t *session, window_t *win,
                                 const event_t *event) {
  uint32_t type;

  if (!(session->flags & GEMOS_CONSOLE_FLAG_POINTER)) {
    return;
  }
  switch (event->type) {
  case EVENT_MOUSE_CLICK:
    if (event->data.mouse.button != MOUSE_BUTTON_LEFT) {
      return;
    }
    type = GEMOS_CONSOLE_EVENT_POINTER_PRESS;
    break;
  case EVENT_MOUSE_MOVE:
    if (!(event->data.mouse.buttons & MOUSE_BUTTON_LEFT)) {
      return;
    }
    type = GEMOS_CONSOLE_EVENT_POINTER_MOVE;
    break;
  case EVENT_MOUSE_RELEASE:
    type = GEMOS_CONSOLE_EVENT_POINTER_RELEASE;
    break;
  default:
    return;
  }
  int32_t col = console_cell_index(
      event->data.mouse.x - win->client_rect.x - CONSOLE_PADDING,
      CONSOLE_CHAR_WIDTH);
  int32_t row = console_cell_index(
      event->data.mouse.y - win->client_rect.y - CONSOLE_PADDING,
      CONSOLE_LINE_HEIGHT);
  console_push_event(session, (gemos_console_event_t){
      type, (uint32_t)col, (uint32_t)row, keyboard_held_modifiers()});
}

static void console_handle_event(window_t *win, event_t *event) {
  console_session_t *session = (console_session_t *)win->user_data;
  if (session == NULL || event == NULL) {
    return;
  }
  if (event->type == EVENT_KEY_PRESS) {
    console_push_event(session, (gemos_console_event_t){
        GEMOS_CONSOLE_EVENT_KEY, event->data.key.key_code,
        (uint32_t)(uint8_t)event->data.key.character, event->data.key.modifiers});
    return;
  }
  if (!session->commands) {
    console_text_pointer(session, win, event);
    return;
  }
  if (event->type == EVENT_WINDOW_RESIZE) {
    console_window_resized(session);
    return;
  }

  uint32_t type;
  switch (event->type) {
  case EVENT_MOUSE_CLICK:
    type = event->data.mouse.button == MOUSE_BUTTON_RIGHT
               ? GEMOS_WINDOW_RIGHT_PRESS : GEMOS_WINDOW_LEFT_PRESS;
    break;
  case EVENT_MOUSE_MOVE: type = GEMOS_WINDOW_POINTER_MOVE; break;
  case EVENT_MOUSE_RELEASE: type = GEMOS_WINDOW_LEFT_RELEASE; break;
  default: return;
  }
  console_push_event(session, (gemos_console_event_t){
      type, (uint32_t)(event->data.mouse.x - win->client_rect.x),
      (uint32_t)(event->data.mouse.y - win->client_rect.y), event->data.mouse.time_ms});
}

static int console_request_close(window_t *win) {
  console_session_t *session = (console_session_t *)win->user_data;
  gemos_console_event_t console_event;

  if (session == NULL) {
    return 0;
  }

  console_event.type = GEMOS_CONSOLE_EVENT_CLOSE_REQUEST;
  console_event.key_code = 0U;
  console_event.character = 0U;
  console_event.modifiers = 0U;
  return console_push_event(session, console_event);
}

static void console_window_close(window_t *win) {
  console_session_t *session;

  if (win == NULL) {
    return;
  }

  session = (console_session_t *)win->user_data;
  win->user_data = NULL;
  if (session != NULL) {
    session->window = NULL;
    if (session->close_reason != CONSOLE_CLOSE_REAP &&
        !process_kill_pid(session->owner_pid, -1)) {
      serial_print("[CONSOLE] Close kill failed PID=");
      serial_print_dec(session->owner_pid);
      serial_print("\n");
    }
    console_release_session(session);
  }

  kfree(win);
}

/* GemFS hook: every graphical window learns that the tree changed. */
static void console_fs_changed(void) {
  for (int i = 0; i < CONSOLE_MAX_SESSIONS; ++i) {
    console_session_t *session = &console_sessions[i];
    if (session->in_use && session->commands) {
      console_push_event(session, (gemos_console_event_t){GEMOS_WINDOW_FS_CHANGED, 0, 0, 0});
    }
  }
}

void console_init(void) {
  memset(console_sessions, 0, sizeof(console_sessions));
  next_console_handle = 1;

  memset(&console_window_app, 0, sizeof(console_window_app));
  console_window_app.name = "Console";
  console_window_app.icon = &icon_terminal;
  console_window_app.render = console_render;
  console_window_app.handle_event = console_handle_event;
  console_window_app.request_close = console_request_close;
  console_window_app.close = console_window_close;
  gemfs_set_change_hook(console_fs_changed);
}

static int console_create_window(uint32_t owner_pid, const char *title,
                                 uint32_t cols, uint32_t rows, uint32_t flags,
                                 bool graphics) {
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
  if (graphics) {
    session->commands = kalloc(GEMOS_DRAW_LIMIT * sizeof(*session->commands));
    if (!session->commands) {
      kfree(win);
      return GEMOS_ERR_BUSY;
    }
  }
  session->in_use = 1;
  session->handle = next_console_handle++;
  session->owner_pid = owner_pid;
  session->cols = console_clamp(cols, CONSOLE_MIN_COLS, CONSOLE_MAX_COLS);
  session->rows = console_clamp(rows, CONSOLE_MIN_ROWS, CONSOLE_MAX_ROWS);
  session->flags = flags;
  session->window = win;
  session->render_mode = CONSOLE_RENDER_LEGACY;
  session->dirty = 1;
  session->input_head = 0;
  session->input_tail = 0;
  console_reset_legacy_screen(session);
  console_reset_surface(session);

  if (title != NULL && title[0] != '\0') {
    strncpy(session->title, title, GEMOS_CONSOLE_MAX_TITLE);
    session->title[GEMOS_CONSOLE_MAX_TITLE] = '\0';
  } else {
    strcpy(session->title, graphics ? "Application" : "User Terminal");
  }

  /* the padding goes all round the cells, inside the window's chrome */
  width = (int)(session->cols * CONSOLE_CHAR_WIDTH + (CONSOLE_PADDING * 2) +
                2 * WINDOW_BORDER_W);
  height = (int)(session->rows * CONSOLE_LINE_HEIGHT + (CONSOLE_PADDING * 2) +
                 WINDOW_TITLE_BAR_H + 2 * WINDOW_BORDER_W);

  if (graphics) {
    rect_t work = wm_work_area();
    width = (int)cols + 2 * WINDOW_BORDER_W;
    height = (int)rows + WINDOW_TITLE_BAR_H + 2 * WINDOW_BORDER_W;
    width = width < work.w ? width : work.w;
    height = height < work.h ? height : work.h;
  }
  window_init(win, graphics ? 100 : 120, 100, width, height);
  window_set_title(win, session->title);
  win->flags = graphics ? WINDOW_FLAG_NONE : WINDOW_FLAG_NO_RESIZE;
  win->app = &console_window_app;
  if (graphics) {
    win->icon = user_draw_icon(flags);
  } else if (strcmp(session->title, "About GemOS") == 0) {
    win->icon = &icon_about;
  } else if (strcmp(session->title, "Text Editor") == 0) {
    win->icon = &icon_textedit;
  }
  win->user_data = session;

  wm_add_window(win);
  if (graphics) {
    console_window_resized(session);
  }
  serial_print(graphics ? "[WINDOW] Opened PID=" : "[CONSOLE] Opened PID=");
  serial_print_dec(owner_pid);
  serial_print(" handle=");
  serial_print_dec(session->handle);
  serial_print(" x=");
  serial_print_dec((uint32_t)win->x);
  serial_print(" y=");
  serial_print_dec((uint32_t)win->y);
  serial_print(" w=");
  serial_print_dec((uint32_t)win->width);
  serial_print(" h=");
  serial_print_dec((uint32_t)win->height);
  serial_print("\n");
  return (int)session->handle;
}

int console_open(uint32_t owner_pid, const char *title, uint32_t cols,
                 uint32_t rows, uint32_t flags) {
  return console_create_window(owner_pid, title, cols, rows, flags, false);
}

int console_open_window(uint32_t owner_pid, const char *title,
                        uint32_t width, uint32_t height, uint32_t icon) {
  if (!width || !height || width > GEMOS_WINDOW_LIMIT || height > GEMOS_WINDOW_LIMIT) {
    return GEMOS_ERR_INVAL;
  }
  return console_create_window(owner_pid, title, width, height, icon, true);
}

int console_present_window(uint32_t owner_pid, int handle,
                           const gemos_draw_command_t *commands, uint32_t count) {
  console_session_t *session = console_find_owned(owner_pid, handle);
  if (!session || !session->commands || count > GEMOS_DRAW_LIMIT ||
      !user_draw_valid(commands, count)) {
    return GEMOS_ERR_INVAL;
  }
  memcpy(session->commands, commands, count * sizeof(*commands));
  session->command_count = count;
  console_damage(session);
  return GEMOS_OK;
}

static void console_menu_action(window_t *owner, uint32_t action) {
  console_push_event(owner->user_data,
                     (gemos_console_event_t){GEMOS_WINDOW_MENU_ACTION, action, 0, 0});
}

int console_window_menu(uint32_t owner_pid, int handle, const gemos_window_menu_t *menu) {
  console_session_t *session = console_find_owned(owner_pid, handle);
  if (!session || !session->commands || menu->count > GEMOS_MENU_LIMIT) {
    return GEMOS_ERR_INVAL;
  }
  if (menu->count == 0) {
    menu_close_for_window(session->window);
    return GEMOS_OK;
  }
  if (wm_get_focused_window() != session->window) {
    return GEMOS_ERR_BUSY;
  }
  rect_t client = session->window->client_rect;
  if (menu->x < 0 || menu->y < 0 || menu->x >= client.w || menu->y >= client.h) {
    return GEMOS_ERR_INVAL;
  }
  menu_context_item_t items[GEMOS_MENU_LIMIT];
  for (uint32_t i = 0; i < menu->count; ++i) {
    if (menu->items[i].label[GEMOS_MENU_LABEL_BYTES - 1] != '\0') {
      return GEMOS_ERR_INVAL;
    }
    items[i] = (menu_context_item_t){menu->items[i].label, menu->items[i].action};
  }
  menu_show_context(session->window, client.x + menu->x, client.y + menu->y,
                    items, (int)menu->count, console_menu_action);
  return GEMOS_OK;
}

#ifdef GEMOS_SELFTEST
bool console_test_contains(uint32_t owner_pid, const char *text) {
  console_session_t *session = console_find_by_pid(owner_pid);
  if (!session || !session->commands) {
    return false;
  }
  for (uint32_t i = 0; i < session->command_count; ++i) {
    const gemos_draw_command_t *command = &session->commands[i];
    if (command->kind == GEMOS_DRAW_TEXT && strcmp(command->text, text) == 0) {
      return true;
    }
  }
  return false;
}

int console_test_event(uint32_t owner_pid, gemos_console_event_t event) {
  return console_push_event(console_find_by_pid(owner_pid), event);
}
#endif

int console_write(uint32_t owner_pid, int handle, const char *buffer,
                  size_t length) {
  console_session_t *session = console_find_owned(owner_pid, handle);

  if (session == NULL || session->commands || buffer == NULL) {
    return GEMOS_ERR_INVAL;
  }
  if (length == 0) {
    return 0;
  }

  session->render_mode = CONSOLE_RENDER_LEGACY;

  for (size_t i = 0; i < length; ++i) {
    console_append_char(session, buffer[i]);
  }

  session->dirty = 1;
  console_damage(session);

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

  if (session == NULL || session->commands) {
    return GEMOS_ERR_INVAL;
  }

  console_reset_legacy_screen(session);
  console_reset_surface(session);
  session->dirty = 1;
  console_damage(session);
  return GEMOS_OK;
}

int console_present(uint32_t owner_pid, int handle,
                    const gemos_console_frame_t *frame,
                    const gemos_console_cell_t *cells) {
  console_session_t *session = console_find_owned(owner_pid, handle);
  size_t cell_count;

  if (session == NULL || session->commands || frame == NULL || cells == NULL) {
    return GEMOS_ERR_INVAL;
  }
  if (frame->cols == 0 || frame->rows == 0 ||
      frame->cols > CONSOLE_MAX_COLS || frame->rows > CONSOLE_MAX_ROWS) {
    return GEMOS_ERR_INVAL;
  }
  if (frame->cols > session->cols || frame->rows > session->rows) {
    return GEMOS_ERR_INVAL;
  }
  if (frame->cursor_row >= frame->rows || frame->cursor_col >= frame->cols) {
    return GEMOS_ERR_INVAL;
  }

  cell_count = (size_t)frame->cols * (size_t)frame->rows;
  if (cell_count > CONSOLE_SURFACE_CELLS) {
    return GEMOS_ERR_INVAL;
  }

  memset(session->surface_cells, 0, sizeof(session->surface_cells));
  memcpy(session->surface_cells, cells,
         cell_count * sizeof(gemos_console_cell_t));
  session->surface_cols = frame->cols;
  session->surface_rows = frame->rows;
  session->surface_cursor_row = frame->cursor_row;
  session->surface_cursor_col = frame->cursor_col;
  session->surface_cursor_visible = frame->cursor_visible != 0U;
  session->render_mode = CONSOLE_RENDER_SURFACE;
  session->dirty = 1;
  console_damage(session);

  return (int)cell_count;
}

void console_destroy_for_pid(uint32_t owner_pid) {
  console_session_t *session;

  while ((session = console_find_by_pid(owner_pid)) != NULL) {
    if (session->window != NULL) {
      session->close_reason = CONSOLE_CLOSE_REAP;
      wm_remove_window(session->window);
      continue;
    }

    console_release_session(session);
  }
}
