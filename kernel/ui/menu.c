#include "menu.h"
#include "../../drivers/serial.h"
#include "../gfx/font/font.h"
#include "../gfx/primitives.h"
#include "../gui/wm/wm.h"
#include "../include/gui_loop.h"
#include "theme.h"
#include <gemos/console_abi.h>
#include <string.h>
#include "ui_scale.h"
#include <stddef.h>

/* Global active menu */
static menu_t *active_menu = NULL;
static menu_t context_menu;
static struct window *context_owner;
static void (*context_actions[MAX_MENU_ITEMS])(struct window *owner);

/* Pool of menus */
static menu_t menu_pool[16];
static int menu_pool_count = 0;

void menu_init(void) {
  active_menu = NULL;
  context_owner = NULL;
  menu_pool_count = 0;
}

menu_t *menu_create(void) {
  if (menu_pool_count >= 16)
    return NULL;
  menu_t *m = &menu_pool[menu_pool_count++];
  m->item_count = 0;
  m->visible = false;
  m->hover_index = -1;
  m->width = MENU_WIDTH;

  /* Default title */
  m->title[0] = 'M';
  m->title[1] = 'e';
  m->title[2] = 'n';
  m->title[3] = 'u';
  m->title[4] = '\0';

  return m;
}

void menu_set_title(menu_t *menu, const char *title) {
  if (!menu || !title)
    return;
  int i = 0;
  while (title[i] && i < 15) {
    menu->title[i] = title[i];
    i++;
  }
  menu->title[i] = '\0';
}

void menu_add_item(menu_t *menu, const char *label, menu_action_t action) {
  if (!menu || menu->item_count >= MAX_MENU_ITEMS)
    return;

  /* Simple string copy */
  int i = 0;
  while (label[i] && i < 31) {
    menu->items[menu->item_count].label[i] = label[i];
    i++;
  }
  menu->items[menu->item_count].label[i] = '\0';

  menu->items[menu->item_count].action = action;
  menu->item_count++;

  /* Recalculate height */
  menu->height = menu->item_count * MENU_ITEM_HEIGHT + 2; /* +2 for border */
}

static void menu_damage(const menu_t *menu) {
  kernel_damage(menu->x, menu->y, menu->width, menu->height);
}

void menu_show(menu_t *menu, int x, int y) {
  if (!menu)
    return;
  menu_hide();
  active_menu = menu;
  active_menu->x = x;
  active_menu->y = y;
  active_menu->visible = true;
  active_menu->hover_index = -1;

  menu_damage(menu);
}

void menu_hide(void) {
  if (active_menu) {
    menu_damage(active_menu);
    active_menu->visible = false;
    active_menu = NULL;
    context_owner = NULL;
  }
}

bool menu_is_active(void) { return (active_menu != NULL); }

bool menu_is_context_active(void) { return context_owner != NULL; }

void menu_close_for_window(const struct window *owner) {
  if (owner && context_owner == owner) {
    menu_hide();
  }
}

void menu_show_context(struct window *owner, int x, int y,
                       const menu_context_item_t *items, int count) {
  if (!owner || !items || count <= 0 || count > MAX_MENU_ITEMS) {
    return;
  }
  menu_hide();
  context_menu.item_count = 0;
  context_menu.width = MENU_WIDTH;
  for (int i = 0; i < count; ++i) {
    menu_add_item(&context_menu, items[i].label, NULL);
    context_actions[i] = items[i].action;
  }

  rect_t area = wm_work_area();
  int right = area.x + area.w - context_menu.width;
  int bottom = area.y + area.h - context_menu.height;
  x = x > right ? right : x;
  y = y > bottom ? bottom : y;
  menu_show(&context_menu, x < area.x ? area.x : x, y < area.y ? area.y : y);
  context_owner = owner;

  serial_print("[MENU] Context x=");
  serial_print_dec((uint32_t)context_menu.x);
  serial_print(" y=");
  serial_print_dec((uint32_t)context_menu.y);
  serial_print(" w=");
  serial_print_dec((uint32_t)context_menu.width);
  serial_print(" h=");
  serial_print_dec((uint32_t)context_menu.height);
  serial_print("\n");
}

void menu_render(gfx_context_t *ctx) {
  if (!active_menu || !active_menu->visible)
    return;

  /* Logic to Physical done by gfx primitives, passes logic coords here */
  int x = active_menu->x;
  int y = active_menu->y;
  int w = active_menu->width;
  int h = active_menu->height;

  gfx_fill_rect(ctx, x, y, w, h, UI_PAPER);
  gfx_draw_rect(ctx, x, y, w, h, UI_FRAME);
  int item_y = y + 1;

  for (int i = 0; i < active_menu->item_count; i++) {
    if (i == active_menu->hover_index) {
      gfx_fill_rect(ctx, x + 1, item_y, w - 2, MENU_ITEM_HEIGHT, UI_SELECTION);
      gfx_fill_rect(ctx, x + 1, item_y, 2, MENU_ITEM_HEIGHT, UI_ACCENT);
    }

    font_draw_text_fit(ctx, x + 10, item_y + 4, active_menu->items[i].label,
                       14, UI_INK, w - 18);

    item_y += MENU_ITEM_HEIGHT;
  }
}

static void menu_highlight(int index) {
  if (index != active_menu->hover_index) {
    active_menu->hover_index = index;
    menu_damage(active_menu);
  }
}

static void menu_activate(int index) {
  if (index < 0 || index >= active_menu->item_count) {
    return;
  }
  struct window *owner = context_owner;
  void (*context_action)(struct window *) = owner ? context_actions[index] : NULL;
  menu_item_t *item = &active_menu->items[index];
  menu_hide();
  if (context_action) {
    context_action(owner);
  } else if (item->action) {
    item->action();
  } else {
    serial_print("[MENU] Selected: ");
    serial_print(item->label);
    serial_print("\n");
  }
}

bool menu_handle_event(event_t *event) {
  if (!active_menu) {
    return false;
  }

  if (event->type == EVENT_KEY_PRESS) {
    int index = active_menu->hover_index;
    switch ((uint8_t)event->data.key.character) {
    case GEMOS_KEY_UP:
      menu_highlight(index > 0 ? index - 1 : active_menu->item_count - 1);
      break;
    case GEMOS_KEY_DOWN:
      menu_highlight(index + 1 < active_menu->item_count ? index + 1 : 0);
      break;
    case GEMOS_KEY_ENTER:
      menu_activate(index);
      break;
    case GEMOS_KEY_ESC:
      menu_hide();
      break;
    }
  } else if (event->type == EVENT_MOUSE_MOVE || event->type == EVENT_MOUSE_CLICK) {
    rect_t area = {active_menu->x, active_menu->y, active_menu->width,
                   active_menu->height};
    int y = event->data.mouse.y;
    bool inside = rect_contains(&area, event->data.mouse.x, y);
    int row = inside && y > area.y ? (y - area.y - 1) / MENU_ITEM_HEIGHT : -1;
    if (row >= active_menu->item_count) {
      row = -1;
    }
    if (event->type == EVENT_MOUSE_MOVE) {
      menu_highlight(row);
    } else if (event->data.mouse.button == MOUSE_BUTTON_LEFT) {
      if (inside) {
        menu_activate(row);
      } else {
        menu_hide();
      }
    }
  }
  return true;
}
