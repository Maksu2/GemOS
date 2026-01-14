#include "window.h"
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

// Modern Palette (Slate / Professional)
#define CL_DESKTOP 0x406080 // Slate Blue
#define CL_WHITE 0xF0F0F0   // Off-white
#define CL_BLACK 0x101010   // Soft black
#define CL_GREY 0xC0C0C0    // Standard grey
#define CL_DARK 0x404040
#define CL_TITLE_ACT 0x000080 // Navy Blue
#define CL_TITLE_INA 0x808080 // Grey
#define CL_HIGHLIGHT 0xFFFFE0 // Pale Yellow for selection

// --- Global State ---
Window *windows_head = 0;
Window *focused_window = 0;
Window *drag_window = 0;
int drag_offset_x = 0;
int drag_offset_y = 0;

int mx = 512;
int my = 384;

// --- Helper Prototypes ---
void draw_windows_recursive(Window *win);
void draw_single_window(Window *win);

void init_window_manager() {
  windows_head = 0;
  focused_window = 0;
}

Window *create_window(int x, int y, int w, int h, char *title) {
  static Window window_pool[20];
  static int window_count = 0;

  if (window_count >= 20)
    return 0;

  Window *win = &window_pool[window_count++];
  win->x = x;
  win->y = y;
  win->width = w;
  win->height = h;
  win->title = title;
  win->bg_color = CL_WHITE;
  win->title_color = CL_TITLE_ACT;
  win->next = 0;
  win->on_paint = 0;
  win->on_key = 0;
  win->on_click = 0;
  win->extra_data = 0;

  // Add to Head
  win->next = windows_head;
  windows_head = win;
  focused_window = win;

  return win;
}

