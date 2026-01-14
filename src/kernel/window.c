#include "window.h"
#include "../drivers/rtc.h"
#include "../drivers/video.h"
#include "apps.h"

// Types
#define NULL ((void *)0)
typedef unsigned int uint32_t;

#ifndef __cplusplus
// C23 defines bool as keyword, older C needs header or fallback
// Simplest: just use int for our simple usage or _Bool
#define bool int
#define true 1
#define false 0
#endif

// Theme Definitions
uint32_t theme_desktop = 0x405060; // Professional Slate Blue
uint32_t theme_window_bg = 0xE0E0E0;
uint32_t theme_title_bg = 0x000080; // Classic Blue
uint32_t theme_text = 0x000000;

#define CL_WHITE 0xFFFFFF
#define CL_HIGHLIGHT 0xA0A0E0

// --- Global State ---
Window *windows_head = 0;
Window *focused_window = 0;

// Mouse State
int mx = 512, my = 384;
Window *drag_window = 0;
int drag_offset_x = 0;
int drag_offset_y = 0;

// --- Helper Prototypes ---
void draw_windows_recursive(Window *win);
void draw_window_decorations(Window *win);

void init_window_manager() {
  windows_head = 0;
  focused_window = 0;
}

Window *create_window(int x, int y, int w, int h, char *title) {
  static Window win_pool[20];
  static int win_pool_idx = 0;

  if (win_pool_idx >= 20)
    return 0;

  Window *win = &win_pool[win_pool_idx++];
  win->x = x;
  win->y = y;
  win->width = w;
  win->height = h;
  win->title = title;
  win->next = windows_head;
  win->on_paint = 0;
  win->on_click = 0;
  win->on_mouse_move = 0;
  win->on_key = 0;
  win->extra_data = 0;

  windows_head = win;
  focused_window = win;
  return win;
}

void bring_to_front(Window *win) {
  if (!win || win == windows_head) {
    focused_window = win;
    return;
  }

  Window *prev = 0;
  Window *cur = windows_head;
  while (cur && cur != win) {
    prev = cur;
    cur = cur->next;
  }

  if (cur == win && prev) {
    prev->next = cur->next;
    cur->next = windows_head;
    windows_head = cur;
    focused_window = cur;
  }
}

void close_window(Window *win) {
  if (!win)
    return;
  if (win == windows_head) {
    windows_head = win->next;
  } else {
    Window *prev = windows_head;
    while (prev && prev->next != win)
      prev = prev->next;
    if (prev)
      prev->next = win->next;
  }
  if (focused_window == win)
    focused_window = windows_head;
}

// --- Drawing ---

void draw_window_decorations(Window *win) {
  // Shadow
  draw_rect(win->x + 4, win->y + 4, win->width, win->height, 0x202020);

  // Border
  draw_rect(win->x - 1, win->y - 1, win->width + 2, win->height + 2, 0x000000);

  // Title Bar
  uint32_t tbg = (win == focused_window) ? theme_title_bg : 0x808080;
  draw_rect(win->x, win->y, win->width, 24, tbg);

  // Close Button
  draw_rect(win->x + win->width - 18, win->y + 4, 14, 14, 0xC0C0C0);
  draw_rect(win->x + win->width - 17, win->y + 5, 12, 12, 0xFFFFFF); // 3D Light
  draw_rect(win->x + win->width - 17, win->y + 5, 12, 1,
            0x000000); // Inner Detail
  draw_string(win->x + win->width - 14, win->y + 5, "X", 0x000000);

  // Title Text
  if (win->title) {
    draw_string(win->x + 8, win->y + 6, win->title, CL_WHITE);
  }

  // Content BG
  draw_rect(win->x, win->y + 24, win->width, win->height - 24, theme_window_bg);
}

void draw_windows_recursive(Window *win) {
  if (!win)
    return;
  draw_windows_recursive(win->next);

  // If minimized, don't draw
  if (win->extra_data == (void *)1)
    return;

  draw_window_decorations(win);
  if (win->on_paint)
    win->on_paint(win);
}

void draw_cursor(int x, int y) {
  // Arrow Bitmap (12x19 approx)
  // 0 = Transparent, 1 = Black, 2 = White
  static char arrow[16][12] = {
      "1           ", "11          ", "121         ", "1221        ",
      "12221       ", "122221      ", "1222221     ", "12222221    ",
      "122222221   ", "1222222221  ", "1222221111  ", "121221      ",
      "11 1221     ", "1  1221     ", "    11      ", "            "};

  for (int r = 0; r < 16; r++) {
    for (int c = 0; c < 12; c++) {
      if (arrow[r][c] != ' ') {
        int px = x + c;
        int py = y + r;
        if (px < screen_width && py < screen_height)
          put_pixel(px, py, (arrow[r][c] == '1') ? 0x000000 : 0xFFFFFF);
      }
    }
  }
}

// File-scope globals for menu state
static bool menu_sys_open_state = false;
static bool menu_apps_open_state = false;

