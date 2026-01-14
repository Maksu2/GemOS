#include "../drivers/video.h"
#include "apps.h"
#include "idt.h"
#include "types.h"
#include "window.h"

extern void init_mouse();

void show_boot_screen() {
  // 1. Draw White Background
  video_clear(0xFFFFFF);

  // 2. Draw "System Logo" (Rectangle + Text)
  // Center is 512, 384
  draw_rect(512 - 50, 384 - 60, 100, 100, 0x000000); // Logo Box
  draw_rect(512 - 48, 384 - 58, 96, 96, 0xFFFFFF);   // Inner White

  // Apple-ish shape or just text "OS"
  draw_rect(512 - 20, 384 - 30, 40, 40, 0x000000);

  draw_string(512 - 60, 384 + 60, "Starting GemOS...", 0x000000);

  // 3. Loading Bar
  draw_rect(512 - 100, 384 + 100, 200, 20, 0x000000); // Border
  draw_rect(512 - 98, 384 + 102, 196, 16, 0xFFFFFF);  // Empty

  video_swap();

  // Simulate Loading
  for (int i = 0; i < 196; i += 2) {
    draw_rect(512 - 98, 384 + 102, i, 16, 0x000000); // Fill
    video_swap();
    // Busy wait delay
    for (int w = 0; w < 1000000; w++)
      asm volatile("nop");
  }
}

void kernel_main() {
  init_video();
  init_idt();
  init_mouse();

  show_boot_screen(); // Call the new boot screen function

  init_window_manager();
  init_apps();

  // Start clean with just About
  start_about();

  // Main Loop
  while (1) {
    desktop_paint();

    // Simple delay to cap FPS?
    // For now just spin a bit or let it run max.
    // Running max in QEMU might use 100% CPU but it's fine.
    // No delay - run at max FPS for responsiveness
    // asm volatile("hlt"); // Use HLT to save CPU if IRQ driven, but we need
    // paint loop.

    // Interrupts (keyboard/mouse) will fire and update state.
    // `desktop_paint` reads that state and draws.
  }
}