void bring_to_front(Window *win) {
  focused_window = win;
  if (win == windows_head)
    return;

  // Find prev
  Window *prev = 0;
  Window *cur = windows_head;
  while (cur && cur != win) {
    prev = cur;
    cur = cur->next;
  }

  if (cur && prev) {
    prev->next = cur->next;   // Unlink
    cur->next = windows_head; // Link at head
    windows_head = cur;
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

  // "Freed" logic: title=0 implies free in pool, but here we just unlink
  win->title = 0;
}

// --- Drawing ---

void draw_single_window(Window *win) {
  if (win->extra_data == (void *)1)
    return; // Minimized

  // Shadow
  draw_rect(win->x + 4, win->y + 4, win->width, win->height, 0x202020);

  // Body
  draw_rect(win->x, win->y, win->width, win->height, CL_WHITE);

  // Borders
  uint32_t border = (win == focused_window) ? 0x000000 : 0x606060;
  draw_rect(win->x, win->y, win->width, 1, border);
  draw_rect(win->x, win->y + win->height - 1, win->width, 1, border);
  draw_rect(win->x, win->y, 1, win->height, border);
  draw_rect(win->x + win->width - 1, win->y, 1, win->height, border);

  // Title Bar
  uint32_t title_bg = (win == focused_window) ? CL_TITLE_ACT : CL_TITLE_INA;
  draw_rect(win->x + 1, win->y + 1, win->width - 2, 23, title_bg);

  // Title Text
  draw_string(win->x + 8, win->y + 8, win->title, 0xFFFFFF);

  // Close Button (Right)
  int cx = win->x + win->width - 18;
  int cy = win->y + 5;
  draw_rect(cx, cy, 14, 14, 0xCC0000);
  draw_string(cx + 3, cy + 3, "X", 0xFFFFFF);

  // Client Paint
  if (win->on_paint)
    win->on_paint(win);
}

void draw_windows_recursive(Window *win) {
  if (!win)
    return;
  draw_windows_recursive(win->next); // Draw bottom-most first
  draw_single_window(win);
}

void draw_cursor(int x, int y) {
  if (x >= 1024 || y >= 768)
    return;
  // Classic Arrow
  static const char *arrow[] = {"11          ", "121         ", "1221        ",
                                "12221       ", "122221      ", "1222221     ",
                                "12222221    ", "122222221   ", "1222222221  ",
                                "1222221111  ", "1221221     ", "11  1221    ",
                                "    1221    ", "     11     ", NULL};
  int r = 0;
  while (arrow[r]) {
    for (int c = 0; arrow[r][c] != 0; c++) {
      if (arrow[r][c] != ' ') {
        int px = x + c;
        int py = y + r;
        if (px < 1024 && py < 768)
          put_pixel(px, py, (arrow[r][c] == '1') ? 0x000000 : 0xFFFFFF);
      }
    }
    r++;
  }
}

// File-scope globals for menu state
static bool menu_sys_open_state = false;
static bool menu_apps_open_state = false;

// Rewriting desktop_paint completely to be correct
void desktop_paint() {
  // 1. Background
  video_clear_dithered(0x404040, 0x505050);

  // 2. Windows
  draw_windows_recursive(windows_head);

  // 3. Menu Bar
  draw_rect(0, 0, screen_width, 24, CL_WHITE);
  draw_rect(0, 24, screen_width, 1, 0x000000);

  // System Menu (GemOS)
  bool sys_h = (mx >= 5 && mx <= 65 && my < 24) || menu_sys_open_state;
  if (sys_h)
    draw_rect(5, 2, 60, 20, CL_HIGHLIGHT);
  draw_string(10, 6, "GemOS", 0x000000);

  // Apps Menu
  bool app_h = (mx >= 70 && mx <= 120 && my < 24) || menu_apps_open_state;
  if (app_h)
    draw_rect(70, 2, 50, 20, CL_HIGHLIGHT);
  draw_string(75, 6, "Apps", 0x000000);

  // Clock
  static int ticks = 0;
  static int min = 45;
  static int hour = 21;
  ticks++;
  if (ticks % 60 == 0) {
    // Tick
  }

  char time[16];
  time[0] = (hour / 10) + '0';
  time[1] = (hour % 10) + '0';
  time[2] = ':';
  time[3] = (min / 10) + '0';
  time[4] = (min % 10) + '0';
  time[5] = 0;

  draw_string(screen_width - 60, 6, time, 0x000000);

  // Active App
  if (focused_window) {
    int l = 0;
    while (focused_window->title[l])
      l++;
    draw_string((screen_width - l * 8) / 2, 6, focused_window->title, 0x000000);
  }

  // 4. Taskbar
  int tb_y = screen_height - 40;
  draw_rect(0, tb_y, screen_width, 40, 0x303030);
  draw_rect(0, tb_y, screen_width, 1, 0x606060);

  int tx = 10;
  Window *cur = windows_head;
  while (cur) {
    if (cur->title) {
      int w = 120;
      bool act = (cur == focused_window);
      bool min = (cur->extra_data == (void *)1);
      uint32_t bg = act ? 0xFFFFFF : (min ? 0x505050 : 0x909090);
      uint32_t fg = act ? 0x000000 : 0xFFFFFF;

      draw_rect(tx, tb_y + 4, w, 32, bg);
      draw_rect(tx, tb_y + 4, w, 1, 0x000000);
      draw_rect(tx, tb_y + 35, w, 1, 0x000000);
      draw_rect(tx, tb_y + 4, 1, 32, 0x000000);
      draw_rect(tx + w - 1, tb_y + 4, 1, 32, 0x000000);

      // Text cap
      char buf[14];
      int i = 0;
      while (i < 11 && cur->title[i]) {
        buf[i] = cur->title[i];
        i++;
      }
      buf[i] = 0;
      draw_string(tx + 8, tb_y + 12, buf, fg);

      tx += 125;
    }
    cur = cur->next;
  }

  // 5. Cursor
  draw_cursor(mx, my);

  // 6. Menu Overlay
  if (menu_sys_open_state) {
    draw_rect(5, 24, 100, 50, 0xFFFFFF);
    draw_rect(5, 24, 100, 1, 0x000000);
    draw_rect(5, 74, 100, 1, 0x000000);
    draw_rect(5, 24, 1, 50, 0x000000);
    draw_rect(104, 24, 1, 50, 0x000000);

    bool h1 = (mx >= 5 && mx <= 105 && my >= 25 && my < 45);
    if (h1)
      draw_rect(6, 25, 98, 20, CL_HIGHLIGHT);
    draw_string(10, 30, "About", 0x000000);

    bool h2 = (mx >= 5 && mx <= 105 && my >= 45 && my < 65);
    if (h2)
      draw_rect(6, 45, 98, 20, CL_HIGHLIGHT);
    draw_string(10, 50, "Restart", 0x000000);
  }

  if (menu_apps_open_state) {
    int count = 6;
    int h = count * 20 + 5;
    draw_rect(70, 24, 120, h, 0xFFFFFF);
    draw_rect(70, 24, 120, 1, 0x000000);
    draw_rect(70, 24 + h, 120, 1, 0x000000);
    draw_rect(70, 24, 1, h, 0x000000);
    draw_rect(190, 24, 1, h, 0x000000);

    char *apps[] = {"Notepad",    "Snake",     "Paint",
                    "Calculator", "Solitaire", "Minesweeper"};
    for (int i = 0; i < count; i++) {
      int y = 25 + i * 20;
      bool h = (mx >= 70 && mx <= 190 && my >= y && my < y + 20);
      if (h)
        draw_rect(71, y, 118, 20, CL_HIGHLIGHT);
      draw_string(75, y + 5, apps[i], 0x000000);
    }
  }

  video_swap();
}

void wm_handle_mouse(int x, int y, int b) {
  mx = x;
  my = y;
  static int prev_b = 0;
  int click = (b & 1) && !(prev_b & 1);
  prev_b = b;

  // Use file-scope states defined above
  // menu_sys_open_state, menu_apps_open_state

  // Stop drag
  if (drag_window && !(b & 1))
    drag_window = 0;

  // Menu Click handling (Global Overlay)
  if (click) {
    if (menu_sys_open_state) {
      if (x >= 5 && x <= 105 && y >= 25 && y < 45) {
        start_about();
        menu_sys_open_state = false;
        return;
      }
      if (x >= 5 && x <= 105 && y >= 45 && y < 65) { /* Reset */
        menu_sys_open_state = false;
        return;
      }
      // If clicked outside, close
      if (y > 24)
        menu_sys_open_state = false;
    }
    if (menu_apps_open_state) {
      if (x >= 70 && x <= 190) {
        int idx = (y - 25) / 20;
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
        if (idx >= 0 && idx <= 5) {
          menu_apps_open_state = false;
          return;
        }
      }
      if (y > 24)
        menu_apps_open_state = false;
    }
  }

  // Drag update
  if (drag_window) {
    drag_window->x = x - drag_offset_x;
    drag_window->y = y - drag_offset_y;
    return; // Consume
  }

  if (!click)
    return;

  // Menu Bar
  if (y < 24) {
    if (x > 5 && x < 65) {
      menu_sys_open_state = !menu_sys_open_state;
      menu_apps_open_state = false;
      return;
    } else if (x > 70 && x < 120) {
      menu_apps_open_state = !menu_apps_open_state;
      menu_sys_open_state = false;
      return;
    }

    return;
  }

  // Close menus if clicked elsewhere
  if (y > 24 && click) {
    menu_sys_open_state = false;
    menu_apps_open_state = false;
  }

  // Taskbar
  if (y > screen_height - 40) { // Dynamic
    int tx = 10;
    Window *cur = windows_head;
    while (cur) {
      if (cur->title) {
        if (x >= tx && x <= tx + 140) { // Wider 140
          // Click taskbar item
          if (cur->extra_data == (void *)1) {
            cur->extra_data = 0; // Restore
            bring_to_front(cur);
          } else if (cur == focused_window) {
            cur->extra_data = (void *)1; // Minimize
            focused_window = 0;
          } else {
            bring_to_front(cur);
          }
          return;
        }
        tx += 150; // Inc 150
      }
      cur = cur->next;
    }
    return;
  }

  // Windows
  Window *cur = windows_head;
  while (cur) {
    if (cur->extra_data == (void *)1) {
      cur = cur->next;
      continue;
    } // Skip min

    if (x >= cur->x && x <= cur->x + cur->width && y >= cur->y &&
        y <= cur->y + cur->height) {
      bring_to_front(cur);

      // Local
      int lx = x - cur->x;
      int ly = y - cur->y;

      if (ly < 24) {
        // Title
        // Close Btn (Right)
        if (lx >= cur->width - 18) {
          close_window(cur);
        } else {
          drag_window = cur;
          drag_offset_x = lx;
          drag_offset_y = ly;
        }
      } else {
        // Content
        if (cur->on_click)
          cur->on_click(cur, lx, ly);
      }
      return;
    }
    cur = cur->next;
  }
}

void wm_handle_keyboard(char c) {
  if (focused_window && focused_window->on_key)
    focused_window->on_key(focused_window, c);
}
