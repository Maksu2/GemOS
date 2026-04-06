/**
 * Text Editor Application (v2 - Files & Fonts)
 *
 * Features:
 * - Typing, Backspace, Enter
 * - File I/O: Open/Save .gemtext
 * - Font Size: Dynamic scaling
 * - Menu: File (New, Open, Save, Save As) + Font Size
 */

#include "textedit.h"
#include "../../drivers/keyboard.h"
#include "../../drivers/serial.h"
#include "../../kernel/app/app.h"
#include "../../kernel/app/app_manager.h"
#include "../../kernel/fs/gemfs.h"
#include "../../kernel/gfx/font/font.h"
#include "../../kernel/gfx/icons.h"
#include "../../kernel/gfx/primitives.h"
#include "../../kernel/gui/wm/wm.h"
#include "../../kernel/include/heap.h"
#include "../../kernel/ui/menu.h"
#include "filepicker.h"
#include "inputbox.h"
#include <stddef.h>
#include <string.h>

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

#define TEXT_BG_COLOR 0xFAFAFA    /* Paper white */
#define TEXT_FG_COLOR 0x1A1A1A    /* Near black */
#define TEXT_CARET_COLOR 0x0066CC /* Blue caret */
#define TEXT_PADDING 12
#define TEXT_BUFFER_SIZE 4096

/* ========================================================================= */
/* State                                                                     */
/* ========================================================================= */

static app_t textedit_app;
static menu_t *file_menu = NULL;
static menu_t *font_menu = NULL;

/* Text buffer */
static char text_buffer[TEXT_BUFFER_SIZE];
static int cursor_pos = 0;

/* Document State */
static char current_filename[32];
static int current_font_size = 16;
/* static bool is_dirty = false; // logic simplified for MVP */

/* View State */
static int scroll_offset = 0;
static int caret_visible = 1;
static int blink_counter = 0;
#define BLINK_INTERVAL 30

/* ========================================================================= */
/* Helpers                                                                   */
/* ========================================================================= */

static int get_line_height(void) { return current_font_size + 4; /* padding */ }

/* ========================================================================= */
/* File I/O (.gemtext)                                                       */
/* ========================================================================= */

static void load_gemtext(const char *data, int size) {
  /* Reset */
  cursor_pos = 0;
  text_buffer[0] = '\0';
  current_font_size = 16;
  scroll_offset = 0;

  int i = 0;

  /* Parse Header */
  /* Check for #GEMTEXT v1 */
  const char *header = "#GEMTEXT v1\n";
  int header_len = 0;
  /* Manual strlen of header */
  while (header[header_len])
    header_len++;

  bool valid_header = true;
  for (int j = 0; j < header_len; j++) {
    if (i + j >= size || data[i + j] != header[j]) {
      valid_header = false;
      break;
    }
  }

  if (valid_header) {
    i += header_len;

    /* Parse FONT_SIZE=XX\n */
    if (i + 10 < size && data[i] == 'F' && data[i + 1] == 'O' &&
        data[i + 4] == '_') {
      /* Skip until '=' */
      while (i < size && data[i] != '=')
        i++;
      i++; /* Skip '=' */

      /* Parse number */
      int fs = 0;
      while (i < size && data[i] >= '0' && data[i] <= '9') {
        fs = fs * 10 + (data[i] - '0');
        i++;
      }
      if (fs > 8 && fs < 72)
        current_font_size = fs;

      /* Skip newline */
      while (i < size && data[i] != '\n')
        i++;
      if (i < size)
        i++; /* Skip \n */
    }
  } else {
    /* No header, treat as plain text, starting from 0 */
    i = 0;
  }

  /* Read Content */
  int t = 0;
  while (i < size && t < TEXT_BUFFER_SIZE - 1) {
    text_buffer[t++] = data[i++];
  }
  text_buffer[t] = '\0';
  cursor_pos = t;

  serial_print("[TextEdit] Loaded. Size: ");
  serial_print_dec(t);
  serial_print("\n");
}

static void save_gemtext(const char *filename) {
  char file_buf[GEMFS_MAX_FILESIZE];
  int ptr = 0;

  /* Write Header */
  const char *h1 = "#GEMTEXT v1\nFONT_SIZE=";
  int k = 0;
  while (h1[k])
    file_buf[ptr++] = h1[k++];

  /* Write Font Size manually (itoa) */
  if (current_font_size >= 10) {
    file_buf[ptr++] = '0' + (current_font_size / 10);
    file_buf[ptr++] = '0' + (current_font_size % 10);
  } else {
    file_buf[ptr++] = '0' + current_font_size;
  }
  file_buf[ptr++] = '\n';

  /* Write Content */
  for (int j = 0; j < cursor_pos; j++) {
    if (ptr < GEMFS_MAX_FILESIZE - 1) {
      file_buf[ptr++] = text_buffer[j];
    } else {
      break;
    }
  }
  file_buf[ptr] = '\0';

  gemfs_write(filename, file_buf, ptr);
  serial_print("[TextEdit] Saved to ");
  serial_print(filename);
  serial_print("\n");
}

