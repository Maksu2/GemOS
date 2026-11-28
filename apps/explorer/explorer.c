#include "explorer.h"
#include "../../drivers/serial.h"
#include "../../kernel/app/app_manager.h"
#include "../../kernel/fs/gemfs.h"
#include "../../kernel/gfx/font/font.h"
#include "../../kernel/gfx/icons.h"
#include "../../kernel/gfx/primitives.h"
#include "../../kernel/gui/wm/wm.h"
#include "../../kernel/include/gui_loop.h"
#include "../../kernel/include/heap.h"
#include "../../kernel/memory/kstack.h"
#include "../../kernel/process.h"
#include "../../kernel/scheduler.h"
#include "../../kernel/ui/menu.h"
#include "../../kernel/ui/theme.h"
#include <gemos/console_abi.h>
#include <gemos/paths.h>
#include <stddef.h>
#include <string.h>

#define TOOLBAR_HEIGHT 32
#define HEADER_HEIGHT 25
#define ROW_HEIGHT 24
#define FOOTER_HEIGHT 24
#define TYPE_WIDTH 108
#define SCROLL_WIDTH 14
#define ITEM_FONT 13
#define DOUBLE_CLICK_MS 500U
#define DOUBLE_CLICK_DISTANCE 5

/* Overlapping chunks cover the viewport at any supported VBE height. The
 * reader still counts every entry, so scrolling can reach the last one. */
#define CACHE_STEP 32
#define CACHE_SIZE 96

static app_t explorer_app;
static unsigned open_windows;
static bool home_ready;
static char current_path[GEMFS_PATH_MAX] = "/";
static char status[GEMFS_NAME_MAX + 26];
static bool status_error;
static int first_row;
static int selected = -1;
static window_t *scroll_drag_window;
static int scroll_drag_offset;
static struct {
  window_t *window;
  uint32_t time_ms;
  int x, y;
} last_click;

static gemfs_entry_t entries[CACHE_SIZE];
static int cached_first;
static int cached_count;
static int total_count;
static uint32_t listed_generation;
static bool listed;
static bool listing_failed;

/* The worker owns each queued request until it publishes the result. */
static int worker_task = -1;
static struct {
  char path[GEMFS_PATH_MAX];
  gemfs_entry_t entries[CACHE_SIZE];
  int first;
  int seen;
  int count;
  int result;
  uint32_t generation;
  enum { LIST_IDLE, LIST_PENDING, LIST_READY } state;
} listing;

static struct {
  window_t *window;
  enum { NAME_IDLE, NAME_INPUT, NAME_PENDING, NAME_READY } state;
  bool rename;
  char path[GEMFS_PATH_MAX];
  char name[GEMFS_NAME_MAX + 2]; /* One extra byte lets us reject overlong input. */
  size_t caret;
  bool select_all;
  int result;
} name_prompt;

typedef struct {
  rect_t list;
  rect_t track;
  int rows;
  int last_top;
} explorer_view_t;

static explorer_view_t explorer_view(const window_t *win) {
  rect_t client = win->client_rect;
  int height = client.h - TOOLBAR_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT;
  int rows = height > 0 ? height / ROW_HEIGHT : 0;
  int last_top = total_count > rows ? total_count - rows : 0;
  int y = client.y + TOOLBAR_HEIGHT + HEADER_HEIGHT;
  int width = client.w - SCROLL_WIDTH;
  return (explorer_view_t){
      .list = {client.x, y, width, rows * ROW_HEIGHT},
      .track = {client.x + width, y, SCROLL_WIDTH, rows * ROW_HEIGHT},
      .rows = rows,
      .last_top = last_top,
  };
}

static rect_t explorer_up_button(const window_t *win) {
  return (rect_t){win->client_rect.x + 8, win->client_rect.y + 4, 38, 24};
}

static rect_t explorer_new_button(const window_t *win) {
  rect_t client = win->client_rect;
  return (rect_t){client.x + client.w - 106, client.y + 4, 98, 24};
}

static bool strends(const char *str, const char *suffix) {
  size_t length = strlen(str);
  size_t tail = strlen(suffix);
  if (length < tail) {
    return false;
  }
  str += length - tail;
  while (*suffix) {
    char ch = *str++;
    if (ch >= 'A' && ch <= 'Z') {
      ch = (char)(ch + 'a' - 'A');
    }
    if (ch != *suffix++) {
      return false;
    }
  }
  return true;
}

