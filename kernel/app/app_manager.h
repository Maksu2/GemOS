#ifndef APP_MANAGER_H
#define APP_MANAGER_H

#include "app.h"

void app_manager_init(void);

/* Register an app provided by a module */
void app_register(app_t *app);

/* Open an app by name (e.g. from menu) */
void app_open(const char *name);

/* Open an app with a file argument */
void app_open_with_file(const char *app_name, const char *path);

/* Some app's windows changed without an event (app_t.changed) */
/* The windows of apps whose content changed without an event (app_t.changed)
 * are drawn again in the next frame */
void app_manager_damage_changed(void);

#endif
