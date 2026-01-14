#include "../drivers/video.h"
#include "apps.h"
#include "idt.h"
#include "types.h"
#include "window.h"

extern void init_mouse();

// Boot Status Helper
void draw_boot_progress(char *msg, int percent) {
  int cx = screen_width / 2;
  int cy = screen_height / 2;

  // Clear Text Area
  draw_rect(cx - 100, cy + 130, 200, 20, 0xFFFFFF);

  // Draw Status Text
  int len = 0;
  while (msg[len])
    len++;
  draw_string(cx - (len * 8) / 2, cy + 130, msg, 0x000000);

  // Update Bar
  int fill = (percent * 196) / 100;
  draw_rect(cx - 98, cy + 102, fill, 16, 0x000000);

  video_swap();

  // Minimal delay for visual effect
  for (int w = 0; w < 500000; w++)
    asm volatile("nop");
}

void show_boot_logo() {
  video_clear(0xFFFFFF);
  int cx = screen_width / 2;
  int cy = screen_height / 2;

  // System Logo
  draw_rect(cx - 50, cy - 60, 100, 100, 0x000000);
  draw_rect(cx - 48, cy - 58, 96, 96, 0xFFFFFF);
  draw_rect(cx - 20, cy - 30, 40, 40, 0x000000);

  draw_string(cx - 60, cy + 60, "Starting GemOS...", 0x000000);

  // Loading Bar Border
  draw_rect(cx - 100, cy + 100, 200, 20, 0x000000);
  draw_rect(cx - 98, cy + 102, 196, 16, 0xFFFFFF);

  video_swap();
}

void kernel_main() {
  // CRITICAL: Initialize IDT first so interrupts don't Triple Fault
  init_idt();
  init_mouse();

  // Now safe to init video
  init_video();

  show_boot_logo();

  // Logs
  draw_boot_progress("System Core Loaded...", 20);

  draw_boot_progress("Initializing Extensions...", 40);
  // Any other init? GemLang?

  draw_boot_progress("Starting Window Manager...", 60);
  init_window_manager();

  draw_boot_progress("Loading Applications...", 80);
  init_apps();

  draw_boot_progress("Starting Desktop...", 100);

  // Final small wait
  for (int w = 0; w < 2000000; w++)
    asm volatile("nop");

  // Start clean with just About
  start_about();

  // Main Loop
  while (1) {
    desktop_paint();
  }
}
