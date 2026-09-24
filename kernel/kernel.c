#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/pic.h"
#include "../drivers/pit.h"
#include "../drivers/serial.h"
#include "../drivers/vbe.h"
#include "console.h"
#include "gdt.h"
#include "isr.h"
#include "process.h"
#include "scheduler.h"
#include "syscall.h"
#include <io.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../kernel/app/app_manager.h"
#include "../kernel/fs/gemfs.h"
#include "../kernel/gfx/context.h"
#include "../kernel/gfx/font/font.h" // Font Logic
#include "../kernel/gfx/primitives.h"
#include "../kernel/gui/desktop.h"
#include "../kernel/gui/topbar/topbar.h" // Top Bar Integration
#include "../kernel/gui/window/window.h" // WM Integration
#include "../kernel/gui/wm/wm.h"         // WM Integration
#include "../kernel/include/boot_info.h"
#include "../kernel/include/event.h"
#include "../kernel/include/heap.h"
#include "../kernel/include/irq.h"
#include "../kernel/memory/paging.h"
#include "../kernel/memory/pmm.h"
#include "../kernel/ui/cursor.h"
#include "../kernel/ui/dock/dock.h"
#include "../kernel/ui/menu.h"
#include "../kernel/ui/ui_scale.h"

extern uintptr_t __kernel_end;
/* Heap beyond the backbuffer: glyph caches, windows, kernel stacks of
 * processes (about 0.3 MB in use with three programs open). */
#define HEAP_RESERVE (4 * 1024 * 1024)
#define USERLAND_DEBUG_AUTOSTART 0

/* Copy of the loader's boot info block (kernel/include/boot_info.h) */
boot_info_t boot_info;

/* Global Screen Context */
gfx_context_t screen_ctx;
static bool pending_redraw = true;

void kernel_request_redraw(void) {
  pending_redraw = true;
  scheduler_wake(TASK_GUI);
}

/* End of a GUI loop iteration. Kernel code is never preempted, so the GUI
 * task gives up the CPU here and sleeps until an input event, a timer tick,
 * a redraw request or a process exit wakes it (hlt only runs in the idle
 * task). The check and the block happen with interrupts off so that an
 * event pushed in between is not missed. */
static void gui_wait(void) {
  uint32_t flags = irq_save();

  if (!event_pending()) {
    scheduler_block_current(0);
  }
  scheduler_yield();
  irq_restore(flags);
}

static void boot_info_log(void) {
  serial_print("[BOOT] Boot drive 0x");
  serial_print_hex(boot_info.boot_drive);
  serial_print(", kernel ");
  serial_print_dec(boot_info.kernel_bytes);
  serial_print(" bytes, VBE mode 0x");
  serial_print_hex(boot_info.vbe_mode);
  serial_print("\n");

  for (uint32_t i = 0; i < boot_info.e820_count; ++i) {
    const e820_entry_t *entry = &boot_info.e820[i];

    serial_print("[BOOT] E820 0x");
    if (entry->base >> 32) {
      serial_print_hex((uint32_t)(entry->base >> 32));
      serial_print(":");
    }
    serial_print_hex((uint32_t)entry->base);
    serial_print(" +0x");
    if (entry->length >> 32) {
      serial_print_hex((uint32_t)(entry->length >> 32));
      serial_print(":");
    }
    serial_print_hex((uint32_t)entry->length);
    serial_print(entry->type == E820_USABLE ? " usable\n" : " reserved\n");
  }
}

