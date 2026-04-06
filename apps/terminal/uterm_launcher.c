#include "../../drivers/serial.h"
#include "../../kernel/app/app.h"
#include "../../kernel/app/app_manager.h"
#include "../../kernel/gfx/icons.h"
#include "../../kernel/process.h"

static app_t uterm_launcher_app;

static void uterm_launcher_open(void) {
  int pid = process_spawn_user_from_file("UTERM.ELF");

  if (pid < 0) {
    serial_print("[UTERM] Failed to spawn UTERM.ELF\n");
    return;
  }

  serial_print("[UTERM] Spawned PID=");
  serial_print_dec((uint32_t)pid);
  serial_print("\n");
}

static void uterm_launcher_init(void) {
  uterm_launcher_app.name = "User Terminal";
  uterm_launcher_app.icon = &icon_terminal;
  uterm_launcher_app.init = NULL;
  uterm_launcher_app.open = uterm_launcher_open;
  uterm_launcher_app.open_file = NULL;
  uterm_launcher_app.render = NULL;
  uterm_launcher_app.handle_event = NULL;
  uterm_launcher_app.close = NULL;
  uterm_launcher_app.menu = NULL;
}

void uterm_launcher_register(void) {
  uterm_launcher_init();
  app_register(&uterm_launcher_app);
}
