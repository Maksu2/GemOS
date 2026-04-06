#include "inputbox.h"
#include "../../drivers/keyboard.h"
#include "../../drivers/serial.h"
#include "../../kernel/app/app.h"
#include "../../kernel/app/app_manager.h"
#include "../../kernel/gfx/font/font.h"
#include "../../kernel/gfx/primitives.h"
#include "../../kernel/gui/wm/wm.h"
#include "../../kernel/include/heap.h"
#include <stddef.h>
#include <string.h>

/* Fixed size input box */
#define INPUT_WIDTH 300
#define INPUT_HEIGHT 120
#define INPUT_BUFFER_SIZE 32

static app_t inputbox_app;
static char input_buffer[INPUT_BUFFER_SIZE];
static int input_len = 0;
static inputbox_callback_t active_callback = NULL;
static const char *active_title = "Input";

/* Render */
static void inputbox_render(window_t *win) {
  int ox = win->client_rect.x;
  int oy = win->client_rect.y;

  /* XP Dialog Background */
  gfx_fill_rect(&win->ctx, ox, oy, win->client_rect.w, win->client_rect.h,
                0xECE9D8);

  /* Inner Label */
  font_draw_text(&win->ctx, ox + 16, oy + 16, "Enter value:", 12, 0x000000);

  /* Input Field Background & Border */
  /* Border: XP Input Blue-Gray */
  gfx_draw_rect(&win->ctx, ox + 15, oy + 39, win->client_rect.w - 30, 26,
                0x7F9DB9);
  /* Interior: White */
  gfx_fill_rect(&win->ctx, ox + 16, oy + 40, win->client_rect.w - 32, 24,
                0xFFFFFF);

  /* Input Text */
  /* Vertical center in 24px height -> y+5 approx */
  font_draw_text(&win->ctx, ox + 20, oy + 45, input_buffer, 14, 0x000000);

  /* Caret */
  int text_w = input_len * 9; /* Approx width per char */
  gfx_fill_rect(&win->ctx, ox + 20 + text_w, oy + 45, 1, 14, 0x000000);

  /* Separator Line (Optional, skips for now) */

  /* Instructions / Buttons area */
  /* Just text for now, bottom aligned */
  font_draw_text(&win->ctx, ox + 16, oy + 85, "[ENTER] OK   [ESC] Cancel", 12,
                 0x404040);
}

/* Input */
static void inputbox_handle_event(window_t *win, event_t *ev) {
  if (ev->type == EVENT_KEY_PRESS) {
    char c = ev->data.key.character;
    uint8_t sc = ev->data.key.key_code;

    serial_print("[INPUT] Key Code: ");
    char buf[16];
    // quick itoa
    int tmp = sc;
    int i = 0;
    if (tmp == 0)
      buf[i++] = '0';
    while (tmp > 0) {
      buf[i++] = (tmp % 10) + '0';
      tmp /= 10;
    }
    buf[i] = '\0';
    // reverse
    for (int j = 0; j < i / 2; j++) {
      char t = buf[j];
      buf[j] = buf[i - 1 - j];
      buf[i - 1 - j] = t;
    }
    serial_print(buf);
    serial_print(" Char: ");
    buf[0] = c > 32 ? c : '.';
    buf[1] = '\0';
    serial_print(buf);
    serial_print("\n");

    if (c == KEY_ENTER) {
      /* Confirm */
      if (active_callback) {
        active_callback(input_buffer);
      }
      wm_remove_window(win);
    } else if (c == KEY_ESC) {
      /* Cancel */
      wm_remove_window(win);
    } else if (c == KEY_BACKSPACE) {
      if (input_len > 0) {
        input_buffer[--input_len] = '\0';
        // Force redraw
      }
    } else if (c >= 0x20 && c < 0x7F) {
      if (input_len < INPUT_BUFFER_SIZE - 1) {
        input_buffer[input_len++] = c;
        input_buffer[input_len] = '\0';
      }
    }
  }
}

static void inputbox_open(void) {
  serial_print("[INPUTBOX] Opening window...\n");
  window_t *win = (window_t *)kalloc(sizeof(window_t));
  if (!win) {
    serial_print("[INPUTBOX] kalloc failed!\n");
    return;
  }

  /* Center on screen (approx) - Screen is 1920x1080 usually? Or VBE?
     Let's use fixed center for 1024x768 approx or dynamic if we knew screen
     size here. WM centers? No. Let's assume 800x600 safe default or just keep
     400,300.
  */
  window_init(win, 400 - (INPUT_WIDTH / 2), 300 - (INPUT_HEIGHT / 2),
              INPUT_WIDTH, INPUT_HEIGHT);

  win->app = &inputbox_app;

  /* DIALOG FLAGS */
  win->flags = WINDOW_FLAG_MODAL | WINDOW_FLAG_NO_RESIZE | WINDOW_FLAG_NO_DOCK;

  /* Title */
  window_set_title(win, active_title ? active_title : "Input");

  wm_add_window(win);
}

static void inputbox_init_app(void) {
  inputbox_app.name = "Input";
  inputbox_app.icon = NULL; /* No icon needed really */
  inputbox_app.init = NULL;
  inputbox_app.open = inputbox_open;
  inputbox_app.render = inputbox_render;
  inputbox_app.handle_event = inputbox_handle_event;
  inputbox_app.close = NULL;
  inputbox_app.menu = NULL;
}

/* Public API */
void inputbox_show(const char *title, const char *initial_text,
                   inputbox_callback_t callback) {
  serial_print("[INPUTBOX] show called: ");
  serial_print(title);
  serial_print("\n");
  active_title = title;
  active_callback = callback;

  /* Init buffer */
  input_len = 0;
  if (initial_text) {
    int i = 0;
    while (initial_text[i] && i < INPUT_BUFFER_SIZE - 1) {
      input_buffer[i] = initial_text[i];
      i++;
    }
    input_len = i;
  }
  input_buffer[input_len] = '\0';

  /* Init app struct if not already? (static so okay) */
  inputbox_init_app();

  /* Open window manually */
  inputbox_open();
}