/* ========================================================================= */
/* Actions                                                                   */
/* ========================================================================= */

static void action_new(void) {
  cursor_pos = 0;
  scroll_offset = 0;
  text_buffer[0] = '\0';
  current_filename[0] = '\0';
  current_font_size = 16;
}

static void on_file_selected(const char *name) {
  serial_print("[TextEdit] Opening: ");
  serial_print(name);
  serial_print("\n");

  /* Copy filename */
  int i = 0;
  while (name[i] && i < 31) {
    current_filename[i] = name[i];
    i++;
  }
  current_filename[i] = '\0';

  /* Read file */
  char buf[GEMFS_MAX_FILESIZE];
  int size = gemfs_read(name, buf, GEMFS_MAX_FILESIZE);
  if (size >= 0) {
    load_gemtext(buf, size);
  }
}

static void action_open(void) {
  // Open File Picker
  filepicker_show(on_file_selected);
}

static void on_save_filename_entered(const char *result) {
  if (!result || !result[0])
    return;

  /* Copy name */
  int i = 0;
  while (result[i] && i < 25) { /* Leave room for extension */
    current_filename[i] = result[i];
    i++;
  }
  current_filename[i] = '\0';

  /* Append extension if missing */
  /* Check for dot */
  bool has_ext = false;
  for (int j = 0; j < i; j++) {
    if (current_filename[j] == '.')
      has_ext = true;
  }
  if (!has_ext) {
    const char *ext = ".gemtext";
    int e = 0;
    while (ext[e] && i < 31) {
      current_filename[i++] = ext[e++];
    }
    current_filename[i] = '\0';
  }

  save_gemtext(current_filename);
}

static void action_save_as(void) {
  inputbox_show("Save As...", current_filename, on_save_filename_entered);
}

static void action_save(void) {
  if (current_filename[0] == '\0') {
    action_save_as();
  } else {
    save_gemtext(current_filename);
  }
}

/* Font Actions */
static void action_font_12(void) { current_font_size = 12; }
static void action_font_16(void) { current_font_size = 16; }
static void action_font_20(void) { current_font_size = 20; }
static void action_font_28(void) { current_font_size = 28; }

static void action_font_menu(void) {
  /* Show font menu at mouse position? Or center? */
  /* Use fixed position relative to click or cascade? */
  /* Simplified: Show at fixed offset */
  menu_show(font_menu, 100, 100);
}

/* ========================================================================= */
/* Rendering                                                                 */
/* ========================================================================= */

static void textedit_render(window_t *win) {
  int x = win->client_rect.x;
  int y = win->client_rect.y;
  int w = win->client_rect.w;
  int h = win->client_rect.h;

  gfx_fill_rect(&win->ctx, x, y, w, h, TEXT_BG_COLOR);

  int text_x = x + TEXT_PADDING;
  int text_y = y + TEXT_PADDING;
  int max_width = w - 2 * TEXT_PADDING;
  int line_height = get_line_height();
  int visible_lines = (h - 2 * TEXT_PADDING) / line_height;

  int line = 0;
  int col = 0;
  int char_width = current_font_size * 6 / 10;
  int chars_per_line = max_width / char_width;
  if (chars_per_line < 1)
    chars_per_line = 1;

  int caret_x = text_x;
  int caret_y = text_y;

  /* Calc Caret Pos */
  for (int i = 0; i <= cursor_pos && i < TEXT_BUFFER_SIZE; i++) {
    char c = text_buffer[i];
    if (i == cursor_pos) {
      caret_x = text_x + col * char_width;
      caret_y = text_y + (line - scroll_offset) * line_height;
    }
    if (c == '\n' || c == '\0') {
      line++;
      col = 0;
    } else {
      col++;
      if (col >= chars_per_line) {
        line++;
        col = 0;
      }
    }
    if (c == '\0')
      break;
  }

  /* Auto-scroll */
  int caret_line = 0;
  col = 0;
  for (int i = 0; i < cursor_pos && i < TEXT_BUFFER_SIZE; i++) {
    if (text_buffer[i] == '\n') {
      caret_line++;
      col = 0;
    } else {
      col++;
      if (col >= chars_per_line) {
        caret_line++;
        col = 0;
      }
    }
  }
  if (caret_line >= scroll_offset + visible_lines)
    scroll_offset = caret_line - visible_lines + 1;
  if (caret_line < scroll_offset)
    scroll_offset = caret_line;

  /* Draw Text */
  line = 0;
  col = 0;
  char line_buf[256];
  int line_len = 0;

  for (int i = 0; i < cursor_pos && text_buffer[i] != '\0'; i++) {
    char c = text_buffer[i];
    if (c == '\n') {
      line_buf[line_len] = '\0';
      if (line >= scroll_offset && line < scroll_offset + visible_lines) {
        font_draw_text(&win->ctx, text_x,
                       text_y + (line - scroll_offset) * line_height, line_buf,
                       current_font_size, TEXT_FG_COLOR);
      }
      line++;
      line_len = 0;
      col = 0;
    } else {
      if (line_len < 255)
        line_buf[line_len++] = c;
      col++;
      if (col >= chars_per_line) {
        line_buf[line_len] = '\0';
        if (line >= scroll_offset && line < scroll_offset + visible_lines) {
          font_draw_text(&win->ctx, text_x,
                         text_y + (line - scroll_offset) * line_height,
                         line_buf, current_font_size, TEXT_FG_COLOR);
        }
        line++;
        line_len = 0;
        col = 0;
      }
    }
  }
  /* Last fragment */
  line_buf[line_len] = '\0';
  if (line >= scroll_offset && line < scroll_offset + visible_lines) {
    font_draw_text(&win->ctx, text_x,
                   text_y + (line - scroll_offset) * line_height, line_buf,
                   current_font_size, TEXT_FG_COLOR);
  }

  /* Caret */
  blink_counter++;
  if (blink_counter >= BLINK_INTERVAL) {
    blink_counter = 0;
    caret_visible = !caret_visible;
  }

  if (caret_visible && win->focused) {
    /* Recompute caret pos for drawing */
    /* Already computed above in caret_x/y! Wait, did line height logic match?
     * Yes. */

    /* But wait, recompute again to be safe due to loop logic matching text
     * loop? */
    /* The first loop calculated caret_x/y based on flow. Should be correct. */

    /* Check bounds */
    if (caret_y >= text_y && caret_y < text_y + h - TEXT_PADDING) {
      gfx_fill_rect(&win->ctx, caret_x, caret_y, 2, current_font_size + 2,
                    TEXT_CARET_COLOR);
    }
  }
}