static bool is_text_file(const char *name) {
  return strends(name, ".gemtext") || strends(name, ".txt");
}

static void explorer_status(const char *prefix, const char *detail, bool error) {
  size_t used = strlen(prefix);
  if (used > sizeof(status) - 1) {
    used = sizeof(status) - 1;
  }
  memcpy(status, prefix, used);
  strncpy(status + used, detail, sizeof(status) - 1 - used);
  status[sizeof(status) - 1] = '\0';
  status_error = error;
}

/* Append a name to the current path, with one separating slash. */
static bool explorer_join(char out[GEMFS_PATH_MAX], const char *name) {
  size_t dir_length = strlen(current_path);
  bool root = dir_length == 1;

  if (dir_length + strlen(name) + (root ? 0 : 1) >= GEMFS_PATH_MAX) {
    explorer_status("Path too long: ", name, true);
    return false;
  }
  strcpy(out, current_path);
  if (!root) {
    strcat(out, "/");
  }
  strcat(out, name);
  return true;
}

static const gemfs_entry_t *explorer_entry(int index) {
  int slot = index - cached_first;
  return slot >= 0 && slot < cached_count ? &entries[slot] : NULL;
}

static void explorer_collect(void *ctx, const gemfs_entry_t *entry) {
  (void)ctx;
  if (listing.seen++ >= listing.first && listing.count < CACHE_SIZE) {
    listing.entries[listing.count++] = *entry;
  }
}

static void explorer_worker(void) {
  for (;;) {
    if (name_prompt.state == NAME_PENDING) {
      name_prompt.result = name_prompt.rename
                               ? gemfs_rename(name_prompt.path, name_prompt.name)
                               : gemfs_mkdir(name_prompt.path);
      name_prompt.state = NAME_READY;
      scheduler_wake(TASK_GUI);
      continue;
    }
    if (listing.state == LIST_PENDING) {
      listing.seen = 0;
      listing.count = 0;
      listing.result = gemfs_list(listing.path, explorer_collect, NULL,
                                  &listing.generation);
      listing.state = LIST_READY;
      scheduler_wake(TASK_GUI);
      continue;
    }
    scheduler_block_current(0);
    scheduler_yield();
  }
}

static void explorer_dismiss_name(void) {
  name_prompt.window = NULL;
  if (name_prompt.state == NAME_INPUT) {
    name_prompt.state = NAME_IDLE;
  }
}

static void explorer_invalidate_listing(void) {
  window_t *focused = wm_get_focused_window();
  if (focused && focused->app == &explorer_app) {
    menu_close_for_window(focused);
  }
  explorer_dismiss_name();
  listed = false;
  cached_count = 0;
  total_count = 0;
  first_row = 0;
  selected = -1;
  last_click.window = NULL;
  status[0] = '\0';
  wm_damage_app_clients(&explorer_app);
}

static void explorer_publish_listing(void) {
  listing.state = LIST_IDLE;
  if (open_windows == 0 || strcmp(listing.path, current_path) != 0) {
    return;
  }

  bool failed = listing.result < 0;
  if (failed && strcmp(current_path, "/") != 0) {
    strcpy(current_path, "/");
    explorer_invalidate_listing();
    return;
  }

  int count = failed ? 0 : listing.count;
  int total = failed ? 0 : listing.result;
  size_t bytes = (size_t)count * sizeof(entries[0]);
  bool changed = cached_first != listing.first || cached_count != count ||
                 total_count != total ||
                 memcmp(entries, listing.entries, bytes) != 0;
  if (listed && listed_generation != listing.generation && changed) {
    selected = -1;
    last_click.window = NULL;
  }
  if (changed) {
    memcpy(entries, listing.entries, bytes);
    cached_first = listing.first;
    cached_count = count;
    total_count = total;
  }
  if (first_row >= total_count) {
    first_row = total_count > 0 ? total_count - 1 : 0;
  }
  if (changed || !listed || listing_failed != failed) {
    wm_damage_app_clients(&explorer_app);
  }
  if (!listed && !failed) {
    serial_print("[EXPLORER] Opened ");
    serial_print(current_path);
    serial_print("\n");
  }
  listed = true;
  listed_generation = listing.generation;
  listing_failed = failed;
}

