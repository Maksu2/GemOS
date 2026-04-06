#include "explorer.h"
#include "../../drivers/serial.h"
#include "../../kernel/app/app_manager.h"
#include "../../kernel/fs/gemfs.h"
#include "../../kernel/gfx/font/font.h"
#include "../../kernel/gfx/primitives.h"
#include "../../kernel/gui/wm/wm.h"
#include "../../kernel/include/heap.h"
#include "explorer_icons.h"
#include <stddef.h>

#define GRID_COLS 5
#define GRID_CELL_W 80
#define GRID_CELL_H 70
#define ICON_SIZE 32
#define TOP_BAR_H 30

static app_t explorer_app;
static int current_folder_id = -1; /* -1 = Root */
static int selected_id = -1;
static int hover_id = -1;

/* Drag State */
static int drag_src_id = -1;
static bool is_dragging = false;
static int drag_mx = 0;
static int drag_my = 0;
static window_t *drag_win_ref = NULL; /* To unproject coords */

/* Colors */
#define COL_BG 0xFFFFFF
#define COL_SELECTION 0xCCE5FF
#define COL_FOLDER 0xFFD700
#define COL_FILE 0xDDDDDD
#define COL_TEXT 0x000000

/* Helper: Check extension */
static bool strends(const char *str, const char *suffix) {
  if (!str || !suffix)
    return false;
  int l1 = 0;
  while (str[l1])
    l1++;
  int l2 = 0;
  while (suffix[l2])
    l2++;
  if (l2 > l1)
    return false;
  const char *sub = str + (l1 - l2);
  while (*sub && *suffix) {
    if (*sub != *suffix)
      return false;
    sub++;
    suffix++;
  }
  return true;
}

static void explorer_render(window_t *win) {
  int ox = win->client_rect.x;
  int oy = win->client_rect.y;

  /* Background */
  gfx_fill_rect(&win->ctx, ox, oy, win->client_rect.w, win->client_rect.h,
                COL_BG);

  /* Toolbar */
  gfx_fill_rect(&win->ctx, ox, oy, win->client_rect.w, TOP_BAR_H, 0xE0E0E0);

  /* Up Button */
  gfx_draw_rect(&win->ctx, ox + 5, oy + 5, 40, 20, 0x808080);
  font_draw_text(&win->ctx, ox + 15, oy + 8, "UP", 12, COL_TEXT);

  /* New Folder Button */
  gfx_draw_rect(&win->ctx, ox + 55, oy + 5, 80, 20, 0x808080);
  font_draw_text(&win->ctx, ox + 60, oy + 8, "New Folder", 12, COL_TEXT);

  /* Path/Status */
  if (current_folder_id == -1) {
    font_draw_text(&win->ctx, ox + 150, oy + 8, "Root", 12, 0x404040);
  } else {
    // Show parent name? Simplify for now
    font_draw_text(&win->ctx, ox + 150, oy + 8, "Folder...", 12, 0x404040);
  }

  /* Grid Area */
  int start_y = oy + TOP_BAR_H + 10;
  int start_x = ox + 10;

  /* int count = gemfs_count(); */
  int grid_idx = 0;

  for (int i = 0; i < 64; i++) { /* using raw max constant */
    const char *name = gemfs_get_name(i);
    if (!name)
      continue;

    /* Filter by Parent */
    int8_t pid = gemfs_get_parent(i);
    if (pid != (int8_t)current_folder_id)
      continue;

    /* Calculate Grid Pos */
    int col = grid_idx % GRID_COLS;
    int row = grid_idx / GRID_COLS;
    int x = start_x + col * GRID_CELL_W;
    int y = start_y + row * GRID_CELL_H;

    /* Selection/Hover BG */
    if (i == selected_id || i == hover_id) {
      gfx_fill_rect(&win->ctx, x, y, GRID_CELL_W - 5, GRID_CELL_H - 5,
                    COL_SELECTION);
    }

    /* Icon - select based on type and extension */
    const icon_t *file_icon;
    if (gemfs_get_type(i) == GEMFS_TYPE_DIR) {
      file_icon = &icon_folder;
    } else if (strends(name, ".gemtext") || strends(name, ".txt")) {
      file_icon = &icon_textfile;
    } else {
      file_icon = &icon_generic_file;
    }
    gfx_blit_icon(&win->ctx, x + (GRID_CELL_W - 32) / 2, y + 2, file_icon);

    /* Name (Turncate to fit?) */
    font_draw_text(&win->ctx, x + 5, y + 42, name, 10, COL_TEXT);

    grid_idx++;
  }

  /* Drag Overlay */
  if (is_dragging && drag_src_id >= 0) {
    int d_x = drag_mx - 16;
    int d_y = drag_my - 16;
    /* Draw Ghost Icon */
    gfx_draw_rect(&win->ctx, d_x, d_y, 32, 32, 0x000000);
  }
}

