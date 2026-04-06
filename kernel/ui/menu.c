#include "menu.h"
#include "../../drivers/serial.h"
#include "../gfx/font/font.h"
#include "../gfx/primitives.h"
#include "../include/string.h"
#include "ui_scale.h"
#include <stddef.h>

/* Colors */
#define COR_MENU_BG 0xF0F0F0
#define COR_MENU_BORDER 0x404040
#define COR_MENU_HOVER 0x000080
#define COR_MENU_TEXT 0x000000
#define COR_MENU_TEXT_HOVER 0xFFFFFF

/* Global active menu */
static menu_t *active_menu = NULL;

/* Pool of menus */
static menu_t menu_pool[16];
static int menu_pool_count = 0;

void menu_init(void) {
  active_menu = NULL;
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

void menu_show(menu_t *menu, int x, int y) {
  if (!menu)
    return;
  if (active_menu == menu) {
    /* Toggle off if clicking self trigger? No, usually handled by caller. */
    /* But if called, we show it. */
  }

  active_menu = menu;
  active_menu->x = x;
  active_menu->y = y;
  active_menu->visible = true;
  active_menu->hover_index = -1;

  /* Bring to front logic (implicit by render order) */
}

void menu_hide(void) {
  if (active_menu) {
    active_menu->visible = false;
    active_menu = NULL;
  }
}

bool menu_is_active(void) { return (active_menu != NULL); }

void menu_render(gfx_context_t *ctx) {
  if (!active_menu || !active_menu->visible)
    return;

  /* Logic to Physical done by gfx primitives, passes logic coords here */
  int x = active_menu->x;
  int y = active_menu->y;
  int w = active_menu->width;
  int h = active_menu->height;

  /* 1. Shadow / Border */
  gfx_fill_rect(ctx, x, y, w, h, COR_MENU_BORDER);

  /* 2. Background */
  gfx_fill_rect(ctx, x + 1, y + 1, w - 2, h - 2, COR_MENU_BG);

  /* 3. Items */
  int item_y = y + 1;

  for (int i = 0; i < active_menu->item_count; i++) {
    uint32_t bg_color = COR_MENU_BG;
    uint32_t txt_color = COR_MENU_TEXT;

    if (i == active_menu->hover_index) {
      bg_color = COR_MENU_HOVER;
      txt_color = COR_MENU_TEXT_HOVER;

      /* Draw Selection Rect */
      gfx_fill_rect(ctx, x + 1, item_y, w - 2, MENU_ITEM_HEIGHT, bg_color);
    }

    /* Draw Text centered vertically */
    font_draw_text(ctx, x + 10, item_y + 4, active_menu->items[i].label, 14,
                   txt_color);

    item_y += MENU_ITEM_HEIGHT;
  }
}

bool menu_handle_event(event_t *event) {
  /* If no menu active, we don't consume anything */
  if (!active_menu)
    return false;

  if (event->type == EVENT_KEY_PRESS) {
    uint8_t key = (uint8_t)event->data.key.character;
    /* Navigation */
    if (key == 0x80) { /* KEY_UP - hardcoded or from header? include driver
                          header? using raw val 0x80 from my def */
      /* Use values from keyboard.h or assume 0x80/81 mapping maintained */
      if (active_menu->hover_index > 0) {
        active_menu->hover_index--;
      } else {
        active_menu->hover_index = active_menu->item_count - 1; /* Wrap */
      }
      return true;
    } else if (key == 0x81) { /* KEY_DOWN */
      if (active_menu->hover_index < active_menu->item_count - 1) {
        active_menu->hover_index++;
      } else {
        active_menu->hover_index = 0; /* Wrap */
      }
      return true;
    } else if (key == 0x0A) { /* KEY_ENTER */
      int index = active_menu->hover_index;
      if (index >= 0 && index < active_menu->item_count) {
        menu_action_t action = active_menu->items[index].action;
        const char *label = active_menu->items[index].label;
        menu_hide();
        if (action) {
          action();
        } else {
          serial_print("[MENU] Selected: ");
          serial_print(label);
          serial_print("\n");
        }
      }
      return true;
    } else if (event->data.key.key_code == 0x01 || key == 0x1B) { /* ESC */
      menu_hide();
      return true; /* Consumed */
    }
  } else if (event->type == EVENT_MOUSE_MOVE) {
    /* Input events are LOGICAL now */
    int mx = event->data.mouse.x;
    int my = event->data.mouse.y;

    /* Check bounds logical */
    if (mx >= active_menu->x && mx < active_menu->x + active_menu->width &&
        my >= active_menu->y && my < active_menu->y + active_menu->height) {

      /* Inside menu */
      int rel_y = my - (active_menu->y + 1);
      if (rel_y >= 0) {
        int index = rel_y / MENU_ITEM_HEIGHT;
        if (index >= 0 && index < active_menu->item_count) {
          active_menu->hover_index = index;
        }
      }
    } else {
      active_menu->hover_index = -1;
    }

    return true; /* Consume mouse move if menu active? */
                 /* Yes, block windows from seeing hover if menu open */
  } else if (event->type == EVENT_MOUSE_CLICK) {
    int mx = event->data.mouse.x;
    int my = event->data.mouse.y;

    if (mx >= active_menu->x && mx < active_menu->x + active_menu->width &&
        my >= active_menu->y && my < active_menu->y + active_menu->height) {

      /* Click inside */
      int rel_y = my - (active_menu->y + 1);
      if (rel_y >= 0) {
        int index = rel_y / MENU_ITEM_HEIGHT;
        if (index >= 0 && index < active_menu->item_count) {
          /* TRIGGER ACTION */
          /* Cache action and label before closing, because menu_hide() clears
           * active_menu */
          menu_action_t action = active_menu->items[index].action;
          const char *label = active_menu->items[index].label;

          /* Auto-close BEFORE action to allow action to open new menus */
          menu_hide();

          if (action) {
            action();
          } else {
            serial_print("[MENU] Selected: ");
            serial_print(label);
            serial_print("\n");
          }
        }
      }
      return true; /* Consumed */
    } else {
      /* Click outside -> Close Menu */
      menu_hide();

      /* Return TRUE or FALSE? */
      /* User said: "przechwytuje input... blokuje interakcję z oknami pod
       * spodem" */
      /* BUT "Click outside -> close" should it allows the click to propagate?
       */
      /* Standard OS: Click outside closes menu and DOES NOT trigger click on
       * window underneath (usually). */
      /* Or does it? Windows: Click outside closes menu, swallows click. */
      /* macOS: Click outside closes menu, swallows click. */
      /* So we return TRUE (Consumed) even if outside, to prevent accidental
       * click on window/desktop. */

      /* EXCEPTION: Top Bar triggers. If I click "File" while "GemOS" menu open.
       */
      /* Top Bar handles usage. Ideally Top Bar logic runs AFTER menu if menu
       * didn't swallow. */
      /* But we stipulated: Priority 1. Menu, 2. TopBar. */
      /* If Menu returns True, TopBar sees nothing. */
      /* So if I click "File", Menu closes, consumes click. "File" DOES NOT
       * open. */
      /* That feels clunky. "File" should open. */
      /* Improvement: Check if click is in TopBar area. If so, return false? */

      /* Let's stick to "Blocking" first as requested ("menu... blokuje
       * interakcję z oknami"). */
      /* Top Bar is not a window. */
      /* Implementation Plan says: "If open, all clicks outside menu close it."
       */
      /* Let's return TRUE primarily. UX refinement later if needed. */

      return true;
    }
  }

  return true; /* Block all other events while menu open? */
}