static void explorer_publish_name(void) {
  if (!name_prompt.window || name_prompt.result == GEMFS_OK) {
    if (name_prompt.result == GEMFS_OK && listed_generation != gemfs_generation()) {
      cached_count = 0;
      selected = -1;
      last_click.window = NULL;
    }
    if (name_prompt.window) {
      explorer_status(name_prompt.rename ? "Renamed to " : "Created ",
                      name_prompt.name, false);
    }
    name_prompt.window = NULL;
    name_prompt.state = NAME_IDLE;
  } else {
    explorer_status(name_prompt.rename ? "Rename: " : "New Folder: ",
                    gemfs_error(name_prompt.result), true);
    name_prompt.state = NAME_INPUT;
  }
  wm_damage_app_clients(&explorer_app);
}

void explorer_update(void) {
  if (listing.state == LIST_READY) {
    explorer_publish_listing();
  }
  if (name_prompt.state == NAME_READY) {
    explorer_publish_name();
  }
  if (!open_windows || worker_task < 0 || !gemfs_available()) {
    return;
  }

  window_t *active = wm_get_focused_window();
  if (active && active->app == &explorer_app) {
    int last_top = explorer_view(active).last_top;
    if (first_row > last_top) {
      first_row = last_top;
      wm_damage_app_clients(&explorer_app);
    }
  }

  int first = first_row - first_row % CACHE_STEP;
  if (listing.state == LIST_IDLE &&
      (!listed || cached_first != first ||
       listed_generation != gemfs_generation())) {
    strcpy(listing.path, current_path);
    listing.first = first;
    listing.state = LIST_PENDING;
    scheduler_wake((uint32_t)worker_task);
  }
}

#ifdef GEMOS_SELFTEST
bool explorer_listing_pending(void) { return listing.state == LIST_PENDING; }

bool explorer_has_current_entry(const char *name) {
  if (listed && !listing_failed && listed_generation == gemfs_generation()) {
    for (int i = 0; i < cached_count; ++i) {
      if (strcmp(entries[i].name, name) == 0) {
        return true;
      }
    }
  }
  return false;
}
#endif

static void explorer_button(gfx_context_t *ctx, rect_t box, const char *label,
                            uint32_t ink) {
  gfx_draw_rect(ctx, box.x, box.y, box.w, box.h, UI_RULE);
  font_draw_text(ctx, box.x + 8, box.y + 4, label, ITEM_FONT, ink);
}

typedef struct {
  rect_t field;
  rect_t accept;
  rect_t cancel;
} name_controls_t;

static name_controls_t explorer_name_controls(const window_t *win) {
  rect_t client = win->client_rect;
  int y = client.y + 4;
  int right = client.x + client.w - 8;
  return (name_controls_t){
      .field = {client.x + 96, y, client.w - 218, 24},
      .accept = {right - 106, y, 38, 24},
      .cancel = {right - 60, y, 60, 24},
  };
}

static int explorer_name_prefix_width(size_t length) {
  char prefix[sizeof(name_prompt.name)];
  memcpy(prefix, name_prompt.name, length);
  prefix[length] = '\0';
  return font_text_width(prefix, ITEM_FONT);
}

static int explorer_name_text_x(rect_t field, int caret_width) {
  int hidden = caret_width - (field.w - 13);
  return field.x + 6 - (hidden > 0 ? hidden : 0);
}

static void explorer_name_toolbar(window_t *win) {
  gfx_context_t *ctx = &win->ctx;
  name_controls_t controls = explorer_name_controls(win);
  rect_t field = controls.field;
  bool editing = name_prompt.state == NAME_INPUT;
  uint32_t ink = editing ? UI_INK : UI_MUTED;
  font_draw_text(ctx, win->client_rect.x + 8, field.y + 4,
                 name_prompt.rename ? "Rename" : "New Folder", ITEM_FONT,
                 UI_INK);
  gfx_draw_rect(ctx, field.x, field.y, field.w, field.h,
                editing ? UI_ACCENT : UI_RULE);
  explorer_button(ctx, controls.accept, "OK", ink);
  explorer_button(ctx, controls.cancel, "Cancel", ink);

  int caret_width = explorer_name_prefix_width(name_prompt.caret);
  int text_x = explorer_name_text_x(field, caret_width);
  gfx_context_t clipped = *ctx;
  gfx_set_clip(&clipped, field.x + 6, field.y + 3, field.w - 12, field.h - 6);
  if (editing && name_prompt.select_all) {
    gfx_fill_rect(&clipped, text_x, field.y + 3,
                  font_text_width(name_prompt.name, ITEM_FONT), field.h - 6,
                  UI_SELECTION);
  }
  font_draw_text(&clipped, text_x, field.y + 4, name_prompt.name, ITEM_FONT, ink);
  if (editing && !name_prompt.select_all) {
    gfx_fill_rect(&clipped, text_x + caret_width, field.y + 4, 1, field.h - 8,
                  UI_ACCENT);
  }
}

