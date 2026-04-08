#ifndef GEMOS_USERLAND_COMMON_HOSTED_APP_H
#define GEMOS_USERLAND_COMMON_HOSTED_APP_H

#include <gemos/console_abi.h>
#include <gemos/user_api.h>

#include <stdint.h>

typedef struct {
  int32_t pid;
  int32_t console_handle;
  gemos_console_event_t event;
  gemos_console_frame_t frame;
} gemos_hosted_app_t;

static inline int32_t gemos_hosted_app_open(gemos_hosted_app_t *app,
                                            const char *title, uint32_t cols,
                                            uint32_t rows, uint32_t flags) {
  if (app == 0) {
    return -1;
  }

  app->pid = gemos_getpid();
  if (app->pid < 0) {
    app->console_handle = -1;
    return app->pid;
  }

  app->console_handle = gemos_console_open(title, cols, rows, flags);
  if (app->console_handle < 0) {
    return app->console_handle;
  }
  app->event.type = GEMOS_CONSOLE_EVENT_NONE;
  app->event.key_code = 0U;
  app->event.character = 0U;
  app->event.modifiers = 0U;
  app->frame.cols = cols;
  app->frame.rows = rows;
  app->frame.cursor_row = 0;
  app->frame.cursor_col = 0;
  app->frame.cursor_visible = 0;
  app->frame.cells = 0;
  return app->console_handle;
}

static inline int32_t
gemos_hosted_app_poll_event(gemos_hosted_app_t *app) {
  if (app == 0 || app->console_handle < 0) {
    return -1;
  }

  return gemos_console_poll_event(app->console_handle, &app->event);
}

static inline int32_t gemos_hosted_app_present(gemos_hosted_app_t *app,
                                               gemos_console_cell_t *cells) {
  if (app == 0 || app->console_handle < 0 || cells == 0) {
    return -1;
  }

  app->frame.cells = cells;
  return gemos_console_present(app->console_handle, &app->frame);
}

static inline void gemos_hosted_app_idle(void) { (void)gemos_yield(); }

#endif /* GEMOS_USERLAND_COMMON_HOSTED_APP_H */