// Rewriting desktop_paint completely to be correct
void desktop_paint() {
  // 1. Background
  // Improved Dither (Checkerboard)
  for (int y = 0; y < screen_height; y++) {
    for (int x = 0; x < screen_width; x++) {
      if ((x + y) % 2 == 0)
        put_pixel(x, y, theme_desktop);
      else {
        // Slightly darker
        uint32_t col = theme_desktop & 0xFEFEFE;
        col = (col >> 1) & 0x7F7F7F;
        // Simple darken:
        put_pixel(x, y, theme_desktop); // Just solid for now, dither is slow in
                                        // C loop?
      }
    }
  }
  // Solid fill is safer for performance in emulator without optimizations
  draw_rect(0, 0, screen_width, screen_height, theme_desktop);

  // 2. Windows
  draw_windows_recursive(windows_head);

  // 3. Menu Bar
  draw_rect(0, 0, screen_width, 24, CL_WHITE);
  draw_rect(0, 24, screen_width, 1, 0x000000);

  // GemOS Menu
  if (mx >= 5 && mx <= 65 && my < 24)
    draw_rect(5, 2, 60, 20, CL_HIGHLIGHT);
  else if (menu_sys_open_state)
    draw_rect(5, 2, 60, 20, CL_HIGHLIGHT);
  draw_string(10, 6, "GemOS", 0x000000);

  // Apps Menu
  if (mx >= 70 && mx <= 120 && my < 24)
    draw_rect(70, 2, 50, 20, CL_HIGHLIGHT);
  else if (menu_apps_open_state)
    draw_rect(70, 2, 50, 20, CL_HIGHLIGHT);
  draw_string(75, 6, "Apps", 0x000000);

  // Clock (Real RTC)
  // Only update every 100 frames to prevent bus contention/freezes
  static int ticks = 0;
  static int h = 0, m = 0, s = 0;

  ticks++;
  if (ticks % 100 == 1) { // Offset 1 to avoid startup block?
    rtc_get_time(&h, &m, &s);
  }

  char time[16];
  time[0] = (h / 10) + '0';
  time[1] = (h % 10) + '0';
  time[2] = ':';
  time[3] = (m / 10) + '0';
  time[4] = (m % 10) + '0';
  time[5] = 0;

  draw_string(screen_width - 60, 6, time, 0x000000);

  // Active App Title
  if (focused_window) {
    int l = 0;
    while (focused_window->title[l])
      l++;
    draw_string((screen_width - l * 8) / 2, 6, focused_window->title, 0x000000);
  } else {
    draw_string((screen_width - 60) / 2, 6, "System", 0x808080);
  }

  // 4. Taskbar
  int tb_y = screen_height - 36;
  draw_rect(0, tb_y, screen_width, 36, 0x303030); // Dark Gray
  draw_rect(0, tb_y, screen_width, 1, 0x606060);  // Highlight Line

  int tx = 6;
  Window *cur = windows_head;
  while (cur) {
    // App Icon emulation: N, S, P, C...
    // Just use first letter of title
    int w = 32;
    bool act = (cur == focused_window);
    bool min = (cur->extra_data == (void *)1);

    uint32_t bg = act ? 0x606060 : (min ? 0x202020 : 0x404040);

    // Button Box
    draw_rect(tx, tb_y + 2, w, 32, bg);
    // Bevel
    draw_rect(tx, tb_y + 2, w, 1, 0x808080);
    draw_rect(tx, tb_y + 2, 1, 32, 0x808080);

    // Icon (First char)
    char ic[2] = {cur->title ? cur->title[0] : '?', 0};
    draw_string(tx + 12, tb_y + 10, ic, 0xFFFFFF);

    tx += 36; // compact toolbar
    cur = cur->next;
  }

  // 5. Menus Overlay
  if (menu_sys_open_state) {
    draw_rect(5, 24, 120, 80, 0xFFFFFF);
    draw_rect(5, 24, 120, 80,
              1); // border hack? No, rect doesn't support border
    draw_rect(5, 24, 120, 1, 0);
    draw_rect(5, 104, 120, 1, 0);
    draw_rect(5, 24, 1, 80, 0);
    draw_rect(125, 24, 1, 80, 0);

    if (my >= 25 && my < 50 && mx < 125)
      draw_rect(6, 25, 118, 25, CL_HIGHLIGHT);
    draw_string(15, 32, "About GemOS", 0);

    if (my >= 50 && my < 75 && mx < 125)
      draw_rect(6, 50, 118, 25, CL_HIGHLIGHT);
    draw_string(15, 57, "Settings", 0);

    if (my >= 75 && my < 100 && mx < 125)
      draw_rect(6, 75, 118, 25, CL_HIGHLIGHT);
    draw_string(15, 82, "Restart", 0);
  }

  if (menu_apps_open_state) {
    // List: Notepad, Snake, Paint, Calc, Sol, Mine
    int cnt = 6;
    int h = cnt * 25 + 5;
    draw_rect(70, 24, 120, h, 0xFFFFFF);
    // Borders
    draw_rect(70, 24, 120, 1, 0);
    draw_rect(70, 24 + h, 120, 1, 0);
    draw_rect(70, 24, 1, h, 0);
    draw_rect(190, 24, 1, h, 0);

    char *names[] = {"Notepad",    "Snake",     "Paint",
                     "Calculator", "Solitaire", "Minesweeper"};
    for (int i = 0; i < cnt; i++) {
      int y = 25 + i * 25;
      if (my >= y && my < y + 25 && mx >= 70 && mx < 190)
        draw_rect(71, y, 118, 25, CL_HIGHLIGHT);
      draw_string(80, y + 8, names[i], 0);
    }
  }

  // 6. Cursor
  draw_cursor(mx, my);
}