static void explorer_toolbar(window_t *win) {
  gfx_context_t *ctx = &win->ctx;
  rect_t client = win->client_rect;
  rect_t up = explorer_up_button(win);
  rect_t create = explorer_new_button(win);
  int path_x = up.x + up.w + 12;

  gfx_fill_rect(ctx, client.x, client.y + TOOLBAR_HEIGHT - 1, client.w, 1,
                UI_RULE);
  if (name_prompt.window == win) {
    explorer_name_toolbar(win);
    return;
  }
  explorer_button(ctx, up, "Up", current_path[1] ? UI_INK : UI_MUTED);
  explorer_button(ctx, create, "New Folder",
                  gemfs_available() && worker_task >= 0 ? UI_INK : UI_MUTED);
  char home_path[GEMFS_PATH_MAX];
  const char *path = current_path;
  size_t home_len = sizeof(GEMOS_HOME_PATH) - 1;
  if (home_ready && strncmp(path, GEMOS_HOME_PATH, home_len) == 0 &&
      (path[home_len] == '\0' || path[home_len] == '/')) {
    home_path[0] = '~';
    strcpy(home_path + 1, path + home_len);
    path = home_path;
  }
  font_draw_text_fit(ctx, path_x, client.y + 8, path, ITEM_FONT, UI_INK,
                     create.x - 8 - path_x);
}

static rect_t explorer_thumb(explorer_view_t view) {
  int height = view.track.h * view.rows / total_count;
  if (height < 16) {
    height = 16;
  }
  if (height > view.track.h) {
    height = view.track.h;
  }
  int top = first_row < view.last_top ? first_row : view.last_top;
  int y = view.track.y + (view.track.h - height) * top / view.last_top;
  return (rect_t){view.track.x + 3, y, view.track.w - 6, height};
}

static void explorer_draw_entry(gfx_context_t *ctx, rect_t row, int type_x,
                                const gemfs_entry_t *entry, bool active) {
  bool folder = entry->type == GEMFS_TYPE_DIR;
  bool text = is_text_file(entry->name);
  bool program = strends(entry->name, ".elf");
  const icon_t *icon = &icon_generic_file;
  const char *type = "File";
  if (folder) {
    icon = &icon_folder;
    type = "Folder";
  } else if (text) {
    icon = &icon_textfile;
    type = "Text file";
  } else if (program) {
    icon = &icon_terminal;
    type = "Program";
  }

  if (active) {
    gfx_fill_rect(ctx, row.x, row.y, row.w, row.h, UI_SELECTION);
    gfx_fill_rect(ctx, row.x, row.y, 2, row.h, UI_ACCENT);
  }
  gfx_fill_rect(ctx, row.x + 10, row.y + row.h - 1, row.w - 20, 1, UI_RULE);
  icon_draw_outline(ctx, row.x + 11, row.y + 4, icon, UI_INK);
  font_draw_text_fit(ctx, row.x + 38, row.y + 4, entry->name, ITEM_FONT,
                     UI_INK, type_x - row.x - 46);
  font_draw_text_fit(ctx, type_x, row.y + 4, type, ITEM_FONT, UI_MUTED,
                     row.x + row.w - type_x - 8);
}

static void explorer_draw_list(window_t *win, explorer_view_t view) {
  gfx_context_t *ctx = &win->ctx;
  int type_x = view.list.x + view.list.w - TYPE_WIDTH;
  int header_y = view.list.y - HEADER_HEIGHT;

  font_draw_text(ctx, view.list.x + 10, header_y + 5, "Name", ITEM_FONT, UI_MUTED);
  font_draw_text(ctx, type_x, header_y + 5, "Type", ITEM_FONT, UI_MUTED);
  gfx_fill_rect(ctx, view.list.x + 10, view.list.y - 1, view.list.w - 20, 1,
                UI_RULE);
  for (int row = 0; row < view.rows && first_row + row < total_count; ++row) {
    int index = first_row + row;
    const gemfs_entry_t *entry = explorer_entry(index);
    if (entry) {
      rect_t line = {view.list.x, view.list.y + row * ROW_HEIGHT,
                     view.list.w, ROW_HEIGHT};
      explorer_draw_entry(ctx, line, type_x, entry, index == selected);
    }
  }
  if (view.last_top > 0 && view.rows > 0) {
    rect_t thumb = explorer_thumb(view);
    gfx_fill_rect(ctx, view.track.x, view.track.y, view.track.w, view.track.h,
                  UI_PAPER_SHADED);
    gfx_fill_rect(ctx, thumb.x, thumb.y, thumb.w, thumb.h, UI_FRAME);
  }
}

