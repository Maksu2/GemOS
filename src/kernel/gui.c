#include "gui.h"
#include "../drivers/video.h"

int mouse_x = 400;
int mouse_y = 300;

#define CURSOR_W 10
#define CURSOR_H 10

uint32_t cursor_save_buffer[CURSOR_W * CURSOR_H];
int saved_x = 400;
int saved_y = 300;
int cursor_visible = 0;

void hide_cursor() {
  if (!cursor_visible)
    return;
  for (int y = 0; y < CURSOR_H; y++) {
    for (int x = 0; x < CURSOR_W; x++) {
      put_pixel(saved_x + x, saved_y + y, cursor_save_buffer[y * CURSOR_W + x]);
    }
  }
  cursor_visible = 0;
}

void show_cursor(int x, int y) {
  saved_x = x;
  saved_y = y;
  // Save Background
  for (int iy = 0; iy < CURSOR_H; iy++) {
    for (int ix = 0; ix < CURSOR_W; ix++) {
      cursor_save_buffer[iy * CURSOR_W + ix] = get_pixel(x + ix, y + iy);
    }
  }
  // Draw Cursor (Red Block)
  draw_rect(x, y, CURSOR_W, CURSOR_H, 0xFF0000);
  draw_rect(x + 1, y + 1, CURSOR_W - 2, CURSOR_H - 2, 0xFFFFFF); // inner white
  draw_rect(x + 2, y + 2, CURSOR_W - 4, CURSOR_H - 4, 0x000000); // inner dot
  cursor_visible = 1;
}

void init_gui() { show_cursor(mouse_x, mouse_y); }

void update_mouse_cursor(int x, int y) {
  hide_cursor();
  show_cursor(x, y);
}

void draw_window(int x, int y, int w, int h, const char *title) {
  // Hide cursor to prevent artifacts
  hide_cursor();

  // 1. Draw Body
  draw_rect(x, y, w, h, 0xC0C0C0);

  // 2. Draw Title Bar
  draw_rect(x + 2, y + 2, w - 4, 20, 0x000080);

  // 3. Draw Title Text
  draw_string(x + 5, y + 8, title, 0xFFFFFF);

  // 4. Borders
  draw_rect(x, y, w, 1, 0xFFFFFF);
  draw_rect(x, y, 1, h, 0xFFFFFF);
  draw_rect(x, y + h - 1, w, 1, 0x000000);
  draw_rect(x + w - 1, y, 1, h, 0x000000);

  // Restore cursor
  show_cursor(saved_x, saved_y);
}
