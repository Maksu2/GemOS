#include "window_test.h"
#include "../drivers/serial.h"
#include "../kernel/gui/window/window.h" // Needed for window struct size
#include "../kernel/gui/wm/wm.h"
#include "../kernel/include/event.h"
#include "../kernel/include/heap.h"

// We need storage for windows.
// Can use KHEAP if available, or static for simplicity.
// Let's use static to avoid heap issues if heap is small,
// though user said Heap is working.
static window_t win1;
static window_t win2;
static window_t win3;

void window_test_init(gfx_context_t *ctx) {
  serial_print("[TEST] Starting Window Test...\n");

  // Initialize WM
  wm_init(ctx);

  // Create 3 windows
  // Win1: Left Top (Reddish idea? No, standard colors)
  window_init(&win1, 50, 50, 200, 150);
  wm_add_window(&win1);
  serial_print("[TEST] Added Window 1\n");

  // Win2: Middle Overlap
  window_init(&win2, 100, 100, 200, 150);
  wm_add_window(&win2);
  serial_print("[TEST] Added Window 2\n");

  // Win3: Right Bottom
  window_init(&win3, 150, 150, 200, 150);
  wm_add_window(&win3);
  serial_print("[TEST] Added Window 3\n");

  // Force Z-Order verify: Win3 is top now.
}

void window_test_update(void) {
  event_t ev;

  // Process all pending events via WM
  while (event_pop(&ev)) {
    wm_handle_event(&ev);
  }

  // Render
  wm_render_all();
}