/* ========================================================================= */
/* Input Handling                                                            */
/* ========================================================================= */

static void textedit_handle_event(window_t *win, event_t *ev) {
  (void)win;

  if (ev->type == EVENT_KEY_PRESS) {
    char c = ev->data.key.character;
    /* UX: Reset blink counter */
    caret_visible = 1;
    blink_counter = 0;

    if (c == KEY_BACKSPACE) {
      if (cursor_pos > 0) {
        cursor_pos--;
        text_buffer[cursor_pos] = '\0';
      }
    } else if (c == KEY_ENTER) {
      if (cursor_pos < TEXT_BUFFER_SIZE - 1) {
        text_buffer[cursor_pos++] = '\n';
        text_buffer[cursor_pos] = '\0';
      }
    } else if (c >= 0x20 && c < 0x7F) {
      if (cursor_pos < TEXT_BUFFER_SIZE - 1) {
        text_buffer[cursor_pos++] = c;
        text_buffer[cursor_pos] = '\0';
      }
    }
  }
}

/* ========================================================================= */
/* Lifecycle                                                                 */
/* ========================================================================= */

static void textedit_open(void) {
  window_t *win = (window_t *)kalloc(sizeof(window_t));
  if (!win)
    return;
  window_init(win, 100, 60, 450, 350);
  win->app = &textedit_app;
  window_set_title(win, "Text Editor");
  wm_add_window(win);

  /* Reset */
  current_filename[0] = '\0';
  action_new();
}

static void textedit_open_file(const char *path) {
  textedit_open();
  on_file_selected(path);
}

static void textedit_init(void) {
  textedit_app.name = "Text Editor";
  textedit_app.icon = &icon_textedit;
  textedit_app.init = NULL;
  textedit_app.open = textedit_open;
  textedit_app.open_file = textedit_open_file;
  textedit_app.render = textedit_render;
  textedit_app.handle_event = textedit_handle_event;
  textedit_app.close = NULL;

  /* Create menus */
  file_menu = menu_create();
  if (file_menu) {
    menu_add_item(file_menu, "New", action_new);
    menu_add_item(file_menu, "Open...", action_open);
    menu_add_item(file_menu, "Save", action_save);
    menu_add_item(file_menu, "Save As...", action_save_as);

    /* Font Size Submenu? No real submenus yet. */
    /* Workaround: "Font Size ->" Action opens another menu */
    menu_add_item(file_menu, "Font Size...", action_font_menu);

    /* Set Title for TopBar */
    menu_set_title(file_menu, "File");
  }

  font_menu = menu_create();
  if (font_menu) {
    menu_add_item(font_menu, "Small (12)", action_font_12);
    menu_add_item(font_menu, "Medium (16)", action_font_16);
    menu_add_item(font_menu, "Large (20)", action_font_20);
    menu_add_item(font_menu, "Huge (28)", action_font_28);
  }

  /* Create a combined menu? No, we attach FILE menu to app */
  /* Topbar will confirm current app and show this menu */
  textedit_app.menu = file_menu;
}

void textedit_register(void) {
  textedit_init();
  app_register(&textedit_app);
}
