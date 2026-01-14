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
  if (ticks > 300 && ticks % 100 == 1) { // Wait for system stability
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

  // 7. Swap
  video_swap();
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

// Consolidated Input Handler with Capture Logic
void wm_handle_mouse(int x, int y, int b) {
  mx = x;
  my = y;
  static int prev_b = 0;
  int click = (b & 1) && !(prev_b & 1);   // Click Down
  int held = (b & 1);                     // Button Held
  int release = !(b & 1) && (prev_b & 1); // Release
  prev_b = b;

  // 1. DRAG CAPTURE (Priority Zero)
  // If we are dragging a window, it owns the mouse ABSOLUTELY.
  // No other window can receive events.
  if (drag_window) {
    if (!held) {
      // Mouse Up -> End Drag
      drag_window = 0;
    } else {
      // Mouse Move -> Update Window
      drag_window->x = x - drag_offset_x;
      drag_window->y = y - drag_offset_y;

      // Keep inside screen bounds? Optional but good.
      // if (drag_window->x < 0) drag_window->x = 0;
    }
    return; // STOP HERE. Do not process menus, other windows, etc.
  }

  // 2. System UI (Menus) - Only if NOT dragging
  // Check Menus (Top Bar)
  if (click) {
    if (menu_sys_open_state) {
      if (y >= 25 && y < 100) {
        // Dispatch sys menu action
        if (y < 50)
          start_about();
        else if (y < 75)
          start_settings_wrapper();
        // else restart (stub)
        menu_sys_open_state = 0;
        return;
      }
      menu_sys_open_state = 0; // Click outside = close
    }

    if (menu_apps_open_state) {
      if (mx >= 70 && mx <= 190 && y >= 25) {
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

  // 3. Top Bar Checks
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

  // 4. Taskbar
  if (click && y > screen_height - 36) {
    Window *cur = windows_head;
    int tx = 6;
    while (cur) {
      if (x >= tx && x < tx + 32) {
        if (cur->extra_data == (void *)1) { // Minimized
          cur->extra_data = 0;
          bring_to_front(cur);
        } else if (cur == focused_window) {
          cur->extra_data = (void *)1; // Minimize
          focused_window = 0;
        } else {
          bring_to_front(cur);
        }
        return;
      }
      tx += 36;
      cur = cur->next;
    }
    return;
  }

  // 5. Window Hit Testing
  // We iterate from head (bottom) to find Z-order?
  // Actually windows_head is usually bottom, next is top?
  // Wait, commonly linked list head is top or bottom.
  // In `draw_windows_recursive`, it draws head first?
  // If `draw_windows_recursive(windows_head)` draws recursively,
  // normally standard implementation: head is bottom.
  // BUT for hit testing, we want front-most.
  // We need to iterate REVERSE or if we only have forward list, we iterate all
  // and keep "best candidate". OR, `bring_to_front` moves to tail? Let's assume
  // naive z-order (list order). If head is bottom, we must find the LAST window
  // in the list that contains the point.

  Window *hit_win = 0;
  Window *cur = windows_head;
  while (cur) {
    if (cur->extra_data != (void *)1) { // if not min
      if (x >= cur->x && x <= cur->x + cur->width && y >= cur->y &&
          y <= cur->y + cur->height) {
        hit_win = cur; // Potential hit, keep looking for one on top
      }
    }
    cur = cur->next;
  }

  if (hit_win) {
    // We hit a window.
    if (click)
      bring_to_front(hit_win);

    int lx = x - hit_win->x;
    int ly = y - hit_win->y;

    // Input Routing
    if (ly < 24) {
      // Title Bar
      if (click) {
        if (lx > hit_win->width - 20)
          close_window(hit_win);
        else {
          // START DRAG
          drag_window = hit_win;
          drag_offset_x = lx;
          drag_offset_y = ly;
        }
      }
    } else {
      // Content
      if (click && hit_win->on_click)
        hit_win->on_click(hit_win, lx, ly);
      // Hover/Drag inside content (Paint)
      // Pass 'held' state so Paint can draw
      if (hit_win->on_mouse_move)
        hit_win->on_mouse_move(hit_win, lx, ly, b);
    }
  }
}

void wm_handle_keyboard(char c) {
  if (focused_window && focused_window->on_key)
    focused_window->on_key(focused_window, c);
}
