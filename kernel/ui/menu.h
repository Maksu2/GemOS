#ifndef MENU_H
#define MENU_H

#include "../gfx/context.h"
#include "../include/event.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_MENU_ITEMS 16
#define MENU_ITEM_HEIGHT 24
#define MENU_WIDTH 160

typedef void (*menu_action_t)(void);

struct window;

typedef struct {
  const char *label;
  void (*action)(struct window *owner);
} menu_context_item_t;

typedef struct {
  char label[32];
  menu_action_t action;
} menu_item_t;

typedef struct {
  char title[16]; /* Title for TopBar (e.g. "File") */
  int x, y;
  int width, height;
  bool visible;
  menu_item_t items[MAX_MENU_ITEMS];
  int item_count;
  int hover_index; /* -1 if none */
} menu_t;

/* Global API */
void menu_init(void);
menu_t *menu_create(void);
void menu_set_title(menu_t *menu, const char *title);
void menu_add_item(menu_t *menu, const char *label, menu_action_t action);
void menu_show(menu_t *menu, int x, int y);
void menu_hide(void);
bool menu_is_active(void);

/* A system menu owned by a window. Item labels and actions are copied. */
void menu_show_context(struct window *owner, int x, int y,
                       const menu_context_item_t *items, int count);
bool menu_is_context_active(void);
void menu_close_for_window(const struct window *owner);

/* Returns true if event was consumed by the menu system */
bool menu_handle_event(event_t *event);

/* Render the active menu if any */
void menu_render(gfx_context_t *ctx);

#endif
