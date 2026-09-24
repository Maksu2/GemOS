#include "filepicker.h"
#include "../../drivers/serial.h"
#include "../../kernel/app/app.h"
#include "../../kernel/app/app_manager.h"
#include "../../kernel/fs/gemfs.h"
#include "../../kernel/gfx/font/font.h"
#include "../../kernel/gfx/primitives.h"
#include "../../kernel/gui/wm/wm.h"
#include "../../kernel/include/heap.h"
#include <stddef.h>
#include <string.h>

#define PICKER_WIDTH 300
#define PICKER_HEIGHT 400
#define ITEM_HEIGHT 24

#define PICKER_MAX_FILES 32

static app_t filepicker_app;
static filepicker_callback_t active_callback = NULL;
static int hover_index = -1;
static char file_names[PICKER_MAX_FILES][GEMFS_NAME_MAX + 1];
static int file_count;

static void filepicker_collect(void *ctx, const gemfs_entry_t *entry) {
  (void)ctx;
  if (entry->type == GEMFS_TYPE_FILE && file_count < PICKER_MAX_FILES) {
    strcpy(file_names[file_count++], entry->name);
  }
}

/* Files in the root directory */
static void filepicker_refresh(void) {
  file_count = 0;
  (void)gemfs_list("/", filepicker_collect, NULL);
}

/* Render */
static void filepicker_render(window_t *win) {
  int ox = win->client_rect.x;
  int oy = win->client_rect.y;

  /* Bg */
  gfx_fill_rect(&win->ctx, ox, oy, win->client_rect.w, win->client_rect.h,
                0xFFFFFF);

  /* List Files */
  int y = oy + 5; /* Start closer to top */

  for (int i = 0; i < file_count; i++) {
    /* Highlight hover */
    if (i == hover_index) {
      gfx_fill_rect(&win->ctx, ox, y, win->client_rect.w, ITEM_HEIGHT,
                    0xCCE5FF);
    }
    font_draw_text(&win->ctx, ox + 10, y + 4, file_names[i], 14, 0x000000);
    y += ITEM_HEIGHT;
  }

  if (file_count == 0) {
    font_draw_text(&win->ctx, ox + 10, oy + 50, "(No files found)", 12,
                   0x808080);
  }
}

/* Event */
static void filepicker_handle_event(window_t *win, event_t *ev) {
  if (ev->type == EVENT_MOUSE_MOVE) {
    /* int mx = ev->data.mouse.x - win->client_rect.x; */
    int my = ev->data.mouse.y - win->client_rect.y;
    int list_start_y = 5;

    if (my >= list_start_y) {
      int idx = (my - list_start_y) / ITEM_HEIGHT;
      if (idx >= 0 && idx < file_count) {
        hover_index = idx;
      } else {
        hover_index = -1;
      }
    } else {
      hover_index = -1;
    }
  } else if (ev->type == EVENT_MOUSE_CLICK) {
    /* int mx = ev->data.mouse.x - win->client_rect.x; */
    int my = ev->data.mouse.y - win->client_rect.y;
    int list_start_y = 5;

    if (my >= list_start_y) {
      int idx = (my - list_start_y) / ITEM_HEIGHT;

      if (idx >= 0 && idx < file_count) {
        if (active_callback) {
          active_callback(file_names[idx]);
        }
        wm_remove_window(win);
        return;
      }
    }
  } else if (ev->type == EVENT_KEY_PRESS) {
    if (ev->data.key.key_code == 0x01) { /* ESC */
      wm_remove_window(win);
    }
  }
}

static void filepicker_open(void) {
  window_t *win = (window_t *)kalloc(sizeof(window_t));
  if (!win)
    return;

  window_init(win, 450, 100, PICKER_WIDTH, PICKER_HEIGHT);
  win->app = &filepicker_app;

  /* DIALOG FLAGS */
  win->flags = WINDOW_FLAG_MODAL | WINDOW_FLAG_NO_RESIZE | WINDOW_FLAG_NO_DOCK;

  /* Title */
  window_set_title(win, "Open File");

  wm_add_window(win);
}

static void filepicker_init_app(void) {
  filepicker_app.name = "File Picker";
  filepicker_app.icon = NULL;
  filepicker_app.init = NULL;
  filepicker_app.open = filepicker_open;
  filepicker_app.render = filepicker_render;
  filepicker_app.handle_event = filepicker_handle_event;
  filepicker_app.close = NULL;
  filepicker_app.menu = NULL;
}

void filepicker_show(filepicker_callback_t callback) {
  active_callback = callback;
  hover_index = -1;
  filepicker_refresh();

  filepicker_init_app();
  filepicker_open();
}
