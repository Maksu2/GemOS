#include "app_manager.h"
#include "../../drivers/serial.h"
#include "../../include/string.h"

#define MAX_APPS 16
static app_t *registered_apps[MAX_APPS];
static int app_count = 0;

void app_manager_init(void) {
  app_count = 0;
  for (int i = 0; i < MAX_APPS; i++) {
    registered_apps[i] = NULL;
  }
  serial_print("[APP] Manager Initialized\n");
}

void app_register(app_t *app) {
  if (app_count >= MAX_APPS || !app)
    return;
  registered_apps[app_count++] = app;
  if (app->init) {
    app->init();
  }
  serial_print("[APP] Registered: ");
  serial_print(app->name);
  serial_print("\n");
}

void app_open(const char *name) {
  if (!name)
    return;

  /* Find app by name */
  /* Note: We need strcmp. Implementing simple loop/comparison if strcmp is not
   * available or safe. */
  /* Assuming strcmp is in string.h and relies on standard lib implementation or
   * custom one. */

  for (int i = 0; i < app_count; i++) {
    if (registered_apps[i]) {
      /* Simple string match */
      const char *n1 = registered_apps[i]->name;
      const char *n2 = name;
      while (*n1 && *n2 && *n1 == *n2) {
        n1++;
        n2++;
      }
      if (*n1 == '\0' && *n2 == '\0') {
        /* Match */
        serial_print("[APP] Opening: ");
        serial_print(name);
        serial_print("\n");

        if (registered_apps[i]->open) {
          registered_apps[i]->open();
        }
        return;
      }
    }
  }
  serial_print("[APP] App not found: ");
  serial_print(name);
  serial_print("\n");
}

void app_open_with_file(const char *app_name, const char *path) {
  if (!app_name || !path)
    return;

  for (int i = 0; i < app_count; i++) {
    if (registered_apps[i]) {
      const char *n1 = registered_apps[i]->name;
      const char *n2 = app_name;
      while (*n1 && *n2 && *n1 == *n2) {
        n1++;
        n2++;
      }
      if (*n1 == '\0' && *n2 == '\0') {
        /* Match */
        serial_print("[APP] Opening with file: ");
        serial_print(app_name);
        serial_print(" -> ");
        serial_print(path);
        serial_print("\n");

        if (registered_apps[i]->open_file) {
          registered_apps[i]->open_file(path);
        } else if (registered_apps[i]->open) {
          /* Fallback */
          serial_print("[APP] No open_file handler, falling back to open()\n");
          registered_apps[i]->open();
        }
        return;
      }
    }
  }
  serial_print("[APP] App not found: ");
  serial_print(app_name);
  serial_print("\n");
}
