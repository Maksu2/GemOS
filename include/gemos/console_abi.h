#ifndef GEMOS_CONSOLE_ABI_H
#define GEMOS_CONSOLE_ABI_H

#include <stdint.h>

#define GEMOS_CONSOLE_MAX_TITLE 31U

enum {
  GEMOS_CONSOLE_EVENT_NONE = 0,
  GEMOS_CONSOLE_EVENT_KEY = 1,
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
};

typedef struct {
  uint32_t type;
  uint32_t key_code;
  uint32_t character;
} gemos_console_event_t;

#endif /* GEMOS_CONSOLE_ABI_H */
