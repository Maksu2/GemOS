#ifndef GEMOS_WINDOW_ABI_H
#define GEMOS_WINDOW_ABI_H

#include <stdint.h>

#define GEMOS_DRAW_LIMIT 192U
#define GEMOS_DRAW_TEXT_BYTES 128U
#define GEMOS_MENU_LIMIT 8U
#define GEMOS_MENU_LABEL_BYTES 32U
#define GEMOS_WINDOW_LIMIT 2048U

/* Graphics windows use the same 16-byte event record and wait syscall as
 * consoles. The three payload words depend on the event kind. */
enum {
  GEMOS_WINDOW_LEFT_PRESS = 3,  /* client x, client y, milliseconds */
  GEMOS_WINDOW_RIGHT_PRESS = 4, /* client x, client y, milliseconds */
  GEMOS_WINDOW_POINTER_MOVE = 5,
  GEMOS_WINDOW_LEFT_RELEASE = 6,
  GEMOS_WINDOW_RESIZE = 7,      /* client width, client height, 0 */
  GEMOS_WINDOW_MENU_ACTION = 8, /* action ID, 0, 0 */
  GEMOS_WINDOW_FS_CHANGED = 9,  /* 0, 0, 0: GemFS changed; read it again */
};

enum {
  GEMOS_DRAW_FILL = 1,
  GEMOS_DRAW_FRAME = 2,
  GEMOS_DRAW_TEXT = 3,
  GEMOS_DRAW_ICON = 4,
};

enum {
  GEMOS_ICON_FOLDER = 1,
  GEMOS_ICON_TEXT = 2,
  GEMOS_ICON_PROGRAM = 3,
  GEMOS_ICON_FILE = 4,
};

typedef struct {
  uint32_t kind;
  int32_t x, y, w, h;
  uint32_t color;
  uint32_t value; /* font size for text, icon ID for an icon */
  char text[GEMOS_DRAW_TEXT_BYTES];
} gemos_draw_command_t;

typedef struct {
  uint32_t action;
  char label[GEMOS_MENU_LABEL_BYTES];
} gemos_menu_item_t;

typedef struct {
  int32_t x, y;
  uint32_t count; /* zero dismisses an owned menu */
  gemos_menu_item_t items[GEMOS_MENU_LIMIT];
} gemos_window_menu_t;

#endif
