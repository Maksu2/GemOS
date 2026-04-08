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