static void explorer_draw_footer(window_t *win) {
  gfx_context_t *ctx = &win->ctx;
  rect_t client = win->client_rect;
  int y = client.y + client.h - FOOTER_HEIGHT;
  gfx_fill_rect(ctx, client.x, y, client.w, FOOTER_HEIGHT, UI_PAPER_SHADED);
  gfx_fill_rect(ctx, client.x, y, client.w, 1, UI_RULE);
  if (status[0] && status_error) {
    gfx_fill_rect(ctx, client.x, y + 1, 2, FOOTER_HEIGHT - 1, UI_ERROR);
  }

  char label[20];
  char *out = label + 10;
  unsigned count = (unsigned)total_count;
  strcpy(out, total_count == 1 ? " item" : " items");
  do {
    *--out = (char)('0' + count % 10);
    count /= 10;
  } while (count);
  font_draw_text(ctx, client.x + 10, y + 4, out, ITEM_FONT, UI_MUTED);

  const gemfs_entry_t *entry = explorer_entry(selected);
  const char *info = entry ? entry->name : "";
  uint32_t ink = UI_MUTED;
  if (status[0]) {
    info = status;
    ink = status_error ? UI_ERROR : UI_INK;
  } else if (!gemfs_available()) {
    info = "No file system";
  } else if (worker_task < 0 || (listed && listing_failed)) {
    info = "Could not read folder";
    ink = UI_ERROR;
  } else if (!listed || (first_row < total_count && !explorer_entry(first_row))) {
    info = "Reading folder...";
  }
  font_draw_text_fit(ctx, client.x + 116, y + 4, info, ITEM_FONT, ink,
                     client.w - 126);
}

static void explorer_render(window_t *win) {
  rect_t client = win->client_rect;
  gfx_fill_rect(&win->ctx, client.x, client.y, client.w, client.h, UI_PAPER);
  explorer_toolbar(win);
  explorer_draw_list(win, explorer_view(win));
  explorer_draw_footer(win);
}

static void explorer_scroll_to(explorer_view_t view, int top) {
  int clamped = top < 0 ? 0 : top > view.last_top ? view.last_top : top;
  if (first_row != clamped) {
    first_row = clamped;
    last_click.window = NULL;
    wm_damage_app_clients(&explorer_app);
  }
}

static void explorer_parent(void) {
  char *slash = strrchr(current_path, '/');
  if (slash != current_path) {
    *slash = '\0';
  } else if (current_path[1]) {
    strcpy(current_path, "/");
  } else {
    return;
  }
  explorer_invalidate_listing();
}

static bool explorer_is_home(const gemfs_entry_t *entry) {
  return home_ready && current_path[1] == '\0' && entry->type == GEMFS_TYPE_DIR &&
         strcmp(entry->name, GEMOS_HOME_PATH + 1) == 0;
}

static void explorer_begin_name(window_t *win, bool rename) {
  if (worker_task < 0 || !gemfs_available() || name_prompt.state == NAME_PENDING ||
      name_prompt.state == NAME_READY) {
    return;
  }
  const gemfs_entry_t *entry = rename ? explorer_entry(selected) : NULL;
  if (entry && explorer_is_home(entry)) {
    explorer_status("", "Home keeps its name", true);
    wm_damage_app_clients(&explorer_app);
    return;
  }
  if (rename && (!entry || !explorer_join(name_prompt.path, entry->name))) {
    wm_damage_app_clients(&explorer_app);
    return;
  }
  name_prompt.window = win;
  name_prompt.state = NAME_INPUT;
  name_prompt.rename = rename;
  strcpy(name_prompt.name, entry ? entry->name : "");
  name_prompt.caret = 0;
  name_prompt.select_all = rename;
  status[0] = '\0';
  wm_damage_app_clients(&explorer_app);
}

