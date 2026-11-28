#ifndef GEMOS_CONSOLE_ABI_H
#define GEMOS_CONSOLE_ABI_H

#include <stdint.h>

#define GEMOS_CONSOLE_MAX_TITLE 31U
#define GEMOS_CONSOLE_MAX_COLS 96U
#define GEMOS_CONSOLE_MAX_ROWS 32U
#define GEMOS_CONSOLE_MAX_CELLS \
  (GEMOS_CONSOLE_MAX_COLS * GEMOS_CONSOLE_MAX_ROWS)

enum {
  GEMOS_CONSOLE_EVENT_NONE = 0,
  GEMOS_CONSOLE_EVENT_KEY = 1,
  GEMOS_CONSOLE_EVENT_CLOSE_REQUEST = 2,
  /* Only for a console opened with GEMOS_CONSOLE_FLAG_POINTER: the left
   * button in the grid. key_code is the column and character the row of
   * the cell under the pointer (as int32_t: outside the grid while the
   * button is held they go below 0 or past the last cell), modifiers the
   * GEMOS_KEYMOD_* held. Moves arrive only while the button is down. The
   * values are those of the matching GEMOS_WINDOW_* events. */
  GEMOS_CONSOLE_EVENT_POINTER_PRESS = 3,
  GEMOS_CONSOLE_EVENT_POINTER_MOVE = 5,
  GEMOS_CONSOLE_EVENT_POINTER_RELEASE = 6,
};

/* flags for SYS_console_open */
enum {
  GEMOS_CONSOLE_FLAG_POINTER = 1U << 0, /* send GEMOS_CONSOLE_EVENT_POINTER_* */
};

enum {
  GEMOS_KEY_BACKSPACE = 0x08,
  GEMOS_KEY_ENTER = 0x0A,
  GEMOS_KEY_ESC = 0x1B,
  GEMOS_KEY_SPACE = 0x20,
  GEMOS_KEY_UP = 0x80,
  GEMOS_KEY_DOWN = 0x81,
  GEMOS_KEY_LEFT = 0x82,
  GEMOS_KEY_RIGHT = 0x83,
  GEMOS_KEY_HOME = 0x84,
  GEMOS_KEY_END = 0x85,
  GEMOS_KEY_PAGE_UP = 0x86,
  GEMOS_KEY_PAGE_DOWN = 0x87,
  GEMOS_KEY_F2 = 0x88,
  GEMOS_KEY_DELETE = 0x89,
};

enum {
  GEMOS_KEYMOD_CTRL = 1U << 0,
  GEMOS_KEYMOD_SHIFT = 1U << 1,
  GEMOS_KEYMOD_ALT = 1U << 2,
};

typedef enum {
  GEMOS_CONSOLE_STYLE_NORMAL = 0,
  GEMOS_CONSOLE_STYLE_DIM = 1,
  GEMOS_CONSOLE_STYLE_ACCENT = 2,
  GEMOS_CONSOLE_STYLE_STATUS = 3,
  GEMOS_CONSOLE_STYLE_ERROR = 4,
  GEMOS_CONSOLE_STYLE_SELECTED = 5, /* selected text: a filled cell */
} gemos_console_cell_style_t;

typedef struct {
  char ch;
  uint8_t style;
  uint16_t flags;
} gemos_console_cell_t;

typedef struct {
  uint32_t type;
  uint32_t key_code;
  uint32_t character;
  uint32_t modifiers;
} gemos_console_event_t;

typedef struct {
  uint32_t cols;
  uint32_t rows;
  uint32_t cursor_row;
  uint32_t cursor_col;
  uint32_t cursor_visible;
  const gemos_console_cell_t *cells;
} gemos_console_frame_t;

#endif /* GEMOS_CONSOLE_ABI_H */