// --- API Wrappers for external ---
extern void start_notepad();
extern void start_snake();
extern void start_paint_wrapper();
extern void start_calculator();
extern void start_solitaire();
extern void start_minesweeper();
extern void start_settings_wrapper();
extern void start_about();

void wm_handle_mouse(int x, int y, int b) {
  mx = x;
  my = y;
  static int prev_b = 0;
  int click = (b & 1) && !(prev_b & 1); // Down edge
  int held = (b & 1);
  prev_b = b;

  // Release drag
  if (drag_window && !held)
    drag_window = 0;

  // 1. Menu Interactions
  if (click) {
    // ... (Same logic for menu clicks, but updated for coordinates) ...
    // Shortcuts for brevity in re-write:
    if (menu_sys_open_state) {
      if (y >= 25 && y < 50) {
        start_about();
        menu_sys_open_state = 0;
        return;
      }
      if (y >= 50 && y < 75) {
        start_settings_wrapper();
        menu_sys_open_state = 0;
        return;
      }
      if (y >= 75 && y < 100) { /* restart */
        menu_sys_open_state = 0;
        return;
      }
      menu_sys_open_state = 0; // Close if clicked outside
    }
    if (menu_apps_open_state) {
      if (mx >= 70 && mx <= 190 && y >= 25 && y < 25 + 6 * 25) {
        int idx = (y - 25) / 25;
        if (idx == 0)
          start_notepad();
        if (idx == 1)
          start_snake();
        if (idx == 2)
          start_paint_wrapper();
        if (idx == 3)
          start_calculator();
        if (idx == 4)
          start_solitaire();
        if (idx == 5)
          start_minesweeper();
        menu_apps_open_state = 0;
        return;
      }
      menu_apps_open_state = 0;
    }
  }

  // 2. Bar Clicks
  if (click && y < 24) {
    if (x > 5 && x < 65) {
      menu_sys_open_state = !menu_sys_open_state;
      menu_apps_open_state = 0;
      return;
    }
    if (x > 70 && x < 120) {
      menu_apps_open_state = !menu_apps_open_state;
      menu_sys_open_state = 0;
      return;
    }
  }

  // 3. Taskbar
  if (click && y > screen_height - 36) {
    // Icon logic: tx=6, step=36
    Window *cur = windows_head;
    int tx = 6;
    while (cur) {
      if (x >= tx && x < tx + 32) {
        if (cur->extra_data == (void *)1) {
          cur->extra_data = 0;
          bring_to_front(cur);
        } else if (cur == focused_window) {
          cur->extra_data = (void *)1;
          focused_window = 0;
        } else {
          bring_to_front(cur);
        }
        return;
      }
      tx += 36;
      cur = cur->next;
    }
  }

  // 4. Windows (Click & Drag & Paint-Draw)
  Window *cur = windows_head;
  while (cur) {
    if (cur->extra_data == (void *)1) {
      cur = cur->next;
      continue;
    } // skip min

    bool hit = (x >= cur->x && x <= cur->x + cur->width && y >= cur->y &&
                y <= cur->y + cur->height);

    if (hit) {
      if (click)
        bring_to_front(cur);

      // Local Coords
      int lx = x - cur->x;
      int ly = y - cur->y;

      // Title Bar
      if (ly < 24) {
        if (click) {
          if (lx > cur->width - 20)
            close_window(cur);
          else {
            drag_window = cur;
            drag_offset_x = lx;
            drag_offset_y = ly;
          }
        }
      } else {
        // Content Area
        // Fire On Click
        if (click && cur->on_click)
          cur->on_click(cur, lx, ly);

        // Fire Mouse Move (Dragging)
        // If button held, and we are NOT dragging window
        if (held && !drag_window && cur->on_mouse_move) {
          cur->on_mouse_move(cur, lx, ly, b);
        }
      }
      // Return only if click?
      // If just moving mouse over app, we might want to propagate?
      // For paint, we need to return if we consumed interaction so we don't
      // click window behind? Yes.
      return;
    }
    cur = cur->next;
  }

  // Drag Window
  if (drag_window) {
    drag_window->x = x - drag_offset_x;
    drag_window->y = y - drag_offset_y;
  }
}

void wm_handle_keyboard(char c) {
  if (focused_window && focused_window->on_key)
    focused_window->on_key(focused_window, c);
}