static void explorer_submit_name(void) {
  if (!gemfs_name_valid(name_prompt.name)) {
    const char *message = strlen(name_prompt.name) > GEMFS_NAME_MAX
                              ? "Name is longer than 55 characters"
                              : "Enter a valid name";
    explorer_status("", message, true);
  } else if (name_prompt.rename || explorer_join(name_prompt.path, name_prompt.name)) {
    explorer_status("", "Saving...", false);
    name_prompt.state = NAME_PENDING;
    scheduler_wake((uint32_t)worker_task);
  }
  wm_damage_app_clients(&explorer_app);
}

static void explorer_cancel_name(void) {
  explorer_dismiss_name();
  status[0] = '\0';
  wm_damage_app_clients(&explorer_app);
}

static void explorer_name_click(const window_t *win, int x, int y) {
  name_controls_t controls = explorer_name_controls(win);
  if (rect_contains(&controls.accept, x, y)) {
    explorer_submit_name();
  } else if (rect_contains(&controls.cancel, x, y)) {
    explorer_cancel_name();
  } else if (rect_contains(&controls.field, x, y)) {
    int caret_width = explorer_name_prefix_width(name_prompt.caret);
    int offset = x - explorer_name_text_x(controls.field, caret_width);
    size_t first = 0;
    size_t last = strlen(name_prompt.name);
    while (first < last) {
      size_t middle = first + (last - first) / 2;
      if (explorer_name_prefix_width(middle + 1) <= offset) {
        first = middle + 1;
      } else {
        last = middle;
      }
    }
    name_prompt.caret = first;
    name_prompt.select_all = false;
    wm_damage_app_clients(&explorer_app);
  }
}

static void explorer_name_key(uint8_t key, uint32_t modifiers) {
  if (key == GEMOS_KEY_ESC) {
    explorer_cancel_name();
    return;
  }
  if (key == GEMOS_KEY_ENTER) {
    explorer_submit_name();
    return;
  }
  if (modifiers & (GEMOS_KEYMOD_CTRL | GEMOS_KEYMOD_ALT)) {
    if ((modifiers & GEMOS_KEYMOD_CTRL) && (key == 'a' || key == 'A')) {
      name_prompt.caret = 0;
      name_prompt.select_all = true;
      wm_damage_app_clients(&explorer_app);
    }
    return;
  }

  size_t length = strlen(name_prompt.name);
  size_t caret = name_prompt.caret;
  if (key == GEMOS_KEY_HOME || key == GEMOS_KEY_LEFT) {
    caret = key == GEMOS_KEY_LEFT && caret > 0 ? caret - 1 : 0;
  } else if (key == GEMOS_KEY_END || key == GEMOS_KEY_RIGHT) {
    caret = key == GEMOS_KEY_RIGHT && !name_prompt.select_all && caret < length
                ? caret + 1 : length;
  } else {
    bool printable = key >= 0x20 && key <= 0x7e;
    if (!printable && key != GEMOS_KEY_BACKSPACE && key != GEMOS_KEY_DELETE) {
      return;
    }
    if (name_prompt.select_all) {
      name_prompt.name[0] = '\0';
      caret = 0;
      length = 0;
    }
    if (printable && length + 1 < sizeof(name_prompt.name)) {
      memmove(name_prompt.name + caret + 1, name_prompt.name + caret,
              length - caret + 1);
      name_prompt.name[caret++] = (char)key;
    } else if (key == GEMOS_KEY_BACKSPACE && caret > 0) {
      memmove(name_prompt.name + caret - 1, name_prompt.name + caret,
              length - caret + 1);
      caret--;
    } else if (key == GEMOS_KEY_DELETE && caret < length) {
      memmove(name_prompt.name + caret, name_prompt.name + caret + 1,
              length - caret);
    }
    status[0] = '\0';
  }
  name_prompt.caret = caret;
  name_prompt.select_all = false;
  wm_damage_app_clients(&explorer_app);
}

static void explorer_open_entry(int index) {
  const gemfs_entry_t *entry = explorer_entry(index);
  char path[GEMFS_PATH_MAX];
  if (!entry || !explorer_join(path, entry->name)) {
    return;
  }

  if (entry->type == GEMFS_TYPE_DIR) {
    strcpy(current_path, path);
    explorer_invalidate_listing();
    return;
  }

  selected = -1;
  last_click.window = NULL;
  if (is_text_file(entry->name)) {
    app_open_with_file("Text Editor", path);
  } else if (strends(entry->name, ".elf")) {
    if (process_spawn_user_from_file(path) < 0) {
      explorer_status("Could not start ", entry->name, true);
    }
  } else {
    explorer_status("No program opens ", entry->name, true);
  }
  wm_damage_app_clients(&explorer_app);
}