void kernel_main(const boot_info_t *loader_info) {
  /* Initialize Serial Port for debugging */
  serial_init();
  serial_print("\n[BOOT] GemOS Kernel Starting...\n");

  /* The block lives in stage 2's memory: copy it before anything else. */
  if (loader_info == NULL || loader_info->magic != BOOT_INFO_MAGIC ||
      loader_info->e820_count > BOOT_INFO_E820_MAX) {
    serial_print("[PANIC] No valid boot info from the loader\n");
    for (;;)
      __asm__ volatile("cli; hlt");
  }
  memcpy(&boot_info, loader_info, sizeof(boot_info));
  boot_info_log();

  gdt_init();

  /* Initialize Interrupt Service Routines */
  init_isr();
  serial_print("[BOOT] ISR initialized\n");

  /* Initialize PIC */
  init_pic();
  serial_print("[BOOT] PIC initialized (0x20/0x28)\n");

  /* Initialize System Timer (PIT) */
  init_pit();

  /* Heap and page frames from the E820 map; the heap must hold the
   * backbuffer of the video mode the loader set. */
  const vbe_mode_info_t *mode = (const vbe_mode_info_t *)boot_info.vbe_mode_info;
  memory_layout_t memory;
  if (!pmm_plan((uintptr_t)&__kernel_end,
                (size_t)mode->pitch * mode->height + HEAP_RESERVE, &memory)) {
    serial_print("[PANIC] System halted: not enough memory\n");
    for (;;)
      __asm__ volatile("cli; hlt");
  }
  heap_init(memory.heap_start, memory.heap_size);

  /* Initialize Event System */
  event_init();

  /* Initialize File System */
  gemfs_init();

  /* Initialize Keyboard */
  init_keyboard();

  /* Initialize Mouse */
  init_mouse();

  /* Print VBE info */
  vbe_mode_info_t *vbe_info = (vbe_mode_info_t *)boot_info.vbe_mode_info;

  serial_print("[BOOT] VBE Mode Info:\n");
  serial_print("  Resolution: ");
  serial_print_dec(vbe_info->width);
  serial_print("x");
  serial_print_dec(vbe_info->height);
  serial_print("x");
  serial_print_dec(vbe_info->bpp);
  serial_print("\n");
  serial_print("  Framebuffer: 0x");
  serial_print_hex(vbe_info->physbase);
  serial_print("\n");

  /* Initialize VBE Driver */
  vbe_init(vbe_info->physbase, vbe_info->width, vbe_info->height, vbe_info->bpp,
           vbe_info->pitch);

  /* The loader falls back to smaller modes (boot/stage2/loader.asm): the UI
   * is drawn 2x on Full HD and 1x below, so it keeps at least 800x540
   * logical pixels. */
  ui_scale = (vbe_info->width >= 1920 && vbe_info->height >= 1080) ? 2 : 1;

  /* Enable kernel-owned paging and a dedicated 4 KB frame pool. */
  paging_init(memory.frames_start, memory.frames_end);
  paging_self_test();
  process_init();

  /* Initialize Graphics Context */
  /* DOUBLE BUFFERING SETUP */
  uint32_t screen_width = vbe_get_width();
  uint32_t screen_height = vbe_get_height();
  uint32_t screen_pitch = vbe_get_pitch();
  uint32_t buffer_size = screen_height * screen_pitch;

  /* Allocate Backbuffer (needed for rendering even with page flip) */
  void *backbuffer = kalloc(buffer_size);
  if (!backbuffer) {
    serial_print("[PANIC] Failed to allocate backbuffer!\n");
    for (;;)
      __asm__("hlt");
  }

  serial_print("[GFX] Backbuffer allocated at 0x");
  serial_print_hex((uintptr_t)backbuffer);
  serial_print(" Size: ");
  serial_print_dec(buffer_size);
  serial_print("\n");

  /* Store VBE framebuffer for fallback memcpy blit */
  void *vbe_buffer = (void *)vbe_get_framebuffer();

  /* Check BGA page flip support */
  int use_page_flip = vbe_has_page_flip();
  if (use_page_flip) {
    serial_print("[GFX] BGA page flipping enabled (tear-free)\n");
  } else {
    serial_print("[GFX] BGA page flip unavailable, using memcpy blit\n");
  }

  /* Initialize Context with BACKBUFFER */
  gfx_init_context(&screen_ctx, (uint32_t *)backbuffer, screen_width,
                   screen_height, screen_pitch, vbe_get_bpp());

  /* Clear screen to Desktop Color immediately */
  desktop_draw(&screen_ctx);
  serial_print("[BOOT] Desktop drawn\n");

  /* Initialize Scheduler (overrides IDT gate 32 with scheduler_irq0_stub).
   * Must be called before STI (so the new IRQ0 handler is in place when
   * interrupts fire). */
  scheduler_init();
  syscall_init();

  /* Enable Interrupts */
  serial_print("[BOOT] Enabling Interrupts (STI)...\n");
  __asm__ volatile("sti");

  /* Enable FPU */
  uint32_t cr0;
  __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 &= ~(1 << 2); // Clear EM
  cr0 |= (1 << 1);  // Set MP
  __asm__ volatile("mov %0, %%cr0" ::"r"(cr0));
  __asm__ volatile("fninit");

  /* Load Font */
  extern uint8_t _binary_font_ttf_start[];
  extern uint8_t _binary_font_ttf_end[];
  size_t font_size = (size_t)(_binary_font_ttf_end - _binary_font_ttf_start);

  serial_print("[BOOT] Loading Font System...\n");
  font_load_ttf(_binary_font_ttf_start, font_size);

  serial_print("\n[BOOT] Kernel initialization complete\n");
  serial_print("[BOOT] Entering main loop (UX-FIX-1 Mode)...\n");

  /* Initialize Cursor */
  cursor_init(&screen_ctx);

  /* Initialize Subsystems */
  wm_init(&screen_ctx);
  topbar_init();
  dock_init();
  app_manager_init();
  console_init();

  extern void testapp_register(void);
  testapp_register();

  extern void uabout_launcher_register(void);
  uabout_launcher_register();

  extern void terminal_register(void);
  terminal_register();

  extern void uterm_launcher_register(void);
  uterm_launcher_register();

  extern void utextedit_launcher_register(void);
  utextedit_launcher_register();

  extern void explorer_init(void);
  explorer_init();

  serial_print("[BOOT] Window Manager Active\n");

#if USERLAND_DEBUG_AUTOSTART
  if (process_seed_userland()) {
    process_spawn_user_from_file("USRSMOKE.ELF");
  }
#else
  process_seed_userland();
#endif

  /* Main kernel loop */
  event_t ev;
  uint64_t last_render_tick = 0;

  /* Target FPS: 60 -> ~16.6ms per frame */
  /* PIT is 1000Hz, so 16 ticks */
  const uint64_t TICKS_PER_FRAME = 16;

  for (;;) {
    process_reap_zombies();

    /* Process Events */
    while (event_pop(&ev)) {
      if (ev.type == EVENT_MOUSE_MOVE) {
        /* 1. Menu Handling (Highest Priority) */
        if (menu_handle_event(&ev)) {
          /* Menu consumed event */
        }
        /* 1.5 Dock Handling */
        else if (dock_handle_event(&ev)) {
          /* Dock consumed event */
        } else {
          /* Pass Move to WM (for dragging) */
          wm_handle_event(&ev);
        }

        pending_redraw = true;
      } else if (ev.type == EVENT_MOUSE_CLICK ||
                 ev.type == EVENT_MOUSE_RELEASE) {

        /* 1. Menu Handling */
        if (menu_handle_event(&ev)) {
          /* Consumed by Menu */
        }
        /* 1.5 Dock Handling */
        else if (dock_handle_event(&ev)) {
          /* Consumed by Dock */
        }
        /* 2. Top Bar */
        else if (topbar_handle_event(&ev)) {
          /* Handled by Top Bar */
        } else {
          /* 3. WM */
          wm_handle_event(&ev);
        }

        pending_redraw = true;
      } else if (ev.type == EVENT_KEY_PRESS) {
        /* Menu Key Handling (ESC) */
        if (menu_handle_event(&ev)) {
          pending_redraw = true;
        } else {
          wm_handle_event(&ev);
        }

        pending_redraw = true;
      } else if (ev.type == EVENT_REDRAW_REQUEST) {
        pending_redraw = true;
      }
      /* EVENT_TIMER_TICK only wakes the loop for the frame pacing below. */
    }

    /* At most one frame per TICKS_PER_FRAME ticks. The request is cleared
     * before drawing, so a redraw requested while this frame is drawn gets
     * the next frame instead of being lost. */
    uint64_t now = timer_get_ticks();
    if (pending_redraw && now - last_render_tick >= TICKS_PER_FRAME) {
      pending_redraw = false;
      last_render_tick = now;

      /* Render to the heap backbuffer, then copy the finished frame to VRAM
       * in one burst: direct VRAM rendering is slow because MMIO writes go
       * through QEMU's hypervisor per pixel. */
      screen_ctx.framebuffer = (uint32_t *)backbuffer;

      desktop_draw(&screen_ctx);
      dock_render(&screen_ctx);
      wm_render_all();
      topbar_render(&screen_ctx);
      menu_render(&screen_ctx);
      cursor_draw();

      if (use_page_flip) {
        /* BGA: copy to the invisible back page, then flip atomically */
        memcpy(vbe_get_back_page(), backbuffer, buffer_size);
        vbe_flip();
      } else {
        memcpy(vbe_buffer, backbuffer, buffer_size);
      }
    }

    gui_wait();
  }
}