static int explorer_hit_test(window_t *win, int mx, int my) {
  /* Adjusted for client rect */
  /* int rx = mx - win->client_rect.x; */
  int ry = my - win->client_rect.y;

  if (ry < TOP_BAR_H)
    return -2; /* Toolbar area */

  int start_y = win->client_rect.y + TOP_BAR_H + 10;
  int start_x = win->client_rect.x + 10;

  int grid_idx = 0;
  for (int i = 0; i < 64; i++) {
    const char *name = gemfs_get_name(i);
    if (!name)
      continue;
    int8_t pid = gemfs_get_parent(i);
    if (pid != (int8_t)current_folder_id)
      continue;

    int col = grid_idx % GRID_COLS;
    int row = grid_idx / GRID_COLS;
    int x = start_x + col * GRID_CELL_W;
    int y = start_y + row * GRID_CELL_H;

    if (mx >= x && mx < x + GRID_CELL_W - 5 && my >= y &&
        my < y + GRID_CELL_H - 5) {
      return i;
    }
    grid_idx++;
  }
  return -1;
}

static void explorer_handle_event(window_t *win, event_t *ev) {
  if (ev->type == EVENT_MOUSE_MOVE) {
    int id = explorer_hit_test(win, ev->data.mouse.x, ev->data.mouse.y);
    if (!is_dragging) {
      hover_id = id;
    } else {
      drag_mx = ev->data.mouse.x;
      drag_my = ev->data.mouse.y;
      hover_id = id; // Update hover for drop target feedback
                     // Force redraw for drag overlay
                     // invalidating rect... we rely on WM or loop
    }
  } else if (ev->type == EVENT_MOUSE_CLICK) {
    int mx = ev->data.mouse.x;
    int my = ev->data.mouse.y;

    /* Check Toolbar */
    int rx = mx - win->client_rect.x;
    int ry = my - win->client_rect.y;
    if (ry < TOP_BAR_H && ry > 0) {
      if (rx > 5 && rx < 45) { /* UP */
        if (current_folder_id != -1) {
          /* Find parent's parent?
             Wait, directory entries store their parent,
             but we need to know the parent OF current_folder_id.
             gemfs doesn't have "get_entry(id)", we have accessors.
             We need gemfs_get_parent(current_folder_id).
          */
          current_folder_id = gemfs_get_parent(current_folder_id);
          selected_id = -1;
        }
      } else if (rx > 55 && rx < 135) { /* New Folder */
        gemfs_create_dir(current_folder_id, "New Folder");
      }
      return;
    }

    int id = explorer_hit_test(win, mx, my);

    /* Double Click Logic (Simplified: Check previous select time?)
       For now: Click = Select. If already Selected = Open.
    */
    if (id >= 0) {
      if (selected_id == id) {
        /* OPEN */
        uint8_t type = gemfs_get_type(id);
        if (type == GEMFS_TYPE_DIR) {
          current_folder_id = id;
          selected_id = -1;
        } else {
          /* FILE ASSOCIATION */
          const char *name = gemfs_get_name(id);
          if (strends(name, ".gemtext")) {
            app_open_with_file("Text Editor", name);
          } else if (strends(name, ".txt")) {
            app_open_with_file("Text Editor", name);
          }
        }
      } else {
        selected_id = id;
        /* Start Drag Potential */
        drag_src_id = id;
        is_dragging = true;
        drag_win_ref = win;
        drag_mx = mx;
        drag_my = my;
      }
    } else {
      selected_id = -1;
    }
  } else if (ev->type == EVENT_MOUSE_RELEASE) {
    if (is_dragging) {
      int target_id =
          explorer_hit_test(win, ev->data.mouse.x, ev->data.mouse.y);
      if (target_id >= 0 && gemfs_get_type(target_id) == GEMFS_TYPE_DIR &&
          target_id != drag_src_id) {
        /* MOVE drag_src_id to target_id */
        /* Need gemfs_move(src, dest_parent) - NOT IMPLEMENTED API yet.
           For now, just log valid drop. */
        serial_print(
            "[Explorer] Dropped on folder (Move not implemented API)\n");
      }
      is_dragging = false;
      drag_src_id = -1;
    }
  }
}

static void explorer_open(void) {
  window_t *win = (window_t *)kalloc(sizeof(window_t));
  if (!win)
    return;

  window_init(win, 100, 100, 450, 400);
  win->app = &explorer_app;
  window_set_title(win, "File Explorer");
  wm_add_window(win);
}

void explorer_init(void) {
  explorer_app.name = "File Explorer";
  explorer_app.icon = NULL;
  explorer_app.init = NULL;
  explorer_app.open = explorer_open;
  explorer_app.open_file = NULL; /* Can't open explorer with file yet */
  explorer_app.render = explorer_render;
  explorer_app.handle_event = explorer_handle_event;
  explorer_app.close = NULL;
  explorer_app.menu = NULL;

  app_register(&explorer_app);
}