static void explorer_menu_open(window_t *win) {
  (void)win;
  explorer_open_entry(selected);
}

static void explorer_menu_rename(window_t *win) { explorer_begin_name(win, true); }

static void explorer_menu_new_folder(window_t *win) {
  explorer_begin_name(win, false);
}

static void explorer_context_click(window_t *win, explorer_view_t view,
                                   int x, int y) {
  last_click.window = NULL;
  rect_t content = view.list;
  content.h = win->client_rect.y + win->client_rect.h - FOOTER_HEIGHT - content.y;
  if (view.last_top == 0) {
    content.w += SCROLL_WIDTH;
  }
  if (!listed || listing_failed || name_prompt.state != NAME_IDLE ||
      !rect_contains(&content, x, y)) {
    return;
  }
  int row = (y - view.list.y) / ROW_HEIGHT;
  int index = first_row + row;
  const gemfs_entry_t *entry = row < view.rows ? explorer_entry(index) : NULL;
  menu_context_item_t items[2];
  int count = 0;
  if (entry) {
    selected = index;
    if (entry->type == GEMFS_TYPE_DIR || is_text_file(entry->name) ||
        strends(entry->name, ".elf")) {
      items[count++] = (menu_context_item_t){"Open", explorer_menu_open};
    }
    if (!explorer_is_home(entry)) {
      items[count++] = (menu_context_item_t){"Rename", explorer_menu_rename};
    }
  } else if (row >= view.rows || index >= total_count) {
    selected = -1;
    items[count++] = (menu_context_item_t){"New Folder", explorer_menu_new_folder};
  }
  status[0] = '\0';
  wm_damage_app_clients(&explorer_app);
  menu_show_context(win, x, y, items, count);
}

static void explorer_click(window_t *win, explorer_view_t view,
                           const event_t *event) {
  int x = event->data.mouse.x;
  int y = event->data.mouse.y;
  uint32_t time_ms = event->data.mouse.time_ms;
  int dx = x - last_click.x;
  int dy = y - last_click.y;
  bool double_click = last_click.window == win &&
                      time_ms - last_click.time_ms <= DOUBLE_CLICK_MS &&
                      dx >= -DOUBLE_CLICK_DISTANCE && dx <= DOUBLE_CLICK_DISTANCE &&
                      dy >= -DOUBLE_CLICK_DISTANCE && dy <= DOUBLE_CLICK_DISTANCE;
  last_click.window = NULL;
  rect_t up = explorer_up_button(win);
  rect_t create = explorer_new_button(win);
  status[0] = '\0';
  if (rect_contains(&up, x, y)) {
    explorer_parent();
  } else if (rect_contains(&create, x, y)) {
    explorer_begin_name(win, false);
  } else if (view.last_top > 0 && view.rows > 0 &&
             rect_contains(&view.track, x, y)) {
    rect_t thumb = explorer_thumb(view);
    if (y >= thumb.y && y < thumb.y + thumb.h) {
      scroll_drag_window = win;
      scroll_drag_offset = y - thumb.y;
    } else {
      int direction = y < thumb.y ? -1 : 1;
      explorer_scroll_to(view, first_row + direction * view.rows);
    }
  } else if (rect_contains(&view.list, x, y)) {
    int index = first_row + (y - view.list.y) / ROW_HEIGHT;
    if (explorer_entry(index)) {
      if (double_click && selected == index) {
        explorer_open_entry(index);
      } else {
        selected = index;
        last_click.window = win;
        last_click.time_ms = time_ms;
        last_click.x = x;
        last_click.y = y;
      }
    } else if (index >= total_count) {
      selected = -1;
    }
  }
  wm_damage_app_clients(&explorer_app);
}

