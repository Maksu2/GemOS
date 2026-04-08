#include "../../drivers/serial.h"
#include "../../kernel/app/app.h"
#include "../../kernel/app/app_manager.h"
#include "../../kernel/gfx/icons.h"
#include "../../kernel/process.h"

static app_t uabout_launcher_app;

static void uabout_launcher_open(void) {
  int pid;

  pid = process_spawn_user_from_file("ABOUT.ELF");
  if (pid < 0) {
    serial_print("[ABOUT] Failed to spawn ABOUT.ELF\n");
    return;
  }

  serial_print("[ABOUT] Spawned PID=");
  serial_print_dec((uint32_t)pid);
  serial_print("\n");
}

static void uabout_launcher_init(void) {
  uabout_launcher_app.name = "About GemOS";
  uabout_launcher_app.icon = &icon_about;
  uabout_launcher_app.init = NULL;
  uabout_launcher_app.open = uabout_launcher_open;
  uabout_launcher_app.open_file = NULL;
  uabout_launcher_app.render = NULL;
  uabout_launcher_app.handle_event = NULL;
  uabout_launcher_app.close = NULL;
  uabout_launcher_app.menu = NULL;
}

void uabout_launcher_register(void) {
  uabout_launcher_init();
  app_register(&uabout_launcher_app);
}
