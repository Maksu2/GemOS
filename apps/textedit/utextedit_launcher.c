#include "../../drivers/serial.h"
#include "../../kernel/app/app.h"
#include "../../kernel/app/app_manager.h"
#include "../../kernel/gfx/icons.h"
#include "../../kernel/process.h"

static app_t utextedit_launcher_app;

/* The editor gets the file to open as argv[1] (NULL: a new document). */
static void utextedit_launcher_spawn(const char *path) {
  int pid = process_spawn_user_with_arg("UTEXTEDIT.ELF", path);

  if (pid < 0) {
    serial_print("[UTEXTEDIT] Failed to spawn UTEXTEDIT.ELF\n");
    return;
  }

  serial_print("[UTEXTEDIT] Spawned PID=");
  serial_print_dec((uint32_t)pid);
  if (path != NULL) {
    serial_print(" for ");
    serial_print(path);
  }
  serial_print("\n");
}

static void utextedit_launcher_open(void) { utextedit_launcher_spawn(NULL); }

static void utextedit_launcher_open_file(const char *path) {
  utextedit_launcher_spawn(path);
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
