#include "../../drivers/serial.h"
#include "../../kernel/app/app.h"
#include "../../kernel/app/app_manager.h"
#include "../../kernel/gfx/icons.h"
#include "../../kernel/process.h"

static app_t utextedit_launcher_app;

static void utextedit_launcher_open(void) {
  int pid = process_spawn_user_from_file("UTEXTEDIT.ELF");

  if (pid < 0) {
    serial_print("[UTEXTEDIT] Failed to spawn UTEXTEDIT.ELF\n");
    return;
  }

  serial_print("[UTEXTEDIT] Spawned PID=");
  serial_print_dec((uint32_t)pid);
  serial_print("\n");
}

static void utextedit_launcher_open_file(const char *path) {
  (void)path;
  serial_print("[UTEXTEDIT] open_file not wired yet, opening empty editor\n");
  utextedit_launcher_open();
}

static void utextedit_launcher_init(void) {
  utextedit_launcher_app.name = "Text Editor";
  utextedit_launcher_app.icon = &icon_textedit;
  utextedit_launcher_app.init = NULL;
  utextedit_launcher_app.open = utextedit_launcher_open;
  utextedit_launcher_app.open_file = utextedit_launcher_open_file;
  utextedit_launcher_app.render = NULL;
  utextedit_launcher_app.handle_event = NULL;
  utextedit_launcher_app.request_close = NULL;
  utextedit_launcher_app.close = NULL;
  utextedit_launcher_app.menu = NULL;
}

void utextedit_launcher_register(void) {
  utextedit_launcher_init();
  app_register(&utextedit_launcher_app);
}