static void explorer_key(window_t *win, explorer_view_t view, uint8_t key) {
  switch (key) {
  case GEMOS_KEY_UP:
    explorer_scroll_to(view, first_row - 1);
    break;
  case GEMOS_KEY_DOWN:
    explorer_scroll_to(view, first_row + 1);
    break;
  case GEMOS_KEY_PAGE_UP:
    explorer_scroll_to(view, first_row - view.rows);
    break;
  case GEMOS_KEY_PAGE_DOWN:
    explorer_scroll_to(view, first_row + view.rows);
    break;
  case GEMOS_KEY_HOME:
    explorer_scroll_to(view, 0);
    break;
  case GEMOS_KEY_END:
    explorer_scroll_to(view, view.last_top);
    break;
  case GEMOS_KEY_ENTER:
    explorer_open_entry(selected);
    break;
  case GEMOS_KEY_F2:
    explorer_begin_name(win, true);
    break;
  case GEMOS_KEY_BACKSPACE:
    explorer_parent();
    break;
  }
}

static void explorer_handle_event(window_t *win, event_t *event) {
  if (event->type == EVENT_MOUSE_RELEASE && scroll_drag_window == win) {
    scroll_drag_window = NULL;
    return;
  }
  if (name_prompt.window == win) {
    if (name_prompt.state == NAME_INPUT) {
      if (event->type == EVENT_KEY_PRESS) {
        explorer_name_key((uint8_t)event->data.key.character,
                          event->data.key.modifiers);
      } else if (event->type == EVENT_MOUSE_CLICK &&
                 event->data.mouse.button == MOUSE_BUTTON_LEFT) {
        explorer_name_click(win, event->data.mouse.x, event->data.mouse.y);
      }
    }
    return;
  }
  explorer_view_t view = explorer_view(win);
  if (event->type == EVENT_MOUSE_CLICK) {
    if (event->data.mouse.button == MOUSE_BUTTON_RIGHT) {
      explorer_context_click(win, view, event->data.mouse.x, event->data.mouse.y);
    } else if (event->data.mouse.button == MOUSE_BUTTON_LEFT) {
      explorer_click(win, view, event);
    }
  } else if (event->type == EVENT_MOUSE_MOVE && scroll_drag_window == win &&
             view.last_top > 0 && view.rows > 0) {
    int span = view.track.h - explorer_thumb(view).h;
    if (span > 0) {
      int offset = event->data.mouse.y - view.track.y - scroll_drag_offset;
      int clamped = offset < 0 ? 0 : offset > span ? span : offset;
      explorer_scroll_to(view, (clamped * view.last_top + span / 2) / span);
    }
  } else if (event->type == EVENT_KEY_PRESS) {
    last_click.window = NULL;
    explorer_key(win, view, (uint8_t)event->data.key.character);
  }
}

static void explorer_open(void) {
  window_t *win = (window_t *)kalloc(sizeof(window_t));
  if (!win) {
    return;
  }

  if (!open_windows) {
    strcpy(current_path, home_ready ? GEMOS_HOME_PATH : "/");
    explorer_invalidate_listing();
    if (!home_ready && gemfs_available()) {
      explorer_status("", "Home unavailable, showing /", true);
    }
  }
  window_init(win, 100, 100, 520, 400);
  win->app = &explorer_app;
  window_set_title(win, "File Explorer");
  wm_add_window(win);
  open_windows++;
}

static void explorer_close(window_t *win) {
  if (last_click.window == win) {
    last_click.window = NULL;
  }
  if (name_prompt.window == win) {
    explorer_cancel_name();
  }
  if (scroll_drag_window == win) {
    scroll_drag_window = NULL;
  }
  open_windows--;
  kfree(win);
}

void explorer_init(void) {
  if (gemfs_available()) {
    gemfs_stat_t home;
    int result = gemfs_stat(GEMOS_HOME_PATH, &home);
    if (result == GEMFS_OK) {
      home_ready = home.type == GEMFS_TYPE_DIR;
    } else if (result == GEMFS_ERR_NOENT) {
      home_ready = gemfs_mkdir(GEMOS_HOME_PATH) == GEMFS_OK;
    }
  }
  int stack_slot = kstack_alloc();
  if (stack_slot >= 0) {
    worker_task = task_create_kernel(explorer_worker, kstack_top(stack_slot));
    if (worker_task < 0) {
      kstack_free(stack_slot);
    }
  }

  explorer_app.name = "File Explorer";
  explorer_app.icon = &icon_folder;
  explorer_app.open = explorer_open;
  explorer_app.render = explorer_render;
  explorer_app.handle_event = explorer_handle_event;
  explorer_app.close = explorer_close;

  app_register(&explorer_app);
}
