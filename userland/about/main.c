#include "../common/hosted_app.h"
#include "about_render.h"
#include "about_state.h"
#include "about_theme.h"

#include <gemos/user_api.h>

static gemos_hosted_app_t about_app;
static about_state_t about_state;
static gemos_console_cell_t about_cells[ABOUT_ROWS * ABOUT_COLS];

int main(void) {
  int32_t poll_result;

  if (gemos_hosted_app_open(&about_app, ABOUT_TITLE, ABOUT_COLS, ABOUT_ROWS,
                            0U) < 0) {
    static const char open_failed[] = "ABOUT failed to open console\n";
    gemos_debug_write(open_failed, sizeof(open_failed) - 1U);
    return 1;
  }

  about_state_init(&about_state, (uint32_t)about_app.pid);

  for (;;) {
    int saw_event = 0;

    while ((poll_result = gemos_hosted_app_poll_event(&about_app)) == 1) {
      saw_event = 1;
      about_state_handle_event(&about_state, &about_app.event);
    }

    if (poll_result < 0) {
      return 2;
    }

    about_state_tick(&about_state, (uint32_t)gemos_ticks_ms());

    if (about_state.dirty) {
      about_render_build_frame(&about_state, &about_app.frame, about_cells);
      if (gemos_hosted_app_present(&about_app, about_cells) < 0) {
        return 3;
      }
      about_state_mark_clean(&about_state);
    }

    if (about_state.should_exit) {
      return 0;
    }

    if (!saw_event && !about_state.dirty) {
      gemos_hosted_app_idle();
    }
  }
}
